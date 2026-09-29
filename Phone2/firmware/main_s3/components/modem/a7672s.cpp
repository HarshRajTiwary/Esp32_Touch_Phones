// SIMCom A7672S driver - modem task, start-up state machine, recovery, command
// queue and the public modem:: API. See docs/MODEM.md.
//
//   OFF -> probe (already on?) -> POWERING_ON (PWRKEY) -> BOOTING (first OK)
//       -> AT_TEST (echo off, identity) -> CMUX_START -> SIM_CHECK -> configure
//       -> NETWORK_SEARCH <-> READY (registered)
//
// Every step has a timeout; a failed start records the reason, power-cycles the
// module and retries, backing off after MODEM_MAX_START_ATTEMPTS.
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "a7672s_internal.h"
#include "board_config.h"
#include "modem_port.h"
#include "plog.h"
#include "s3_config.h"
#include "text_util.h"

namespace a7672s {

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
NetStatus g_st;
CallInfo g_call;
modem::Config g_cfg;
int g_tzQuarters = 22;  // IST until the network says otherwise

namespace {

constexpr uint32_t kValidEpoch = 1704067200;  // 2024-01-01: anything earlier = clock not set

enum class Mc : uint8_t {
  Dial, Answer, HangUp, Dtmf, Mute, AudioProfile, SpeakerGain, MicGain,
  SendSms, Ussd, UssdCancel, EnterPin, ReadSimContacts, SetClock, NetMode,
  RawAt, Restart, Refresh, PowerOff,
};

struct ModemCmd {
  Mc type;
  int32_t a;
  char str[64];
  void* ptr;  // OutgoingSms* for SendSms (owned by the modem task afterwards)
};

QueueHandle_t s_cmdQ;
volatile bool s_ready = false;
volatile bool s_needReinit = false;
char s_reinitWhy[48] = "";
int s_startFailures = 0;
bool s_poweredOff = false;

void setFail(const char* why) {
  LOG("MODEM", "start failed: %s", why);
  updateStatus([&](NetStatus& s) { txt::copy(s.failReason, why, sizeof s.failReason); });
}

// ---- control pins ---------------------------------------------------------------
void press(int pin, bool activeHigh, uint32_t ms) {
  if (pin < 0) return;
  gpio_set_level((gpio_num_t)pin, activeHigh ? 1 : 0);
  vTaskDelay(pdMS_TO_TICKS(ms));
  gpio_set_level((gpio_num_t)pin, activeHigh ? 0 : 1);
}

bool statusPinHigh() {
  return PIN_MODEM_STATUS >= 0 && gpio_get_level((gpio_num_t)PIN_MODEM_STATUS) == 1;
}

// Leaves a multiplexer that may still be running from before an S3 reset, so
// plain AT works again. Harmless noise for a module that is not in CMUX mode.
void forceMuxClose() {
  static const uint8_t cld[] = {0xF9, 0x03, 0xEF, 0x05, 0xC3, 0x01, 0xF2, 0xF9};
  mport::write(mport::kChanRaw, cld, sizeof cld);
  vTaskDelay(pdMS_TO_TICKS(300));
  mport::writeStr(mport::kChanRaw, "\r");
  vTaskDelay(pdMS_TO_TICKS(100));
  mport::flushInput();
  flushLines();
}

bool probe(uint32_t windowMs) {
  uint32_t start = nowMs();
  while (nowMs() - start < windowMs) {
    if (exec("AT", 300) == AtFinal::Ok) return true;
  }
  return false;
}

void powerCycle() {
  setModemState(ModemState::Recovering);
  if (mport::mode() == mport::Mode::Mux) mport::muxStop();
  // A long PWRKEY press switches a running module off; on a module that is
  // already off it would switch it on instead, so skip it when STATUS says "off".
  bool knownOff = PIN_MODEM_STATUS >= 0 && !statusPinHigh();
  if (PIN_MODEM_PWRKEY >= 0 && !knownOff) {
    LOG("MODEM", "switching the module off (PWRKEY %u ms)", MODEM_PWRKEY_OFF_MS);
    press(PIN_MODEM_PWRKEY, MODEM_PWRKEY_ACTIVE_HIGH, MODEM_PWRKEY_OFF_MS);
    vTaskDelay(pdMS_TO_TICKS(MODEM_OFF_SETTLE_MS));
  } else if (knownOff) {
    LOG("MODEM", "module is off (STATUS low)");
  } else if (PIN_MODEM_RESET >= 0) {
    LOG("MODEM", "resetting the module (RESET 2.5 s)");
    press(PIN_MODEM_RESET, MODEM_RESET_ACTIVE_HIGH, 2500);
    vTaskDelay(pdMS_TO_TICKS(1000));
  } else {
    LOG("MODEM", "no PWRKEY/RESET pin wired - trying AT+CRESET");
    exec("AT+CRESET", 2000);
    vTaskDelay(pdMS_TO_TICKS(MODEM_OFF_SETTLE_MS));
  }
  updateStatus([](NetStatus& s) {
    uint32_t resets = s.modemResets + 1;
    NetStatus fresh;
    fresh.modemResets = resets;
    txt::copy(fresh.imei, s.imei, sizeof fresh.imei);
    txt::copy(fresh.model, s.model, sizeof fresh.model);
    txt::copy(fresh.fw, s.fw, sizeof fresh.fw);
    txt::copy(fresh.failReason, s.failReason, sizeof fresh.failReason);
    fresh.modem = ModemState::Recovering;
    s = fresh;
  });
}

// ---- start-up state machine -----------------------------------------------------
bool stepPowerOn() {
  setModemState(ModemState::PoweringOn);
  if (mport::mode() == mport::Mode::Mux) mport::muxStop();  // re-init while running
  mport::setBaud(MODEM_BAUD);
  forceMuxClose();                                          // mux left over from before an S3 reset
  if (probe(MODEM_PROBE_MS)) {
    LOG("MODEM", "module already on");
    return true;
  }
  if (PIN_MODEM_PWRKEY >= 0) {
    LOG("MODEM", "pressing PWRKEY (%u ms)", MODEM_PWRKEY_ON_MS);
    press(PIN_MODEM_PWRKEY, MODEM_PWRKEY_ACTIVE_HIGH, MODEM_PWRKEY_ON_MS);
  } else {
    LOG("MODEM", "no PWRKEY pin - waiting for the board to start the module by itself");
  }
  setModemState(ModemState::Booting);
  uint32_t start = nowMs();
  bool statusLogged = false;
  while (nowMs() - start < MODEM_BOOT_TIMEOUT_MS) {
    if (!statusLogged && statusPinHigh()) {
      LOG("MODEM", "STATUS pin high after %lu ms", (unsigned long)(nowMs() - start));
      statusLogged = true;
    }
    if (exec("AT", 500) == AtFinal::Ok) {
      LOG("MODEM", "module answered after %lu ms", (unsigned long)(nowMs() - start));
      return true;
    }
  }
  setFail(PIN_MODEM_PWRKEY >= 0 ? "no answer on UART (power, TX/RX swap, level shifter, PWRKEY?)"
                                : "no answer on UART (power, TX/RX swap, level shifter?)");
  return false;
}

bool stepAtTest() {
  setModemState(ModemState::AtTest);
  if (!ok("ATE0")) {
    setFail("ATE0 failed");
    return false;
  }
  ok("AT+CMEE=1");  // numeric +CME ERROR codes (atErrorText translates them)
  netReadIdentity();
  if (PIN_MODEM_RTS >= 0 && PIN_MODEM_CTS >= 0) ok("AT+IFC=2,2");  // RTS/CTS flow control
#if MODEM_TRY_FAST_BAUD
  char cmd[32];
  snprintf(cmd, sizeof cmd, "AT+IPR=%u", (unsigned)MODEM_BAUD_FAST);
  if (ok(cmd)) {
    vTaskDelay(pdMS_TO_TICKS(100));
    mport::setBaud(MODEM_BAUD_FAST);
    if (probe(1500)) {
      LOG("MODEM", "UART now %u baud", (unsigned)MODEM_BAUD_FAST);
    } else {
      // The level shifters cannot keep up: go back (IPR is not saved, a power cycle also restores 115200)
      LOG("MODEM", "%u baud does not work on this board - staying at %u", (unsigned)MODEM_BAUD_FAST, (unsigned)MODEM_BAUD);
      mport::setBaud(MODEM_BAUD);
      if (!probe(1000)) {
        setFail("lost the module after a baud rate change");
        return false;
      }
    }
  }
#endif
  updateStatus([](NetStatus& s) { s.baud = mport::baud(); });
  return true;
}

bool stepCmux() {
  setModemState(ModemState::CmuxStart);
  int code = 0;
  AtFinal f = exec("AT+CMUX=0", 3000, nullptr, false, &code);
  if (f != AtFinal::Ok) {
    // Voice + SMS still work on the plain UART; only 4G data needs the second channel.
    LOG("MODEM", "module refused AT+CMUX (%s) - running without mobile data", atErrorText(f, code));
    updateStatus([](NetStatus& s) { s.cmux = false; });
    return true;
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!mport::muxStart(1500)) {
    setFail("CMUX channels did not open");
    return false;
  }
  flushLines();
  if (!probe(2000)) {
    setFail("no AT answer on CMUX channel 1");
    return false;
  }
  ok("ATE0");
  ok("AT+CATR=0");  // URCs on every port: they reach channel 1 whatever the port numbering is
  updateStatus([](NetStatus& s) { s.cmux = true; });
  return true;
}

bool stepSim() {
  setModemState(ModemState::SimCheck);
  uint32_t start = nowMs();
  for (;;) {
    int code = 0;
    AtFinal f = exec("AT+CPIN?", 5000, nullptr, false, &code);
    if (f == AtFinal::CmeError && code == 10) updateStatus([](NetStatus& s) { s.sim = SimState::Absent; });
    SimState sim = statusSnapshot().sim;
    if (sim == SimState::Ready || sim == SimState::PinRequired || sim == SimState::PukRequired ||
        sim == SimState::Absent)
      break;
    if (nowMs() - start > MODEM_SIM_TIMEOUT_MS) {
      LOG("MODEM", "SIM still not ready after %u s", MODEM_SIM_TIMEOUT_MS / 1000);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  NetStatus st = statusSnapshot();
  LOG("SIM", "%s", st.sim == SimState::Ready ? "READY"
                   : st.sim == SimState::Absent ? "NOT INSERTED"
                   : st.sim == SimState::PinRequired ? "PIN REQUIRED"
                   : st.sim == SimState::PukRequired ? "PUK REQUIRED" : "not ready");
  return true;  // continue without a SIM: emergency calls and the UI still work
}

void configure() {
  // Calls
  ok("AT+CLIP=1");   // caller ID with RING
  ok("AT+CLCC=1");   // automatic +CLCC on every call state change
  ok("AT+CVHU=0");   // ATH hangs up voice calls
  ok("AT+CRC=0");
  netConfigure();
  smsConfigure();
  audioConfigure();
}

bool startModem() {
  s_ready = false;
  resetTimeouts();
  if (!stepPowerOn() || !stepAtTest() || !stepCmux() || !stepSim()) return false;
  configure();
  setModemState(ModemState::NetworkSearch);
  updateStatus([](NetStatus& s) { s.failReason[0] = 0; });
  LOG("MODEM", "ready (%s, IMEI %s, %s)", statusSnapshot().model, logNum(statusSnapshot().imei),
      statusSnapshot().cmux ? "CMUX" : "single channel");
  netRefreshSoon();
  smsScheduleSweep(nowMs() + 4000);
  dataModemStarted(statusSnapshot().cmux);
  return true;
}

void stopForReinit() {
  dataModemLost();
  callEndAll(CallEndReason::Failed);
  s_ready = false;
}

// ---- commands -----------------------------------------------------------------
void rejectWhileDown(ModemCmd& c) {
  switch (c.type) {
    case Mc::Dial: {
      notice("Modem not ready");
      CallInfo* snap = new CallInfo();
      txt::copy(snap->number, c.str, sizeof snap->number);
      snap->endReason = CallEndReason::NoNetwork;
      events::post(Ev::CallEnded, (int32_t)CallEndReason::NoNetwork, 0, snap);
      break;
    }
    case Mc::SendSms: {
      OutgoingSms* o = static_cast<OutgoingSms*>(c.ptr);
      events::post(Ev::SmsSent, (int32_t)o->id, 0, strdup("Modem not ready"));
      delete o;
      break;
    }
    case Mc::Ussd:
      events::post(Ev::UssdReply, 0, 0, strdup("Modem not ready"));
      break;
    case Mc::Restart:
      s_startFailures = 0;
      break;
    default:
      break;
  }
}

void handleCmd(ModemCmd& c) {
  switch (c.type) {
    case Mc::Dial: callDial(c.str); break;
    case Mc::Answer: callAnswer(); break;
    case Mc::HangUp: callHangup(); break;
    case Mc::Dtmf: callDtmf((char)c.a); break;
    case Mc::Mute: callMute(c.a != 0); break;
    case Mc::AudioProfile: audioSetProfile((AudioRoute)c.a); break;
    case Mc::SpeakerGain: audioSetSpeakerGain(c.a); break;
    case Mc::MicGain: audioSetMicGain(c.a); break;
    case Mc::SendSms: smsSend(static_cast<OutgoingSms*>(c.ptr)); break;
    case Mc::Ussd: ussdStart(c.str); break;
    case Mc::UssdCancel: ussdCancel(); break;
    case Mc::EnterPin: {
      char cmd[80];
      snprintf(cmd, sizeof cmd, "AT+CPIN=\"%s\"", c.str);
      if (exec(cmd, 10000) == AtFinal::Ok) {
        notice("SIM unlocked");
        requestReinit("SIM unlocked");  // re-run the SIM dependent setup
      } else {
        notice("Wrong PIN");
      }
      exec("AT+CPIN?", 5000);
      memset(c.str, 0, sizeof c.str);  // do not keep the PIN around
      break;
    }
    case Mc::ReadSimContacts: smsReadSimContacts(); break;
    case Mc::SetClock: {
      int y, mo, d, h, mi, s;
      txt::splitEpoch((uint32_t)c.a, &y, &mo, &d, &h, &mi, &s, nullptr);
      char cmd[48];
      snprintf(cmd, sizeof cmd, "AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03d\"", y % 100, mo, d, h, mi, s, g_tzQuarters);
      ok(cmd);
      setSystemTime((uint32_t)c.a, "manual");
      break;
    }
    case Mc::NetMode: netSetMode((NetMode)c.a); break;
    case Mc::RawAt: {
      LOG("AT", "> %s", c.str);
      AtFinal f = exec(c.str, 15000, [](const char* l) { LOG("AT", "< %s", l); });
      LOG("AT", "< %s", f == AtFinal::None ? "(timeout)" : atErrorText(f, 0));
      break;
    }
    case Mc::Restart:
      requestReinit("restart requested");
      s_poweredOff = false;
      powerCycle();
      break;
    case Mc::Refresh: netRefreshSoon(); break;
    case Mc::PowerOff:
      LOG("MODEM", "powering the module down (AT+CPOF)");
      dataModemLost();
      exec("AT+CPOF", 9000);
      s_poweredOff = true;
      s_ready = false;
      setModemState(ModemState::Off);
      break;
  }
}

void modemTask(void*) {
  for (;;) {
    if (s_poweredOff) {
      ModemCmd c;
      if (xQueueReceive(s_cmdQ, &c, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (c.type == Mc::Restart) {
          s_poweredOff = false;
          s_startFailures = 0;
        } else {
          rejectWhileDown(c);
        }
      }
      continue;
    }
    if (s_needReinit) {
      LOG("MODEM", "re-initialising: %s", s_reinitWhy);
      s_needReinit = false;
      stopForReinit();
    }
    if (!s_ready) {
      if (startModem()) {
        s_ready = true;
        s_startFailures = 0;
        mport::clearRestartSeen();
      } else {
        dataModemLost();
        ++s_startFailures;
        powerCycle();
        if (s_startFailures >= MODEM_MAX_START_ATTEMPTS) {
          setModemState(ModemState::NotResponding);
          LOG("MODEM", "giving up for %u s after %d attempts", MODEM_RETRY_BACKOFF_MS / 1000, s_startFailures);
          uint32_t until = nowMs() + MODEM_RETRY_BACKOFF_MS;
          while ((int32_t)(until - nowMs()) > 0) {
            ModemCmd c;
            if (xQueueReceive(s_cmdQ, &c, pdMS_TO_TICKS(200)) != pdTRUE) continue;
            if (c.type == Mc::Restart) break;
            rejectWhileDown(c);
          }
          s_startFailures = 0;
        }
        continue;
      }
    }
    ModemCmd c;
    if (xQueueReceive(s_cmdQ, &c, pdMS_TO_TICKS(20)) == pdTRUE) handleCmd(c);
    drainUrcs();
    uint32_t now = nowMs();
    netPeriodic(now);
    callPeriodic(now);
    smsPeriodic(now);
    if (consecutiveTimeouts() >= MODEM_AT_TIMEOUTS_RESET) {
      LOG("MODEM", "not responding - recovering");
      setModemState(ModemState::NotResponding);
      stopForReinit();
      powerCycle();
    } else if (mport::restartSeen()) {
      mport::clearRestartSeen();
      requestReinit("module restarted by itself (power dip?)");
      notice("Modem restarted - check the battery");
    }
  }
}

bool postCmd(Mc type, int32_t a = 0, const char* str = nullptr, void* ptr = nullptr) {
  ModemCmd c{};
  c.type = type;
  c.a = a;
  c.ptr = ptr;
  if (str) txt::copy(c.str, str, sizeof c.str);
  if (!s_cmdQ || xQueueSend(s_cmdQ, &c, pdMS_TO_TICKS(100)) != pdTRUE) {
    LOG("MODEM", "command queue full");
    return false;
  }
  return true;
}

}  // namespace

// ---- shared helpers -----------------------------------------------------------------
uint32_t nowMs() { return plogMillis(); }

bool isRegistered(const NetStatus& s) { return s.reg == RegState::Home || s.reg == RegState::Roaming; }

bool clockValid() { return time(nullptr) > (time_t)kValidEpoch; }

uint32_t nowLocal() { return clockValid() ? (uint32_t)time(nullptr) : 0; }

void setSystemTime(uint32_t epoch, const char* source) {
  if (epoch < kValidEpoch) return;
  time_t cur = time(nullptr);
  if (llabs((long long)cur - (long long)epoch) <= 2) return;
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  char c[6];
  txt::fmtClock(epoch, c);
  LOG("TIME", "clock set to %s from %s", c, source);
  events::post(Ev::TimeSynced, (int32_t)epoch);
}

NetStatus statusSnapshot() {
  portENTER_CRITICAL(&g_mux);
  NetStatus s = g_st;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

CallInfo callSnapshot() {
  portENTER_CRITICAL(&g_mux);
  CallInfo c = g_call;
  portEXIT_CRITICAL(&g_mux);
  return c;
}

void notice(const char* text) { events::post(Ev::ModemNotice, 0, 0, strdup(text)); }

void setModemState(ModemState m) {
  ModemState before = statusSnapshot().modem;
  updateStatus([m](NetStatus& s) { s.modem = m; });
  if (before != m) LOG("MODEM", "state: %s", modemStateName(m));
}

void requestReinit(const char* why) {
  txt::copy(s_reinitWhy, why, sizeof s_reinitWhy);
  s_needReinit = true;
}

void handleLine(const char* line) {
  if (smsHandleLine(line)) return;   // first: multi-line USSD replies swallow following lines
  if (isModemBootBanner(line)) {
    if (s_ready) requestReinit("module restarted by itself");
    return;
  }
  if (callHandleLine(line)) return;
  if (netHandleLine(line)) return;
}

}  // namespace a7672s

// =====================================================================================
//  Public API
// =====================================================================================
namespace modem {
using namespace a7672s;

void begin(const Config& cfg) {
  g_cfg = cfg;
  atInit();
  s_cmdQ = xQueueCreate(16, sizeof(ModemCmd));
  mport::Config pc = {MODEM_UART, PIN_MODEM_TX, PIN_MODEM_RX, PIN_MODEM_RTS, PIN_MODEM_CTS, MODEM_BAUD};
  if (!mport::begin(pc, onPortBytes)) {
    updateStatus([](NetStatus& s) {
      s.modem = ModemState::NotResponding;
      txt::copy(s.failReason, "UART driver failed", sizeof s.failReason);
    });
    return;
  }
  dataBegin();
  xTaskCreatePinnedToCore(modemTask, "modem", 10240, nullptr, 6, nullptr, 1);
}

void dial(const char* number) {
  char clean[NUMBER_MAX];
  txt::sanitizeDial(number, clean, sizeof clean);
  if (clean[0]) postCmd(Mc::Dial, 0, clean);
}
void answer() { postCmd(Mc::Answer); }
void hangup() { postCmd(Mc::HangUp); }
void dtmf(char key) { postCmd(Mc::Dtmf, key); }
void mute(bool on) { postCmd(Mc::Mute, on ? 1 : 0); }
void setAudioProfile(AudioRoute r) { postCmd(Mc::AudioProfile, (int32_t)r); }
void setSpeakerGain(int g) { postCmd(Mc::SpeakerGain, g < 0 ? 0 : g > 7 ? 7 : g); }
void setMicGain(int g) { postCmd(Mc::MicGain, g < 0 ? 0 : g > 7 ? 7 : g); }

bool sendSms(uint32_t localId, const char* number, const char* text) {
  auto* o = new OutgoingSms();
  o->id = localId;
  txt::copy(o->number, number, sizeof o->number);
  txt::copy(o->text, text, sizeof o->text);
  if (!postCmd(Mc::SendSms, 0, nullptr, o)) {
    delete o;
    return false;
  }
  return true;
}

void ussd(const char* code) { postCmd(Mc::Ussd, 0, code); }
void ussdCancel() { postCmd(Mc::UssdCancel); }

void enterPin(const char* pin) {
  // Only digits, 4..8 of them (input validation - nothing else reaches AT+CPIN)
  size_t n = strlen(pin);
  if (n < 4 || n > 8) return;
  for (size_t i = 0; i < n; ++i)
    if (pin[i] < '0' || pin[i] > '9') return;
  postCmd(Mc::EnterPin, 0, pin);
}

void readSimContacts() { postCmd(Mc::ReadSimContacts); }
void setClock(uint32_t localEpoch) { postCmd(Mc::SetClock, (int32_t)localEpoch); }
void setNetworkMode(NetMode m) { postCmd(Mc::NetMode, (int32_t)m); }
void setDataEnabled(bool on) { dataSetEnabled(on); }
void setApn(const char* apn, const char* user, const char* pass) { dataSetApn(apn, user, pass); }
DataStatus data() { return dataSnapshot(); }
void rawAt(const char* cmd) { postCmd(Mc::RawAt, 0, cmd); }
void restart() { postCmd(Mc::Restart); }
void refresh() { postCmd(Mc::Refresh); }
void powerOff() { postCmd(Mc::PowerOff); }

NetStatus status() { return statusSnapshot(); }
CallInfo call() { return callSnapshot(); }
bool registered() { return isRegistered(status()); }

PhoneState phoneState() {
  NetStatus s = status();
  switch (s.modem) {
    case ModemState::Off:
    case ModemState::PoweringOn:
    case ModemState::Booting:
    case ModemState::Recovering:
      return PhoneState::Boot;
    case ModemState::AtTest:
    case ModemState::CmuxStart:
    case ModemState::SimCheck:
      return PhoneState::InitializingModem;
    case ModemState::NotResponding:
      return PhoneState::Error;
    case ModemState::NetworkSearch:
    case ModemState::Ready:
      break;
  }
  if (s.sim == SimState::Absent) return PhoneState::NoSim;
  if (s.sim == SimState::PinRequired || s.sim == SimState::PukRequired) return PhoneState::SimLocked;
  if (s.sim != SimState::Ready) return PhoneState::InitializingModem;
  return isRegistered(s) ? PhoneState::Ready : PhoneState::SearchingNetwork;
}

const char* phoneStateText(PhoneState s) {
  switch (s) {
    case PhoneState::Boot: return "Starting modem...";
    case PhoneState::InitializingModem: return "Checking SIM...";
    case PhoneState::NoSim: return "SIM not detected";
    case PhoneState::SimLocked: return "SIM locked";
    case PhoneState::SearchingNetwork: return "Searching for network";
    case PhoneState::Ready: return "Ready";
    case PhoneState::Error: return "Modem not responding";
  }
  return "";
}

int signalBars(const NetStatus& s) {
  if (!isRegistered(s)) return 0;
  if (s.rat == Rat::Lte && s.rsrp != 0) return rsrpToBars(s.rsrp);
  return csqToBars(s.csq);
}

int tzQuarters() { return g_tzQuarters; }

const char* ratText(const NetStatus& s) {
  if (!isRegistered(s)) return "";
  switch (s.rat) {
    case Rat::Lte: return "4G";
    case Rat::Gsm: return "2G";
    default: return "";
  }
}

}  // namespace modem
