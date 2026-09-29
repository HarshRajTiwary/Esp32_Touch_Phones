#include "power.h"

#include <Arduino.h>
#include <Wire.h>

#include "../config/board_config.h"
#include "../core/log.h"

namespace power {

static const uint8_t kIp5306 = 0x75;
static const uint8_t kAxp192 = 0x34;

static PmuType s_type = PmuType::None;
static bool s_charging = false, s_usb = false;
static int s_level = -1;
static uint32_t s_nextRead = 0;

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static int readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)addr, 1) != 1) return -1;
  return Wire.read();
}

static bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static void readStatus() {
  if (s_type == PmuType::IP5306) {
    int r70 = readReg(kIp5306, 0x70), r71 = readReg(kIp5306, 0x71), r78 = readReg(kIp5306, 0x78);
    if (r70 >= 0) s_charging = (r70 & 0x08) != 0;
    if (r71 >= 0 && (r71 & 0x08)) s_charging = false;  // charge complete
    s_usb = s_charging || (r71 >= 0 && (r71 & 0x08));
    if (r78 >= 0) {
      switch (r78 & 0xF0) {
        case 0x00: s_level = 100; break;
        case 0x80: s_level = 75; break;
        case 0xC0: s_level = 50; break;
        case 0xE0: s_level = 25; break;
        default: s_level = 0; break;
      }
    }
  } else if (s_type == PmuType::AXP192) {
    int r00 = readReg(kAxp192, 0x00), r01 = readReg(kAxp192, 0x01);
    if (r00 >= 0) s_usb = (r00 & 0xA0) != 0;       // ACIN or VBUS present
    if (r01 >= 0) s_charging = (r01 & 0x40) != 0;
  }
}

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  if (probe(kIp5306)) {
    s_type = PmuType::IP5306;
    // SYS_CTL0: boost + charger enabled, keep boost on at light load, button shutdown.
    // Without "keep on" the IP5306 turns the 5 V boost off when the ESP32 draws little.
    bool ok = writeReg(kIp5306, 0x00, 0x37);
    LOG("PWR", "IP5306 power IC found, boost keep-on %s", ok ? "set" : "FAILED");
  } else if (probe(kAxp192)) {
    s_type = PmuType::AXP192;
    LOG("PWR", "AXP192 power IC found - NOTE: on this variant GPIO32/33 may be modem DTR/RI, see HARDWARE.md");
  } else {
    LOG("PWR", "no power IC on I2C (USB powered board?)");
  }
  readStatus();
}

PmuType type() { return s_type; }

const char* typeName() {
  switch (s_type) {
    case PmuType::IP5306: return "IP5306";
    case PmuType::AXP192: return "AXP192";
    default: return "none";
  }
}

bool charging() { return s_charging; }
bool usbPowered() { return s_usb; }
int pmuLevel() { return s_level; }

int batteryPercent(int modemPct, int modemMv) {
  if (modemMv > 3000 && modemPct > 0) return modemPct;
  if (s_level >= 0) return s_level;
  return modemPct;
}

void service(uint32_t nowMs) {
  if ((int32_t)(nowMs - s_nextRead) < 0) return;
  s_nextRead = nowMs + 20000;
  if (s_type != PmuType::None) readStatus();
}

}  // namespace power
