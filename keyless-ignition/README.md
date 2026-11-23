# Keyless ignition (ESP32-C3 SuperMini)

Tap an RFID card to unlock, then start the car with a push button.
The relays are wired **in parallel** with the factory ignition switch, so the original key still works as a backup.

## How it works

| Screen   | What you do                                         | Result                                   |
|----------|-----------------------------------------------------|------------------------------------------|
| LOCKED   | Tap a saved card                                    | READY for 60 s                           |
| READY    | Short press START (no pedal)                        | Ignition ON (fuel pump, ECU, gauges)     |
| READY    | Hold clutch/brake + hold START                      | Ignition ON → 1.5 s fuel prime → crank   |
| IGN ON   | Hold clutch/brake + hold START                      | Crank while held (max 4 s)               |
| RUNNING  | Hold START for 2 s                                  | Everything OFF, back to READY for 60 s   |
| READY    | Turn the knob (master card only)                    | Menu: add card, remove others             |

Safety behaviour built into the firmware:

- The starter only runs while you hold the button **and** the pedal. It stops after 4 s whatever happens, then needs 8 s to cool down.
- A separate hardware timer releases the starter even if the main program hangs.
- The firmware never switches the ignition off on its own. Only a 2-second hold does that.
- If the ESP32 crashes or the watchdog resets it while the engine is running, the ignition output is latched (`gpio_hold`) and restored after the reset, so the engine keeps running.
- After 5 unknown cards in a row, card reading is locked out for 30 s.
- The first card you tap after flashing becomes the **master** card. Only the master can open the menu.

## Wiring

### Changes from the original pin table

| GPIO | Before            | Now                                                                 |
|------|-------------------|---------------------------------------------------------------------|
| 0    | LCD RESET         | **IGN relay driver**. Wire LCD RESET to 3.3 V instead.              |
| 3    | RFID RST          | **START relay driver**. Wire RFID RST to 3.3 V instead.             |
| 2    | unused            | **Clutch/brake input** (optocoupler, LOW = pedal pressed)           |
| 8    | unused            | Optional **engine-running input** (optocoupler, LOW = running)      |
| 9    | encoder SW        | Unchanged. It's the START/STOP button: wire a dash push button in parallel (to GND). |

Everything else stays the same: SPI on 4/5/6, LCD CS 10, LCD DC 1, RFID SS 7, encoder 20/21.

### Power

- Use an automotive **12 V → 5 V buck converter** with a fuse and TVS diode on the input, wired to the SuperMini **5V** pin.
- Feed it from a constant-12 V fused circuit (5 A fuse). The board draws about 80 mA, so if the car sits for weeks, add a hidden cut-off switch.
- During cranking the battery can drop to about 8 V, so pick a buck converter that keeps 5 V down to 7 V input.

### Relay drivers (one each for IGN and START)

```
GPIO ──220Ω──┬── Gate   (logic-level N-MOSFET, e.g. IRLZ44N / AO3400)
            10k
             │
            GND ──────── Source
                         Drain ── relay coil (85) ──┐
                                                    1N4007 (across coil, stripe to +12 V)
                                  relay coil (86) ──┴── +12 V (constant)
```

- Use 40 A automotive relays (Bosch-style). A blue 5 V hobby relay board **cannot** carry ignition or starter current.
- Relay contacts, in parallel with the key switch:
  - **IGN relay** 30 → battery feed of the ignition switch (B / 30). 87 → the switch's **IG / ON** output.
  - **START relay** 30 → battery feed. 87 → the switch's **ST / START** output (the wire to the starter solenoid or starter-cut relay).
  - If the switch has a separate IG2 (e.g. ignition coils / fuel pump on a second circuit), add a second IGN relay with its coil wired in parallel with the first.
- Fuse each relay feed to match the factory circuit (check the factory fuse rating).
- The 10 kΩ pull-downs keep both relays off while the ESP32 boots or resets.

### Pedal and engine-running inputs (PC817 optocoupler)

```
12 V signal ──2.2kΩ── PC817 pin 1 (anode)      PC817 pin 4 (collector) ── GPIO 2 or 8
chassis GND ───────── PC817 pin 2 (cathode)    PC817 pin 3 (emitter)   ── ESP32 GND
```

- **Pedal (GPIO 2):** on a manual, use the clutch switch. On an automatic, use the brake-light switch output, which reads 12 V when the pedal is pressed. Add a 10 kΩ pull-up from GPIO 2 to 3.3 V. If you don't want a pedal interlock, set `REQUIRE_INTERLOCK = false`, but I strongly recommend keeping it.
- **Engine running (GPIO 8, optional):** use a 12 V signal that only appears when the engine runs, e.g. the alternator **L** terminal. Then set `ENGINE_SENSE_ENABLED = true`. With it, the firmware stops cranking as soon as the engine catches, refuses to crank a running engine, and lets you re-crank straight away after a stall.
- Without engine sense, the firmware assumes the engine started after any crank attempt. If it didn't start, hold START 2 s to switch off, then try again (no card tap needed for 60 s).

## Before you install it in the car

1. **Bench test first.** Connect LEDs in place of the relay coils and walk through every screen.
2. **Steering lock:** if your column has a mechanical steering lock, the key must still be turned to unlock it. Most people leave a spare key in ACC, or have the lock removed. Check whether that's legal where you live.
3. **Immobiliser:** if the car has a transponder immobiliser, the transponder chip has to stay near the ignition antenna ring, or the ECU won't let the engine run.
4. **Security:** MIFARE Classic card UIDs can be cloned. Treat this as convenience, not as theft protection. Use a hidden kill switch if theft matters to you.
5. Mount the board and relays away from heat and vibration, and use proper crimp terminals. Breadboards and dupont jumpers will shake loose in a car.

## Build and flash

```
pio run -t upload
pio device monitor
```

Arduino IDE: put `src/main.cpp` in a folder called `keyless_ignition` and rename it to `keyless_ignition.ino`. Install the libraries *Adafruit ST7735 and ST7789*, *Adafruit GFX* and *MFRC522*, select **ESP32C3 Dev Module**, and set **USB CDC On Boot: Enabled**.

Settings you might change are at the top of `src/main.cpp`: crank time, prime time, arm window, `TFT_TAB` (if the image is shifted or the colours are wrong), and `ENC_STEPS_PER_DETENT`.
