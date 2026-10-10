/*
 * Voltage Protector Device - ESP32 (Wokwi simulation)
 * ---------------------------------------------------
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ---------------- Pins ----------------
#define PIN_VSENSE     34
#define PIN_ISENSE     35
#define PIN_RELAY      26
#define PIN_LED_FAULT  25
#define PIN_LED_OK     32
#define PIN_BUZZER     33
#define PIN_BTN_MENU   14
#define PIN_BTN_UP     13
#define PIN_BTN_DOWN   4

// ---------------- Constants ----------------
const float   V_FULL_SCALE      = 300.0f;  // pot fully turned = 300 V
const float   I_FULL_SCALE      = 20.0f;   // pot fully turned = 20 A
const float   HYST_V            = 5.0f;    // reconnect hysteresis (V)
const uint8_t TRIP_CONFIRM      = 3;       // bad samples in a row before trip
const uint32_t LCD_PERIOD_MS    = 200;
const uint32_t MENU_TIMEOUT_MS  = 15000;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Preferences prefs;

// ---------------- Settings ----------------
struct Settings {
  float    overV        = 250.0f;  // cut off above this (V)
  float    underV       = 180.0f;  // cut off below this (V)
  float    maxI         = 10.0f;   // cut off above this (A)
  uint16_t reconnectSec = 5;       // wait before reconnecting (s)
};
Settings cfg;

// ---------------- Shared state ----------------
enum Fault : uint8_t { F_NONE, F_OV, F_UV, F_OC, F_STARTUP };
const char* FAULT_NAME[] = { "NONE", "OVER-VOLTAGE", "UNDER-VOLTAGE",
                             "OVER-CURRENT", "STARTUP" };

volatile float    gV = 0, gI = 0;
volatile bool     gRelayOn   = false;
volatile Fault    gFault     = F_STARTUP;
volatile bool     gManualOff = false;
volatile bool     reqReset   = false;
volatile uint32_t gTripCount = 0;

struct TripInfo { Fault f; float v; float i; uint32_t detectToCutUs; uint32_t atMs; };
volatile TripInfo lastTrip;
volatile bool tripEvent = false, reconnectEvent = false;

bool logEnabled = true;

// ================= Protection task (runs every 1 ms) =================

static inline void relayWrite(bool on) {
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  gRelayOn = on;
}

// Counts consecutive "bad" samples, remembers when the first one happened.
static inline bool confirm(bool bad, uint8_t &count, uint32_t &firstUs) {
  if (bad) {
    if (count == 0) firstUs = micros();
    if (count < 255) count++;
  } else {
    count = 0;
  }
  return count >= TRIP_CONFIRM;
}

void protectionTask(void *) {
  uint8_t  ovN = 0, uvN = 0, ocN = 0;
  uint32_t ovT = 0, uvT = 0, ocT = 0;
  bool     safeTiming = false;
  uint32_t safeSince  = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    float v = analogRead(PIN_VSENSE) * V_FULL_SCALE / 4095.0f;
    float i = analogRead(PIN_ISENSE) * I_FULL_SCALE / 4095.0f;
    gV = v; gI = i;

    if (reqReset) {                       // user cleared a latched fault
      reqReset = false;
      if (gFault == F_OC) { gFault = F_STARTUP; safeTiming = false; }
    }

    if (gManualOff && gRelayOn) relayWrite(false);

    if (gRelayOn) {
      // ---- Load connected: watch for faults ----
      bool oc = confirm(i > cfg.maxI,   ocN, ocT);
      bool ov = confirm(v > cfg.overV,  ovN, ovT);
      bool uv = confirm(v < cfg.underV, uvN, uvT);

      if (oc || ov || uv) {
        relayWrite(false);                // CUT OFF FIRST, bookkeeping after
        uint32_t now = micros();
        Fault f = oc ? F_OC : (ov ? F_OV : F_UV);
        uint32_t first = oc ? ocT : (ov ? ovT : uvT);
        gFault = f;
        lastTrip.f = f; lastTrip.v = v; lastTrip.i = i;
        lastTrip.detectToCutUs = now - first;
        lastTrip.atMs = millis();
        tripEvent = true;
        gTripCount++;
        ovN = uvN = ocN = 0;
        safeTiming = false;
      }
    } else if (!gManualOff && gFault != F_OC) {
      // ---- Load disconnected: reconnect when voltage is safe long enough ----
      bool vOk = (v < cfg.overV - HYST_V) && (v > cfg.underV + HYST_V);
      if (!vOk) {
        safeTiming = false;
      } else if (!safeTiming) {
        safeTiming = true;
        safeSince = millis();
      } else if (millis() - safeSince >= (uint32_t)cfg.reconnectSec * 1000UL) {
        gFault = F_NONE;
        relayWrite(true);
        reconnectEvent = true;
        safeTiming = false;
      }
    } else {
      safeTiming = false;
    }

    vTaskDelayUntil(&lastWake, 1);        // 1 ms period
  }
}

// ================= Settings (validate + save) =================

void saveSettings() {
  prefs.putFloat("ov", cfg.overV);
  prefs.putFloat("uv", cfg.underV);
  prefs.putFloat("oc", cfg.maxI);
  prefs.putUShort("dly", cfg.reconnectSec);
}

void loadSettings() {
  cfg.overV        = prefs.getFloat("ov", cfg.overV);
  cfg.underV       = prefs.getFloat("uv", cfg.underV);
  cfg.maxI         = prefs.getFloat("oc", cfg.maxI);
  cfg.reconnectSec = prefs.getUShort("dly", cfg.reconnectSec);
}

// Returns nullptr if OK, otherwise an error message. Does not save.
const char* applySetting(const char* key, float val) {
  if (!strcmp(key, "OV")) {
    if (val < 100 || val > 300)     return "OV must be 100..300 V";
    if (val < cfg.underV + 20)      return "OV must be at least 20 V above UV";
    cfg.overV = val;
  } else if (!strcmp(key, "UV")) {
    if (val < 50 || val > 280)      return "UV must be 50..280 V";
    if (val > cfg.overV - 20)       return "UV must be at least 20 V below OV";
    cfg.underV = val;
  } else if (!strcmp(key, "OC")) {
    if (val < 0.5f || val > 20)     return "OC must be 0.5..20 A";
    cfg.maxI = val;
  } else if (!strcmp(key, "DELAY")) {
    if (val < 1 || val > 300)       return "DELAY must be 1..300 s";
    cfg.reconnectSec = (uint16_t)val;
  } else {
    return "Unknown setting (use OV, UV, OC, DELAY)";
  }
  return nullptr;
}

// ================= Serial (computer) interface =================

void printSettings() {
  Serial.printf("SETTINGS  OV=%.1f V  UV=%.1f V  OC=%.2f A  DELAY=%u s\n",
                cfg.overV, cfg.underV, cfg.maxI, cfg.reconnectSec);
}

void printStatus() {
  const char* st = gManualOff ? "MANUAL-OFF" : (gRelayOn ? "NORMAL" : "DISCONNECTED");
  Serial.printf("[%lu ms] V=%.1f V  I=%.2f A  LOAD=%s  STATE=%s  FAULT=%s  TRIPS=%lu\n",
                millis(), gV, gI, gRelayOn ? "ON" : "OFF", st,
                FAULT_NAME[gFault], (unsigned long)gTripCount);
}

void printHelp() {
  Serial.println(F(
    "Commands:\n"
    "  HELP              this list\n"
    "  GET               show settings\n"
    "  STATUS            show live readings\n"
    "  SET OV <volts>    over-voltage cut-off  (e.g. SET OV 250)\n"
    "  SET UV <volts>    under-voltage cut-off (e.g. SET UV 180)\n"
    "  SET OC <amps>     over-current cut-off  (e.g. SET OC 10)\n"
    "  SET DELAY <sec>   reconnect delay       (e.g. SET DELAY 5)\n"
    "  RESET             clear latched over-current fault\n"
    "  OFF / ON          manually disconnect / allow reconnect\n"
    "  LOG ON / LOG OFF  1-second status log"));
}

void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line.length() == 0) return;

  if (line == "HELP")          printHelp();
  else if (line == "GET")      printSettings();
  else if (line == "STATUS")   printStatus();
  else if (line == "RESET")  { reqReset = true; Serial.println("OK fault reset requested"); }
  else if (line == "OFF")    { gManualOff = true;  Serial.println("OK load manually OFF"); }
  else if (line == "ON")     { gManualOff = false; Serial.println("OK manual OFF cleared (reconnects when voltage is safe)"); }
  else if (line == "LOG ON") { logEnabled = true;  Serial.println("OK log on"); }
  else if (line == "LOG OFF"){ logEnabled = false; Serial.println("OK log off"); }
  else if (line.startsWith("SET ")) {
    char key[8]; float val;
    if (sscanf(line.c_str() + 4, "%7s %f", key, &val) == 2) {
      const char* err = applySetting(key, val);
      if (err) Serial.printf("ERROR %s\n", err);
      else { saveSettings(); Serial.print("OK "); printSettings(); }
    } else {
      Serial.println("ERROR format: SET <OV|UV|OC|DELAY> <value>");
    }
  } else {
    Serial.println("ERROR unknown command, type HELP");
  }
}

void serialPoll() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(buf); buf = ""; }
    else if (buf.length() < 40) buf += c;
  }
}

// ================= Buttons + LCD menu (on-device settings) =================

struct Button {
  uint8_t pin; bool stable; bool lastRead; uint32_t changedAt;
  bool pressed() {                         // true once per press (debounced)
    bool r = digitalRead(pin);
    if (r != lastRead) { lastRead = r; changedAt = millis(); }
    if (millis() - changedAt > 30 && r != stable) {
      stable = r;
      return stable == LOW;
    }
    return false;
  }
};
Button bMenu{PIN_BTN_MENU, HIGH, HIGH, 0};
Button bUp  {PIN_BTN_UP,   HIGH, HIGH, 0};
Button bDown{PIN_BTN_DOWN, HIGH, HIGH, 0};

enum Screen { SCR_STATUS, SCR_OV, SCR_UV, SCR_OC, SCR_DELAY, SCR_COUNT };
Screen screen = SCR_STATUS;
bool settingsDirty = false;
uint32_t lastKeyMs = 0;

void adjust(int dir) {
  const char* err = nullptr;
  switch (screen) {
    case SCR_OV:    err = applySetting("OV",    cfg.overV  + dir * 5.0f); break;
    case SCR_UV:    err = applySetting("UV",    cfg.underV + dir * 5.0f); break;
    case SCR_OC:    err = applySetting("OC",    cfg.maxI   + dir * 0.5f); break;
    case SCR_DELAY: err = applySetting("DELAY", cfg.reconnectSec + dir);  break;
    default: return;
  }
  if (!err) settingsDirty = true;
}

void leaveMenu() {
  screen = SCR_STATUS;
  if (settingsDirty) {
    saveSettings();
    settingsDirty = false;
    Serial.print("Settings changed on device -> ");
    printSettings();
  }
}

void buttonsPoll() {
  if (bMenu.pressed()) {
    lastKeyMs = millis();
    Screen next = (Screen)((screen + 1) % SCR_COUNT);
    if (next == SCR_STATUS) leaveMenu(); else screen = next;
  }
  if (bUp.pressed()) {
    lastKeyMs = millis();
    if (screen == SCR_STATUS) {                 // UP on status = reset fault
      if (gFault == F_OC) { reqReset = true; Serial.println("Fault reset from device"); }
    } else adjust(+1);
  }
  if (bDown.pressed()) {
    lastKeyMs = millis();
    if (screen == SCR_STATUS) {                 // DOWN on status = manual on/off
      gManualOff = !gManualOff;
      Serial.printf("Manual %s from device\n", gManualOff ? "OFF" : "ON");
    } else adjust(-1);
  }
  if (screen != SCR_STATUS && millis() - lastKeyMs > MENU_TIMEOUT_MS) leaveMenu();
}

void lcdLine(uint8_t row, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void lcdUpdate() {
  char l1[24], l2[24];
  switch (screen) {
    case SCR_STATUS:
      snprintf(l1, sizeof(l1), "V:%5.1f I:%5.2f", gV, gI);
      if (gManualOff)               snprintf(l2, sizeof(l2), "MANUAL OFF");
      else if (gRelayOn)            snprintf(l2, sizeof(l2), "NORMAL  LOAD ON");
      else if (gFault == F_OC)      snprintf(l2, sizeof(l2), "OVER-CUR UP=RST");
      else if (gFault == F_OV)      snprintf(l2, sizeof(l2), "OVER-VOLT  OFF");
      else if (gFault == F_UV)      snprintf(l2, sizeof(l2), "UNDER-VOLT OFF");
      else                          snprintf(l2, sizeof(l2), "STARTING...");
      break;
    case SCR_OV:    snprintf(l1, sizeof(l1), "Set Over-Volt");  snprintf(l2, sizeof(l2), "> %.0f V", cfg.overV);  break;
    case SCR_UV:    snprintf(l1, sizeof(l1), "Set Under-Volt"); snprintf(l2, sizeof(l2), "> %.0f V", cfg.underV); break;
    case SCR_OC:    snprintf(l1, sizeof(l1), "Set Max Current");snprintf(l2, sizeof(l2), "> %.1f A", cfg.maxI);   break;
    case SCR_DELAY: snprintf(l1, sizeof(l1), "Reconnect Delay");snprintf(l2, sizeof(l2), "> %u s", cfg.reconnectSec); break;
    default: return;
  }
  lcdLine(0, l1);
  lcdLine(1, l2);
}

// ================= Indicators =================

void indicatorsUpdate() {
  bool faulted = (gFault == F_OV || gFault == F_UV || gFault == F_OC);
  digitalWrite(PIN_LED_OK, gRelayOn ? HIGH : LOW);
  digitalWrite(PIN_LED_FAULT, faulted ? HIGH : LOW);

  // Buzzer: beeps (200 ms on / 300 ms off) while a fault is active
  static bool buzzing = false;
  bool want = faulted && (millis() % 500) < 200;
  if (want && !buzzing)      { tone(PIN_BUZZER, 2000); buzzing = true; }
  else if (!want && buzzing) { noTone(PIN_BUZZER);     buzzing = false; }
}

// ================= Setup / loop =================

void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);              // load OFF at power-up
  pinMode(PIN_LED_FAULT, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  analogReadResolution(12);

  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(400000);
  lcd.init();
  lcd.backlight();
  lcdLine(0, "Voltage Protect");
  lcdLine(1, "Starting...");

  prefs.begin("vprot", false);
  loadSettings();

  Serial.println("\n=== Voltage Protector ready ===");
  printSettings();
  Serial.println("Type HELP for commands.");

  // Protection runs as its own high-priority task, independent of UI work.
  xTaskCreatePinnedToCore(protectionTask, "protect", 4096, nullptr,
                          configMAX_PRIORITIES - 1, nullptr, 0);
}

void loop() {
  serialPoll();
  buttonsPoll();
  indicatorsUpdate();

  if (tripEvent) {
    tripEvent = false;
    Serial.printf("[%lu ms] *** TRIP: %s  V=%.1f V  I=%.2f A  load cut %lu us after first bad sample ***\n",
                  (unsigned long)lastTrip.atMs, FAULT_NAME[lastTrip.f],
                  lastTrip.v, lastTrip.i, (unsigned long)lastTrip.detectToCutUs);
  }
  if (reconnectEvent) {
    reconnectEvent = false;
    Serial.printf("[%lu ms] Load reconnected (V=%.1f V)\n", millis(), gV);
  }

  static uint32_t lastLcd = 0, lastLog = 0;
  if (millis() - lastLcd >= LCD_PERIOD_MS) { lastLcd = millis(); lcdUpdate(); }
  if (logEnabled && millis() - lastLog >= 1000) { lastLog = millis(); printStatus(); }

  delay(5);
}
