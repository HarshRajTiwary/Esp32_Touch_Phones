# Phone2 – testing

Two kinds of tests:

1. **On the PC, no hardware** – already passing (section 1).
2. **On the hardware**, one stage at a time (sections 2–4). Each stage only needs the
   parts of the stages before it, so when something fails you know where to look.
   Keep the USB consoles open – the log says what the phone is doing.

## 1. PC tests (done)

```powershell
powershell -File tools\test.ps1     # 405 unit tests
powershell -File tools\uisim.ps1    # renders all 36 screens to docs\screens\*.png
```

| Area | What is tested |
|---|---|
| Modem parser | A76XX responses: `+CBC: 3.749V`, `+CPSI` (LTE/GSM, both RSRP formats), `+CREG/+CEREG` with access technology, `VOICE CALL: BEGIN/END`, `MISSED_CALL`, `+ICCID`, `+CNMP`, `+CTZV`, boot banners, `+CGEV` |
| AT response parser | final result codes, CME/CMS errors, `+CLCC`, `+CLIP`, `+CMTI`, `+CUSD` (multi-line, UCS2), `+CPBR`, `+CCLK` … |
| CMUX | frames checked against the 27.010 reference bytes (SABM, UA, close-down), 2-byte lengths, 0xF9 inside data, FCS errors, restart text after a frame |
| Phone state machine | outgoing / incoming / missed / declined / busy / no-network / failed calls, safety polls, call waiting, mute, data "calls" ignored |
| SMS parser | PDU decode/encode, GSM 7-bit & UCS2, alphanumeric senders, long-SMS join and time-out |
| Touch calibration | straight, mirrored, swapped and rotated panels; collinear points rejected; clamping |
| Contact storage | add / sort / update / delete / search by number, SIM import duplicates, deferred save, no writes during calls, reload |
| Bluetooth state | BT link framing (COBS/CRC, resync, corruption), message sizes, SCO voice-link policy table |
| Hotspot / data rules | SSID / WPA2 password rules, AT-safe APN, reconnect back-off |
| Other | battery %, DSP filters, text/time helpers, call log, message threads |

## 2. Flashing

One-time: `powershell -File tools\setup.ps1` (ESP-IDF 5.5.5 + libraries – already done on this PC).

Find the COM ports in Device Manager (the S3's **COM/UART** port, not its native USB port).

```powershell
powershell -File tools\build.ps1 -Target bt   -Flash -Port COM5 -Monitor   # ESP32 helper
powershell -File tools\build.ps1 -Target main -Flash -Port COM7 -Monitor   # ESP32-S3 phone
```

If flashing does not start, hold BOOT, tap EN/RST, release BOOT. Leave the monitor with
`Ctrl+]`. Both consoles accept commands – type `help`.

## 3. Hardware tests

### TEST-001 – both boards boot (nothing connected)

S3 console:
```
I (…) BOARD: phone2-s3-main: esp32s3 rev v0.x, 2 cores @ 240 MHz, flash 16 MB, PSRAM 8 MB, MAC …
    … [APP] Phone2 0.9.0 starting
    … [FS] LittleFS mounted: …/… KB used
    … [CFG] hotspot defaults generated (name Phone2-XXXX)        (first boot only)
    … [LINK] BT link on UART2 TX=39 RX=40 @ 921600 baud
    … [MODEM] UART1 TX=GPIO17 RX=GPIO18 @ 115200 baud
    … [CON] debug console ready - type 'help'
```
Helper console:
```
I (…) BOARD: phone2-esp32-bt: esp32 rev v…, 2 cores, flash 4 MB, Bluetooth Classic: yes
    … [AUDIO] bridge ready: DAC GPIO25, ADC GPIO34
    … [BT] Bluetooth ready as "Phone2" (…)
    … [BT] HFP audio gateway ready
```
**Fail:** `PSRAM 0 MB` → board is not an R8 – the phone still runs with fewer messages;
tell me the exact module. Flash smaller than 16 MB → change the partition table.

### TEST-002 – display

Wire the TFT ([PINMAP.md](PINMAP.md)), power on. Expected: `[LCD] ILI9341 240x320 ready,
PSRAM frame buffer`, the Phone2 logo, then touch calibration (first boot).
Home → Diagnostics → **Display**: colour bars (white, yellow, cyan, green, magenta, red,
blue, black), gradients, then the orientation page (red corner top-left).
**Fail:** white screen → CS 10 / DC 9 / SCK 12 / SDI 11 / RESET 8; red and blue swapped →
`TFT_RGB_ORDER true`; negative colours → `TFT_INVERT true`; noise → `TFT_SPI_WRITE_HZ` 27 MHz.

### TEST-003 – touch

Console `touch`, press each corner:
`[TOUCH] raw x=312 y=3620 -> screen x=12 y=300` – raw values change smoothly (≈200–3900).
Do the 4+1-point calibration; then Diagnostics → **Touch**: the dots follow the stylus.
**Fail:** no values → T_CS 6, T_DO 13, T_DIN 11, T_CLK 12, TFT SDO **not** connected.

### TEST-004 – modem UART (antenna connected!)

Do the checks M1–M4 in [HARDWARE.md](HARDWARE.md) first. Connect GND, TXD→GPIO18,
RXD←GPIO17, PWRKEY→GPIO21 (if present), power the carrier board from its own supply.
```
[MODEM] state: powering on
[MODEM] pressing PWRKEY (1000 ms)          or  [MODEM] module already on
[MODEM] module answered after 8120 ms
[MODEM] A7672S-LASE, firmware A011B…
[MUX] multiplexer up: channel 1 = AT, channel 2 = data
```
Console `at AT+CGMR`, `at AT+CPSI?` answer. Settings → Network & SIM shows model/firmware.
**Fail:** see [MODEM.md §9](MODEM.md#9-troubleshooting). Without CMUX
(`module refused AT+CMUX`) calls/SMS still work; data does not.

### TEST-005 – SIM

Insert the Airtel SIM with the power off. Expected `[SIM] READY` (or the PIN screen).
Settings → Network & SIM: SIM card "Ready", ICCID (masked), own number if the SIM stores it.

### TEST-006 – network

```
[NET] Registered (4G)
[NET] Operator: Airtel
[MODEM] state: ready
```
Status bar: signal bars, **4G**, Airtel. Network & SIM: RSRP (−80…−110 dBm is fine), band
(B3/B40/…). Try Network mode "2G only" and back to "Auto".

### TEST-007 – SMS send

Messages → + → your other phone → "Test 1". Log `[SMS] send to +91******XXXX (1 part): OK`.
Also send a 200-character message (2 parts) and one with Hindi/emoji (UCS2).

### TEST-008 – SMS receive

From another phone send a short SMS, a long one, and one with ₹/emoji. Log
`[SMS] new message in slot N`, `[SMS] received from …`; notification + blue LED blip;
message readable in the thread. Also an operator SMS (`AX-AIRTEL` style sender).

### TEST-009 – outgoing call (wired audio first)

Connect a 32 Ω earpiece to EAR_P/EAR_N and an electret mic to MIC_P/MIC_N (or use the
carrier board's jack). Dial your other phone: `[CALL] dialing`, `alerting`, `active`,
timer runs, both directions audible. Try Mute, Speaker (louder profile), Keypad (DTMF in
an IVR, e.g. 121), volume ±, End. Recents shows the call with duration.

### TEST-010 – incoming call

Call the SIM: `[CALL] Incoming: …`, ring screen (buzzer if fitted), green LED blink.
Answer; later: Decline, Decline + SMS, and a missed call (red in Recents, notification).

### TEST-011 – Bluetooth pairing (helper alone)

Helper console: `scan`, `list`, `pair 0` with the earbuds in pairing mode:
`[BT] paired with …`, `[BT] HFP connected to …`, LED steady. Power-cycle the helper:
it reconnects by itself. Then with the S3 connected: Home → Earbuds shows the earbuds,
`[BT] Headset connected: …` on the S3 console.

### TEST-012 – HFP

Helper console `ring +911234567890` → the earbuds ring; tapping answers
(`earbud button: answer`); `talk`, `route on` → `[BT] voice link open (CVSD/mSBC)`;
`idle` → link closed. With the S3: a real incoming call rings the earbuds, the earbud
tap answers it, double-tap (varies) redials, volume buttons change the earbud volume.

### TEST-013 – audio bridge

Build the two RC networks ([AUDIO_ARCHITECTURE.md](AUDIO_ARCHITECTURE.md)).
1. Settings → Sound → **Test tone**: steady 1 kHz in the earbuds (proves Bluetooth → earbuds).
2. **Mic echo**: you hear yourself (proves the earbud microphone).
3. A real call with the earbuds: the "To earbuds" meter moves around −20 dB when the other
   person talks; ask them how you sound. Tune Call volume / Bridge level (too quiet /
   distorted) and Mic level / Bridge mic (too quiet / echo). Gaps should stay near 0.

### TEST-014 – cellular data

Home → Mobile data: **Connected**, IP, DNS (log `[NET] IP acquired: …`).
**Test internet connection** → "Internet OK (… ms)". Then make a call while data is on:
the data link must survive (or reconnect by itself within a minute).

### TEST-015 – Wi-Fi hotspot

Follow [HOTSPOT.md §5](HOTSPOT.md#5-test-with-a-laptop-test-015): laptop joins, gets
192.168.4.x, browses; a call during the session works; pulling and re-fitting the antenna
recovers by itself.

### TEST-016 – battery

Only after [POWER.md](POWER.md) stage B. Battery % in the status bar follows the cell
voltage (Network & SIM → Supply); no modem restarts during 2G calls at full signal
(no `module printed its boot banner`); warning at 10 %.

## 4. Recovery and soak tests

| Test | Expected |
|---|---|
| Disconnect the modem supply for 5 s | "Modem not responding" → automatic restart → registered again |
| Reset the S3 while the modem is in CMUX | the modem is taken out of CMUX and re-initialised |
| Unplug the helper for 20 s | "Bluetooth helper offline", call audio falls back to the handset; reconnects on its own (EN reset if GPIO41 wired) |
| Earbuds out of range during a call | audio moves to the handset |
| 24 h on, a few calls and SMS | Diagnostics → free RAM stays roughly constant; no reboots (`heap` command) |

Report results with the console log around the problem – numbers and SMS text are
masked in the log, so it is safe to share.
