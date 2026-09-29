// Network registration, signal, operator, SIM, module identity, supply voltage,
// temperature and network time.
//
// Registration is tracked for both the circuit-switched domain (+CREG, needed
// for calls/SMS via CSFB on 2G) and the LTE packet domain (+CEREG). The phone
// counts as registered when either reports home/roaming; the access technology
// (4G/2G) comes from the AcT fields and AT+CPSI?.
#include <stdio.h>
#include <stdlib.h>

#include "a7672s_internal.h"
#include "plog.h"
#include "s3_config.h"
#include "text_util.h"

namespace a7672s {
namespace {

RegState s_regCs = RegState::Unknown;
RegState s_regEps = RegState::Unknown;
int s_actCs = -1, s_actEps = -1;
uint32_t s_tPoll = 0, s_tSlow = 0, s_tClock = 0;
uint32_t s_searchSince = 0;
bool s_searchWarned = false;

bool reg(RegState r) { return r == RegState::Home || r == RegState::Roaming; }

void applyRegistration() {
  RegState best = reg(s_regCs) ? s_regCs : reg(s_regEps) ? s_regEps : s_regCs != RegState::Unknown ? s_regCs : s_regEps;
  Rat rat = Rat::None;
  if (reg(s_regEps) && (s_actEps == 7 || s_actEps < 0)) rat = Rat::Lte;
  else if (reg(s_regCs)) rat = s_actCs == 7 ? Rat::Lte : Rat::Gsm;
  bool was = isRegistered(statusSnapshot());
  updateStatus([&](NetStatus& s) {
    s.reg = best;
    if (rat != Rat::None || !reg(best)) s.rat = rat;
  });
  bool now = reg(best);
  NetStatus st = statusSnapshot();
  if (!was && now) {
    LOG("NET", "Registered%s (%s)", best == RegState::Roaming ? " - roaming" : "", modem::ratText(st));
    s_tSlow = s_tClock = nowMs() + 500;  // fetch operator name, network time, CPSI
    s_searchWarned = false;
    if (st.modem == ModemState::NetworkSearch) setModemState(ModemState::Ready);
  } else if (was && !now) {
    LOG("NET", "Searching...");
    s_searchSince = nowMs();
    if (st.modem == ModemState::Ready) setModemState(ModemState::NetworkSearch);
  }
}

void queryRegistration() {
  exec("AT+CREG?");
  exec("AT+CEREG?");
}

}  // namespace

bool netHandleLine(const char* line) {
  SimState sim;
  RegState r;
  int n1, n2, act;
  uint32_t epoch;
  if (parseCpin(line, &sim)) {
    SimState before = statusSnapshot().sim;
    updateStatus([&](NetStatus& s) { s.sim = sim; });
    if (before != sim && before != SimState::Unknown) LOG("SIM", "state changed (%d -> %d)", (int)before, (int)sim);
    return true;
  }
  if (parseRegistration(line, "+CREG:", &r, &act)) {
    s_regCs = r;
    if (act >= 0) s_actCs = act;
    applyRegistration();
    return true;
  }
  if (parseRegistration(line, "+CEREG:", &r, &act)) {
    s_regEps = r;
    if (act >= 0) s_actEps = act;
    applyRegistration();
    return true;
  }
  if (parseRegistration(line, "+CGREG:", &r, &act)) return true;  // 2G/3G packet domain: informational
  if (parseCsq(line, &n1)) {
    updateStatus([&](NetStatus& s) { s.csq = (int8_t)n1; });
    return true;
  }
  if (txt::startsWith(line, "+COPS:")) {
    char op[OPERATOR_MAX];
    if (parseCops(line, op, sizeof op) && op[0]) {
      bool changed = strcmp(statusSnapshot().op, op) != 0;
      updateStatus([&](NetStatus& s) { txt::copy(s.op, op, sizeof s.op); });
      if (changed) LOG("NET", "Operator: %s", op);
    }
    return true;
  }
  CpsiInfo ci;
  if (parseCpsi(line, &ci)) {
    updateStatus([&](NetStatus& s) {
      if (ci.rat != Rat::None && isRegistered(s)) s.rat = ci.rat;
      txt::copy(s.band, ci.band, sizeof s.band);
      s.rsrp = (int16_t)ci.rsrpDbm;
      s.rsrq = (int8_t)ci.rsrqDb;
      s.sinr = (int8_t)ci.sinrDb;
    });
    return true;
  }
  if (parseCbc(line, &n1, &n2)) {
    updateStatus([&](NetStatus& s) { s.battMv = (uint16_t)n2; });
    return true;
  }
  if (parseCpmutemp(line, &n1)) {
    updateStatus([&](NetStatus& s) { s.modemTempC = (int8_t)n1; });
    return true;
  }
  if (parseCclk(line, &epoch, &n1)) {
    if (epoch >= 1704067200) {
      g_tzQuarters = n1;
      setSystemTime(epoch, "network");
    }
    return true;
  }
  if (parseCtzv(line, &n1)) {
    g_tzQuarters = n1;
    s_tClock = nowMs() + 1500;  // read the updated clock
    return true;
  }
  if (parseCnmp(line, &n1)) return true;
  if (isPdpDeactivation(line)) {
    LOG("NET", "network dropped the data context (%s)", line);
    return true;
  }
  if (strcmp(line, "PB DONE") == 0) return true;
  return false;
}

void netReadIdentity() {
  exec("AT+CGMM", 2000, [](const char* l) {
    const char* v = txt::startsWith(l, "+CGMM:") ? l + 6 : l;
    while (*v == ' ') ++v;
    if (*v && !txt::startsWith(v, "AT")) updateStatus([&](NetStatus& s) { txt::copy(s.model, v, sizeof s.model); });
  });
  exec("AT+CGMR", 2000, [](const char* l) {
    const char* v = txt::startsWith(l, "+CGMR:") ? l + 6 : txt::startsWith(l, "Revision:") ? l + 9 : l;
    while (*v == ' ') ++v;
    if (*v) updateStatus([&](NetStatus& s) { txt::copy(s.fw, v, sizeof s.fw); });
  });
  exec("AT+CGSN", 2000, [](const char* l) {
    const char* v = txt::startsWith(l, "+CGSN:") ? l + 6 : l;
    while (*v == ' ') ++v;
    if (strlen(v) >= 14 && v[0] >= '0' && v[0] <= '9')
      updateStatus([&](NetStatus& s) { txt::copy(s.imei, v, sizeof s.imei); });
  });
  NetStatus st = statusSnapshot();
  LOG("MODEM", "%s, firmware %s", st.model[0] ? st.model : "unknown model", st.fw[0] ? st.fw : "?");
}

void netConfigure() {
  s_regCs = s_regEps = RegState::Unknown;
  s_actCs = s_actEps = -1;
  s_searchSince = nowMs();
  s_searchWarned = false;
  ok("AT+CREG=2");      // +CREG: <stat>,<lac>,<ci>,<AcT> on every change
  ok("AT+CEREG=2");     // same for the LTE packet domain
  ok("AT+COPS=3,0");    // long alphanumeric operator name
  ok("AT+CTZU=1");      // take the time zone / clock from the network (NITZ)
  ok("AT+CTZR=1");      // +CTZV when the time zone changes
  // Network mode is saved inside the module: only write it when it differs.
  int mode = -1;
  exec("AT+CNMP?", 3000, [&](const char* l) { parseCnmp(l, &mode); });
  if (mode >= 0 && mode != (int)g_cfg.netMode) netSetMode(g_cfg.netMode);
  if (statusSnapshot().sim == SimState::Ready) {
    exec("AT+CICCID", 2000, [](const char* l) {
      char id[24];
      if (parseIccid(l, id, sizeof id)) updateStatus([&](NetStatus& s) { txt::copy(s.iccid, id, sizeof s.iccid); });
    });
    exec("AT+CIMI", 2000, [](const char* l) {
      if (strlen(l) >= 10 && l[0] >= '0' && l[0] <= '9')
        updateStatus([&](NetStatus& s) { txt::copy(s.imsi, l, sizeof s.imsi); });
    });
    exec("AT+CNUM", 3000, [](const char* l) {
      char num[NUMBER_MAX];
      if (parseCnum(l, num, sizeof num))
        updateStatus([&](NetStatus& s) { txt::copy(s.ownNumber, num, sizeof s.ownNumber); });
    });
  }
  queryRegistration();
  exec("AT+CSQ");
  s_tPoll = nowMs() + MODEM_POLL_SEARCH_MS;
}

void netRefreshSoon() { s_tPoll = s_tSlow = s_tClock = nowMs(); }

void netSetMode(NetMode m) {
  char cmd[20];
  snprintf(cmd, sizeof cmd, "AT+CNMP=%d", (int)m);
  if (ok(cmd, 9000)) {
    g_cfg.netMode = m;
    LOG("NET", "network mode: %s", m == NetMode::Auto ? "auto (4G/2G)" : m == NetMode::LteOnly ? "4G only" : "2G only");
  }
}

void netPeriodic(uint32_t now) {
  NetStatus st = statusSnapshot();
  bool registered = isRegistered(st);
  // Registration may already have arrived while the start-up sequence was running.
  if (st.modem == ModemState::NetworkSearch && registered) setModemState(ModemState::Ready);
  else if (st.modem == ModemState::Ready && !registered) setModemState(ModemState::NetworkSearch);
  if ((int32_t)(now - s_tPoll) >= 0) {
    exec("AT+CSQ");
    queryRegistration();
    s_tPoll = now + (registered ? MODEM_POLL_IDLE_MS : MODEM_POLL_SEARCH_MS);
  }
  if ((int32_t)(now - s_tSlow) >= 0) {
    exec("AT+CBC");
    exec("AT+CPMUTEMP");
    if (registered) {
      exec("AT+COPS?", 5000);
      exec("AT+CPSI?", 3000);
    }
    if (st.sim != SimState::Ready) exec("AT+CPIN?", 5000);
    s_tSlow = now + MODEM_POLL_SLOW_MS;
  }
  if ((int32_t)(now - s_tClock) >= 0) {
    if (registered) exec("AT+CCLK?");
    s_tClock = now + MODEM_CLOCK_POLL_MS;
  }
  if (!registered && st.sim == SimState::Ready && !s_searchWarned && now - s_searchSince > MODEM_SEARCH_WARN_MS) {
    s_searchWarned = true;
    LOG("NET", "no network after %u s (CSQ %d) - check the antenna, coverage and the SIM's plan",
        MODEM_SEARCH_WARN_MS / 1000, st.csq);
  }
}

}  // namespace a7672s
