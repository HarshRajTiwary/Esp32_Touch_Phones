#pragma once
// Battery / power-management IC on the T-Call.
#include <stdint.h>

enum class PmuType : uint8_t { None, IP5306, AXP192 };

namespace power {

void begin();                  // detects the PMU and keeps the boost converter on
PmuType type();
const char* typeName();
bool charging();
bool usbPowered();
int pmuLevel();                // 0..100 in 25% steps from the IP5306, -1 if unknown
// Best battery estimate: modem-reported % (fine grained) or PMU level.
int batteryPercent(int modemPct, int modemMv);
void service(uint32_t nowMs);  // re-reads the PMU every 20 s

}  // namespace power
