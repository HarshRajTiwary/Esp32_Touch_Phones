#pragma once
// Plain data types shared by the modem, storage, networking, Bluetooth-client
// and UI layers of the S3 phone. No ESP-IDF dependencies so tests/host and the
// UI simulator can include it.
#include <stdint.h>

#include "app_limits.h"

// ---- modem start-up state machine (docs/MODEM.md) --------------------------
enum class ModemState : uint8_t {
  Off,            // not started / powered down
  PoweringOn,     // PWRKEY pulse
  Booting,        // waiting for the first "OK" (up to ~20 s)
  AtTest,         // echo off, identity, error format
  CmuxStart,      // AT+CMUX + opening the virtual channels
  SimCheck,       // AT+CPIN?
  NetworkSearch,  // waiting for +CREG/+CEREG registration
  Ready,          // registered, calls/SMS possible
  NotResponding,  // gave up for now, recovery scheduled
  Recovering,     // power-cycling the module
};
const char* modemStateName(ModemState s);

enum class SimState : uint8_t { Unknown, Ready, Absent, PinRequired, PukRequired, Error };
enum class RegState : uint8_t { Unknown, NotRegistered, Searching, Home, Roaming, Denied };
enum class Rat : uint8_t { None, Gsm, Lte };          // radio access technology in use
enum class NetMode : uint8_t { Auto = 2, GsmOnly = 13, LteOnly = 38 };  // AT+CNMP values

// Overall phone readiness, derived from the states above.
enum class PhoneState : uint8_t {
  Boot, InitializingModem, NoSim, SimLocked, SearchingNetwork, Ready, Error
};

struct NetStatus {
  ModemState modem = ModemState::Off;
  SimState sim = SimState::Unknown;
  RegState reg = RegState::Unknown;
  Rat rat = Rat::None;
  int8_t csq = 99;            // 0..31, 99 = unknown
  int16_t rsrp = 0;           // LTE reference signal power, dBm (0 = unknown)
  int8_t rsrq = 0;            // dB (0 = unknown)
  int8_t sinr = 0;            // dB
  char band[16] = "";         // "LTE BAND3", "EGSM 900"...
  uint16_t battMv = 0;        // module supply voltage from AT+CBC (= battery when fed directly)
  int8_t modemTempC = -128;   // AT+CPMUTEMP, -128 = unknown
  bool smsReady = false;
  bool cmux = false;          // AT + data multiplexed on one UART
  uint32_t baud = 0;
  char op[OPERATOR_MAX] = "";
  char imei[20] = "";
  char iccid[24] = "";
  char imsi[20] = "";
  char ownNumber[NUMBER_MAX] = "";
  char model[24] = "";
  char fw[40] = "";
  uint32_t modemResets = 0;
  char failReason[48] = "";   // why the last start-up attempt failed
};

// ---- cellular data (PPP over the second CMUX channel) -----------------------
enum class DataState : uint8_t {
  Off,            // disabled in settings
  WaitNetwork,    // enabled, waiting for registration
  Dialing,        // ATD*99# on the data channel
  Negotiating,    // PPP LCP/IPCP
  Connected,      // got an IP address
  Backoff,        // failed, retrying later
  Unavailable,    // no CMUX -> data cannot share the UART with calls/SMS
};
const char* dataStateName(DataState s);

struct DataStatus {
  DataState state = DataState::Off;
  char ip[16] = "";
  char dns[16] = "";
  uint32_t rxBytes = 0, txBytes = 0;    // since boot
  uint32_t sessions = 0;
  uint32_t upSinceMs = 0;               // plogMillis() when connected
  char lastError[48] = "";
};

// ---- calls -------------------------------------------------------------------
enum class CallPhase : uint8_t { Idle, Dialing, Alerting, Incoming, Active };
enum class CallEndReason : uint8_t { None, Normal, Busy, NoAnswer, NoCarrier, Rejected, Missed, Failed, NoNetwork };

struct CallInfo {
  CallPhase phase = CallPhase::Idle;
  bool incoming = false;
  bool muted = false;
  bool everActive = false;
  char number[NUMBER_MAX] = "";
  uint32_t setupMs = 0;       // plogMillis() when the call appeared
  uint32_t activeMs = 0;      // plogMillis() when it was answered (0 = never)
  CallEndReason endReason = CallEndReason::None;
};

enum class CallType : uint8_t { Outgoing = 0, Incoming = 1, Missed = 2, Rejected = 3 };

struct CallLogEntry {
  char number[NUMBER_MAX];
  uint32_t ts;                // local epoch seconds, 0 = unknown
  uint16_t duration;          // seconds
  uint8_t type;               // CallType
  uint8_t reserved;
};

// Where call audio goes. The modem has one analog path; "Earbuds" means the
// ESP32 helper bridges that path to Bluetooth HFP.
enum class AudioRoute : uint8_t { Handset, Speaker, Earbuds };

// ---- contacts / SMS ----------------------------------------------------------
struct Contact {
  char name[CONTACT_NAME_MAX];
  char number[NUMBER_MAX];
};

enum SmsFlags : uint8_t {
  SMS_OUT      = 0x01,
  SMS_UNREAD   = 0x02,
  SMS_FAILED   = 0x04,
  SMS_SENDING  = 0x08,
  SMS_PARTIAL  = 0x10,        // some parts of a long SMS never arrived
};

struct SmsRecord {
  uint32_t id;
  uint32_t ts;                // local epoch seconds
  char number[NUMBER_MAX];    // phone number or alphanumeric sender ("AX-AIRTEL")
  uint8_t flags;
  uint8_t reserved;
  uint16_t len;
  char text[SMS_TEXT_MAX];    // UTF-8
};

// ---- Bluetooth (mirror of the ESP32 helper's state) -------------------------
enum class BtLinkState : uint8_t { Offline, Online };  // helper reachable over the UART link
