# TouchPhone – hardware guide

This page explains how to connect everything. Read it fully **before** you solder anything.

## 1. Summary

| Item | Details |
|---|---|
| **BOARD** | LilyGO **T-Call SIM800L** (ESP32 + SIM800L on one board, IP5306 power chip). Revision v1.3/v1.4 assumed. |
| **ESP32** | ESP32 module with PSRAM (WROVER type), 4 MB flash. The firmware detects PSRAM at boot and says so in the log. |
| **DISPLAY** | 2.4" SPI TFT, 240×320, **ILI9341** controller (the red module with an SD slot on the back). |
| **TOUCH** | **XPT2046** resistive touch, shares the SPI bus with the display. |
| **MODEM** | SIM800L, 2G (GSM 850/900/1800/1900). Connected inside the board to GPIO 26/27. |
| **SIM** | Airtel works on 2G (GSM 900/1800). A 4G Airtel SIM also works in 2G phones. |
| **AUDIO** | Three modes: Bluetooth earbuds (HFP), wired speaker + mic on the SIM800L, or Auto. |
| **EXTRA PARTS** | For earbud calls: 5 resistors, 6 capacitors and a jumper (about ₹50, list in section 6). Nothing else. |
| **BUILD SYSTEM** | Arduino core for ESP32 **3.3.8** (ESP-IDF 5.5.4) + LovyanGFX 1.2.30, built with `arduino-cli`. |

> **Things I could not confirm without seeing your board:** the exact T-Call revision,
> which header pins are broken out, and the exact position of the MIC/SPK pads.
> Section 8 explains how to check each one.

## 2. Why the pins are different from the first suggestion

The earlier plan put the TFT on GPIO 4, 5, 23 and 27. **On the T-Call those pins already belong to the
SIM800L** (power key, reset, power switch and UART). Using them would stop the modem from working.

These GPIOs are used inside the T-Call and must not be used for anything else:

| GPIO | T-Call function |
|---|---|
| 4 | SIM800L PWRKEY |
| 5 | SIM800L RESET |
| 23 | SIM800L power switch (POWER_ON) |
| 26 | SIM800L TXD → ESP32 RX |
| 27 | ESP32 TX → SIM800L RXD |
| 21, 22 | I2C to the IP5306 battery chip |
| 13 | Blue LED (TouchPhone uses it as a **notification light**) |

## 3. Full GPIO map

| GPIO | Used for | Notes |
|---|---|---|
| 0 | – | BOOT button / strapping pin – leave free |
| 1, 3 | USB serial console | debug console, 115200 baud |
| **2** | TFT **DC** | strapping pin, fine as an output |
| 4 | SIM800L PWRKEY | fixed by the board |
| 5 | SIM800L RESET | fixed by the board |
| 6–11 | SPI flash | never use |
| 12 | – | strapping pin (sets flash voltage) – **keep unconnected** |
| 13 | Blue LED | notification light |
| **14** | Touch **T_CS** | |
| **15** | TFT **CS** | strapping pin, fine as a chip select |
| 16, 17 | PSRAM | never use |
| **18** | SPI **SCK** | TFT SCK **and** T_CLK |
| **19** | SPI **MOSI** | TFT SDI **and** T_DIN |
| 21, 22 | I2C (IP5306) | fixed by the board |
| 23 | SIM800L power switch | fixed by the board |
| **25** | Audio **DAC out** → SIM800L MIC+ | only needed for earbud calls |
| 26, 27 | SIM800L UART | fixed by the board |
| **32** | TFT **LED** (backlight brightness) | or tie LED to 3V3 (always on) |
| 33 | spare | (DTR/RI on the rare AXP192 variant) |
| **34** | Audio **ADC in** ← SIM800L SPK+ | input only; only for earbud calls |
| 35, 36 | spare | input only |
| **39** (VN) | SPI **MISO** ← T_DO | input only – that is fine for MISO |

Every pin is defined in one file: `firmware/TouchPhone/src/config/board_config.h`.
If a pin is not available on your board, change it there (free: 33, 35, 36) and rebuild.

## 4. Display + touch wiring

The TFT module has 14 pins on one side. Connect them like this:

| TFT pin | Connect to (T-Call) | Why |
|---|---|---|
| VCC | **3V3** | |
| GND | **GND** | |
| CS | **IO15** | display chip select |
| RESET | **3V3** | the firmware uses a software reset |
| DC (or RS) | **IO2** | data/command |
| SDI (MOSI) | **IO19** | |
| SCK | **IO18** | |
| LED | **IO32** | brightness + screen off. (Or 3V3 = always bright) |
| SDO (MISO) | **not connected** | many of these modules do not release this line and it breaks touch |
| T_CLK | **IO18** | same wire as SCK |
| T_CS | **IO14** | touch chip select |
| T_DIN | **IO19** | same wire as SDI |
| T_DO | **IO39 / VN** | touch data out |
| T_IRQ | **not connected** | the firmware polls the touch pressure instead |

The 4 SD-card pins on the other side stay unconnected.

**Tips**
* Keep the wires short (under 15 cm). The SPI clock is set to 27 MHz, which is safe for jumper wires.
  On a PCB you can raise `TFT_SPI_WRITE_HZ` to 40 MHz in `board_config.h`.
* If colours look wrong: red↔blue swapped → set `TFT_RGB_ORDER true`; colours inverted → set `TFT_INVERT true`.
* If touch is mirrored or rotated, you do not need to change anything: the calibration handles any orientation.

```
   2.4" TFT (ILI9341 + XPT2046)                     LilyGO T-Call
  ┌──────────────────────────┐
  │ VCC ─────────────────────┼──────────────── 3V3
  │ GND ─────────────────────┼──────────────── GND
  │ CS  ─────────────────────┼──────────────── IO15
  │ RESET ───────────────────┼──────────────── 3V3
  │ DC  ─────────────────────┼──────────────── IO2
  │ SDI ─────────────┬───────┼──────────────── IO19
  │ SCK ───────────┬─┼───────┼──────────────── IO18
  │ LED ───────────┼─┼───────┼──────────────── IO32
  │ SDO   (not connected)
  │ T_CLK ─────────┘ │
  │ T_CS ────────────┼───────┼──────────────── IO14
  │ T_DIN ───────────┘
  │ T_DO ────────────────────┼──────────────── IO39 (VN)
  │ T_IRQ (not connected)
  └──────────────────────────┘
```

## 5. Power, antenna, SIM

* **Always connect the GSM antenna** before powering the modem. Transmitting without an antenna can damage the SIM800L.
* The SIM800L takes up to **2 A for a few milliseconds** when it transmits. Power the board with a
  **3.7 V Li-ion/LiPo battery** (18650 or 1000 mAh+ LiPo) on the battery connector. USB alone often
  causes the modem to restart during calls ("UNDER-VOLTAGE" in the log). The IP5306 charges the battery from USB.
* The firmware tells the IP5306 to keep its output on, so the board does not switch itself off when the
  ESP32 draws little current.
* Insert the SIM with the phone **off**. If the SIM has a PIN, the phone asks for it on screen.
* SIM800L is **2G only**. Airtel still runs 2G in most of India; test coverage at your location first
  (Settings → Network & SIM shows "Registered (2G)" and the signal in dBm).

## 6. Audio – three ways to talk

### Mode A – wired (simplest, no extra circuit)

Connect directly to the SIM800L audio pads on the T-Call:

| SIM800L pad | Connect |
|---|---|
| SPK+ / SPK− | small 8 Ω speaker or 32 Ω earpiece (both wires, **never connect SPK− to GND**) |
| MIC+ / MIC− | electret microphone (MIC+ to the mic's + side) |

Set **Settings → Sound & call audio → Call audio goes to: Wired**. The speaker also plays the SIM800L ring tone.

### Mode B – Bluetooth earbuds (TWS)

The ESP32 acts like a phone to your earbuds (Bluetooth **HFP Audio Gateway**). Call audio flows like this:

```
 earbud mic ──Bluetooth──► ESP32 ──DAC GPIO25──► RC network ──► SIM800L MIC+
 earbuds  ◄──Bluetooth── ESP32 ◄──ADC GPIO34── RC network ◄── SIM800L SPK+
```

The SIM800L only has **analog** audio, so we need a small passive network between the ESP32's own
DAC/ADC and the SIM800L. **No codec chip is needed** – this design uses the ESP32's built-in converters.

**Downlink – SIM800L speaker → ESP32 ADC (what you hear)**

```
                      3V3
                       │
                      [R1 10k]
                       │
 SIM800L SPK+ ──[C1 1µF]──●──[R3 1k]──●──────► GPIO34
                       │            │
                      [R2 10k]     [C3 47nF]
                       │            │
                      GND          GND

 SIM800L SPK− : leave free (or keep your wired speaker on SPK+/SPK−)
```
R1/R2 bias the signal to the middle of the ADC range; R3 + C3 remove everything above ~3.4 kHz.

**Uplink – ESP32 DAC → SIM800L microphone input (what the other person hears)**

```
 GPIO25 ──[C4 1µF]──[R4 22k]──●──[C6 1µF]──o J1 o──► SIM800L MIC+
                              │
                        [R5 470Ω] ∥ [C5 100nF]
                              │
                             GND

 SIM800L MIC− ──[C7 1µF]──► GND
```
R4/R5 divide the 3.3 V DAC signal down to microphone level (~70 mV); C5 smooths the DAC steps.
**J1** is a jumper: open it when you use a wired microphone (Mode A), otherwise R5 loads the wired mic.

**Parts list (extra)**

| Part | Value | Qty |
|---|---|---|
| Resistor | 10 kΩ | 2 (R1, R2) |
| Resistor | 1 kΩ | 1 (R3) |
| Resistor | 22 kΩ (or a 50 kΩ trimmer for fine tuning) | 1 (R4) |
| Resistor | 470 Ω | 1 (R5) |
| Capacitor | 1 µF **ceramic** (non-polar, so the direction does not matter) | 4 (C1, C4, C6, C7) |
| Capacitor | 47 nF | 1 (C3) |
| Capacitor | 100 nF | 1 (C5) |
| Jumper / switch | 2-pin | 1 (J1) |

**Tuning after the first call** (Settings → Sound & call audio shows live meters):
* *To earbuds* meter should move to about −20 dB when the other person talks. Too quiet → raise
  "Wired volume" (SIM800L speaker level); red / distorted → lower it.
* The other person hears you too quietly → raise "SIM800 mic"; echo or distortion → lower it.
* "Earbud volume" and "Earbud mic" are fine digital gains on top.
* The **Test tone** and **Mic echo** buttons check the Bluetooth half without making a call.

**Honest note on quality:** the ESP32 DAC is 8-bit and its ADC is not hi-fi, but GSM voice itself is
narrow-band (300–3400 Hz), so speech is clear. If you ever want better sound, a full-duplex I2S codec
(ES8311/ES8388/WM8960) can replace this network; the audio code is isolated in `src/audio/`.

### Mode C – Auto (default)

Earbuds are used when they are connected, otherwise the wired speaker/mic.
During a call you can switch with the "Earbuds / Wired" button.

## 7. Bluetooth earbuds

* Pair from **Home → Earbuds → Search for earbuds**, then tap your earbuds. Most TWS buds enter pairing
  mode when you take both out of the case and hold the touch area for ~5 s.
* The phone remembers the earbuds and reconnects automatically.
* Earbud buttons work: tap = answer / hang up, double tap = redial, volume buttons = call volume.
* Earbuds must support **HFP** (hands-free profile). Nearly all TWS buds with a microphone do.

## 8. How to verify the unknowns

| Unknown | How to check |
|---|---|
| T-Call revision / power chip | Boot log line `[PWR] IP5306 power IC found` (or `AXP192`). With AXP192, GPIO32/33 may be taken – move the backlight to 13 or 33 in `board_config.h`. |
| PSRAM present | Boot log `[LCD] ... PSRAM frame buffer`. Without PSRAM the UI still works but may flicker. |
| Header pins | Look at the silkscreen next to the pin headers. Every pin in section 3 must be reachable. |
| MIC/SPK pads | Silkscreen "MIC" / "SPK" near the SIM800L. Measure: SPK+ to GND ≈ half the battery voltage while powered. |
| Display controller | If the screen stays white, the module may be an ST7789 – change `Panel_ILI9341` in `src/hw/display.cpp`. |
| Touch wiring | Serial console command `touch` prints raw values while you press. |

## 9. Safety

* Never short SPK− to GND (the SIM800L amplifier drives both pins).
* Do not power the SIM800L from the ESP32's 3.3 V pin – the T-Call already powers it correctly.
* Li-ion batteries can be dangerous: use a protected cell and do not leave it charging unattended.
