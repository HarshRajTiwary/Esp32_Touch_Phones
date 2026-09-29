#pragma once
// Plain data types shared by the modem, storage, Bluetooth and UI layers.
// No Arduino dependencies so the host unit tests can include it.
#include <stdint.h>
#include "../config/app_config.h"

enum class ModemState : uint8_t { Off, Booting, Initializing, Ready, NotResponding };
enum class SimState   : uint8_t { Unknown, Ready, Absent, PinRequired, PukRequired, Error };
enum class RegState   : uint8_t { Unknown, NotRegistered, Searching, Home, Roaming, Denied };

// Overall phone readiness, derived from the three states above.
enum class PhoneState : uint8_t {
  Boot, InitializingModem, NoSim, SimLocked, SearchingNetwork, Ready, Error
};

struct NetStatus {
  ModemState modem = ModemState::Off;
  SimState sim = SimState::Unknown;
  RegState reg = RegState::Unknown;
  int8_t csq = 99;            // 0..31, 99 = unknown
  uint8_t battPct = 0;        // from AT+CBC
  uint16_t battMv = 0;
  bool callReady = false;
  bool smsReady = false;
  char op[24] = "";
  char imei[20] = "";
  char iccid[24] = "";
  char ownNumber[NUMBER_MAX] = "";
  char fw[40] = "";
  uint32_t modemResets = 0;
};

enum class CallPhase : uint8_t { Idle, Dialing, Alerting, Incoming, Active };
enum class CallEndReason : uint8_t { None, Normal, Busy, NoAnswer, NoCarrier, Rejected, Missed, Failed, NoNetwork };

struct CallInfo {
  CallPhase phase = CallPhase::Idle;
  bool incoming = false;
  bool muted = false;
  bool everActive = false;
  char number[NUMBER_MAX] = "";
  uint32_t setupMs = 0;       // millis() when the call appeared
  uint32_t activeMs = 0;      // millis() when it was answered (0 = never)
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
