#pragma once
// =============================================================================
//  Phone2 - data limits and compile-time options shared by both MCUs
//  (ESP32-S3 main phone + ESP32 Bluetooth helper) and by the host tests.
// =============================================================================

#define FW_NAME               "Phone2"
#define FW_VERSION            "0.9.0"
#define BT_DEVICE_NAME        "Phone2"

// ---- Logging ----------------------------------------------------------------
// PHONE_LOG_PRIVATE=1 prints full phone numbers and SMS text in the logs.
// Keep it 0 for normal use - numbers are masked as +91******3210.
#ifndef PHONE_LOG_PRIVATE
#define PHONE_LOG_PRIVATE     0
#endif

// ---- Data limits ------------------------------------------------------------
#define NUMBER_MAX            24      // incl. NUL, e.g. "+919876543210"
#define CONTACT_NAME_MAX      32
#define OPERATOR_MAX          24
#define BT_NAME_MAX           32
#define SMS_TEXT_MAX          768     // UTF-8 bytes incl. NUL (about 4-5 joined SMS)
#define MAX_CONTACTS          500
#define MAX_CALL_LOG          100
#define MAX_SMS               500     // kept in PSRAM on the S3
#define SMS_SEND_MAX_PARTS    5       // longest outgoing message = 5 joined SMS
#define USSD_TEXT_MAX         400
#define APN_MAX               64
#define APN_USER_MAX          32
#define APN_PASS_MAX          32
#define WIFI_SSID_MAX         33      // 32 + NUL
#define WIFI_PASS_MAX         65      // 64 + NUL
