#pragma once
// =============================================================================
//  Phone2 - ESP32 Bluetooth helper: every GPIO used by the helper firmware.
//
//  Assumed board: generic ESP32 development board with an ESP32-WROOM-32
//  module (DOIT DevKit V1 / DevKitC style). The helper has only two jobs:
//  Bluetooth Classic HFP Audio Gateway for the TWS earbuds, and bridging the
//  A7672S's analog call audio to/from the earbuds.
//
//  Never use: GPIO6-11 (flash), GPIO1/3 (USB console), GPIO0/2/5/12/15 as inputs
//  that could be driven at boot (strapping pins; GPIO12 HIGH at boot breaks flash).
// =============================================================================

#define BOARD_NAME            "phone2-esp32-bt"

// ---- BT link to the ESP32-S3 (UART2) -----------------------------------------
// GPIO16/17 are labelled RX2/TX2 on most ESP32 boards. On a WROVER (PSRAM) module
// they are taken by the PSRAM - use 32/33 there instead.
#define LINK_UART             2
#define PIN_LINK_RX           16   // <- S3 GPIO39
#define PIN_LINK_TX           17   // -> S3 GPIO40

// ---- Call audio bridge (see docs/AUDIO_ARCHITECTURE.md for the RC networks) ----
//  Uplink   : earbud mic --HFP--> ESP32 --DAC(GPIO25)--> network --> A7672S MIC_P
//  Downlink : A7672S EAR_P/SPK_P --> network --> ADC1(GPIO34) --> ESP32 --HFP--> earbuds
#define PIN_AUDIO_DAC_OUT     25   // DAC channel 1 (must be 25 or 26)
#define PIN_AUDIO_ADC_IN      34   // ADC1 channel 6, input-only (must be an ADC1 pin: 32-39)
// Optional analog switch (e.g. CD4066 / TS5A3159) that disconnects a wired
// handset microphone while the earbuds are used. -1 = not fitted (use a jumper).
#define PIN_AUDIO_SWITCH      -1
#define AUDIO_SWITCH_EARBUDS_LEVEL 1

// ---- Misc ------------------------------------------------------------------------
#define PIN_STATUS_LED        2    // on-board blue LED on DOIT boards (-1 = none)
