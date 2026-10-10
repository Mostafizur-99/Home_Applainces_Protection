# Voltage Protector Device (ESP32)

A microcontroller-based protector that disconnects an appliance when the grid
voltage goes too high or too low, or when the load current is too high.
Settings can be changed on the device (3 buttons + LCD) or from a computer
(Serial / USB).

Built and tested in the **Wokwi** simulator. Grid voltage and load current are
emulated with two potentiometers (no mains voltage is used anywhere).

## Folder structure

```
src/voltage_protector/voltage_protector.ino   firmware
design_document.pdf                           one-page report
schematics/                                   block diagram, schematic and wiring diagram (PNG/PDF + editable .drawio/.svg)
test_evidence/                                screenshots and serial logs
project_files/wokwi/                          Wokwi project (diagram.json, libraries.txt)
project_files/user_guide.pdf                  step-by-step user guide (PDF)
readme.md                                     this file
```

## How to run (Wokwi, no hardware needed)

### Option A: one click

Open the ready-made simulation: **[Wokwi project link](https://wokwi.com/projects/REPLACE_WITH_YOUR_PROJECT_ID)**
and press the green **▶ Play** button.

### Option B: build it yourself (about 2 minutes)

1. Go to https://wokwi.com, click **New Project** and choose **ESP32**.
2. Open the **sketch.ino** tab, delete everything, and paste the code from
   `src/voltage_protector/voltage_protector.ino`.
3. Open the **diagram.json** tab, delete everything, and paste the content of
   `project_files/wokwi/diagram.json`. All parts and wires appear automatically.
4. Open the **Library Manager** tab, click **+** and add **LiquidCrystal I2C**
   (or create a file `libraries.txt` with the content of `project_files/wokwi/libraries.txt`).
5. Press the green **▶ Play** button. The Serial Monitor opens at the bottom.

### Quick test after pressing Play

1. Click **Pot 1** (grid voltage) and set it to about the middle (≈220 V).
   After 5 seconds the yellow appliance LED and green LED turn on (`LOAD=ON`).
2. Turn **Pot 1** above 250 V → appliance turns off, red LED + buzzer,
   `*** TRIP: OVER-VOLTAGE ***` in the Serial Monitor.
3. Turn it back to ≈220 V → the appliance reconnects after 5 seconds.
4. Turn **Pot 2** (load current) above 10 A → `OVER-CURRENT` trip.
   It stays off until you type `RESET` in the Serial Monitor (or press UP on the device).
5. Click in the Serial Monitor, type `HELP` and press Enter to see all commands.

### Troubleshooting

| Problem | Fix |
|---|---|
| Compile error mentioning `LiquidCrystal_I2C` | Add the **LiquidCrystal I2C** library (step 4) |
| No parts or wires on the right | `diagram.json` was not pasted completely; paste it again |
| Serial Monitor is empty | Stop and press **▶ Play** again |
| Button click not detected | Click and hold a little longer; the buttons are debounced |

## How it works

| Input / Output | Pin | Meaning |
|---|---|---|
| Pot 1 | GPIO34 | Grid voltage, 0–300 V |
| Pot 2 | GPIO35 | Load current, 0–20 A |
| Relay | GPIO26 | HIGH = appliance ON, LOW = appliance cut off |
| Red LED | GPIO25 | Fault |
| Green LED | GPIO32 | Appliance powered |
| Buzzer | GPIO33 | Beeps during a fault |
| MENU / UP / DOWN | GPIO14 / 13 / 4 | Buttons (to GND, internal pull-up) |
| LCD 16x2 I2C | SDA 21, SCL 22 | Readings and menu |

**Cut-off:** a separate high-priority FreeRTOS task reads both sensors every
1 ms. If a limit is crossed for 3 samples in a row, it switches the relay off
immediately (about 3 ms after the problem starts). The LCD and Serial code run
in a different task, so they can never delay a trip.

- **Over-voltage / under-voltage:** the appliance reconnects automatically once
  the voltage has been back in the safe range (with 5 V margin) for the
  reconnect delay.
- **Over-current:** latched. The appliance stays off until the user resets it,
  because a short circuit should not be retried automatically.
- At power-up the relay starts OFF and only turns on after the voltage has been
  safe for the reconnect delay.

Default settings: Over-voltage 250 V, Under-voltage 180 V, Max current 10 A,
Reconnect delay 5 s. Settings are saved in flash and survive a restart.

## Changing settings on the device

- **MENU**: go to the next screen: Status → Over-Volt → Under-Volt → Max Current → Reconnect Delay → Status (saves).
- **UP / DOWN** on a setting screen: change the value (5 V, 0.5 A or 1 s steps).
- **UP** on the status screen: reset an over-current fault.
- **DOWN** on the status screen: manual OFF / ON.
- The menu returns to the status screen (and saves) after 15 s without a button press.

## Changing settings from a computer

Open the Serial Monitor at **115200 baud** and type:

| Command | Example | Action |
|---|---|---|
| `HELP` | | List commands |
| `GET` | | Show settings |
| `STATUS` | | Show live readings |
| `SET OV <V>` | `SET OV 260` | Over-voltage limit (100–300) |
| `SET UV <V>` | `SET UV 170` | Under-voltage limit (50–280) |
| `SET OC <A>` | `SET OC 8` | Over-current limit (0.5–20) |
| `SET DELAY <s>` | `SET DELAY 10` | Reconnect delay (1–300) |
| `RESET` | | Clear an over-current fault |
| `OFF` / `ON` | | Manual disconnect / allow reconnect |
| `LOG ON` / `LOG OFF` | | 1-second status log |

Invalid values are rejected with an `ERROR` message (for example OV must stay at
least 20 V above UV).

## Test scenarios

1. Start: wait 5 s, appliance LED and green LED turn on.
2. Turn Pot 1 above 250 V: appliance off, red LED, buzzer, `TRIP: OVER-VOLTAGE` in the log.
3. Turn it back to ~220 V: after 5 s the appliance reconnects.
4. Turn Pot 1 below 180 V: `UNDER-VOLTAGE` trip.
5. Turn Pot 2 above 10 A: `OVER-CURRENT` trip; stays off even after lowering the current until `RESET` or UP button.
6. Type `SET OV 230`, then raise the voltage past 230 V: trips at the new limit.
7. Change a limit with the buttons: the Serial Monitor prints the new settings.

## Real product notes

In a real device, Pot 1 would be a ZMPT101B voltage sensor (RMS calculated over
each mains cycle), Pot 2 an ACS712 current sensor, and the relay would drive a
contactor rated for the appliance. That part was not built because of the
safety notice in the assignment.
