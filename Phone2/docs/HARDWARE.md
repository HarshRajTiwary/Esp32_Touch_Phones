# Phone2 – hardware

What every part is, how sure we are about it, and exactly what still has to be
checked on the real boards. **Read this before connecting anything.**

Words used on this page:

| Word | Meaning |
|---|---|
| **CONFIRMED** | Taken from the manufacturer's official document (SIMCom / Espressif), or measured. |
| **SUSPECTED** | What the part almost certainly is, but nobody has looked at your actual board yet. |
| **UNKNOWN** | Cannot be known without a photo or a measurement. The firmware keeps it configurable. |
| **UNVERIFIED** | Written and compiled, but not yet tried on real hardware. |

> Nothing in this project has been run on real hardware yet. All firmware compiles
> and the hardware-independent logic passes 405 PC tests, but every pin, voltage and
> timing must be confirmed with [TESTING.md](TESTING.md).

---

## 1. Why two ESP32 boards

| Need | ESP32-S3 | Generic ESP32 |
|---|---|---|
| Bluetooth **Classic** (HFP – how TWS earbuds do phone calls) | **no** (Bluetooth LE only) | **yes** |
| RAM for a smooth UI, 500 SMS, contacts | 8 MB PSRAM | 520 KB, no PSRAM |
| Radio for the Wi-Fi hotspot | its own | would have to share with Bluetooth |
| USB host (possible faster 4G link later) | yes | no |

CONFIRMED from ESP-IDF v5.5.5: only the ESP32 has `SOC_BT_CLASSIC_SUPPORTED`; the
ESP32-S3 and ESP32-C6 do not. Espressif rates Wi-Fi SoftAP + Classic Bluetooth on one
ESP32 as "unstable" (coexistence table) and does not list SCO voice links at all.

So: **the S3 is the phone, the ESP32 is the earbud helper.** Each has its own radio,
so a hotspot and an earbud call can run at the same time. The ESP32-C6 and the
SIM800L are not used (the C6 also has no Bluetooth Classic).

## 2. Parts inventory and confidence

| Part | Identification | Voltage | Interface | Confidence | Evidence |
|---|---|---|---|---|---|
| **ESP32-S3 board** (main phone) | ESP32-S3-WROOM-1 **N16R8** dev board: 16 MB flash, 8 MB octal PSRAM, two USB-C ports | 5 V USB in, 3.3 V logic | USB (console), SPI, 2× UART | SUSPECTED | You said "16 MB, good PSRAM". Octal PSRAM (R8) is the common version. |
| **ESP32 board** (Bluetooth helper) | ESP32-WROOM-32 dev board (DOIT DevKit V1 / DevKitC style) | 5 V USB in, 3.3 V logic | UART, DAC, ADC, Bluetooth Classic | SUSPECTED | "Generic ESP32 development board". A WROVER module would change 2 pins (see PINMAP). |
| **4G module** | SIMCom **A7672S** (LTE Cat-1) | VBAT 3.4–4.2 V (3.8 V typ.) | UART (1.8 V at the module), USB device | CONFIRMED (module) | SIMCom *A7672S Hardware Design V1.00*, *A7672X Series Hardware Design V1.00* |
| **Carrier board** "LASPLO076_RevB" | Unknown maker; not documented online | UNKNOWN (many such boards take 5–12 V and regulate to 3.9 V) | UNKNOWN header | **UNKNOWN** | Web search found no match. Photos needed (section 5). |
| **Display** | 2.4" SPI TFT 240×320, **ILI9341** | 3.3 V logic (VCC 3.3–5 V on most modules) | SPI | SUSPECTED | The usual red 2.4" module with touch + SD slot. |
| **Touch** | **XPT2046** resistive | 3.3 V | SPI (shared bus) | SUSPECTED | Same module. |
| **MicroSD slot** | on the TFT module | 3.3 V | SPI (shared bus) | SUSPECTED | Optional, not used by v1 firmware. |
| **TWS earbuds** | any with a microphone | – | Bluetooth HFP 1.7 | – | All mainstream TWS support HFP for calls. |
| **Li-Po batteries ×2** | UNKNOWN capacity / protection | nominal 3.7 V | – | **UNKNOWN** | Read the labels, see [POWER.md](POWER.md). |
| SIM800L, ESP32-C6 | not used | – | – | – | Kept for other projects. |

### A7672S facts (CONFIRMED from SIMCom documents)

| Item | Value |
|---|---|
| Bands – A7672S (A7672X family) | GSM 900/1800; LTE-FDD B1/B3/B5/B8; LTE-TDD B34/B38/B39/B40/B41 |
| Bands – A7672S-MANS/FANS | same LTE bands, **no GSM** in the table |
| Airtel India bands | mostly LTE B3 (1800), B40 (2300), some B1/B8 → covered |
| Peak current | **2 A** in GSM transmit bursts (A7672X), 1 A in LTE; supply must drop < 300 mV |
| PWRKEY | pulled up to VBAT inside the module; pull **low** to power on (≈50 ms+), low ≥ 2.5 s to power off |
| RESET | low 2–2.5 s, "emergency only" |
| Boot time | STATUS high ≈ 7 s, UART ready ≈ 8 s after PWRKEY |
| UART | 1.8 V logic at the module; default 115200 baud, up to 3.6864 Mbaud; RTS/CTS; **CMUX (GSM 07.10) supported** |
| Level shifting | SIMCom's 2-transistor shifter is "not recommended above 115200 bps" |
| Audio | **analog only**: 1 microphone input (MIC_P/MIC_N + MIC_BIAS), 1 speaker output (EAR_P/EAR_N, class-AB, 32 Ω, 37 mW). **No PCM/I2S digital audio.** |
| USB | USB 2.0 **device only** (for PC use / firmware updates) |
| Module pins (A7672X) | PWRKEY 1, DTR 3, RI 4, DCD 5, CTS 7, RTS 8, TXD 9, RXD 10, RESET 16, EAR_P 40, EAR_N 41, MIC_P 42, MIC_N 43, NETLIGHT 52, VBAT 55–57, STATUS 66 |

What this means for the design:
* The call audio has to be tapped **as an analog signal** from EAR_P and fed into
  MIC_P – that is the ESP32 helper's job ([AUDIO_ARCHITECTURE.md](AUDIO_ARCHITECTURE.md)).
* 4G data runs as **PPP on the UART**, on a second CMUX channel so calls and SMS keep
  working at the same time ([MODEM.md](MODEM.md)).
* The carrier board **must** convert the module's 1.8 V UART to 3.3 V. Check it (M3).

## 3. Component details (what the firmware assumes)

### ESP32-S3 (main)
| | |
|---|---|
| Confirmed pins (chip level) | GPIO26–32 flash/PSRAM, GPIO33–37 octal PSRAM, GPIO19/20 USB, GPIO43/44 UART0, GPIO0/3/45/46 strapping – **never used** |
| Suspected | on-board RGB LED on GPIO48 (DevKitC-1 v1.0) or GPIO38 (v1.1) |
| Unknown | which USB port is "COM" (UART bridge) on your board |
| Verify | boot log line `[BOARD] phone2-s3-main: esp32s3 ... flash 16 MB, PSRAM 8 MB` |

### ESP32 (helper)
| | |
|---|---|
| Confirmed | Bluetooth Classic, DAC on GPIO25/26, ADC1 on GPIO32–39, GPIO6–11 flash |
| Suspected | WROOM-32 module (GPIO16/17 free), blue LED on GPIO2 |
| Verify | boot log `[BOARD] phone2-esp32-bt: esp32 ... Bluetooth Classic: yes` |

### A7672S carrier board "LASPLO076_RevB"
| | |
|---|---|
| Confirmed | nothing yet |
| Suspected | a header with at least VIN (or VCC), GND, TXD, RXD; maybe PWRKEY / "PWR" / "EN"; an LTE antenna connector (IPEX/U.FL or SMA); a SIM holder; a NETLIGHT LED; often a micro-USB for the module's USB; often a 5–12 V buck regulator to ≈3.9 V and a UART level shifter |
| Unknown | pin order, input voltage, UART logic level, PWRKEY polarity, whether MIC/SPK/STATUS/RI/DTR/RTS/CTS are brought out, GNSS/BLE variant |
| Verify | section 5, checks M1–M8 |

### Display + touch
| | |
|---|---|
| Suspected | ILI9341 panel, XPT2046 touch, 14-pin header: VCC, GND, CS, RESET, DC, SDI, SCK, LED, SDO, T_CLK, T_CS, T_DIN, T_DO, T_IRQ; 4-pin SD header |
| Firmware assumptions | TFT SDO **not connected** (many modules never release it and it breaks touch reads); touch IRQ not needed (pressure polling) |
| Verify | Diagnostics → Display test (colour bars, orientation); a white screen means wrong wiring or a non-ILI9341 panel (ST7789 → change `Panel_ILI9341` in `display.cpp`) |

## 4. How everything connects

```
                       ┌──────────────────────── ESP32-S3 (phone) ─────────────────────────┐
 2.4" TFT + touch ─SPI─┤ UI, SMS, contacts, calls, 4G PPP, Wi-Fi hotspot, settings, console │
                       └──────┬─────────────────────────────┬──────────────────────────────┘
                   UART1 (AT+PPP via CMUX)          UART2 "BT link" (3 wires + GND)
                       ┌──────┴──────┐              ┌───────┴────────┐
          Airtel 4G ───┤   A7672S    ├── EAR_P ────►│ ESP32 helper   │))) Bluetooth HFP ))) TWS earbuds
         (antenna!)    │ carrier bd. │◄── MIC_P ────┤ DAC/ADC bridge │
                       └─────────────┘  (analog RC)  └────────────────┘
```

Wire-by-wire tables: **[PINMAP.md](PINMAP.md)**. Power: **[POWER.md](POWER.md)**.

## 5. What to check on your boards (please send photos)

Do these **before** wiring the modem to the ESP32-S3. A multimeter is enough.

| # | Check | How | Why it matters |
|---|---|---|---|
| **M1** | Photos of **both sides** of the carrier board, and a close-up of the module label | Phone camera, good light, silkscreen readable | Tells the exact module variant (e.g. `A7672S-LASE`, `-LASC`, `-FASE`, `-MANS`) and the header labels |
| **M2** | Power input | Read the header labels (VIN / 5V / VCC / VBAT) and any regulator chip marking | Wrong voltage destroys the module. See [POWER.md](POWER.md) |
| **M3** | **UART logic level** | Power the board (antenna connected), measure TXD → GND while idle | **~3.3 V** = OK. **~1.8 V** = no level shifter: add one (e.g. TXS0108E / BSS138 module). **~5 V** = add a 10 k/20 k divider on the ESP32-S3's RX – ESP32 pins are **not** 5 V tolerant |
| **M4** | Power key | Is there a PWR/PWRKEY pin or button? Does the NETLIGHT LED start blinking by itself when powered? Measure the PWRKEY pin: ≈VBAT (raw module pin) or ≈0 V (transistor input)? | Sets `PIN_MODEM_PWRKEY` / `MODEM_PWRKEY_ACTIVE_HIGH` in `boards/s3_main/board_config.h` (or −1 if the board starts the module itself) |
| **M5** | Audio pads | Look for MIC+/MIC−, SPK+/SPK− (or EAR+/EAR−), or a 3.5 mm jack | Needed for the handset and for the earbud bridge. If none: the module pins are EAR_P 40, EAR_N 41, MIC_P 42, MIC_N 43 (fine soldering) |
| **M6** | Other signals | Are STATUS, RI, DTR, RTS, CTS brought out? At which voltage? | Optional; only connect if level-shifted to 3.3 V |
| **M7** | Antenna | Connector type; the LTE antenna goes on "MAIN"/"LTE", not "GNSS"/"BT" | Transmitting without an antenna can damage the RF stage |
| **M8** | USB port on the carrier | micro-USB / USB-C near the module? | Lets you test the modem from a PC (SIMCom USB drivers + a terminal) before the ESP32-S3 is involved |

Send the photos and the M3/M4 readings and the pin map can be filled in as CONFIRMED.

## 6. Safety rules (from the project brief)

1. Never invent pin numbers – everything unknown is `-1` or marked UNKNOWN in `board_config.h`.
2. Never connect unknown power pins blindly – do M2 first.
3. Never power the A7672S from an ESP32 3.3 V pin (2 A bursts; 3.3 V is also below the useful range).
4. Never connect the two Li-Po batteries together without checking them ([POWER.md](POWER.md)).
5. Always connect the LTE antenna before powering the modem.
6. Common ground: every board's GND must be connected together.
7. Never short EAR_N or EAR_P to GND (bridge-tied speaker output).
