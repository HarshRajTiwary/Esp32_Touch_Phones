# Phone2 – architecture

## 1. System

```
                    ┌──────────────────────────────── ESP32-S3 "phone" ───────────────────────────────┐
  2.4" TFT ─SPI─────┤ UI task: touch → screens → frame buffer (PSRAM) → display                       │
  XPT2046 ─(shared)─┤ app core: calls, SMS, contacts, call log, notifications, audio routing          │
  BOOT button ──────┤ storage: NVS settings, LittleFS contacts/SMS/calls                              │
  RGB LED, buzzer ──┤ modem task ─┐  data task ─┐   Wi-Fi SoftAP + NAPT   btclient (link mirror)     │
                    └─────────────┼─────────────┼─────────┬──────────────────────┬──────────────────┘
                     UART1 + CMUX │ ch.1 AT     │ ch.2 PPP│ Wi-Fi                │ UART2 "BT link"
                    ┌─────────────┴─────────────┴──┐      │               ┌──────┴──────────────────┐
     Airtel ))) ────┤ SIMCom A7672S (4G Cat-1)     │   laptop            │ ESP32 "Bluetooth helper" │))) TWS earbuds
                    │ EAR_P ──────────────────────────────────── ADC ────►│ HFP Audio Gateway        │   (HFP)
                    │ MIC_P ◄─────────────────────────────────── DAC ─────┤ analog ↔ HFP audio bridge│
                    └──────────────────────────────┘                     └──────────────────────────┘
```

| Board | Role | Why |
|---|---|---|
| **ESP32-S3** (N16R8) | the phone: UI, modem control, SMS/contacts, 4G data, hotspot, settings | 8 MB PSRAM (smooth UI, big stores), 16 MB flash, own Wi-Fi radio, USB host for later |
| **ESP32** (WROOM) | Bluetooth Classic HFP Audio Gateway + analog call-audio bridge | the only chip here with Bluetooth Classic, which TWS earbuds need for calls |
| **A7672S** | cellular: calls, SMS, USSD, LTE data | Airtel bands B1/3/5/8/40 (+2G) |

The prompt's original single-ESP32 plan was changed with your approval: one ESP32
would run out of RAM with UI + Bluetooth Classic + Wi-Fi + PPP, and hotspot and SCO
would share one radio (Espressif rates SoftAP + Classic BT as "unstable").

## 2. Source tree

```
Phone2/
  boards/s3_main/        board_config.h (every S3 GPIO), board_init.c/.h
  boards/esp32_bt/       board_config.h (every helper GPIO), board_init.c/.h
  firmware/common/       used by both MCUs
    core/                logging (privacy masking), text/UTF-8/number/time helpers, COBS+CRC, sample ring
    btlink/              S3↔ESP32 protocol, framing, UART transport, SCO policy
    board/               include/board_config.h → picks the board from the build target
  firmware/main_s3/      ESP32-S3 phone (ESP-IDF project)
    main/app_main.cpp
    components/
      phonecore/         phone_types.h (central types), events (event bus), s3_config.h, net_rules
      modem/             modem.h (CellularModem API) + A7672S driver, CMUX, PPP, parsers
      storage/           settings (NVS), contacts / call log / messages (LittleFS)
      net/               hotspot (SoftAP + NAPT), SNTP, connectivity test
      btclient/          Bluetooth-helper mirror + audio test/levels API
      display/ touch/    ILI9341 + XPT2046 (LovyanGFX), calibration math
      ui/                GUI toolkit, keyboard, icons, all screens
      phone/             app core (phone_app), battery estimate, ringer
      diag/              serial console
  firmware/bt_esp32/     ESP32 Bluetooth helper (ESP-IDF project)
    components/bt_hfp/   GAP + HFP AG
    components/audio/    ADC/DAC bridge + DSP
    main/                link handling, LED, test console
  tests/host/            405 PC unit tests       tests/uisim/  PC screen renderer
  tools/                 setup / build / test / uisim scripts
  docs/                  this documentation + screenshots
```

C++ (ESP-IDF v5.5.5, gnu++2b) throughout; pure logic (parsers, codecs, state machines,
rules, stores) has no ESP-IDF dependencies so it runs in the PC tests.

## 3. Tasks (S3)

| Task | Core | Priority | Job | Blocks on |
|---|---|---|---|---|
| main (`app::loop`) | 1 | 1 | events → screens, touch poll, render, stores | never on hardware I/O (4 ms tick) |
| `modem` | 1 | 6 | start-up/recovery state machine, AT commands, URCs, SMS | AT replies (its own queue) |
| `modem_rx` | 0 | 18 | UART read, CMUX demux, line splitting, PPP input | UART |
| `data` | 1 | 5 | PPP dial / reconnect | data channel replies, PPP events |
| `btlink_rx` | any | 9 | link frames from the helper | UART |
| `nettest` | any | 3 | one-shot HTTP check | network |
| IDF Wi-Fi/lwIP/event | – | – | hotspot, PPP, NAPT | – |

Helper (ESP32): `main` (HFP service, heartbeat, LED), `btlink_rx`, `audio` (ADC DMA →
earbuds) + a 16 kHz timer ISR (DAC), `console`, Bluedroid's own tasks.

Shared data is only exchanged through: FreeRTOS queues (modem commands, events, AT
lines), event groups (CMUX UA, PPP events), and snapshots copied under a spinlock
(`modem::status()`, `call()`, `data()`, `bt::` state). No screen touches hardware.

## 4. Events and state

**Event bus** (`phonecore/events.h`), background → UI task:
`NetChanged` (modem ready, network registered, signal, SIM), `CallChanged`,
`CallEnded`, `SmsReceived`, `SmsSent`, `UssdReply`, `SimContacts`, `TimeSynced`,
`ModemNotice`, `DataChanged`, `HotspotChanged`, `BtHelper`, `BtChanged` (connected /
disconnected), `BtScanResult`, `BtScanDone`, `BtAudio` (SCO), `BtAnswer`, `BtHangup`,
`BtRedial`, `BtDial`, `BtVolume`, `BtAudioToEarbuds`, `BatteryChanged`.

**State model** (`phonecore/phone_types.h`): each subsystem owns its part and publishes
a thread-safe snapshot; the app core caches them for the UI every frame.

| State | Type | Owner |
|---|---|---|
| modem / SIM / network / signal / RAT | `NetStatus` (`ModemState`, `SimState`, `RegState`, `Rat`) | modem task |
| call | `CallInfo` (`CallPhase`, number, timers, end reason) | modem task (CallTracker) |
| mobile data | `DataStatus` (`DataState`, IP, DNS, counters) | data task |
| Bluetooth | `bt::State`, peer, battery, SCO, helper online | btclient (from the helper) |
| audio route | `AudioRoute` (Handset / Speaker / Earbuds) + `AudioPref` | app core |
| hotspot | `hotspot::Status` | net |
| battery | percent + charging guess | app core (from modem VBAT) |
| UI | navigation stack, modal, toast | ui |
| overall | `PhoneState` (Boot, InitializingModem, NoSim, SimLocked, SearchingNetwork, Ready, Error) | derived |

## 5. Key flows

**Incoming call with earbuds connected**
1. Modem prints `RING` / `+CLIP` / `+CLCC … 4` on CMUX channel 1 → `modem_rx` queues lines.
2. `modem` task → `CallTracker` → phase INCOMING → `CallChanged`.
3. App: wake screen, route = Earbuds (Auto), push Call screen; `bt::onPhoneState` sends
   `CallState` to the helper; the helper sends `RING`+`+CLIP` to the earbuds.
4. Earbud tap → HFP `ATA` → helper `Button(Answer)` → S3 `Ev::BtAnswer` → `ATA` on the modem.
5. `VOICE CALL: BEGIN` → ACTIVE → `CallState` → helper's SCO policy opens the voice
   link → audio bridge runs (EAR_P → ADC → earbuds, earbud mic → DAC → MIC_P).
6. Hang-up from any side → `CallEnded` → call log, SCO closed, route back to handset.

**SMS received**: `+CMTI` → `AT+CMGR` (PDU) → decode / join parts → `SmsReceived` →
message store (saved when quiet) → notification → SIM slot deleted.

**Hotspot**: data task dials PPP on channel 2 → `IP_EVENT_PPP_GOT_IP` → hotspot sets DHCP
DNS, NAPT forwards laptop traffic; calls continue on channel 1.

## 6. Error handling (prompt §30)

| Failure | Behaviour |
|---|---|
| A7672S does not answer | retry, PWRKEY / RESET / `AT+CRESET` power cycle, 3 attempts, then 30 s back-off; reason shown on screen |
| module restarts by itself | boot banner / AT timeouts detected → calls closed with "failed", data stopped, full re-init, notice |
| SIM missing / locked | "SIM not detected" / PIN screen; emergency calls still allowed |
| no network | "Searching for network", warning in the log after 2 min |
| call fails | end reason shown on the call screen (busy, no answer, no network, failed + modem error text) |
| SMS fails | message marked failed, reason in a notification, tap to resend |
| data fails | reason shown (dial timeout, auth failed …), back-off 5 s → 2 min, hotspot waits |
| Bluetooth helper silent | "Bluetooth helper offline", calls fall back to the handset, helper reset via EN |
| earbuds drop mid-call | audio moves to the handset automatically |
| touch not calibrated / invalid | calibration screen at first boot; default mapping otherwise; re-run from Settings |
| storage damaged | file ignored with a log line, crash-safe `.tmp` + rename on every save |
| settings format changed | defaults used, logged |
| crash | panic → reboot; core dump kept in flash for analysis |

## 7. Security (prompt §34)

* No arbitrary AT commands from the normal UI (serial console only).
* Input validation before anything reaches an AT command: SIM PIN digits only, USSD
  charset only, APN/user/password without quotes or control characters (`net_rules`).
* SIM PIN / APN password never logged (redacted); phone numbers and SMS text masked in
  logs unless `PHONE_LOG_PRIVATE=1`; ICCID shown masked.
* Hotspot: WPA2, random per-device password, no secrets in the source.
* Bluetooth: bonding with SSP; discoverable only while the Earbuds screen is open.
* Bounds-checked buffers everywhere (`txt::copy`, sized `snprintf`, `-Werror=format-truncation`).
* Not done (future): flash encryption / NVS encryption, secure boot.

## 8. Firmware updates (prompt §33)

The S3 partition table already has two 4 MB app slots (`ota_0`, `ota_1`) + `otadata`,
so OTA can be added without re-partitioning; OTA itself is not implemented.

## 9. Future ESP32-S3 roles

The S3 is now the main MCU. Planned next steps, all without changing the modem or
Bluetooth design: USB-host link to the A7672S (GPIO19/20 reserved) for multi-Mbit/s
data; OTA; camera / AI features on the S3's spare cores and PSRAM.

## 10. Milestones

| # | Milestone | Status |
|---|---|---|
| 0 | Hardware audit, docs | done (carrier board UNKNOWN – photos needed) |
| 1 | ESP-IDF projects boot | builds; UNVERIFIED on hardware |
| 2 | TFT | code done (display test screen); UNVERIFIED |
| 3 | Touch | code done (calibration, touch test); UNVERIFIED |
| 4 | A7672S UART | code done (state machine, CMUX); UNVERIFIED |
| 5 | SMS | code done; UNVERIFIED |
| 6 | Voice call (wired audio) | code done; UNVERIFIED |
| 7 | Bluetooth Classic | code done (helper console tests); UNVERIFIED |
| 8 | HFP | code done; UNVERIFIED |
| 9 | Audio bridge | code + RC design done; UNVERIFIED |
| 10 | Phone UI | done, 36 screens rendered on the PC |
| 11 | 4G data | code done (PPP over CMUX); UNVERIFIED |
| 12 | Hotspot | code done (NAPT); UNVERIFIED |
| 13 | Power | requirements only ([POWER.md](POWER.md)) |
| 14 | Integration | after hardware testing ([TESTING.md](TESTING.md)) |
