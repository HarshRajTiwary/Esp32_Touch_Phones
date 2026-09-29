// 4G mobile data: PPP over CMUX channel 2, in its own task so dialing and
// reconnecting never delay calls or SMS on channel 1.
//
//   OFF / WAIT_NETWORK -> DIALING (AT+CGDCONT, ATD*99# -> CONNECT)
//     -> NEGOTIATING (lwIP PPP: LCP, auth, IPCP) -> CONNECTED (got IP)
//     -> link lost / error -> BACKOFF (5 s .. 2 min, doubling) -> DIALING
//
// The PPP netif becomes the default route; the hotspot shares it with NAPT.
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_netif_ppp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/ip4_addr.h>
#include <stdio.h>

#include "a7672s_internal.h"
#include "modem_port.h"
#include "net_rules.h"
#include "plog.h"
#include "s3_config.h"
#include "text_util.h"

namespace a7672s {
namespace {

constexpr EventBits_t kGotIp = 1 << 0;
constexpr EventBits_t kLostIp = 1 << 1;
constexpr EventBits_t kPppDown = 1 << 2;    // error or phase DEAD
constexpr EventBits_t kWake = 1 << 3;       // settings / modem state changed

struct PppDriver {
  esp_netif_driver_base_t base;
};

PppDriver s_drv;
esp_netif_t* s_ppp = nullptr;
EventGroupHandle_t s_ev;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
DataStatus s_ds;
volatile bool s_enabled = true;
volatile bool s_modemUp = false;
volatile bool s_cmux = false;
volatile int s_pppError = 0;
char s_apn[APN_MAX] = DEFAULT_APN;
char s_user[APN_USER_MAX] = "";
char s_pass[APN_PASS_MAX] = "";
volatile uint32_t s_rx = 0, s_tx = 0;

void setState(DataState st, const char* err = nullptr) {
  bool changed;
  portENTER_CRITICAL(&s_mux);
  changed = s_ds.state != st || (err && strcmp(err, s_ds.lastError) != 0);
  s_ds.state = st;
  if (err) txt::copy(s_ds.lastError, err, sizeof s_ds.lastError);
  if (st != DataState::Connected) {
    s_ds.ip[0] = 0;
    s_ds.dns[0] = 0;
  }
  portEXIT_CRITICAL(&s_mux);
  if (changed) {
    if (err) LOG("DATA", "%s (%s)", dataStateName(st), err);
    else LOG("DATA", "%s", dataStateName(st));
    events::post(Ev::DataChanged);
  }
}

// ---- esp_netif glue ------------------------------------------------------------------
esp_err_t pppTransmit(void*, void* buffer, size_t len) {
  if (!mport::write(mport::kChanData, buffer, len)) return ESP_FAIL;
  s_tx = s_tx + (uint32_t)len;
  return ESP_OK;
}

esp_err_t postAttach(esp_netif_t* netif, void* args) {
  PppDriver* d = static_cast<PppDriver*>(args);
  d->base.netif = netif;
  esp_netif_driver_ifconfig_t ifc = {};
  ifc.handle = d;
  ifc.transmit = pppTransmit;
  return esp_netif_set_driver_config(netif, &ifc);
}

void pppInput(const uint8_t* data, size_t len) {
  s_rx = s_rx + (uint32_t)len;
  if (s_ppp) esp_netif_receive(s_ppp, (void*)data, len, nullptr);
}

void onIpEvent(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == IP_EVENT_PPP_GOT_IP) {
    auto* e = static_cast<ip_event_got_ip_t*>(data);
    esp_netif_dns_info_t dns = {};
    esp_netif_get_dns_info(s_ppp, ESP_NETIF_DNS_MAIN, &dns);
    portENTER_CRITICAL(&s_mux);
    snprintf(s_ds.ip, sizeof s_ds.ip, IPSTR, IP2STR(&e->ip_info.ip));
    snprintf(s_ds.dns, sizeof s_ds.dns, IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    portEXIT_CRITICAL(&s_mux);
    xEventGroupSetBits(s_ev, kGotIp);
  } else if (id == IP_EVENT_PPP_LOST_IP) {
    xEventGroupSetBits(s_ev, kLostIp);
  }
}

void onPppStatus(void*, esp_event_base_t, int32_t id, void*) {
  if (id > NETIF_PPP_ERRORNONE && id < NETIF_PP_PHASE_OFFSET) {
    s_pppError = id;
    xEventGroupSetBits(s_ev, kPppDown);
  } else if (id == NETIF_PPP_PHASE_DEAD || id == NETIF_PPP_CONNECT_FAILED) {
    xEventGroupSetBits(s_ev, kPppDown);
  }
}

const char* pppErrorText(int e) {
  switch (e) {
    case NETIF_PPP_ERRORAUTHFAIL: return "authentication failed (check APN user/password)";
    case NETIF_PPP_ERRORPEERDEAD: return "no answer from the network";
    case NETIF_PPP_ERRORCONNECT: return "connection lost";
    case NETIF_PPP_ERRORPROTOCOL: return "PPP protocol error";
    case NETIF_PPP_ERRORUSER: return "stopped";
    default: return "PPP error";
  }
}

// ---- data channel AT helpers -----------------------------------------------------------
// Sends a command on channel 2 and waits for OK / ERROR / CONNECT / NO CARRIER.
// URCs that the module copies to this channel are simply ignored here: the AT
// channel receives them too.
enum class DataReply { Ok, Error, Connect, NoCarrier, Timeout };

DataReply dataCmd(const char* cmd, uint32_t timeoutMs, const LineFn& fn = nullptr) {
  char line[256];
  dataFlushLines();
  dataSend(cmd);
  dataSend("\r");
  uint32_t start = nowMs();
  while (nowMs() - start < timeoutMs) {
    if (!dataReadLine(line, sizeof line, 100)) continue;
    if (!strcmp(line, cmd)) continue;
    if (!strcmp(line, "OK")) return DataReply::Ok;
    if (txt::startsWith(line, "CONNECT")) return DataReply::Connect;
    if (!strcmp(line, "NO CARRIER")) return DataReply::NoCarrier;
    if (!strcmp(line, "ERROR") || txt::startsWith(line, "+CME ERROR")) {
      LOG("DATA", "%s -> %s", redactCmd(cmd), line);
      return DataReply::Error;
    }
    if (fn) fn(line);
  }
  return DataReply::Timeout;
}

// Writes the PDP context only when it differs (it is stored in the module).
bool configurePdp() {
  char apn[APN_MAX], user[APN_USER_MAX], pass[APN_PASS_MAX];
  portENTER_CRITICAL(&s_mux);
  txt::copy(apn, s_apn, sizeof apn);
  txt::copy(user, s_user, sizeof user);
  txt::copy(pass, s_pass, sizeof pass);
  portEXIT_CRITICAL(&s_mux);
  // Never build an AT command from text that could close the quoted argument
  if (!netrules::atSafe(apn) || !netrules::atSafe(user) || !netrules::atSafe(pass)) {
    LOG("DATA", "APN settings contain characters that are not allowed");
    return false;
  }
  bool same = false;
  dataCmd("AT+CGDCONT?", 3000, [&](const char* l) {
    AtArgs a;
    if (txt::startsWith(l, "+CGDCONT:") && atParseArgs(l, &a) && a.i(0) == 1)
      same = txt::ieq(a.s(1), "IP") && strcmp(a.s(2), apn) == 0;
  });
  if (!same) {
    char cmd[APN_MAX + 32];
    snprintf(cmd, sizeof cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (dataCmd(cmd, 5000) != DataReply::Ok) return false;
    LOG("DATA", "APN set to \"%s\"", apn);
  }
  if (user[0]) {
    bool authSame = false;
    dataCmd("AT+CGAUTH?", 3000, [&](const char* l) {
      AtArgs a;
      if (txt::startsWith(l, "+CGAUTH:") && atParseArgs(l, &a) && a.i(0) == 1)
        authSame = strcmp(a.s(2), user) == 0 && strcmp(a.s(3), pass) == 0;
    });
    if (!authSame) {
      // Note the A76XX order: <cid>,<auth_type>,<passwd>,<user>; 3 = PAP or CHAP
      char cmd[APN_USER_MAX + APN_PASS_MAX + 32];
      snprintf(cmd, sizeof cmd, "AT+CGAUTH=1,3,\"%s\",\"%s\"", pass, user);
      dataCmd(cmd, 5000);
    }
    esp_netif_ppp_set_auth(s_ppp, NETIF_PPP_AUTHTYPE_PAP, user, pass);
  } else {
    esp_netif_ppp_set_auth(s_ppp, NETIF_PPP_AUTHTYPE_NONE, "", "");
  }
  return true;
}

bool canRun() { return s_enabled && s_modemUp && s_cmux && isRegistered(statusSnapshot()); }

// While connected, a short loss of registration (cell change, a CSFB voice call
// on 2G) must not tear down PPP: only give up after this long.
constexpr uint32_t kRegLossGraceMs = 15000;

void waitWake(uint32_t ms) { xEventGroupWaitBits(s_ev, kWake, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)); }

void stopPpp() {
  xEventGroupClearBits(s_ev, kPppDown);
  esp_netif_action_stop(s_ppp, nullptr, 0, nullptr);
  xEventGroupWaitBits(s_ev, kPppDown, pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
  setDataMode(false, nullptr);
  vTaskDelay(pdMS_TO_TICKS(500));
  if (s_modemUp && s_cmux) dataCmd("ATH", 2000);  // make sure the data call is really down
}

void dataTask(void*) {
  uint32_t backoff = DATA_BACKOFF_MIN_MS;
  for (;;) {
    if (!s_enabled) {
      setState(DataState::Off);
      waitWake(5000);
      continue;
    }
    if (s_modemUp && !s_cmux) {
      setState(DataState::Unavailable, "module has no second channel (CMUX)");
      waitWake(5000);
      continue;
    }
    if (!canRun()) {
      setState(DataState::WaitNetwork);
      waitWake(1000);
      continue;
    }

    // ---- dial ----
    setState(DataState::Dialing);
    xEventGroupClearBits(s_ev, kGotIp | kLostIp | kPppDown);
    s_pppError = 0;
    const char* err = nullptr;
    if (dataCmd("AT", 2000) != DataReply::Ok) err = "data channel not answering";
    else if (dataCmd("ATE0", 2000), !configurePdp()) err = "could not set the APN";
    if (!err) {
      DataReply r = dataCmd("ATD*99#", DATA_DIAL_TIMEOUT_MS);
      if (r != DataReply::Connect) err = r == DataReply::Timeout ? "dial timeout" : "network refused the data call";
    }
    if (!err) {
      // ---- PPP ----
      setState(DataState::Negotiating);
      setDataMode(true, pppInput);
      esp_netif_action_start(s_ppp, nullptr, 0, nullptr);
      esp_netif_action_connected(s_ppp, nullptr, 0, nullptr);
      EventBits_t b = xEventGroupWaitBits(s_ev, kGotIp | kPppDown | kWake, pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(DATA_PPP_TIMEOUT_MS));
      if (b & kGotIp) {
        portENTER_CRITICAL(&s_mux);
        s_ds.sessions++;
        s_ds.upSinceMs = nowMs();
        portEXIT_CRITICAL(&s_mux);
        setState(DataState::Connected);
        DataStatus d = dataSnapshot();
        LOG("NET", "IP acquired: %s (DNS %s)", d.ip, d.dns);
        backoff = DATA_BACKOFF_MIN_MS;
        // Stay here until the link drops, data is switched off or the modem goes away.
        uint32_t regLostSince = 0;
        for (;;) {
          EventBits_t w = xEventGroupWaitBits(s_ev, kLostIp | kPppDown | kWake, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
          if (w & (kLostIp | kPppDown)) {
            err = s_pppError ? pppErrorText(s_pppError) : "link lost";
            break;
          }
          if (!s_enabled || !s_modemUp || !s_cmux) break;
          if (isRegistered(statusSnapshot())) {
            regLostSince = 0;
          } else if (!regLostSince) {
            regLostSince = nowMs();
          } else if (nowMs() - regLostSince > kRegLossGraceMs) {
            err = "network lost";
            break;
          }
        }
      } else {
        err = (b & kPppDown) ? pppErrorText(s_pppError) : (b & kWake) ? nullptr : "PPP negotiation timeout";
      }
      stopPpp();
    }
    if (!canRun()) continue;  // switched off / modem gone: no back-off needed
    setState(DataState::Backoff, err ? err : "disconnected");
    waitWake(backoff);
    backoff = netrules::nextBackoff(backoff, DATA_BACKOFF_MIN_MS, DATA_BACKOFF_MAX_MS);
  }
}

}  // namespace

void dataBegin() {
  s_ev = xEventGroupCreate();
  s_enabled = g_cfg.dataEnabled;
  if (g_cfg.apn[0]) txt::copy(s_apn, g_cfg.apn, sizeof s_apn);
  txt::copy(s_user, g_cfg.apnUser, sizeof s_user);
  txt::copy(s_pass, g_cfg.apnPass, sizeof s_pass);
  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
  s_ppp = esp_netif_new(&cfg);
  if (!s_ppp) {
    LOG("DATA", "could not create the PPP interface");
    return;
  }
  s_drv.base.post_attach = postAttach;
  esp_netif_attach(s_ppp, &s_drv);
  esp_netif_ppp_config_t pc = {};
  pc.ppp_phase_event_enabled = true;
  pc.ppp_error_event_enabled = true;
  esp_netif_ppp_set_params(s_ppp, &pc);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, onIpEvent, nullptr);
  esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, onPppStatus, nullptr);
  xTaskCreatePinnedToCore(dataTask, "data", 6144, nullptr, 5, nullptr, 1);
}

void dataModemStarted(bool cmux) {
  s_cmux = cmux;
  s_modemUp = true;
  xEventGroupSetBits(s_ev, kWake);
}

void dataModemLost() {
  s_modemUp = false;
  if (s_ev) xEventGroupSetBits(s_ev, kWake);
}

void dataSetEnabled(bool on) {
  s_enabled = on;
  g_cfg.dataEnabled = on;
  if (s_ev) xEventGroupSetBits(s_ev, kWake);
}

void dataSetApn(const char* apn, const char* user, const char* pass) {
  portENTER_CRITICAL(&s_mux);
  txt::copy(s_apn, apn && apn[0] ? apn : DEFAULT_APN, sizeof s_apn);
  txt::copy(s_user, user ? user : "", sizeof s_user);
  txt::copy(s_pass, pass ? pass : "", sizeof s_pass);
  portEXIT_CRITICAL(&s_mux);
  // Reconnect with the new settings
  if (s_ev) xEventGroupSetBits(s_ev, kPppDown | kWake);
}

DataStatus dataSnapshot() {
  portENTER_CRITICAL(&s_mux);
  DataStatus d = s_ds;
  portEXIT_CRITICAL(&s_mux);
  d.rxBytes = s_rx;
  d.txBytes = s_tx;
  return d;
}

}  // namespace a7672s

void* modem::pppNetif() { return a7672s::s_ppp; }
