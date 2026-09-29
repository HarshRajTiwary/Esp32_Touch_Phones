#pragma once
// =============================================================================
//  Phone2 - ESP32-S3 main board (the phone): every GPIO used by the S3 firmware.
//
//  Assumed board: ESP32-S3 "N16R8" development board (ESP32-S3-WROOM-1 with
//  16 MB flash + 8 MB octal PSRAM, two USB-C ports "COM"/"USB").
//  See docs/PINMAP.md for the wiring tables and docs/HARDWARE.md for what is
//  CONFIRMED / SUSPECTED / UNKNOWN.
//
//  Pins that are NOT available on an N16R8 module and must never be used:
//    GPIO26-32 : SPI flash / PSRAM          GPIO33-37 : octal PSRAM (R8 modules)
//    GPIO19/20 : native USB D-/D+ (kept for a future USB host link to the A7672S)
//    GPIO43/44 : UART0 console (COM port)   GPIO45/46 : strapping pins
//    GPIO3     : JTAG strapping pin
// =============================================================================

#define BOARD_NAME            "phone2-s3-main"

// -----------------------------------------------------------------------------
//  2.4" ILI9341 TFT + XPT2046 resistive touch + MicroSD, one SPI bus (SPI2).
//  These are the SPI2 IO-MUX pins, which allow the full 40 MHz display clock.
//  Do NOT connect the TFT's SDO(MISO) pin: many of these modules never release
//  it and it corrupts touch readings. Only T_DO (and SD_DO) go to MISO.
// -----------------------------------------------------------------------------
#define PIN_SPI_SCK           12   // TFT SCK + T_CLK + SD_SCK
#define PIN_SPI_MOSI          11   // TFT SDI + T_DIN + SD_DI
#define PIN_SPI_MISO          13   // T_DO + SD_DO
#define PIN_TFT_CS            10
#define PIN_TFT_DC            9
#define PIN_TFT_RST           8    // -1 = tie TFT RESET to 3V3 (software reset is used)
#define PIN_TFT_BL            7    // TFT "LED" pin, PWM brightness (-1 = tie LED to 3V3)
#define PIN_TOUCH_CS          6
#define PIN_TOUCH_IRQ         -1   // XPT2046 PENIRQ: optional, wire to GPIO5 and set 5 here. -1 = poll the
                                   // pressure (safe default: an unwired IRQ pin would block all touches)
#define PIN_SD_CS             4    // optional MicroSD slot on the TFT module (-1 = unused)

#define TFT_SPI_HOST          SPI2_HOST
#define TFT_SPI_WRITE_HZ      40000000  // lower to 27 MHz if the picture shows noise on long wires
#define TOUCH_SPI_HZ          1000000
#define TFT_WIDTH             240
#define TFT_HEIGHT            320
#define TFT_INVERT            false     // set true if colours look like a photo negative
#define TFT_RGB_ORDER         false     // set true if red and blue are swapped

// -----------------------------------------------------------------------------
//  SIMCom A7672S on the "LASPLO076_RevB" carrier board - UART1.
//
//  UNVERIFIED: the carrier board pinout is not published. The module's own I/O is
//  1.8 V; the board MUST level-shift TXD/RXD to 3.3 V (docs/HARDWARE.md, check M1)
//  before these pins are connected.
// -----------------------------------------------------------------------------
#define MODEM_UART            1
#define PIN_MODEM_TX          17   // S3 TX  -> carrier board RXD
#define PIN_MODEM_RX          18   // carrier board TXD -> S3 RX
#define PIN_MODEM_RTS         -1   // optional hardware flow control (S3 RTS -> modem RTS)
#define PIN_MODEM_CTS         -1   // optional (modem CTS -> S3 CTS)
#define PIN_MODEM_PWRKEY      21   // -1 = board powers the module on by itself
// PWRKEY polarity on the carrier board:
//   0 = pin goes straight to the module's PWRKEY (pulled up to VBAT inside the
//       module): the S3 pulls it LOW (open-drain) to "press" the key.
//   1 = the board has an NPN transistor on PWRKEY (common on breakout boards):
//       the S3 drives it HIGH to press.
#define MODEM_PWRKEY_ACTIVE_HIGH  0
#define PIN_MODEM_RESET       -1   // optional emergency reset (same polarity rule as PWRKEY)
#define MODEM_RESET_ACTIVE_HIGH   0
// Status lines are 1.8 V on the module. Only connect them if the carrier board
// level-shifts them to 3.3 V; otherwise leave them at -1 (the firmware does not need them).
#define PIN_MODEM_STATUS      -1   // high = module powered and booted
#define PIN_MODEM_RI          -1   // low pulse on SMS/URC, low while ringing
#define PIN_MODEM_DTR         -1   // sleep control (not used in v1)

#define MODEM_BAUD            115200    // A7672S factory default
// After CMUX starts, the firmware can switch both ends to a faster rate for 4G
// data. Only enable if the carrier board's level shifters are fast (not the
// 2-transistor type SIMCom warns about above 115200) - see docs/MODEM.md.
#define MODEM_BAUD_FAST       921600
#define MODEM_TRY_FAST_BAUD   0

// -----------------------------------------------------------------------------
//  BT link to the ESP32 Bluetooth helper - UART2 (3 wires + GND)
// -----------------------------------------------------------------------------
#define LINK_UART             2
#define PIN_LINK_TX           39   // S3 TX -> ESP32 GPIO16 (RX2)
#define PIN_LINK_RX           40   // S3 RX <- ESP32 GPIO17 (TX2)
#define PIN_LINK_RESET        41   // -> ESP32 EN (open-drain, optional; -1 = not wired)

// -----------------------------------------------------------------------------
//  Misc
// -----------------------------------------------------------------------------
#define PIN_BUTTON_HOME       0    // BOOT button: wake / home (active low, has pull-up)
#define PIN_RGB_LED           48   // on-board WS2812 (GPIO48 on DevKitC-1 v1.0, 38 on v1.1); -1 = none
#define PIN_BUZZER            42   // optional passive buzzer for the ringtone (-1 = none)
#define PIN_BATT_ADC          -1   // optional battery divider (100k/100k) on an ADC1 pin (GPIO1..10)
