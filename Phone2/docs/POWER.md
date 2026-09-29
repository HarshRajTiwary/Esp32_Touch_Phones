# Phone2 – power

> Status: **requirements and options only**. Per the project plan, the battery system is
> designed and built only after the phone works on the bench (Milestone 13). Nothing
> below has been built or measured. Do not connect a battery until you have done the
> checks in section 4.

## 1. What each part needs

| Part | Supply | Typical current | Peak | Source |
|---|---|---|---|---|
| A7672S module (VBAT) | **3.4–4.2 V**, 3.8 V typ. | 17 mA idle (LTE, DRX 0.32 s), ≈ 600 mA LTE max power | **2 A** GSM bursts (577 µs every 4.6 ms); 1 A LTE | SIMCom hardware design (CONFIRMED) |
| Carrier board "LASPLO076_RevB" | UNKNOWN (many take 5–12 V → 3.9 V buck) | + regulator losses | – | check M2 in [HARDWARE.md](HARDWARE.md) |
| ESP32-S3 board | 5 V (USB/5V pin) or 3.3 V (3V3 pin) | 80–120 mA (UI + hotspot idle) | ≈ 350 mA Wi-Fi TX | Espressif datasheet (typical) |
| ESP32 helper board | 5 V or 3.3 V | ≈ 100 mA with an SCO link | ≈ 250 mA | Espressif datasheet (typical) |
| TFT backlight | 3.3 V | 30–60 mA (brightness) | – | typical module |
| Buzzer, LEDs | 3.3 V | < 30 mA | – | – |

**Worst case together: ≈ 1 A average during a call with the hotspot on, ≈ 3 A peaks.**
The modem's supply must hold its voltage drop under 300 mV during a 2 A burst
(SIMCom). A weak supply shows up as modem restarts during calls
(`module printed its boot banner` in the log).

Never power the modem from an ESP32 3.3 V pin: 3.3 V is below its 3.4 V minimum and the
dev-board regulators cannot deliver 2 A.

## 2. Stage A – on the bench (use this first)

* ESP32-S3 and ESP32: each from its own USB cable (also the debug consoles).
* Modem: from a separate supply sized for the bursts: a 5 V / 3 A adapter into the
  carrier board's input **if** M2 shows it accepts 5 V, or a bench supply set to 3.8 V /
  3 A on the module's VBAT.
* 470–1000 µF low-ESR electrolytic + 100 nF ceramic right at the modem's supply pins.
* **All grounds connected together.** Short, thick modem supply wires (≥ 0.5 mm², < 10 cm).

## 3. Stage B – on a battery (options, UNVERIFIED)

Recommended architecture:

```
 USB-C ─► power-path Li-ion charger (e.g. BQ24074 / MCP73871 board) ─► SYS rail
                        │                                              │
               1S protected Li-ion/LiPo                                │
               (≥ 2000 mAh, ≥ 3 A peak)                                │
                                                                       ├─► polyfuse 3 A ─► modem VBAT (3.4–4.2 V)
                                                                       │        + 1000 µF + 100 nF + 33 pF at the module
                                                                       └─► buck-boost 3.3 V / ≥ 1.5 A (e.g. TPS63020 board)
                                                                                ─► ESP32-S3 3V3, ESP32 3V3, TFT
```

Why these choices:

* **Modem straight from the cell**: the A7672S input range (3.4–4.2 V) is exactly a
  Li-ion cell's range – no converter losses on the 2 A bursts. This needs the carrier
  board to expose VBAT (bypassing its own 5–12 V regulator) – check M2. If it only has a
  5–12 V input, feed it from a 5 V boost rated ≥ 3 A instead (less efficient).
* **Power-path charger**: the phone keeps running while charging; a plain TP4056 feeds
  the load and the cell from the same node and can mis-terminate charging. A TP4056
  *with protection* is acceptable for a first prototype if the phone is off while charging.
* **Buck-boost for 3.3 V**: a Li-ion cell drops from 4.2 V to about 3.3 V; the dev boards'
  AMS1117-type regulators need ≈ 4.4 V in for a clean 3.3 V out. Feeding 3.3 V into the
  boards' 3V3 pins bypasses them. (Do not also connect USB 5 V at the same time unless
  the board's schematic says that is safe.)
* **Battery gauge**: the modem measures its own VBAT (`AT+CBC`), which equals the cell
  voltage in this layout; the firmware turns that into % with a Li-ion curve
  (`phone/battery.cpp`, host-tested) and warns at 10 %. An extra ADC divider on the S3
  (`PIN_BATT_ADC`) is optional.
* Add a real power switch in the battery lead.

Expected battery life with 2000 mAh (rough, before any sleep modes): ≈ 2 h of earbud
calls with the hotspot on; ≈ 15 h standby with the screen off. Power-saving (light sleep,
modem DTR sleep) is future work.

## 4. Your two Li-Po batteries – check before use

1. Read the labels: voltage (3.7 V = single cell), capacity (mAh), C-rating if printed.
2. Look for the protection board (small PCB under the tape at the wires with a DW01 +
   8205A). **No protection board → do not use the cell in this project.**
3. Measure each voltage: 3.0–4.2 V is normal; below 2.8 V the cell may be damaged – do
   not charge it at high current.
4. **Use one battery.** Only connect two in parallel if they are the same model and
   capacity, both protected, and within 0.05 V of each other – otherwise one charges the
   other with a large uncontrolled current. **Never in series** (7.4 V would destroy the
   modem and the 3.3 V parts).
5. The cell should be at least 1000 mAh and able to deliver 3 A peaks (the 2 A modem
   bursts plus the rest); small drone/toy cells often are, tiny wearable cells are not.
6. Never leave a Li-Po charging unattended; do not puncture, bend or short it.
