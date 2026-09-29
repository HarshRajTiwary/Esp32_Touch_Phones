# Phone2 – pin map

Every GPIO lives in exactly one file per board:

* ESP32-S3 phone: [`boards/s3_main/board_config.h`](../boards/s3_main/board_config.h)
* ESP32 Bluetooth helper: [`boards/esp32_bt/board_config.h`](../boards/esp32_bt/board_config.h)

(`firmware/common/board/include/board_config.h` picks the right one from the build
target.) Change a pin there and rebuild – no GPIO number appears anywhere else.

Status column: **FIXED** = chip rule, **CHOSEN** = our choice (safe pin, can be moved),
**UNKNOWN** = depends on the carrier board, check [HARDWARE.md §5](HARDWARE.md#5-what-to-check-on-your-boards-please-send-photos).

## 1. ESP32-S3 (phone)

### Display, touch and MicroSD (one SPI bus, SPI2 IO-MUX pins – full 40 MHz)

| TFT module pin | ESP32-S3 | Status | Notes |
|---|---|---|---|
| VCC | 3V3 | CHOSEN | |
| GND | GND | – | |
| CS | **GPIO10** | CHOSEN | |
| RESET | **GPIO8** | CHOSEN | or tie to 3V3 and set `PIN_TFT_RST -1` |
| DC (RS) | **GPIO9** | CHOSEN | |
| SDI (MOSI) | **GPIO11** | CHOSEN | IO-MUX pin of SPI2 |
| SCK | **GPIO12** | CHOSEN | IO-MUX pin of SPI2 |
| LED | **GPIO7** | CHOSEN | PWM brightness; or 3V3 = always on (`PIN_TFT_BL -1`) |
| SDO (MISO) | **not connected** | – | many modules never release it → corrupts touch |
| T_CLK | GPIO12 | CHOSEN | same wire as SCK |
| T_CS | **GPIO6** | CHOSEN | |
| T_DIN | GPIO11 | CHOSEN | same wire as SDI |
| T_DO | **GPIO13** | CHOSEN | IO-MUX MISO of SPI2 |
| T_IRQ | (GPIO5) | optional | firmware default −1 = pressure polling |
| SD_CS | (GPIO4) | optional | SD card not used by v1 |
| SD_MOSI / SD_SCK / SD_MISO | GPIO11 / 12 / 13 | optional | same bus |

### A7672S carrier board (UART1)

| Carrier board pin | ESP32-S3 | Status | Notes |
|---|---|---|---|
| RXD (module input) | **GPIO17** (S3 TX) | CHOSEN / UNKNOWN label | only if the board level-shifts to 3.3 V (check M3) |
| TXD (module output) | **GPIO18** (S3 RX) | CHOSEN / UNKNOWN label | 5 V output → add a divider |
| PWRKEY / PWR | **GPIO21** | CHOSEN / UNKNOWN polarity | `MODEM_PWRKEY_ACTIVE_HIGH` 0 = open-drain low (raw pin), 1 = drives a transistor |
| RESET | (−1) | optional | emergency only |
| STATUS, RI, DTR | (−1) | optional | only if level-shifted to 3.3 V |
| RTS / CTS | (−1) | optional | hardware flow control for faster PPP |
| GND | GND | – | **must** be common |
| VIN / VBAT | – | UNKNOWN | from the battery system, never from an ESP32 pin – [POWER.md](POWER.md) |

### BT link to the ESP32 helper (UART2, 921600 baud)

| ESP32-S3 | ESP32 helper | Notes |
|---|---|---|
| **GPIO39** (TX) | **GPIO16** (RX2) | |
| **GPIO40** (RX) | **GPIO17** (TX2) | |
| **GPIO41** | **EN** | optional: lets the phone reset a hung helper (open-drain) |
| GND | GND | required |

### Other

| Function | ESP32-S3 | Status |
|---|---|---|
| Home / wake button | GPIO0 (on-board BOOT button) | FIXED by the board |
| RGB notification LED | GPIO48 (38 on DevKitC-1 v1.1) | SUSPECTED |
| Buzzer (ringtone) | GPIO42 → passive buzzer → GND (via 100 Ω) | optional |
| Battery ADC | −1 (battery voltage comes from the modem's AT+CBC) | optional |
| Console | GPIO43/44 (UART0, "COM" USB port) | FIXED |
| USB D−/D+ | GPIO19/20 | reserved for a future USB-host link to the A7672S |

**Never used on the S3:** GPIO26–32 (flash/PSRAM), GPIO33–37 (octal PSRAM),
GPIO3/45/46 (strapping). GPIO0 is only read after boot.

## 2. ESP32 (Bluetooth helper)

| Function | ESP32 | Status | Notes |
|---|---|---|---|
| Link RX ← S3 GPIO39 | **GPIO16** (RX2) | CHOSEN | on a WROVER module use 32/33 instead (16/17 = PSRAM) |
| Link TX → S3 GPIO40 | **GPIO17** (TX2) | CHOSEN | |
| Uplink audio DAC → A7672S MIC_P | **GPIO25** | FIXED (DAC1) | through the RC network in [AUDIO_ARCHITECTURE.md](AUDIO_ARCHITECTURE.md) |
| Downlink audio ADC ← A7672S EAR_P | **GPIO34** | CHOSEN (ADC1 only) | input-only pin; ADC2 is not usable with the radio on |
| Status LED | GPIO2 | SUSPECTED | on-board blue LED of DOIT boards |
| Mic switch (optional) | −1 | optional | analog switch that disconnects a wired handset mic |
| EN | ← S3 GPIO41 | optional | |
| GND | common ground with S3 **and** modem | required | audio needs a clean shared ground |

**Never used on the ESP32:** GPIO6–11 (flash), GPIO1/3 (console), GPIO12 (flash-voltage strap).

## 3. Wiring checklist

```
TFT VCC ── S3 3V3            TFT GND ── GND           TFT CS ── S3 10     TFT RESET ── S3 8
TFT DC  ── S3 9              TFT SDI ── S3 11         TFT SCK ── S3 12    TFT LED ── S3 7
T_CLK   ── S3 12             T_CS ── S3 6             T_DIN ── S3 11      T_DO ── S3 13
(TFT SDO and T_IRQ: leave unconnected)

Modem TXD ── S3 18           Modem RXD ── S3 17       Modem PWRKEY ── S3 21 (if present)
Modem GND ── GND             Modem VIN/VBAT ── power system (POWER.md)

S3 39 ── ESP32 16            S3 40 ── ESP32 17        S3 41 ── ESP32 EN (optional)
S3 GND ── ESP32 GND

ESP32 25 ── uplink RC network ── Modem MIC_P      Modem MIC_N ── 1 µF ── GND
ESP32 34 ── downlink RC network ── Modem EAR_P    (EAR_N: earpiece only, never GND)
```

Keep the SPI and audio wires short (< 15 cm) and away from the antenna.
