// Host-side unit tests for Phone2's hardware-independent code.
//
//   powershell -File tools\test.ps1
#include "test.h"

int g_pass = 0, g_fail = 0;

int main() {
  struct Group {
    const char* name;
    void (*run)();
  };
  const Group groups[] = {
      {"legacy (SMS PDU, long SMS, AT parsers, text, DSP)", runLegacyTests},
      {"modem (A76XX parsers, CMUX, call state machine)", runModemTests},
      {"link (COBS/CRC, BT link frames, SCO policy)", runLinkTests},
      {"phone (touch calibration, battery, net rules, storage)", runPhoneTests},
  };
  for (const Group& g : groups) {
    int before = g_fail, passBefore = g_pass;
    g.run();
    printf("%-60s %4d checks  %s\n", g.name, g_pass - passBefore + g_fail - before, g_fail == before ? "ok" : "FAILED");
  }
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
