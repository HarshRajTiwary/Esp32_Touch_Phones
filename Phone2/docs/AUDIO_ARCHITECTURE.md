# Phone2 – call audio

## 1. The situation

* The A7672S has **analog audio only** (CONFIRMED, SIMCom hardware design):
  one microphone input **MIC_P/MIC_N** (with MIC_BIAS) and one speaker output
  **EAR_P/EAR_N** (bridge-tied class-AB, 32 Ω, 37 mW). No PCM/I2S.
* TWS earbuds make calls with **Bluetooth Classic HFP** (not A2DP, not BLE).
  Only the ESP32 (not the S3) has Bluetooth Classic.
* So the ESP32 helper converts between the modem's analog audio and Bluetooth:

```
 UPLINK (what the other person hears)
   earbud mic ─HFP (CVSD 8 kHz / mSBC 16 kHz)─► ESP32 ─gain, 8→16 kHz─► DAC GPIO25
             ─► RC network ─► A7672S MIC_P                         (MIC_N: AC-grounded)

 DOWNLINK (what you hear)
   A7672S EAR_P ─► RC network ─► ADC1 GPIO34 (DMA, 24/32 kHz) ─► DC block, FIR low-pass,
             decimate to 8/16 kHz, gain ─► HFP ─► earbuds
```

No codec chip is needed for version 1: the ESP32's own 8-bit DAC and 12-bit ADC are
used. Phone-network voice is narrow-band (300–3400 Hz for 2G/AMR-NB, up to 7 kHz for
VoLTE AMR-WB), so speech stays clear. A proper codec is the planned upgrade (section 7).

## 2. Three ways to talk

| Mode | Where audio goes | Modem profile | Extra hardware |
|---|---|---|---|
| **Handset** | 32 Ω earpiece on EAR_P/EAR_N + electret mic on MIC_P/MIC_N | `AT+CSDVC=1` | earpiece + mic (or the carrier board's jack) |
| **Speaker** | same output through a small amplifier (e.g. PAM8302) to an 8 Ω speaker | `AT+CSDVC=3` (hands-free gains + echo canceller) | amplifier + speaker (optional) |
| **Earbuds** | ESP32 helper taps EAR_P / drives MIC_P | `AT+CSDVC=1` | the two RC networks below |

Settings → Sound → "Calls use": **Auto** (earbuds when connected), Earbuds, Handset.
During a call the Speaker / Earbuds buttons switch live; tapping the earbuds' "move
audio" control is followed too (the helper reports it).

## 3. The RC networks (UNVERIFIED values – tune on the bench)

### Downlink: A7672S EAR_P → ESP32 GPIO34

```
                          3V3
                           │
                          [R1 15k]
                           │
 A7672S EAR_P ──[C1 1µF]───●───[R3 1k]───●──────────► ESP32 GPIO34 (ADC1_CH6)
                           │             │
                          [R2 10k]      [C3 22nF]
                           │             │
                          GND           GND
 A7672S EAR_N : earpiece only – never to GND
```

* R1/R2 bias the ADC input at ≈ 1.3 V, inside the ESP32 ADC's linear range
  (≈ 0.15–2.45 V at 12 dB attenuation). EAR_P swings up to ≈ ±0.77 V at full volume.
* R3 + C3 = 7 kHz anti-alias filter (the firmware's FIR does the real filtering:
  3.6 kHz for CVSD, 7 kHz for mSBC).
* The load (≈ 6 kΩ through 1 µF) is negligible next to a 32 Ω earpiece.
* Level: Settings → Sound → **Call volume** (`AT+COUTGAIN`, 0–7) and **Bridge level**.

### Uplink: ESP32 GPIO25 → A7672S MIC_P

```
 ESP32 GPIO25 ──[C4 1µF]──[R4 22k]──●──[C6 1µF]──o J1 o──► A7672S MIC_P
                                     │
                           [R5 470Ω] ∥ [C5 47nF]
                                     │
                                    GND
 A7672S MIC_N ──[C7 1µF]──► GND
```

* R4/R5 divide the DAC's ±1.6 V down to ≈ ±35 mV – electret-microphone level.
* C5 smooths the DAC steps (7 kHz).
* **J1** (jumper, or an analog switch on `PIN_AUDIO_SWITCH`) disconnects the injection
  while a wired microphone is used, otherwise R5 loads the wired mic.
* If the carrier board already biases MIC_P for an on-board microphone, C6 keeps our DC
  out of it. Whether the module biases MIC_P internally when no microphone network is
  fitted is UNKNOWN – if the far end hears nothing, add the reference-design bias:
  MIC_BIAS → 2.2 kΩ → MIC_P and MIC_N → 2.2 kΩ → GND.
* Level: **Mic level** (`AT+CMICGAIN`, 0–7) and **Bridge mic**.

### Parts

| Part | Value | Qty |
|---|---|---|
| Resistor | 15 kΩ (R1), 10 kΩ (R2), 1 kΩ (R3), 22 kΩ (R4), 470 Ω (R5) | 1 each |
| Capacitor (ceramic, non-polar) | 1 µF (C1, C4, C6, C7) | 4 |
| Capacitor | 22 nF (C3), 47 nF (C5) | 1 each |
| Jumper / switch | 2-pin (J1) | 1 |

Keep these wires short, twisted with a ground wire, away from the antenna and the
TFT's SPI lines, and use one ground point for modem, ESP32 and the networks.

## 4. Digital side (ESP32 helper, `bt_esp32/components/audio/`)

| | CVSD (narrow-band) | mSBC (wide-band) |
|---|---|---|
| HFP PCM rate / format | 8 kHz, 16-bit mono | 16 kHz, 16-bit mono |
| Chosen by | the earbuds (codec negotiation) | the earbuds |
| ADC sampling (DMA) | 24 kHz → FIR 3.6 kHz → ÷3 | 32 kHz → FIR 7 kHz → ÷2 |
| DAC update | 16 kHz timer (8 kHz ×2 linear interpolation) | 16 kHz timer |
| HFP frame | 120 bytes / 7.5 ms | 240 bytes / 7.5 ms |

Voice over HCI (`CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI`): Bluedroid hands PCM frames to
`audio::onHfIncoming()` / `onHfOutgoing()`; CVSD/mSBC coding is done by the stack.

Buffers and latency (one way, added by the bridge):

| Stage | Delay |
|---|---|
| ADC DMA frame | ≈ 10 ms |
| FIR filter (48 taps) | ≈ 1 ms |
| Downlink ring buffer | 40 ms target, trimmed if it grows past 120 ms |
| Uplink ring buffer | 30 ms pre-fill, 50 ms target, trimmed past 150 ms |
| Bluetooth + earbuds | 20–80 ms (earbud dependent) |

Total ≈ 80–150 ms on top of the network – normal for Bluetooth calls.

Level meters, gaps (under-runs) and overruns are shown in Settings → Sound and
Diagnostics, and printed by the helper console (`status`).

## 5. Echo and noise

* **Acoustic echo in the earbuds** – handled by the earbuds' own echo canceller.
* **Modem echo canceller** – the A7672S applies its handset profile (`AT+CSDVC=1`); in
  speaker mode the hands-free profile.
* **Electrical crosstalk** EAR→MIC wiring would be heard by the far end as echo: keep
  the downlink and uplink wires apart, shared ground at one point.
* **Side-tone**: if you hear your own voice delayed in the earbuds, the modem is mixing
  the mic into EAR (side-tone). The A76XX command for this is not documented in the AT
  manual we have – lower Mic level and report it (UNVERIFIED).
* Hum/buzz: TDMA bursts (217 Hz on 2G) couple into long audio wires – short wires, a
  470 µF–1000 µF capacitor at the modem supply, antenna away from the audio wiring.

## 6. Development phases

| Phase | Goal | How to test |
|---|---|---|
| 1 | Calls on the wired handset | earpiece + mic on the modem; TEST-009/010 |
| 2 | Bluetooth voice link without a call | helper console `tone` / `echo`, or Settings → Sound → Test tone / Mic echo |
| 3 | Earbud calls | build the RC networks; call; watch the meters (TEST-013) |
| 4 | Better quality | optional codec (next section) |

## 7. Upgrade: I2S codec (phase 4, not implemented)

An ES8311 (mono ADC+DAC, I2C control) or ES8388 board would replace the ESP32 DAC/ADC:
EAR_P/N → codec line-in, codec out → MIC_P/N through a divider, I2S to the ESP32
helper (MCLK must be on GPIO0/1/3 on the ESP32). Benefits: 16-bit both ways, real
anti-alias filters, lower noise. Only `bt_esp32/components/audio/audio_bridge.cpp`
would change; the HFP code, the link protocol and the phone would not.
