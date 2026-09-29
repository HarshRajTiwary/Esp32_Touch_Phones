#pragma once
// GSM 03.40 SMS PDU encoder / decoder (pure C++, host-tested).
//
// Why PDU mode instead of text mode:
//  * long (concatenated) bank / operator messages arrive as parts with a header
//    that text mode hides - PDU mode lets us join them back together;
//  * Unicode (UCS2) messages - Hindi, emoji, the rupee sign - decode correctly;
//  * alphanumeric senders ("AX-AIRTEL") decode correctly.
#include <stddef.h>
#include <stdint.h>
#include "../config/app_config.h"

struct SmsPdu {
  bool isSubmit = false;          // true for SMS-SUBMIT (our own sent messages on SIM)
  bool isStatusReport = false;
  char number[NUMBER_MAX] = "";   // originating (deliver) or destination (submit) address
  // Service-centre timestamp, local time as epoch seconds + zone in quarter hours
  uint32_t ts = 0;
  int tzQuarters = 0;
  // Concatenation info (UDH IE 0x00 or 0x08)
  bool concat = false;
  uint16_t ref = 0;
  uint8_t total = 1;
  uint8_t seq = 1;
  uint8_t dcs = 0;
  bool binary = false;            // 8-bit data SMS (WAP push etc.) - not human text
  char text[SMS_TEXT_MAX] = "";   // UTF-8
};

// Decodes a PDU hex string as returned by AT+CMGR / AT+CMGL in PDU mode
// (including the leading SMSC address). Returns false on malformed data.
bool smsDecodePdu(const char* hex, SmsPdu* out);

struct SmsPart {
  char hex[2 * 180 + 1];          // TPDU as hex, preceded by "00" (use modem's SMSC)
  int tpduLen;                    // value for AT+CMGS=<len>
};

// Encodes UTF-8 text into 1..maxParts SMS-SUBMIT PDUs (GSM 7-bit if possible,
// otherwise UCS2). Returns the number of parts, or 0 if the text is too long
// or the number is invalid.
int smsEncodeSubmit(const char* number, const char* utf8, uint8_t ref,
                    SmsPart* parts, int maxParts);

// Counts how the text would be sent: returns number of parts, and fills
// used/perPart (characters used and capacity per part) for the UI counter.
int smsMeasure(const char* utf8, int* used, int* perPart, bool* unicode);

// Helpers exposed for USSD decoding and tests.
size_t ucs2HexToUtf8(const char* hex, char* out, size_t cap);
size_t gsm7UnpackedToUtf8(const uint8_t* septets, size_t n, char* out, size_t cap);
bool gsm7Encodable(uint32_t cp);
