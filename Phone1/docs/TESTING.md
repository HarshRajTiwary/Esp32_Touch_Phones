# TouchPhone – step-by-step hardware testing

Test in this order. Each stage only needs the parts from the stages before it, so if something
fails you know exactly where to look. Keep the USB cable connected and the serial console open
the whole time – it tells you what the phone is doing.

## Before you start

1. Install the tools once: `powershell -File tools\setup.ps1`
2. Find the COM port of the T-Call (Device Manager → Ports), for example `COM5`.
3. Flash and open the console in one go:

   ```
   powershell -File tools\build.ps1 -Upload -Port COM5 -Monitor
   ```

   If the upload does not start, hold the board's BOOT button (if it has one) while it says "Connecting...".

Type `help` in the console at any time to list the commands.

---

## Stage 1 – ESP32 boots (no SIM, no display needed)

**Do:** power the board over USB, open the console.

**You should see** (numbers will differ):

```
    412 [APP] TouchPhone 1.0.0 starting
    430 [FS] LittleFS mounted: 8/896 KB used
    455 [PWR] IP5306 power IC found, boost keep-on set
    620 [LCD] ILI9341 240x320 ready, PSRAM frame buffer
    640 [AUDIO] bridge ready: DAC GPIO25, ADC GPIO34
   1210 [BT] Bluetooth ready as "TouchPhone"
   1215 [CON] debug console ready - type 'help'
```

**If not:** `PSRAM frame buffer` missing → board has no PSRAM (UI still works, may flicker).
`no power IC on I2C` → fine on USB, but check the board is really a T-Call.

## Stage 2 – SIM800L and Airtel network (still no display)

**Do:** switch off, connect the **antenna** and the **battery**, insert the Airtel SIM, switch on.

**You should see within ~30 s:**

```
[MODEM] initializing
[MODEM] SIM READY
[MODEM] ready (IMEI 86xxxxxxxxxxxxx)
[MODEM] network registered
```

Then type `status`:

```
[CON] operator    : Airtel  (home)
[CON] signal      : CSQ 18 = -77 dBm, 3 bars
```

Raw AT commands also work, for example `at AT+CSQ` or `at AT+COPS?`.

**If not:**
* `no response on UART` → the modem is not powered: battery connected? antenna? Try `restart-modem`.
* `SIM NOT INSERTED` → re-seat the SIM (power off first).
* Stays "Searching" → no 2G coverage here, or weak signal. Move near a window. CSQ below 8 is too weak.
* Modem keeps restarting / `UNDER-VOLTAGE` → the battery is weak or missing (USB alone is not enough).

## Stage 3 – SMS

**Send:** `sms +91XXXXXXXXXX Hello from TouchPhone` → expect `[SMS] send to +91******XXXX (1 part): OK`.

**Receive:** send an SMS to the SIM from another phone → expect `[SMS] received from +91******XXXX: <n chars>`.

Numbers and texts are masked in the log on purpose (privacy). Build with `PHONE_LOG_PRIVATE=1` in
`app_config.h` if you want to see them.

## Stage 4 – calls without audio

**Do:** `dial +91XXXXXXXXXX` → the other phone rings and shows your number. Answer it there, then `hangup`.
Call the TouchPhone from another phone → the log shows `[CALL] incoming +91******XXXX`; type `answer`, then `hangup`.

You will not hear anything yet – that is Stage 8/9. This stage proves the call logic works.

## Stage 5 – display

**Do:** power off, wire the TFT exactly as in [HARDWARE.md](HARDWARE.md#4-display--touch-wiring), power on.

**You should see:** the TouchPhone logo and "Starting modem...", then the touch calibration screen (first boot only).

**If not:** white screen → check CS (IO15), DC (IO2), SCK (IO18), SDI (IO19) and that RESET goes to 3V3.
Wrong colours → see the tips in HARDWARE.md.

## Stage 6 – touch

**Do:** type `touch` in the console and press the screen with the stylus in each corner.

```
[TOUCH] raw x=312 y=3620 -> screen x=12 y=300
```

Raw values should change smoothly between about 200 and 3900. Then follow the calibration screen:
press the centre of each of the 5 targets. "Not accurate – let's try again" means a target was missed;
just repeat. You can re-run it later from **Settings → Touch calibration** (or type `cal`).

**If not:** no values at all → check T_CS (IO14), T_DO (IO39), T_DIN (IO19), T_CLK (IO18);
make sure the TFT's SDO pin is **not** connected.

## Stage 7 – the phone UI

Try everything with the stylus:

- [ ] Home → **Phone**: dial a number, green button calls, `*123#` + Send shows your balance
- [ ] Incoming call screen: Answer / Decline / Message (declines and sends an SMS)
- [ ] **Messages**: new message, reply, long-press a conversation to delete
- [ ] **Contacts**: add, edit, delete, search; **Settings → Import SIM contacts**
- [ ] **Recents**: call back button, missed calls shown in red
- [ ] Screen turns off after 30 s, tap to wake → lock screen → hold to unlock
- [ ] Blue LED blinks during an incoming call and blips for unread messages / missed calls

## Stage 8 – earbuds over Bluetooth (no call yet)

**Do:** Home → **Earbuds** → Search for earbuds, put the earbuds in pairing mode, tap them in the list.

**You should see:** `[BT] HFP connected to <name>` and a toast "<name> connected".

Then **Settings → Sound & call audio → Test tone**: you should hear a steady beep in the earbuds.
**Mic echo**: speak – you should hear yourself in the earbuds. Both prove the Bluetooth voice link works.

**If not:** pairing fails → forget the TouchPhone on the earbuds (or reset the earbuds) and try again;
some buds only pair when nothing else is connected to them.

## Stage 9 – the audio bridge (earbud calls)

**Do:** power off, build the small RC network from [HARDWARE.md → Mode B](HARDWARE.md#mode-b--bluetooth-earbuds-tws),
power on, connect the earbuds, and call someone.

**Tune:** open **Sound & call audio** during the call and watch the meters (see HARDWARE.md for the
exact tuning steps). Ask the other person whether your voice is loud and clear.

**If not:**
* You hear nothing → check C1/R1/R2/R3 and GPIO34; raise "Wired volume".
* They hear nothing → check C4/R4/R5/C6 and GPIO25, jumper J1 closed; raise "SIM800 mic".
* Buzz / hum → keep the audio wires short and away from the antenna.

## Stage 10 – soak test

Leave the phone on for a day with the earbuds connected: receive a few SMS, make and take calls,
let the screen sleep and wake it. **Settings → About** shows free memory, uptime and audio statistics;
the console command `heap` does too. Free internal RAM should stay roughly constant.

---

## Host-side tests (no hardware)

These run on the PC and were used during development:

```
powershell -File tools\test.ps1     # 127 unit tests: SMS PDU codec, AT parser, text/time helpers, DSP
powershell -File tools\uisim.ps1    # renders every screen to docs\screens\*.png
```
