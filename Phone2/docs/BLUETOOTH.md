# Phone2 – Bluetooth earbuds

## 1. Design

* The generic **ESP32 is the Bluetooth helper**: it runs Bluetooth Classic with the
  **Hands-Free Profile, Audio Gateway role** (ESP-IDF v5.5.5 Bluedroid, `esp_hf_ag_*`).
  The earbuds are the Hands-Free unit – exactly like a phone and a headset.
* Not A2DP (music profile, no microphone), not BLE (earbud calls do not use it).
* The ESP32-S3 phone owns the call and network state and sends it to the helper over
  a UART ("BT link"). The helper never talks to the modem directly – it only touches the
  modem's analog audio (see [AUDIO_ARCHITECTURE.md](AUDIO_ARCHITECTURE.md)).
* The helper has no Wi-Fi at all, so its radio is 100 % Bluetooth.

Code: `firmware/bt_esp32/` (helper) – `components/bt_hfp/hfp_ag.cpp`,
`components/audio/audio_bridge.cpp`, `main/app_main.cpp`, `main/helper_console.cpp`.
S3 side: `firmware/main_s3/components/btclient/bt_client.cpp`.
Shared: `firmware/common/btlink/` (protocol, framing, SCO policy).

## 2. What the earbuds see

| HFP feature | Phone2 behaviour |
|---|---|
| Pairing | Secure Simple Pairing "Just Works"; legacy PIN requests answered with 0000 |
| Class of Device | Phone / cellular, Telephony + Audio services |
| Indicators (`+CIEV`) | service, signal (0–5 from RSRP/CSQ), roaming, battery (0–5) |
| Incoming call | `RING` + caller ID (`+CLIP`); the earbuds play their own ring tone (in-band ring not provided) |
| Button: answer / end / reject | `ATA` / `AT+CHUP` → the phone answers / hangs up |
| Button: redial (`AT+BLDN`) | redials the last number from Recents |
| Dial from earbuds / voice dial | `ATD<number>` is dialled; voice recognition is not supported |
| DTMF from earbuds | forwarded to the modem |
| Volume buttons (`+VGS`/`+VGM`) | earbud volume saved in Settings |
| `AT+CLCC`, `AT+COPS?`, `AT+CNUM` | current call, operator name, own number |
| Codec | CVSD 8 kHz; mSBC 16 kHz wide-band when the earbuds support it |
| Earbud battery | many TWS report it with Apple's `AT+XAPL`/`AT+IPHONEACCEV`; the helper answers these and shows the % (UNVERIFIED per model) |

## 3. When the voice link (SCO) is open

Decided by `sco_policy.cpp` (pure function, tested on the PC):

* **open** when the earbuds are connected **and** the call audio is routed to them
  during dialing, ring-back or an active call – or an audio test is running;
  retried every 3 s until the earbuds accept;
* **close** when the call ends, when you move the audio back to the handset, or 5 s
  after the earbuds opened a link on their own with no call.
* Ringing does not open SCO – the earbuds ring by themselves.

## 4. Pairing and reconnecting

* Home → **Earbuds** → *Search for earbuds* (the phone is discoverable while that
  screen is open). Put TWS earbuds in pairing mode first (usually: out of the case,
  hold the touch area ~5 s until the light flashes).
* The helper stores the Bluetooth link keys (Bluedroid, in its own NVS) and the last
  earbuds; after a reboot or when they come back in range it reconnects with a
  back-off of 8 s → 60 s. *Disconnect* stops that until you reconnect.
* *Forget* removes the bond; the "Paired before" list lets you reconnect or forget
  other remembered devices.

## 5. The BT link (S3 ↔ ESP32)

UART at 921600 baud, 3 wires + GND ([PINMAP.md](PINMAP.md)). Frames:
`COBS( type | seq | payload | CRC-16 ) 0x00` – a 0x00 byte always ends a frame, so the
receiver resynchronises after noise; the CRC catches corrupted frames.

| S3 → helper | | helper → S3 | |
|---|---|---|---|
| Hello | versions | HelloAck | helper started / answers Hello |
| SetEnabled, Discoverable | radio on/off | Status | state, earbuds, battery, SCO, heap – on change **and every 2 s** |
| ScanStart / ScanStop | discovery | ScanResult / ScanDone | found devices |
| Connect / Disconnect / Forget | earbud management | Bonded | remembered devices |
| CallState, NetState | phone state for HFP – on change **and every 2 s** | Button | answer / hang up / redial / dial / DTMF / volume / audio moved |
| AudioRoute, Volume, Gains, AudioTest | audio control | AudioStats | levels + gaps every 0.5 s while SCO is open |
| GetStatus, ListBonded, Reboot | maintenance | Log | helper log lines, shown on the S3 console as `[BT] …` |

Robustness: the full state is re-sent every 2 s, so a lost frame or a helper reboot
heals itself. The S3 marks the helper offline after 6.5 s without frames and, if
GPIO41 is wired to the helper's EN pin, resets it after 15 s. Version mismatch
between the two firmwares is logged.

## 6. Testing the helper on its own

Flash the helper, open its USB console (115200) and type `help`:

```
scan          search for earbuds (pairing mode first)
list          devices found
pair 0        pair + connect
ring +91...   fake an incoming call - the earbuds must ring, their button answers
talk / idle   fake an active call / end it
route on      send "call" audio to the earbuds
tone | echo   open the voice link: 1 kHz test tone / microphone echo
status        state, levels, gaps, heap
```

This proves pairing, HFP, SCO and the earbud buttons (TEST-011/012) before the S3 or
the modem are connected.

## 7. Limits (honest)

* One pair of earbuds at a time (`CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`).
* The "legacy" HFP PCM data API of ESP-IDF 5.5 is used (the one Espressif's own HFP-AG
  example uses). Espressif recommends the newer external-codec API for new designs;
  moving to it would only touch `hfp_ag.cpp` + `audio_bridge.cpp`.
* Earbud battery display depends on the earbud model (UNVERIFIED).
* Everything here is UNVERIFIED on hardware until TEST-011/012/013 pass.
