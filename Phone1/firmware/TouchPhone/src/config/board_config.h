#pragma once
// =============================================================================
//  TouchPhone - board / wiring configuration
//
//  Every GPIO used by the firmware is defined here and nowhere else.
//  See docs/HARDWARE.md for the reasoning behind each choice and the
//  conflict analysis against the LilyGO T-Call SIM800L board.
// =============================================================================

// -----------------------------------------------------------------------------
//  SIM800L (fixed by the T-Call PCB - do NOT use these pins for anything else)
// -----------------------------------------------------------------------------
#define PIN_MODEM_PWRKEY      4    // held LOW = modem on (T-Call reference code)
#define PIN_MODEM_RST         5    // SIM800L RESET (active LOW)
#define PIN_MODEM_POWER_ON    23   // enables the SIM800L supply switch
#define PIN_MODEM_TX          27   // ESP32 TX  -> SIM800L RXD
#define PIN_MODEM_RX          26   // ESP32 RX  <- SIM800L TXD
#define MODEM_BAUD            115200

// Power-management IC (IP5306 on T-Call v1.3/v1.4, AXP192 on some variants)
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22
#define PIN_BOARD_LED         13   // blue LED on the T-Call

// -----------------------------------------------------------------------------
//  2.4" ILI9341 TFT + XPT2046 touch (shared SPI bus, separate chip selects)
//
//  Wire TFT SDO(MISO) *not at all* - many of these modules do not tri-state it
//  and it corrupts touch readings. Only T_DO goes to the MISO pin.
// -----------------------------------------------------------------------------
#define PIN_SPI_SCK           18   // TFT SCK  + T_CLK
#define PIN_SPI_MOSI          19   // TFT SDI  + T_DIN
#define PIN_SPI_MISO          39   // T_DO only (GPIO39 = "VN", input-only is fine)
#define PIN_TFT_CS            15   // strapping pin, safe for a CS input
#define PIN_TFT_DC            2    // strapping pin, safe for a DC input
#define PIN_TFT_RST           -1   // tie TFT RESET to 3V3 (software reset is used)
#define PIN_TFT_BL            32   // TFT "LED" pin, PWM brightness (-1 = tie LED to 3V3)
#define PIN_TOUCH_CS          14
#define PIN_TOUCH_IRQ         -1   // not needed: touch is polled by pressure

#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_WRITE_HZ      27000000  // 27 MHz is reliable on jumper wires; try 40 MHz on a PCB
#define TFT_SPI_READ_HZ       16000000
#define TOUCH_SPI_HZ          1000000
#define TFT_WIDTH             240       // UI size (portrait)
#define TFT_HEIGHT            320
// Size of the controller's frame memory. Classic ILI9341 modules are 240x320;
// the AM-036 kit's 2.4" module (glass marked TPM-3076-2.4) is a native 320x240
// (ILI9342-type) panel: with 240x320 it shows mirrored text and a noise strip.
#define TFT_NATIVE_W          320
#define TFT_NATIVE_H          240
// LovyanGFX rotation that gives an upright portrait UI on that panel:
// 5 = pin header at the top, 7 = pin header at the bottom (1/3 = mirrored).
// Can be changed at run time with the console command "rot <n>" (saved).
#define TFT_ROTATION          5
#define TFT_INVERT            false     // set true if colours look like a photo negative
#define TFT_RGB_ORDER         false     // set true if red and blue are swapped
#define TFT_BL_PWM_CHANNEL    7

// -----------------------------------------------------------------------------
//  SIM800L <-> Bluetooth earbud audio bridge (see docs/HARDWARE.md "Audio")
//
//  Uplink   : earbud mic --HFP--> ESP32 --DAC(GPIO25)--> RC network --> SIM800L MIC+
//  Downlink : SIM800L SPK+ --> RC network --> ADC1(GPIO34) --> ESP32 --HFP--> earbuds
// -----------------------------------------------------------------------------
#define PIN_AUDIO_DAC_OUT     25   // DAC channel 1 (the only DAC pin not used by the modem)
#define PIN_AUDIO_ADC_IN      34   // ADC1 channel 6, input-only pin

// -----------------------------------------------------------------------------
//  Reserved / do-not-use (documented for the pin-conflict rule)
// -----------------------------------------------------------------------------
//  GPIO0  : boot strap (BOOT button)       GPIO1/3 : USB serial console
//  GPIO6-11: SPI flash                      GPIO12  : strap (flash voltage) - keep free
//  GPIO16/17: PSRAM on the WROVER module    GPIO33  : spare (DTR/RI on AXP192 variant)
//  GPIO35/36: spare inputs
