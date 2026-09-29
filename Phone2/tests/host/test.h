#pragma once
// Minimal test macros for the host (PC) unit tests.
#include <stdio.h>

#include <string>

extern int g_pass, g_fail;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (cond) {                                                                  \
      ++g_pass;                                                                  \
    } else {                                                                     \
      ++g_fail;                                                                  \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                     \
    }                                                                            \
  } while (0)

#define CHECK_STR(a, b)                                                                                 \
  do {                                                                                                  \
    std::string _a = (a), _b = (b);                                                                     \
    if (_a == _b) {                                                                                     \
      ++g_pass;                                                                                         \
    } else {                                                                                            \
      ++g_fail;                                                                                         \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a.c_str(), _b.c_str());            \
    }                                                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                                                  \
  do {                                                                                                  \
    long long _a = (long long)(a), _b = (long long)(b);                                                 \
    if (_a == _b) {                                                                                     \
      ++g_pass;                                                                                         \
    } else {                                                                                            \
      ++g_fail;                                                                                         \
      printf("FAIL %s:%d  %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b);                \
    }                                                                                                   \
  } while (0)

// Test groups (one per file)
void runLegacyTests();    // SMS PDU, long SMS, AT parsers, text, DSP (from Phone1)
void runModemTests();     // A76XX parsers, CMUX codec, call state machine
void runLinkTests();      // COBS/CRC, BT link framing, SCO policy
void runPhoneTests();     // touch calibration, battery, network rules, contacts/log/messages storage
