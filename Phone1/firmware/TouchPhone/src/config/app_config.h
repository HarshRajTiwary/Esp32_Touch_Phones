#pragma once
// =============================================================================
//  TouchPhone - application limits, timings and compile-time options
// =============================================================================

#define FW_NAME               "TouchPhone"
#define FW_VERSION            "1.0.0"
#define BT_DEVICE_NAME        "TouchPhone"

// ---- Debug console ----------------------------------------------------------
// PHONE_DEBUG=1 prints [MODEM]/[CALL]/[BT]... logs on the USB serial port.
#ifndef PHONE_DEBUG
#define PHONE_DEBUG           1
#endif
// PHONE_LOG_PRIVATE=1 prints full phone numbers and SMS text in the logs.
// Keep it 0 for normal use - numbers are masked as +91******3210.
#ifndef PHONE_LOG_PRIVATE
#define PHONE_LOG_PRIVATE     0
#endif

// ---- Data limits ------------------------------------------------------------
#define NUMBER_MAX            24      // incl. NUL, e.g. "+919876543210"
#define CONTACT_NAME_MAX      32
#define SMS_TEXT_MAX          512     // UTF-8 bytes incl. NUL (about 3 joined SMS)
#define MAX_CONTACTS          300
#define MAX_CALL_LOG          60
#define MAX_SMS_PSRAM         300     // messages kept when PSRAM is present
#define MAX_SMS_NO_PSRAM      50
#define SMS_SEND_MAX_PARTS    4       // longest outgoing message = 4 joined SMS
#define USSD_TEXT_MAX         400

// ---- Timings (ms) -----------------------------------------------------------
#define MODEM_POLL_IDLE_MS        15000
#define MODEM_POLL_SEARCH_MS      4000
#define MODEM_POLL_SLOW_MS        60000
#define MODEM_SMS_SWEEP_MS        300000
#define MODEM_CLOCK_POLL_MS       1800000
#define MODEM_CALL_POLL_MS        2500
#define SMS_CONCAT_TIMEOUT_MS     600000   // give up waiting for missing parts
#define STORAGE_SAVE_DELAY_MS     1500
#define UI_TOUCH_POLL_MS          15
#define UI_IDLE_FRAME_MS          1000
#define TOAST_MS                  3500

// ---- Filesystem -------------------------------------------------------------
#define FS_PARTITION_LABEL    "spiffs"   // data partition name in the Arduino partition tables
#define FS_BASE_PATH          "/lfs"
