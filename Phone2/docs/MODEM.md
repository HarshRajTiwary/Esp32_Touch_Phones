# Phone2 – the A7672S modem

Code: `firmware/main_s3/components/modem/` – public API in `include/modem.h`
("CellularModem"), SIMCom A7672S driver in `a7672s*.cpp`.

## 1. What works where

A feature needs every layer to support it. Status today:

| Feature | A76XX chip family | A7672S module | Carrier board "LASPLO076_RevB" | Phone2 firmware | Airtel network |
|---|---|---|---|---|---|
| Voice calls | yes | yes | needs audio pads (UNKNOWN) | implemented, UNVERIFIED | 2G (CSFB) yes; VoLTE depends on module firmware – UNVERIFIED |
| SMS (PDU, long, Unicode) | yes | yes | – | implemented, UNVERIFIED | yes (CS or over LTE) |
| USSD (`*123#`) | yes | yes | – | implemented, UNVERIFIED | usually via 2G fallback – UNVERIFIED |
| LTE Cat-1 data | 10/5 Mbit/s | yes | – | PPP over UART, UNVERIFIED | yes |
| Data at full speed | – | UART up to 3.6864 Mbaud, USB 2.0 | UART level shifter may cap it at 115200 (UNKNOWN) | 115200 by default (≈ 85 kbit/s), 921600 option | – |
| CMUX (calls + data together) | yes (`AT+CMUX=0`) | yes | – | implemented, UNVERIFIED | – |
| PCM digital audio | – | **no** (analog only) | – | not needed | – |
| USB RNDIS/ECM | device side only | device only | port may exist (M8) | not used (the S3 could be a USB host later) | – |
| GNSS | optional | only "…FASE"/"…FANS" variants | UNKNOWN | not implemented | – |
| Module Bluetooth | optional | only some variants | – | not used (it cannot do phone-side HFP for us) | – |

## 2. Serial link: one UART, two channels

```
 ESP32-S3 UART1 ──TX/RX── A7672S main UART
      │
      └─ CMUX (3GPP TS 27.010 basic option, AT+CMUX=0)
           DLCI 0  control (open/close, flow control, modem status)
           DLCI 1  AT commands + all unsolicited results (RING, +CMTI, +CLCC ...)
           DLCI 2  4G data: "ATD*99#" then PPP frames
```

Why CMUX: without it the UART can carry either AT commands **or** a PPP data session,
not both – an incoming call would be missed while the hotspot is on.
If the module refuses `AT+CMUX=0`, the driver keeps running on the plain UART:
calls and SMS still work, mobile data shows "unavailable".

Speed of the 4G link through the UART (theory, UNVERIFIED):

| UART baud | Usable PPP throughput | Needs |
|---|---|---|
| 115200 (default) | ≈ 85–90 kbit/s | any level shifter |
| 921600 | ≈ 650–700 kbit/s | fast level shifter; set `MODEM_TRY_FAST_BAUD 1` |
| 3 000 000+ | ≈ 2 Mbit/s | a push-pull shifter or a 3.3 V-I/O board; RTS/CTS recommended |
| USB host (future) | several Mbit/s | ESP32-S3 GPIO19/20 are kept free for this |

SIMCom warns that the simple 2-transistor level shifter "is not recommended above
115200 bps". The firmware tries the fast rate only when `MODEM_TRY_FAST_BAUD` is 1, checks
it, and falls back to 115200 automatically.

## 3. Start-up state machine

Every state has a timeout, a failure reason (shown in Settings → Network & SIM and
Diagnostics) and a recovery path. Timings are in `phonecore/include/s3_config.h`.

| State | What happens | Timeout / retries | Failure reason | Recovery |
|---|---|---|---|---|
| `OFF` → `POWERING_ON` | close any CMUX left over from an S3 reset; probe `AT` | probe 3 s | – | if silent: press PWRKEY 1 s |
| `BOOTING` | `AT` every 0.5 s (STATUS pin logged if wired) | 25 s | "no answer on UART (power, TX/RX swap, level shifter, PWRKEY?)" | power cycle |
| `AT_TEST` | `ATE0`, `AT+CMEE=1`, model / firmware / IMEI, optional flow control and fast baud | 3 s per command | "ATE0 failed", "lost the module after a baud rate change" | power cycle |
| `CMUX_START` | `AT+CMUX=0`, open DLCI 0/1/2 (SABM→UA, 3 tries each), MSC, `AT` on DLCI 1, `AT+CATR=0` | 1.5 s per channel | "CMUX channels did not open" | power cycle (refusal = run without data) |
| `SIM_CHECK` | `AT+CPIN?` every second | 15 s | – (continues: emergency calls and the UI still work) | SIM PIN screen; re-init after PIN |
| configure | calls, SMS, network, audio settings (section 4) | 3 s each | logged | – |
| `NETWORK_SEARCH` | `+CREG`/`+CEREG` URCs, polls every 3 s | warning after 120 s | "no network after 120 s (CSQ n)" | keeps searching |
| `READY` | registered; data task dials | – | – | back to search on loss |
| `NOT_RESPONDING` | after 3 failed starts | back-off 30 s | last reason kept | full retry; "Restart modem" button skips the wait |

While running, the driver re-initialises when:
* 4 AT commands in a row time out → power cycle;
* the module prints its boot banner (`RDY`, `*ATREADY`) – it restarted by itself
  (usually a supply dip during a transmit burst) → notice "Modem restarted – check the battery".

Power cycle = PWRKEY held 3 s (only if STATUS does not say "off"), 3 s pause, then
the normal start; RESET pin (2.5 s) or `AT+CRESET` when PWRKEY is not wired.

## 4. AT commands used

| Command | Purpose | Saved in module? |
|---|---|---|
| `ATE0`, `AT+CMEE=1` | no echo, numeric error codes | no |
| `AT+CGMM`, `AT+CGMR`, `AT+CGSN` | model, firmware, IMEI | – |
| `AT+IFC=2,2` | RTS/CTS (only if both pins are wired) | – |
| `AT+IPR=921600` | fast UART (optional) | no (temporary) |
| `AT+CMUX=0` | start the multiplexer | no |
| `AT+CATR=0` | URCs on every port (so they reach channel 1 whatever the port numbering) | no |
| `AT+CPIN?`, `AT+CPIN="…"` | SIM state / PIN (PIN never logged) | – |
| `AT+CLIP=1`, `AT+CLCC=1`, `AT+CVHU=0`, `AT+CRC=0` | caller ID, automatic call-state reports, ATH hangs up | no |
| `AT+CREG=2`, `AT+CEREG=2` | registration URCs with location + access technology | no |
| `AT+COPS=3,0`, `AT+COPS?` | long operator name | no |
| `AT+CTZU=1`, `AT+CTZR=1`, `AT+CCLK?` | network time + time zone | CTZU: yes |
| `AT+CNMP?` / `AT+CNMP=2/13/38` | network mode auto / 2G / 4G (written only when different) | yes |
| `AT+CICCID`, `AT+CIMI`, `AT+CNUM` | SIM identity, own number (masked in the UI) | – |
| `AT+CMGF=0`, `AT+CSCS="IRA"`, `AT+CPMS="SM","SM","SM"`, `AT+CNMI=2,1,0,0,0` | PDU mode, SMS stored on SIM + `+CMTI` | – |
| `AT+CGEREP=2,1` | `+CGEV` data events | no |
| `AT+CSQ`, `AT+CPSI?`, `AT+CBC`, `AT+CPMUTEMP` | signal, RAT/band/RSRP, supply voltage, temperature | – |
| `ATD<n>;`, `ATA`, `AT+CHUP`/`ATH`, `AT+VTS="x"`, `AT+CMUT` | calls | – |
| `AT+CSDVC=1/3` | handset / speakerphone audio profile | no |
| `AT+COUTGAIN`, `AT+CMICGAIN` (0–7) | speaker / mic gain (**read first, written only on change**) | yes |
| `AT+CMGR`, `AT+CMGL=4`, `AT+CMGS`, `AT+CMGD` | read / list / send / delete SMS | – |
| `AT+CUSD=1,"…",15`, `AT+CUSD=2` | USSD | – |
| `AT+CPBS="SM"`, `AT+CPBR` | SIM phone book import | – |
| `AT+CGDCONT=1,"IP","apn"`, `AT+CGAUTH` (data channel) | APN + authentication (written only on change; A76XX order is password, then user) | yes |
| `ATD*99#` (data channel) | start PPP | – |
| `AT+CRESET`, `AT+CPOF` | reset / power down | – |

Unsolicited results handled: `RING`, `+CLIP`, `+CLCC`, `VOICE CALL: BEGIN/END`,
`MISSED_CALL`, `NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE`, `+CMTI`, `+CUSD`
(multi-line), `+CPIN`, `+CREG`, `+CEREG`, `+CGREG`, `+CTZV`, `+CGEV`, `SMS DONE`,
`PB DONE`, `RDY`, `*ATREADY`.

## 5. Calls

`call_tracker.cpp` is a pure state machine (tested on the PC with scripted URC
sequences); `a7672s_call.cpp` feeds it and publishes `CallChanged` / `CallEnded`.

```
IDLE ─ATD─► DIALING ─+CLCC 3─► ALERTING ─+CLCC 0 / VOICE CALL: BEGIN─► ACTIVE ─► ended ─► IDLE
IDLE ─RING/+CLIP─► INCOMING ─ATA─► ACTIVE ─NO CARRIER / VOICE CALL: END─► ended ─► IDLE
```

End reasons: normal, busy, no answer, no carrier, **declined** (you pressed decline),
**missed** (incoming, never answered), failed, no network. A safety `AT+CLCC` poll every
3 s during calls closes a call that disappeared without a result code.

Emergency numbers (112, 100, 108) can be dialled without registration.

**VoLTE vs 2G (UNVERIFIED):** on Airtel 4G, voice needs either VoLTE in the module
firmware or a fall-back to 2G (CSFB). "Auto" network mode allows both. "4G only" may
block calls if VoLTE is not active – the UI warns about this.

## 6. SMS and USSD

* PDU mode: long messages are joined (up to 10 minutes wait for missing parts),
  Unicode (Hindi, ₹, emoji) decoded, alphanumeric senders like `AX-AIRTEL` handled.
* A received SMS stays on the SIM until the phone has stored it, then is deleted.
  A sweep every 5 minutes catches any missed `+CMTI`.
* Sending: GSM 7-bit or UCS2, up to 5 joined parts, 60 s network confirmation.
* USSD: `*123#` style codes, multi-line menus, replies, 30 s timeout.

## 7. 4G data (PPP)

Own task (`a7672s_data.cpp`) so dialing never delays calls:

```
OFF / WAIT_NETWORK ─► DIALING (AT, ATE0, AT+CGDCONT, [AT+CGAUTH], ATD*99# → CONNECT)
   ─► NEGOTIATING (lwIP PPP: LCP, PAP/CHAP, IPCP) ─► CONNECTED (IP + DNS)
   ─► link lost / error ─► BACKOFF (5 s, 10 s … 2 min) ─► DIALING
```

* The PPP interface becomes the default route; the hotspot shares it ([HOTSPOT.md](HOTSPOT.md)).
* A registration loss shorter than 15 s (cell change, a 2G voice call) does not drop PPP.
* APN / user / password: Settings → Mobile data (default `airtelgprs.com` – only a
  default, not assumed correct). Quotes/control characters are refused so text can never
  escape the AT command.
* Byte counters (down/up) are shown in Mobile data and Hotspot.
* The clock is also set by SNTP once online (converted to the network's time zone).

## 8. Driver structure

| File | Role |
|---|---|
| `include/modem.h` | CellularModem API (the only thing the app uses) |
| `a7672s.cpp` | modem task, start-up state machine, recovery, command queue |
| `a7672s_at.cpp` | line splitting, `exec()` with URC dispatch, SMS prompt, secret redaction |
| `modem_port.cpp` | UART driver + CMUX sessions (SABM/UA, MSC, flow control, restart detection) |
| `cmux.cpp` | CMUX frame codec (pure, tested) |
| `a7672s_call.cpp` + `call_tracker.cpp` | calls |
| `a7672s_sms.cpp` | SMS, USSD, SIM phone book |
| `a7672s_network.cpp` | registration, signal, operator, identity, time |
| `a7672s_audio.cpp` | audio profile and gains |
| `a7672s_data.cpp` | PPP data manager |
| `at_parse.cpp`, `sms_pdu.cpp`, `sms_assembler.cpp` | parsers / codecs (pure, tested) |

Threads: `modem` (commands + URCs), `modem_rx` (UART + CMUX demux), `data` (PPP).
Only the modem task blocks on AT replies; the UI talks to it through a queue and
receives events.

A different modem (e.g. the SIM800L driver of the old TouchPhone firmware) would
implement the same `modem.h`; the app and UI would not change.

## 9. Troubleshooting

| Symptom (log) | Likely cause |
|---|---|
| `no answer on UART` | no power, TX/RX swapped, 1.8 V UART without shifter, PWRKEY not pressed |
| `CMUX channels did not open` | noisy link / baud mismatch – try without `MODEM_TRY_FAST_BAUD` |
| `module printed its boot banner` / modem restarts during calls | supply dips on 2 A bursts – see [POWER.md](POWER.md) |
| `SIM ... NOT INSERTED` | SIM orientation, SIM holder contacts |
| stays "Searching" | no antenna, wrong antenna port, weak signal, SIM without a plan |
| data `retrying (dial timeout)` | APN wrong, data plan exhausted |
| data `authentication failed` | APN user/password |
