#include "bt_manager.h"

#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_hf_ag_api.h>
#include <string.h>

#include "../audio/audio_bridge.h"
#include "../config/app_config.h"
#include "../core/events.h"
#include "../core/log.h"
#include "../core/text_util.h"
#include "../modem/at_parse.h"
#include "../modem/modem.h"
#include "../store/settings.h"

// Tell the Arduino core not to release the Bluetooth controller memory at boot.
extern "C" bool btInUse(void) { return true; }

namespace {

constexpr int kMaxDevices = 16;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_started = false;
bool s_enabled = true;
volatile bt::State s_state = bt::State::Off;
volatile bool s_scanning = false;
volatile bool s_audio = false;
volatile uint32_t s_audioRate = 0;
uint8_t s_peer[6] = {0};
bool s_hasPeer = false;
char s_peerName[32] = "";
bt::Device s_devs[kMaxDevices];
int s_devCount = 0;
bool s_userDisconnected = false;
uint32_t s_nextReconnect = 0;
uint32_t s_backoff = 8000;
int s_lastSignal = -1, s_lastBatt = -1, s_lastService = -1;

bool sameAddr(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

bt::Device* findDev(const uint8_t* bda) {
  for (int i = 0; i < s_devCount; ++i)
    if (sameAddr(s_devs[i].bda, bda)) return &s_devs[i];
  return nullptr;
}

// ---------------------------------------------------------------------------
void onDiscoveryResult(esp_bt_gap_cb_param_t* p) {
  char name[32] = "";
  uint32_t cod = 0;
  int8_t rssi = -127;
  for (int i = 0; i < p->disc_res.num_prop; ++i) {
    esp_bt_gap_dev_prop_t& pr = p->disc_res.prop[i];
    switch (pr.type) {
      case ESP_BT_GAP_DEV_PROP_COD: cod = *(uint32_t*)pr.val; break;
      case ESP_BT_GAP_DEV_PROP_RSSI: rssi = *(int8_t*)pr.val; break;
      case ESP_BT_GAP_DEV_PROP_BDNAME: {
        int n = pr.len < (int)sizeof name - 1 ? pr.len : (int)sizeof name - 1;
        memcpy(name, pr.val, n);
        name[n] = 0;
        break;
      }
      case ESP_BT_GAP_DEV_PROP_EIR: {
        if (name[0]) break;
        uint8_t len = 0;
        uint8_t* d = esp_bt_gap_resolve_eir_data((uint8_t*)pr.val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
        if (!d) d = esp_bt_gap_resolve_eir_data((uint8_t*)pr.val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
        if (d) {
          int n = len < sizeof name - 1 ? len : sizeof name - 1;
          memcpy(name, d, n);
          name[n] = 0;
        }
        break;
      }
      default: break;
    }
  }
  portENTER_CRITICAL(&s_mux);
  bt::Device* d = findDev(p->disc_res.bda);
  if (!d && s_devCount < kMaxDevices) {
    d = &s_devs[s_devCount++];
    memset(d, 0, sizeof *d);
    memcpy(d->bda, p->disc_res.bda, 6);
    d->rssi = -127;
  }
  if (d) {
    if (name[0]) txt::copy(d->name, name, sizeof d->name);
    if (cod) {
      d->cod = cod;
      d->audio = esp_bt_gap_get_cod_major_dev(cod) == ESP_BT_COD_MAJOR_DEV_AV;
    }
    if (rssi != -127) d->rssi = rssi;
  }
  portEXIT_CRITICAL(&s_mux);
  char a[18];
  bt::formatAddress(p->disc_res.bda, a);
  LOG("BT", "found %s \"%s\" class 0x%06x rssi %d", a, name, (unsigned)cod, rssi);
  events::post(Ev::BtScanResult);
}

// Asks nameless audio devices for their name one at a time (parallel
// requests are rejected by the stack).
void requestNextName() {
  for (int i = 0; i < s_devCount; ++i)
    if (!s_devs[i].name[0] && s_devs[i].audio && !s_devs[i].nameAsked) {
      s_devs[i].nameAsked = true;
      esp_bt_gap_read_remote_name(s_devs[i].bda);
      return;
    }
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
      onDiscoveryResult(p);
      break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
        s_scanning = true;
      } else {
        s_scanning = false;
        LOG("BT", "scan finished: %d device(s)", s_devCount);
        requestNextName();
        events::post(Ev::BtScanDone);
      }
      break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
      if (p->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
        portENTER_CRITICAL(&s_mux);
        bt::Device* d = findDev(p->read_rmt_name.bda);
        if (d) txt::copy(d->name, (const char*)p->read_rmt_name.rmt_name, sizeof d->name);
        if (s_hasPeer && sameAddr(s_peer, p->read_rmt_name.bda) && !s_peerName[0])
          txt::copy(s_peerName, (const char*)p->read_rmt_name.rmt_name, sizeof s_peerName);
        portEXIT_CRITICAL(&s_mux);
        LOG("BT", "name: %s", (const char*)p->read_rmt_name.rmt_name);
        events::post(Ev::BtScanResult);
      }
      requestNextName();
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) LOG("BT", "paired with %s", (const char*)p->auth_cmpl.device_name);
      else LOG("BT", "pairing failed (status %d)", p->auth_cmpl.stat);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
      // Legacy pairing: headsets use a fixed PIN, almost always 0000.
      esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
      esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
      LOG("BT", "legacy PIN requested - replied 0000");
      break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
      esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);  // "Just Works" for earbuds
      break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      LOG("BT", "passkey %06u", (unsigned)p->key_notif.passkey);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
esp_hf_call_addr_type_t addrType(const char* number) {
  return number[0] == '+' ? ESP_HF_CALL_ADDR_TYPE_INTERNATIONAL : ESP_HF_CALL_ADDR_TYPE_UNKNOWN;
}

int signalLevel(const NetStatus& st) {
  int bars = csqToBars(st.csq);  // 0..4
  return bars == 0 ? 0 : bars + 1;
}

void sendCind(esp_bd_addr_t addr) {
  NetStatus st = modem::status();
  CallInfo c = modem::call();
  esp_hf_call_status_t call = c.phase == CallPhase::Active ? ESP_HF_CALL_STATUS_CALL_IN_PROGRESS : ESP_HF_CALL_STATUS_NO_CALLS;
  esp_hf_call_setup_status_t setup = ESP_HF_CALL_SETUP_STATUS_IDLE;
  if (c.phase == CallPhase::Incoming) setup = ESP_HF_CALL_SETUP_STATUS_INCOMING;
  else if (c.phase == CallPhase::Dialing) setup = ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING;
  else if (c.phase == CallPhase::Alerting) setup = ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING;
  bool reg = st.reg == RegState::Home || st.reg == RegState::Roaming;
  int batt = s_lastBatt >= 0 ? s_lastBatt : 5;
  esp_hf_ag_cind_response(addr, call, setup, reg ? ESP_HF_NETWORK_STATE_AVAILABLE : ESP_HF_NETWORK_STATE_NOT_AVAILABLE,
                          signalLevel(st), st.reg == RegState::Roaming ? ESP_HF_ROAMING_STATUS_ACTIVE : ESP_HF_ROAMING_STATUS_INACTIVE,
                          batt, ESP_HF_CALL_HELD_STATUS_NONE);
}

void hfCallback(esp_hf_cb_event_t event, esp_hf_cb_param_t* p) {
  switch (event) {
    case ESP_HF_CONNECTION_STATE_EVT: {
      esp_hf_connection_state_t st = p->conn_stat.state;
      if (st == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
        portENTER_CRITICAL(&s_mux);
        memcpy(s_peer, p->conn_stat.remote_bda, 6);
        s_hasPeer = true;
        bt::Device* d = findDev(s_peer);
        if (d && d->name[0]) txt::copy(s_peerName, d->name, sizeof s_peerName);
        portEXIT_CRITICAL(&s_mux);
        s_state = bt::State::Connected;
        s_userDisconnected = false;
        s_backoff = 8000;
        // The earbuds play their own ring tone (we cannot send one in-band).
        esp_hf_ag_bsir(p->conn_stat.remote_bda, ESP_HF_IN_BAND_RINGTONE_NOT_PROVIDED);
        if (!s_peerName[0]) esp_bt_gap_read_remote_name(p->conn_stat.remote_bda);
        s_lastSignal = s_lastBatt = s_lastService = -1;
        LOG("BT", "HFP connected to %s", s_peerName[0] ? s_peerName : "earbuds");
        events::post(Ev::BtChanged, 1);
      } else if (st == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
        bool wasConnected = s_state == bt::State::Connected;
        s_state = bt::State::Idle;
        s_audio = false;
        if (wasConnected) LOG("BT", "HFP disconnected");
        events::post(Ev::BtChanged, wasConnected ? 0 : 2);  // 0 = link lost, 2 = attempt failed
      } else if (st == ESP_HF_CONNECTION_STATE_CONNECTING || st == ESP_HF_CONNECTION_STATE_CONNECTED) {
        if (s_state != bt::State::Connected) s_state = bt::State::Connecting;
      }
      break;
    }
    case ESP_HF_AUDIO_STATE_EVT: {
      esp_hf_audio_state_t a = p->audio_stat.state;
      if (a == ESP_HF_AUDIO_STATE_CONNECTED || a == ESP_HF_AUDIO_STATE_CONNECTED_MSBC) {
        s_audio = true;
        s_audioRate = a == ESP_HF_AUDIO_STATE_CONNECTED_MSBC ? 16000 : 8000;
        LOG("BT", "voice link open (%s)", s_audioRate == 16000 ? "mSBC 16 kHz" : "CVSD 8 kHz");
        events::post(Ev::BtAudio, 1, (int32_t)s_audioRate);
      } else if (a == ESP_HF_AUDIO_STATE_DISCONNECTED) {
        if (s_audio) LOG("BT", "voice link closed");
        s_audio = false;
        s_audioRate = 0;
        events::post(Ev::BtAudio, 0, 0);
      }
      break;
    }
    case ESP_HF_CIND_RESPONSE_EVT:
      sendCind(p->cind_rep.remote_addr);
      break;
    case ESP_HF_COPS_RESPONSE_EVT: {
      NetStatus st = modem::status();
      char op[24];
      txt::copy(op, st.op[0] ? st.op : "TouchPhone", sizeof op);
      esp_hf_ag_cops_response(p->cops_rep.remote_addr, op);
      break;
    }
    case ESP_HF_CLCC_RESPONSE_EVT: {
      CallInfo c = modem::call();
      char num[NUMBER_MAX];
      txt::copy(num, c.number, sizeof num);
      if (c.phase != CallPhase::Idle) {
        esp_hf_current_call_status_t cs = ESP_HF_CURRENT_CALL_STATUS_ACTIVE;
        if (c.phase == CallPhase::Dialing) cs = ESP_HF_CURRENT_CALL_STATUS_DIALING;
        else if (c.phase == CallPhase::Alerting) cs = ESP_HF_CURRENT_CALL_STATUS_ALERTING;
        else if (c.phase == CallPhase::Incoming) cs = ESP_HF_CURRENT_CALL_STATUS_INCOMING;
        esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 1,
                                c.incoming ? ESP_HF_CURRENT_CALL_DIRECTION_INCOMING : ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                                cs, ESP_HF_CURRENT_CALL_MODE_VOICE, ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, num, addrType(num));
      }
      esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 0, ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                              ESP_HF_CURRENT_CALL_STATUS_ACTIVE, ESP_HF_CURRENT_CALL_MODE_VOICE,
                              ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
      break;
    }
    case ESP_HF_CNUM_RESPONSE_EVT: {
      NetStatus st = modem::status();
      char num[NUMBER_MAX];
      txt::copy(num, st.ownNumber, sizeof num);
      esp_hf_ag_cnum_response(p->cnum_rep.remote_addr, num[0] ? num : nullptr, num[0] == '+' ? 145 : 129,
                              ESP_HF_SUBSCRIBER_SERVICE_TYPE_VOICE);
      break;
    }
    case ESP_HF_ATA_RESPONSE_EVT:
      LOG("BT", "earbud button: answer");
      events::post(Ev::BtAnswer);
      break;
    case ESP_HF_CHUP_RESPONSE_EVT:
      LOG("BT", "earbud button: hang up");
      events::post(Ev::BtHangup);
      break;
    case ESP_HF_DIAL_EVT:
      esp_hf_ag_cmee_send(p->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
      if (p->out_call.num_or_loc && p->out_call.type == ESP_HF_DIAL_NUM && p->out_call.num_or_loc[0]) {
        events::post(Ev::BtDial, 0, 0, strdup(p->out_call.num_or_loc));
      } else {
        LOG("BT", "earbud button: redial");
        events::post(Ev::BtRedial);
      }
      break;
    case ESP_HF_VTS_RESPONSE_EVT:
      if (p->vts_rep.code && p->vts_rep.code[0]) modem::dtmf(p->vts_rep.code[0]);
      break;
    case ESP_HF_VOLUME_CONTROL_EVT:
      events::post(Ev::BtVolume, p->volume_control.type == ESP_HF_VOLUME_TYPE_SPK ? 0 : 1, p->volume_control.volume);
      break;
    case ESP_HF_UNAT_RESPONSE_EVT:
      esp_hf_ag_unknown_at_send(p->unat_rep.remote_addr, nullptr);  // replies ERROR
      break;
    case ESP_HF_BCS_RESPONSE_EVT:
      LOG("BT", "codec negotiated: %s", p->bcs_rep.mode == ESP_HF_WBS_YES ? "mSBC" : "CVSD");
      break;
    case ESP_HF_PROF_STATE_EVT:
      if (p->prof_stat.state == ESP_HF_INIT_SUCCESS) LOG("BT", "HFP audio gateway ready");
      break;
    default:
      break;
  }
}

}  // namespace

// ===========================================================================
namespace bt {

bool begin(bool enabled) {
  s_enabled = enabled;
  Settings& cfg = settings::get();
  if (cfg.hasPeer) {
    memcpy(s_peer, cfg.peer, 6);
    s_hasPeer = true;
    txt::copy(s_peerName, cfg.peerName, sizeof s_peerName);
  }
  if (!btStarted() && !btStartMode(BT_MODE_CLASSIC_BT)) {
    LOG("BT", "controller start failed");
    return false;
  }
  esp_bluedroid_config_t bcfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  bcfg.ssp_en = true;
  if (esp_bluedroid_init_with_cfg(&bcfg) != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
    LOG("BT", "bluedroid init failed");
    return false;
  }
  esp_bt_gap_register_callback(gapCallback);
  esp_bt_gap_set_device_name(BT_DEVICE_NAME);

  esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
  esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof iocap);
  esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
  esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 4, pin);

  esp_bt_cod_t cod = {};
  cod.major = ESP_BT_COD_MAJOR_DEV_PHONE;
  cod.minor = 0x01;  // cellular
  cod.service = ESP_BT_COD_SRVC_TELEPHONY | ESP_BT_COD_SRVC_AUDIO;
  esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);

  esp_hf_ag_register_callback(hfCallback);
  esp_hf_ag_init();
  esp_hf_ag_register_data_callback(audio::onHfIncoming, audio::onHfOutgoing);

  esp_bt_gap_set_scan_mode(enabled ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  s_started = true;
  s_state = enabled ? State::Idle : State::Off;
  s_nextReconnect = millis() + 4000;
  LOG("BT", "Bluetooth ready as \"%s\"%s", BT_DEVICE_NAME, s_hasPeer ? ", will reconnect saved earbuds" : "");
  return true;
}

void setEnabled(bool on) {
  if (!s_started || on == s_enabled) return;
  s_enabled = on;
  if (!on) {
    stopScan();
    if (s_state == State::Connected || s_state == State::Connecting) esp_hf_ag_slc_disconnect(s_peer);
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_state = State::Off;
  } else {
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_state = State::Idle;
    s_userDisconnected = false;
    s_nextReconnect = millis() + 500;
  }
  events::post(Ev::BtChanged, 2);
}

bool enabled() { return s_enabled; }
State state() { return s_state; }
bool connected() { return s_state == State::Connected; }
bool audioOpen() { return s_audio; }
uint32_t audioRate() { return s_audioRate; }

const char* stateText() {
  switch (s_state) {
    case State::Off: return "Off";
    case State::Idle: return s_hasPeer ? "Not connected" : "No earbuds paired";
    case State::Connecting: return "Connecting...";
    case State::Connected: return s_audio ? "Connected - audio on" : "Connected";
  }
  return "";
}

const char* peerName() { return s_peerName[0] ? s_peerName : (s_hasPeer ? "Earbuds" : ""); }

bool peerAddress(uint8_t out[6]) {
  if (!s_hasPeer) return false;
  memcpy(out, s_peer, 6);
  return true;
}

void startScan() {
  if (!s_started || !s_enabled || s_scanning) return;
  portENTER_CRITICAL(&s_mux);
  s_devCount = 0;
  portEXIT_CRITICAL(&s_mux);
  esp_err_t e = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);  // ~12.8 s
  if (e != ESP_OK) {
    LOG("BT", "scan could not start: %s", esp_err_to_name(e));
    s_scanning = false;
    events::post(Ev::BtScanDone);
    return;
  }
  s_scanning = true;
  LOG("BT", "scanning for devices");
}

void stopScan() {
  if (s_scanning) esp_bt_gap_cancel_discovery();
}

bool scanning() { return s_scanning; }

int devices(Device* out, int max) {
  portENTER_CRITICAL(&s_mux);
  int n = 0;
  // earbuds/headsets first, then everything else
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < s_devCount && n < max; ++i)
      if (s_devs[i].audio == (pass == 0)) out[n++] = s_devs[i];
  portEXIT_CRITICAL(&s_mux);
  return n;
}

void connect(const uint8_t bda[6], const char* name) {
  if (!s_started || !s_enabled) return;
  stopScan();
  if (s_state == State::Connected && !sameAddr(bda, s_peer)) esp_hf_ag_slc_disconnect(s_peer);
  portENTER_CRITICAL(&s_mux);
  memcpy(s_peer, bda, 6);
  s_hasPeer = true;
  if (name && name[0]) txt::copy(s_peerName, name, sizeof s_peerName);
  else s_peerName[0] = 0;
  portEXIT_CRITICAL(&s_mux);
  s_userDisconnected = false;
  s_state = State::Connecting;
  esp_hf_ag_slc_connect(s_peer);
  s_nextReconnect = millis() + 20000;
  char a[18];
  formatAddress(bda, a);
  LOG("BT", "connecting to %s (%s)", name && name[0] ? name : "device", a);
  events::post(Ev::BtChanged, 2);
}

void connectSaved() {
  if (s_hasPeer) connect(s_peer, s_peerName);
}

void disconnect() {
  s_userDisconnected = true;
  if (s_state == State::Connected || s_state == State::Connecting) esp_hf_ag_slc_disconnect(s_peer);
  s_state = State::Idle;
  events::post(Ev::BtChanged, 2);
}

void forget() {
  if (s_hasPeer) {
    if (s_state == State::Connected || s_state == State::Connecting) esp_hf_ag_slc_disconnect(s_peer);
    esp_bt_gap_remove_bond_device(s_peer);
  }
  s_hasPeer = false;
  s_peerName[0] = 0;
  s_state = s_enabled ? State::Idle : State::Off;
  events::post(Ev::BtChanged, 2);
}

void setDiscoverable(bool on) {
  if (!s_started || !s_enabled) return;
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, on ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
}

void openAudio() {
  if (s_state == State::Connected && !s_audio) esp_hf_ag_audio_connect(s_peer);
}

void closeAudio() {
  if (s_audio) esp_hf_ag_audio_disconnect(s_peer);
}

static int s_spkVol = 12;

void setSpeakerVolume(int v) {
  s_spkVol = v < 0 ? 0 : v > 15 ? 15 : v;
  if (s_state == State::Connected) esp_hf_ag_volume_control(s_peer, ESP_HF_VOLUME_CONTROL_TARGET_SPK, s_spkVol);
}

int speakerVolume() { return s_spkVol; }

void noteSpeakerVolume(int v) { s_spkVol = v; }

void onPhoneState(const CallInfo& now, const CallInfo& before) {
  if (s_state != State::Connected) return;
  if (now.phase == before.phase) return;
  char num[NUMBER_MAX];
  txt::copy(num, now.phase == CallPhase::Idle ? before.number : now.number, sizeof num);
  esp_hf_call_addr_type_t t = addrType(num);
  switch (now.phase) {
    case CallPhase::Incoming:
      // AC_INCALL is a pure state update: the stack sends RING + caller ID.
      esp_hf_ag_answer_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_INCOMING, num, t);
      break;
    case CallPhase::Dialing:
      esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING, num, t);
      break;
    case CallPhase::Alerting:
      if (before.phase == CallPhase::Idle)
        esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING, num, t);
      esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING, num, t);
      break;
    case CallPhase::Active:
      esp_hf_ag_answer_call(s_peer, 1, 0, ESP_HF_CALL_STATUS_CALL_IN_PROGRESS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      break;
    case CallPhase::Idle:
      if (before.phase == CallPhase::Incoming)
        esp_hf_ag_reject_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      else
        esp_hf_ag_end_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      break;
  }
}

void onNetStatus(const NetStatus& st, int battPct) {
  if (s_state != State::Connected) return;
  int sig = signalLevel(st);
  int batt = battPct < 0 ? 5 : (battPct + 10) / 20;
  if (batt > 5) batt = 5;
  int svc = (st.reg == RegState::Home || st.reg == RegState::Roaming) ? 1 : 0;
  if (sig != s_lastSignal) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_SIGNAL, sig);
  if (batt != s_lastBatt) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_BATTCHG, batt);
  if (svc != s_lastService) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_SERVICE, svc);
  s_lastSignal = sig;
  s_lastBatt = batt;
  s_lastService = svc;
}

void service(uint32_t nowMs) {
  if (!s_started || !s_enabled || !s_hasPeer || s_userDisconnected || s_scanning) return;
  if (s_state == State::Connected) return;
  if ((int32_t)(nowMs - s_nextReconnect) < 0) return;
  if (modem::call().phase != CallPhase::Idle && s_state == State::Connecting) return;
  s_state = State::Connecting;
  esp_hf_ag_slc_connect(s_peer);
  s_nextReconnect = nowMs + s_backoff;
  if (s_backoff < 60000) s_backoff += s_backoff / 2;
}

void formatAddress(const uint8_t b[6], char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

}  // namespace bt
