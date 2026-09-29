#include "hfp_ag.h"

#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_hf_ag_api.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_bridge.h"
#include "plog.h"
#include "sco_policy.h"
#include "text_util.h"

namespace hfp {
namespace {

constexpr int kMaxDevices = 20;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Callbacks s_cb = {};
bool s_started = false;
volatile bool s_enabled = true;
volatile btl::BtState s_state = btl::BtState::Off;
volatile bool s_scanning = false;
volatile bool s_audio = false;
volatile uint32_t s_audioRate = 0;
uint8_t s_peer[6] = {0};
bool s_hasPeer = false;
char s_peerName[BT_NAME_MAX] = "";
int8_t s_peerBattery = -1;
int s_spkVol = 12;

struct Dev {
  uint8_t bda[6];
  char name[BT_NAME_MAX];
  uint32_t cod;
  int8_t rssi;
  bool audio;
};
Dev s_devs[kMaxDevices];
int s_devCount = 0;

bool s_userDisconnected = false;
uint32_t s_nextReconnect = 0;
uint32_t s_backoff = 8000;
int s_lastSignal = -1, s_lastBatt = -1, s_lastService = -1, s_lastRoam = -1;

btl::CallStateMsg s_call = {};
btl::NetStateMsg s_net = {};
volatile bool s_route = false;                 // S3 wants the call audio in the earbuds
volatile btl::AudioTestMode s_test = btl::AudioTestMode::None;
volatile bool s_scoPending = false;            // we asked for SCO and wait for the result
volatile bool s_scoClosing = false;            // we are closing SCO ourselves
uint32_t s_scoLastTry = 0;
uint32_t s_audioOpenedMs = 0;
volatile bool s_pendingStatus = false;

bool sameAddr(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }
uint32_t nowMs() { return plogMillis(); }

void notifyStatus() {
  if (s_cb.statusChanged) s_cb.statusChanged();
}

void button(btl::ButtonKind k, int v = 0, const char* text = nullptr) {
  if (s_cb.button) s_cb.button(k, v, text ? text : "");
}

Dev* findDev(const uint8_t* bda) {
  for (int i = 0; i < s_devCount; ++i)
    if (sameAddr(s_devs[i].bda, bda)) return &s_devs[i];
  return nullptr;
}

void toDeviceMsg(const Dev& d, btl::DeviceMsg* m) {
  memset(m, 0, sizeof *m);
  memcpy(m->bda, d.bda, 6);
  m->rssi = d.rssi;
  m->isAudio = d.audio;
  m->cod = d.cod;
  txt::copy(m->name, d.name, sizeof m->name);
}

// ---- persisted "last earbuds" (Bluedroid keeps the link keys itself) --------------
void savePeer() {
  nvs_handle_t h;
  if (nvs_open("hfp", NVS_READWRITE, &h) != ESP_OK) return;
  uint8_t old[6] = {0};
  size_t len = sizeof old;
  char oldName[BT_NAME_MAX] = "";
  size_t nlen = sizeof oldName;
  bool same = nvs_get_blob(h, "peer", old, &len) == ESP_OK && sameAddr(old, s_peer) &&
              nvs_get_str(h, "pname", oldName, &nlen) == ESP_OK && strcmp(oldName, s_peerName) == 0;
  if (!same) {  // write only on change (flash wear)
    nvs_set_blob(h, "peer", s_peer, 6);
    nvs_set_str(h, "pname", s_peerName);
    nvs_commit(h);
  }
  nvs_close(h);
}

void loadPeer() {
  nvs_handle_t h;
  if (nvs_open("hfp", NVS_READONLY, &h) != ESP_OK) return;
  size_t len = 6;
  if (nvs_get_blob(h, "peer", s_peer, &len) == ESP_OK && len == 6) {
    s_hasPeer = true;
    size_t nlen = sizeof s_peerName;
    if (nvs_get_str(h, "pname", s_peerName, &nlen) != ESP_OK) s_peerName[0] = 0;
  }
  nvs_close(h);
}

void clearPeer() {
  nvs_handle_t h;
  if (nvs_open("hfp", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "peer");
  nvs_erase_key(h, "pname");
  nvs_commit(h);
  nvs_close(h);
}

// ---------------------------------------------------------------------------
void onDiscoveryResult(esp_bt_gap_cb_param_t* p) {
  char name[BT_NAME_MAX] = "";
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
  btl::DeviceMsg msg;
  bool have = false;
  portENTER_CRITICAL(&s_mux);
  Dev* d = findDev(p->disc_res.bda);
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
    toDeviceMsg(*d, &msg);
    have = true;
  }
  portEXIT_CRITICAL(&s_mux);
  if (have && s_cb.scanResult) s_cb.scanResult(msg);
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
        // ask nameless audio devices for their name
        for (int i = 0; i < s_devCount; ++i)
          if (!s_devs[i].name[0] && s_devs[i].audio) esp_bt_gap_read_remote_name(s_devs[i].bda);
        if (s_cb.scanDone) s_cb.scanDone();
      }
      notifyStatus();
      break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
      if (p->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
        btl::DeviceMsg msg;
        bool have = false;
        portENTER_CRITICAL(&s_mux);
        Dev* d = findDev(p->read_rmt_name.bda);
        if (d) {
          txt::copy(d->name, (const char*)p->read_rmt_name.rmt_name, sizeof d->name);
          toDeviceMsg(*d, &msg);
          have = true;
        }
        bool isPeer = s_hasPeer && sameAddr(s_peer, p->read_rmt_name.bda) && !s_peerName[0];
        if (isPeer) txt::copy(s_peerName, (const char*)p->read_rmt_name.rmt_name, sizeof s_peerName);
        portEXIT_CRITICAL(&s_mux);
        if (have && s_cb.scanResult) s_cb.scanResult(msg);
        if (isPeer) {
          savePeer();
          notifyStatus();
        }
      }
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
btl::CallPhaseW phase() { return (btl::CallPhaseW)s_call.phase; }

esp_hf_call_addr_type_t addrType(const char* number) {
  return number[0] == '+' ? ESP_HF_CALL_ADDR_TYPE_INTERNATIONAL : ESP_HF_CALL_ADDR_TYPE_UNKNOWN;
}

int battLevel() { return s_net.battery > 5 ? 5 : s_net.battery; }

void sendCind(esp_bd_addr_t addr) {
  esp_hf_call_status_t call = phase() == btl::CallPhaseW::Active ? ESP_HF_CALL_STATUS_CALL_IN_PROGRESS
                                                                  : ESP_HF_CALL_STATUS_NO_CALLS;
  esp_hf_call_setup_status_t setup = ESP_HF_CALL_SETUP_STATUS_IDLE;
  if (phase() == btl::CallPhaseW::Incoming) setup = ESP_HF_CALL_SETUP_STATUS_INCOMING;
  else if (phase() == btl::CallPhaseW::Dialing) setup = ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING;
  else if (phase() == btl::CallPhaseW::Alerting) setup = ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING;
  esp_hf_ag_cind_response(addr, call, setup,
                          s_net.service ? ESP_HF_NETWORK_STATE_AVAILABLE : ESP_HF_NETWORK_STATE_NOT_AVAILABLE,
                          s_net.signal > 5 ? 5 : s_net.signal,
                          s_net.roaming ? ESP_HF_ROAMING_STATUS_ACTIVE : ESP_HF_ROAMING_STATUS_INACTIVE, battLevel(),
                          ESP_HF_CALL_HELD_STATUS_NONE);
}

// Apple accessory extensions: most TWS earbuds report their battery with
// AT+IPHONEACCEV once the phone answers AT+XAPL. (UNVERIFIED per earbud model.)
void onUnknownAt(esp_bd_addr_t addr, const char* at) {
  if (!at) {
    esp_hf_ag_unknown_at_send(addr, nullptr);
    return;
  }
  if (txt::startsWith(at, "+XAPL=") || txt::startsWith(at, "AT+XAPL=")) {
    char resp[] = "+XAPL=iPhone,2";            // feature bit 1: battery reporting
    esp_hf_ag_unknown_at_send(addr, resp);
    esp_hf_ag_cmee_send(addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
    return;
  }
  const char* ev = strstr(at, "IPHONEACCEV=");
  if (ev) {
    // AT+IPHONEACCEV=<n>,<key1>,<val1>,...  key 1 = battery 0..9
    const char* p = ev + 12;
    int n = atoi(p);
    for (int i = 0; i < n && p; ++i) {
      p = strchr(p, ',');
      if (!p) break;
      int key = atoi(++p);
      p = strchr(p, ',');
      if (!p) break;
      int val = atoi(++p);
      if (key == 1) {
        s_peerBattery = (int8_t)((val + 1) * 10);
        LOG("BT", "earbud battery %d%%", s_peerBattery);
        s_pendingStatus = true;
      }
    }
    esp_hf_ag_cmee_send(addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
    return;
  }
  esp_hf_ag_unknown_at_send(addr, nullptr);  // replies ERROR
}

void hfCallback(esp_hf_cb_event_t event, esp_hf_cb_param_t* p) {
  switch (event) {
    case ESP_HF_CONNECTION_STATE_EVT: {
      esp_hf_connection_state_t st = p->conn_stat.state;
      if (st == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
        portENTER_CRITICAL(&s_mux);
        memcpy(s_peer, p->conn_stat.remote_bda, 6);
        s_hasPeer = true;
        Dev* d = findDev(s_peer);
        if (d && d->name[0]) txt::copy(s_peerName, d->name, sizeof s_peerName);
        portEXIT_CRITICAL(&s_mux);
        s_state = btl::BtState::Connected;
        s_userDisconnected = false;
        s_backoff = 8000;
        s_peerBattery = -1;
        // The earbuds play their own ring tone (we cannot send one in-band).
        esp_hf_ag_bsir(p->conn_stat.remote_bda, ESP_HF_IN_BAND_RINGTONE_NOT_PROVIDED);
        if (!s_peerName[0]) esp_bt_gap_read_remote_name(p->conn_stat.remote_bda);
        s_lastSignal = s_lastBatt = s_lastService = s_lastRoam = -1;
        savePeer();
        LOG("BT", "HFP connected to %s", s_peerName[0] ? s_peerName : "earbuds");
        // An incoming call may already be ringing
        if (phase() == btl::CallPhaseW::Incoming)
          esp_hf_ag_answer_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_INCOMING,
                                s_call.number, addrType(s_call.number));
        notifyStatus();
      } else if (st == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
        bool wasConnected = s_state == btl::BtState::Connected;
        s_state = s_enabled ? btl::BtState::Idle : btl::BtState::Off;
        s_audio = false;
        s_scoPending = false;
        if (wasConnected) LOG("BT", "HFP disconnected");
        notifyStatus();
      } else if (st == ESP_HF_CONNECTION_STATE_CONNECTING || st == ESP_HF_CONNECTION_STATE_CONNECTED) {
        if (s_state != btl::BtState::Connected) {
          s_state = btl::BtState::Connecting;
          notifyStatus();
        }
      }
      break;
    }
    case ESP_HF_AUDIO_STATE_EVT: {
      esp_hf_audio_state_t a = p->audio_stat.state;
      if (a == ESP_HF_AUDIO_STATE_CONNECTED || a == ESP_HF_AUDIO_STATE_CONNECTED_MSBC) {
        bool ours = s_scoPending;
        s_scoPending = false;
        s_audio = true;
        s_audioOpenedMs = nowMs();
        s_audioRate = a == ESP_HF_AUDIO_STATE_CONNECTED_MSBC ? 16000 : 8000;
        LOG("BT", "voice link open (%s)", s_audioRate == 16000 ? "mSBC 16 kHz" : "CVSD 8 kHz");
        if (s_cb.audioChanged) s_cb.audioChanged(true, s_audioRate);
        // The earbuds took the call audio themselves (e.g. a tap while talking on the handset)
        btl::CallPhaseW ph = phase();
        if (!ours && !s_route && ph != btl::CallPhaseW::Idle && ph != btl::CallPhaseW::Incoming)
          button(btl::ButtonKind::AudioOn);
        notifyStatus();
      } else if (a == ESP_HF_AUDIO_STATE_DISCONNECTED) {
        bool wasOpen = s_audio;
        bool ours = s_scoClosing;
        s_scoClosing = false;
        s_scoPending = false;
        s_audio = false;
        s_audioRate = 0;
        if (wasOpen) {
          LOG("BT", "voice link closed");
          if (s_cb.audioChanged) s_cb.audioChanged(false, 0);
          // The user moved the call back to the phone from the earbuds
          btl::CallPhaseW ph = phase();
          if (!ours && s_route && (ph == btl::CallPhaseW::Active || ph == btl::CallPhaseW::Alerting))
            button(btl::ButtonKind::AudioOff);
        }
        notifyStatus();
      }
      break;
    }
    case ESP_HF_CIND_RESPONSE_EVT:
      sendCind(p->cind_rep.remote_addr);
      break;
    case ESP_HF_COPS_RESPONSE_EVT: {
      char op[24];
      txt::copy(op, s_net.op[0] ? s_net.op : FW_NAME, sizeof op);
      esp_hf_ag_cops_response(p->cops_rep.remote_addr, op);
      break;
    }
    case ESP_HF_CLCC_RESPONSE_EVT: {
      char num[NUMBER_MAX];
      txt::copy(num, s_call.number, sizeof num);
      btl::CallPhaseW ph = phase();
      if (ph != btl::CallPhaseW::Idle) {
        esp_hf_current_call_status_t cs = ESP_HF_CURRENT_CALL_STATUS_ACTIVE;
        if (ph == btl::CallPhaseW::Dialing) cs = ESP_HF_CURRENT_CALL_STATUS_DIALING;
        else if (ph == btl::CallPhaseW::Alerting) cs = ESP_HF_CURRENT_CALL_STATUS_ALERTING;
        else if (ph == btl::CallPhaseW::Incoming) cs = ESP_HF_CURRENT_CALL_STATUS_INCOMING;
        esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 1,
                                s_call.incoming ? ESP_HF_CURRENT_CALL_DIRECTION_INCOMING
                                                : ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                                cs, ESP_HF_CURRENT_CALL_MODE_VOICE, ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, num,
                                addrType(num));
      }
      // index 0 terminates the list (sends OK)
      esp_hf_ag_clcc_response(p->clcc_rep.remote_addr, 0, ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                              ESP_HF_CURRENT_CALL_STATUS_ACTIVE, ESP_HF_CURRENT_CALL_MODE_VOICE,
                              ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
      break;
    }
    case ESP_HF_CNUM_RESPONSE_EVT: {
      char num[NUMBER_MAX];
      txt::copy(num, s_net.ownNumber, sizeof num);
      esp_hf_ag_cnum_response(p->cnum_rep.remote_addr, num[0] ? num : nullptr, num[0] == '+' ? 145 : 129,
                              ESP_HF_SUBSCRIBER_SERVICE_TYPE_VOICE);
      break;
    }
    case ESP_HF_ATA_RESPONSE_EVT:
      LOG("BT", "earbud button: answer");
      button(btl::ButtonKind::Answer);
      break;
    case ESP_HF_CHUP_RESPONSE_EVT:
      LOG("BT", "earbud button: hang up");
      button(btl::ButtonKind::Hangup);
      break;
    case ESP_HF_DIAL_EVT:
      esp_hf_ag_cmee_send(p->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
      if (p->out_call.num_or_loc && p->out_call.type == ESP_HF_DIAL_NUM && p->out_call.num_or_loc[0]) {
        button(btl::ButtonKind::Dial, 0, p->out_call.num_or_loc);
      } else {
        LOG("BT", "earbud button: redial");
        button(btl::ButtonKind::Redial);
      }
      break;
    case ESP_HF_VTS_RESPONSE_EVT:
      if (p->vts_rep.code && p->vts_rep.code[0]) button(btl::ButtonKind::Dtmf, p->vts_rep.code[0]);
      break;
    case ESP_HF_VOLUME_CONTROL_EVT:
      if (p->volume_control.type == ESP_HF_VOLUME_TYPE_SPK) {
        s_spkVol = p->volume_control.volume;
        button(btl::ButtonKind::SpeakerVol, p->volume_control.volume);
      } else {
        button(btl::ButtonKind::MicVol, p->volume_control.volume);
      }
      s_pendingStatus = true;
      break;
    case ESP_HF_BVRA_RESPONSE_EVT:
      button(btl::ButtonKind::VoiceRecognition, p->vra_rep.value == ESP_HF_VR_STATE_ENABLED ? 1 : 0);
      break;
    case ESP_HF_UNAT_RESPONSE_EVT:
      onUnknownAt(p->unat_rep.remote_addr, p->unat_rep.unat);
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

// Mirrors a phone call phase change to the earbuds (RING/CLIP, call setup indicators).
void onPhoneState(const btl::CallStateMsg& now, const btl::CallStateMsg& before) {
  if (s_state != btl::BtState::Connected) return;
  if (now.phase == before.phase) return;
  char num[NUMBER_MAX];
  txt::copy(num, now.phase == (uint8_t)btl::CallPhaseW::Idle ? before.number : now.number, sizeof num);
  esp_hf_call_addr_type_t t = addrType(num);
  switch ((btl::CallPhaseW)now.phase) {
    case btl::CallPhaseW::Incoming:
      // AC_INCALL is a pure state update: the stack sends RING + caller ID.
      esp_hf_ag_answer_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_INCOMING, num, t);
      break;
    case btl::CallPhaseW::Dialing:
      esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING, num, t);
      break;
    case btl::CallPhaseW::Alerting:
      if (before.phase == (uint8_t)btl::CallPhaseW::Idle)
        esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING, num, t);
      esp_hf_ag_out_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING, num, t);
      break;
    case btl::CallPhaseW::Active:
      esp_hf_ag_answer_call(s_peer, 1, 0, ESP_HF_CALL_STATUS_CALL_IN_PROGRESS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      break;
    case btl::CallPhaseW::Idle:
      if (before.phase == (uint8_t)btl::CallPhaseW::Incoming)
        esp_hf_ag_reject_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      else
        esp_hf_ag_end_call(s_peer, 0, 0, ESP_HF_CALL_STATUS_NO_CALLS, ESP_HF_CALL_SETUP_STATUS_IDLE, num, t);
      break;
  }
}

void reportIndicators() {
  if (s_state != btl::BtState::Connected) return;
  int sig = s_net.signal > 5 ? 5 : s_net.signal;
  int batt = battLevel();
  int svc = s_net.service ? 1 : 0;
  int roam = s_net.roaming ? 1 : 0;
  if (sig != s_lastSignal) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_SIGNAL, sig);
  if (batt != s_lastBatt) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_BATTCHG, batt);
  if (svc != s_lastService) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_SERVICE, svc);
  if (roam != s_lastRoam) esp_hf_ag_ciev_report(s_peer, ESP_HF_IND_TYPE_ROAM, roam);
  s_lastSignal = sig;
  s_lastBatt = batt;
  s_lastService = svc;
  s_lastRoam = roam;
}

void scoPolicy(uint32_t now) {
  btl::ScoInputs in;
  in.connected = s_state == btl::BtState::Connected;
  in.audioOpen = s_audio;
  in.pending = s_scoPending;
  in.closing = s_scoClosing;
  in.route = s_route;
  in.phase = phase();
  in.test = s_test;
  in.nowMs = now;
  in.lastTryMs = s_scoLastTry;
  in.audioOpenedMs = s_audioOpenedMs;
  switch (btl::scoDecide(in)) {
    case btl::ScoAction::Open:
      s_scoLastTry = now;
      s_scoPending = true;
      LOG("BT", "opening voice link");
      esp_hf_ag_audio_connect(s_peer);
      break;
    case btl::ScoAction::Close:
      s_scoClosing = true;
      LOG("BT", "closing voice link");
      esp_hf_ag_audio_disconnect(s_peer);
      break;
    case btl::ScoAction::None:
      break;
  }
}

}  // namespace

// ===========================================================================
bool begin(const Callbacks& cb) {
  s_cb = cb;
  loadPeer();
  esp_bt_controller_mem_release(ESP_BT_MODE_BLE);  // Classic only: give the BLE memory back to the heap
  esp_bt_controller_config_t bc = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&bc) != ESP_OK || esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
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

  esp_bt_gap_set_scan_mode(s_enabled ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  s_started = true;
  s_state = s_enabled ? btl::BtState::Idle : btl::BtState::Off;
  s_nextReconnect = nowMs() + 3000;
  const uint8_t* own = esp_bt_dev_get_address();
  LOG("BT", "Bluetooth ready as \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X)%s", BT_DEVICE_NAME, own[0], own[1], own[2],
      own[3], own[4], own[5], s_hasPeer ? ", will reconnect saved earbuds" : "");
  return true;
}

void setEnabled(bool on) {
  if (!s_started || on == s_enabled) return;
  s_enabled = on;
  if (!on) {
    stopScan();
    if (s_state == btl::BtState::Connected || s_state == btl::BtState::Connecting) esp_hf_ag_slc_disconnect(s_peer);
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_state = btl::BtState::Off;
  } else {
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_state = btl::BtState::Idle;
    s_userDisconnected = false;
    s_nextReconnect = nowMs() + 500;
  }
  LOG("BT", "Bluetooth %s", on ? "on" : "off");
  notifyStatus();
}

void startScan(int seconds) {
  if (!s_started || !s_enabled || s_scanning) return;
  portENTER_CRITICAL(&s_mux);
  s_devCount = 0;
  portEXIT_CRITICAL(&s_mux);
  int units = seconds > 0 ? (seconds * 100 + 127) / 128 : 10;  // inquiry length in 1.28 s units
  if (units < 1) units = 1;
  if (units > 0x30) units = 0x30;
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, units, 0);
  s_scanning = true;
  LOG("BT", "scanning for devices");
  notifyStatus();
}

void stopScan() {
  if (s_scanning) esp_bt_gap_cancel_discovery();
}

void connect(const uint8_t bda[6], const char* name) {
  if (!s_started || !s_enabled) return;
  stopScan();
  if (s_state == btl::BtState::Connected && !sameAddr(bda, s_peer)) esp_hf_ag_slc_disconnect(s_peer);
  portENTER_CRITICAL(&s_mux);
  memcpy(s_peer, bda, 6);
  s_hasPeer = true;
  if (name && name[0]) txt::copy(s_peerName, name, sizeof s_peerName);
  else s_peerName[0] = 0;
  portEXIT_CRITICAL(&s_mux);
  s_userDisconnected = false;
  s_state = btl::BtState::Connecting;
  esp_hf_ag_slc_connect(s_peer);
  s_nextReconnect = nowMs() + 20000;
  LOG("BT", "connecting to %s (%02X:%02X:%02X:%02X:%02X:%02X)", name && name[0] ? name : "device", bda[0], bda[1],
      bda[2], bda[3], bda[4], bda[5]);
  notifyStatus();
}

void disconnect() {
  s_userDisconnected = true;
  if (s_state == btl::BtState::Connected || s_state == btl::BtState::Connecting) esp_hf_ag_slc_disconnect(s_peer);
  s_state = s_enabled ? btl::BtState::Idle : btl::BtState::Off;
  notifyStatus();
}

void forget(const uint8_t bda[6]) {
  bool isPeer = s_hasPeer && sameAddr(bda, s_peer);
  if (isPeer && (s_state == btl::BtState::Connected || s_state == btl::BtState::Connecting))
    esp_hf_ag_slc_disconnect(s_peer);
  esp_bt_gap_remove_bond_device((uint8_t*)bda);
  if (isPeer) {
    s_hasPeer = false;
    s_peerName[0] = 0;
    clearPeer();
    s_state = s_enabled ? btl::BtState::Idle : btl::BtState::Off;
  }
  LOG("BT", "forgot %02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
  notifyStatus();
}

void setDiscoverable(bool on) {
  if (!s_started || !s_enabled) return;
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, on ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
}

void setCallState(const btl::CallStateMsg& c) {
  btl::CallStateMsg before = s_call;
  s_call = c;
  s_call.number[NUMBER_MAX - 1] = 0;
  if (before.phase != c.phase) onPhoneState(s_call, before);
}

void setNetState(const btl::NetStateMsg& n) {
  s_net = n;
  s_net.op[sizeof s_net.op - 1] = 0;
  s_net.ownNumber[NUMBER_MAX - 1] = 0;
  reportIndicators();
}

void setRoute(bool earbuds) {
  if (s_route != earbuds) LOG("BT", "call audio route: %s", earbuds ? "earbuds" : "handset");
  s_route = earbuds;
  s_scoLastTry = 0;  // act on the next service() call
}

void setAudioTest(btl::AudioTestMode m) {
  s_test = m;
  s_scoLastTry = 0;
  audio::setTestMode(m);
}

void setSpeakerVolume(int v) {
  s_spkVol = v < 0 ? 0 : v > 15 ? 15 : v;
  if (s_state == btl::BtState::Connected) esp_hf_ag_volume_control(s_peer, ESP_HF_VOLUME_CONTROL_TARGET_SPK, s_spkVol);
}

void fillStatus(btl::StatusMsg* s) {
  memset(s, 0, sizeof *s);
  s->enabled = s_enabled;
  s->state = (uint8_t)s_state;
  s->scanning = s_scanning;
  s->audio = s_audio;
  s->audioRate = (uint16_t)s_audioRate;
  portENTER_CRITICAL(&s_mux);
  s->hasPeer = s_hasPeer;
  memcpy(s->peer, s_peer, 6);
  txt::copy(s->peerName, s_peerName, sizeof s->peerName);
  portEXIT_CRITICAL(&s_mux);
  s->spkVolume = (int8_t)s_spkVol;
  s->peerBattery = s_peerBattery;
  s->bridgeRunning = audio::running();
  s->route = s_route;
}

int bonded(btl::DeviceMsg* out, int max) {
  int num = esp_bt_gap_get_bond_device_num();
  if (num <= 0) return 0;
  esp_bd_addr_t* list = (esp_bd_addr_t*)calloc(num, sizeof(esp_bd_addr_t));
  if (!list) return 0;
  esp_bt_gap_get_bond_device_list(&num, list);
  int n = 0;
  for (int i = 0; i < num && n < max; ++i, ++n) {
    memset(&out[n], 0, sizeof out[n]);
    memcpy(out[n].bda, list[i], 6);
    out[n].rssi = -127;
    out[n].isAudio = 1;
    portENTER_CRITICAL(&s_mux);
    Dev* d = findDev(list[i]);
    if (d) txt::copy(out[n].name, d->name, sizeof out[n].name);
    else if (s_hasPeer && sameAddr(list[i], s_peer)) txt::copy(out[n].name, s_peerName, sizeof out[n].name);
    portEXIT_CRITICAL(&s_mux);
  }
  free(list);
  return n;
}

void service(uint32_t now) {
  if (s_pendingStatus) {
    s_pendingStatus = false;
    notifyStatus();
  }
  if (!s_started || !s_enabled) return;
  scoPolicy(now);
  if (!s_hasPeer || s_userDisconnected || s_scanning) return;
  if (s_state == btl::BtState::Connected) return;
  if ((int32_t)(now - s_nextReconnect) < 0) return;
  s_state = btl::BtState::Connecting;
  esp_hf_ag_slc_connect(s_peer);
  s_nextReconnect = now + s_backoff;
  if (s_backoff < 60000) s_backoff += s_backoff / 2;
  notifyStatus();
}

}  // namespace hfp
