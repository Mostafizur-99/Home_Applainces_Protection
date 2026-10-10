# Voltage Protector Device (ESP32)

A microcontroller-based protector that disconnects an appliance when the grid
voltage goes too high or too low, or when the load current is too high.
Settings can be changed on the device (3 buttons + LCD) or from a computer
(Serial / USB).

Built and tested in the **Wokwi** simulator. Grid voltage and load current are
emulated with two potentiometers (no mains voltage is used anywhere).



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
