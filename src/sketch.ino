#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <LiquidCrystal_I2C.h>

// Simulated sensor inputs
const int VOLTAGE_PIN = 34;
const int CURRENT_PIN = 35;

// Simulated load output
const int LOAD_SWITCH_PIN = 23;

// Status LEDs
const int LED_NORMAL = 18;
const int LED_FAULT = 19;
const int LED_RECOVERY = 5;

// Simulated sensor scaling
const int ADC_MAX = 4095;
const float MAX_SIM_VOLTAGE = 300.0f;
const float MAX_SIM_CURRENT = 10.0f;

// Configurable protection settings
float overVoltageLimit = 250.0f;
float overCurrentLimit = 5.0f;
unsigned long recoveryDelayMs = 5000;

const unsigned long FAULT_QUALIFY_MS = 500;
const unsigned long TELEMETRY_INTERVAL_MS = 1000;

const float VOLTAGE_HYSTERESIS = 10.0f;
const float CURRENT_HYSTERESIS = 0.5f;

const int MAX_RECOVERY_ATTEMPTS = 3;
int recoveryAttempts = 0;

// State machine
enum SystemState {
  STARTUP,
  NORMAL,
  FAULT_OVERVOLTAGE,
  FAULT_OVERCURRENT,
  RECOVERY_WAIT,
  LOCKOUT
};

SystemState state = STARTUP;
SystemState lastFault = FAULT_OVERVOLTAGE;

float voltage = 0.0f;
float current = 0.0f;

unsigned long voltageFaultStart = 0;
unsigned long currentFaultStart = 0;
unsigned long recoveryStart = 0;
unsigned long lastTelemetry = 0;

bool voltageTiming = false;
bool currentTiming = false;


char serialBuffer[96];
size_t serialLength = 0;

// --------------------------------------------------
// Local LCD menu and buttons (Milestone 5)
// --------------------------------------------------

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int BTN_UP = 25;
const int BTN_DOWN = 26;
const int BTN_SELECT = 27;

int selectedItem = 0;       // 0=voltage, 1=current, 2=recovery delay
bool editingSetting = false;

unsigned long lastUiRefresh = 0;
const unsigned long UI_REFRESH_INTERVAL_MS = 500;

struct Button {
  int pin;
  int stableState;
  int lastReading;
  unsigned long lastChange;
};

Button upButton = {BTN_UP, HIGH, HIGH, 0};
Button downButton = {BTN_DOWN, HIGH, HIGH, 0};
Button selectButton = {BTN_SELECT, HIGH, HIGH, 0};

// --------------------------------------------------
// State and output helpers
// --------------------------------------------------

const char* stateName(SystemState s) {
  switch (s) {
    case STARTUP: return "STARTUP";
    case NORMAL: return "NORMAL";
    case FAULT_OVERVOLTAGE: return "OVER_VOLTAGE";
    case FAULT_OVERCURRENT: return "OVER_CURRENT";
    case RECOVERY_WAIT: return "RECOVERY_WAIT";
    case LOCKOUT: return "LOCKOUT";
    default: return "UNKNOWN";
  }
}

void setLoad(bool enabled) {
  digitalWrite(LOAD_SWITCH_PIN, enabled ? HIGH : LOW);
}

bool readingsSafeForRecovery() {
  return voltage <= overVoltageLimit - VOLTAGE_HYSTERESIS
      && current <= overCurrentLimit - CURRENT_HYSTERESIS;
}

void updateIndicators() {
  digitalWrite(LED_NORMAL, state == NORMAL ? HIGH : LOW);

  bool faultActive =
      state == FAULT_OVERVOLTAGE ||
      state == FAULT_OVERCURRENT ||
      state == LOCKOUT;

  digitalWrite(LED_FAULT, faultActive ? HIGH : LOW);
  digitalWrite(
      LED_RECOVERY,
      state == RECOVERY_WAIT ? HIGH : LOW
  );
}

void readSensors() {
  int v = analogRead(VOLTAGE_PIN);
  int i = analogRead(CURRENT_PIN);

  voltage = (v / (float)ADC_MAX) * MAX_SIM_VOLTAGE;
  current = (i / (float)ADC_MAX) * MAX_SIM_CURRENT;
}

void enterFault(SystemState fault) {
  if (state != NORMAL) return;

  // Command the simulated load OFF first.
  setLoad(false);

  lastFault = fault;
  voltageTiming = false;
  currentTiming = false;

  if (recoveryAttempts >= MAX_RECOVERY_ATTEMPTS) {
    state = LOCKOUT;
    Serial.println("EVENT: LOCKOUT");
  } else {
    state = fault;
    Serial.print("EVENT: FAULT ");
    Serial.println(stateName(state));
  }

  updateIndicators();
  Serial.println("Load=OFF");
}

void updateProtection(unsigned long now) {
  if (state == STARTUP || state == LOCKOUT) {
    setLoad(false);
    return;
  }

  if (state == NORMAL) {
    // Over-voltage qualification
    if (voltage > overVoltageLimit) {
      if (!voltageTiming) {
        voltageTiming = true;
        voltageFaultStart = now;
      }

      if (now - voltageFaultStart >= FAULT_QUALIFY_MS) {
        enterFault(FAULT_OVERVOLTAGE);
        return;
      }
    } else {
      voltageTiming = false;
    }

    // Over-current qualification
    if (current > overCurrentLimit) {
      if (!currentTiming) {
        currentTiming = true;
        currentFaultStart = now;
      }

      if (now - currentFaultStart >= FAULT_QUALIFY_MS) {
        enterFault(FAULT_OVERCURRENT);
        return;
      }
    } else {
      currentTiming = false;
    }

    setLoad(true);
    return;
  }

  if (state == FAULT_OVERVOLTAGE ||
      state == FAULT_OVERCURRENT) {
    setLoad(false);

    if (readingsSafeForRecovery()) {
      state = RECOVERY_WAIT;
      recoveryStart = now;
      Serial.println("EVENT: RECOVERY_WAIT");
    }

    return;
  }

  if (state == RECOVERY_WAIT) {
    setLoad(false);

    if (!readingsSafeForRecovery()) {
      // Remember the new cause if another trip threshold
      // is crossed during recovery.
      if (voltage > overVoltageLimit) {
        lastFault = FAULT_OVERVOLTAGE;
      } else if (current > overCurrentLimit) {
        lastFault = FAULT_OVERCURRENT;
      }

      state = lastFault;
      Serial.println("EVENT: RECOVERY_INTERRUPTED");
      return;
    }

    if (now - recoveryStart >= recoveryDelayMs) {
      if (recoveryAttempts >= MAX_RECOVERY_ATTEMPTS) {
        state = LOCKOUT;
        setLoad(false);
        Serial.println("EVENT: LOCKOUT");
      } else {
        recoveryAttempts++;
        state = NORMAL;

        voltageTiming = false;
        currentTiming = false;

        setLoad(true);

        Serial.print("EVENT: RECONNECTED, attempt=");
        Serial.println(recoveryAttempts);
      }
    }
  }
}

// --------------------------------------------------
// Status and configuration reporting
// --------------------------------------------------

void printStatus() {
  Serial.print("STATUS voltage=");
  Serial.print(voltage, 1);
  Serial.print("V(sim), current=");
  Serial.print(current, 2);
  Serial.print("A(sim), state=");
  Serial.print(stateName(state));
  Serial.print(", load=");
  Serial.print(state == NORMAL ? "ON" : "OFF");
  Serial.print(", retries=");
  Serial.print(recoveryAttempts);
  Serial.print("/");
  Serial.println(MAX_RECOVERY_ATTEMPTS);
}

void printSettings() {
  Serial.println("--- CONFIGURATION ---");

  Serial.print("SET_OV=");
  Serial.println(overVoltageLimit, 1);

  Serial.print("SET_OC=");
  Serial.println(overCurrentLimit, 2);

  Serial.print("SET_DELAY=");
  Serial.println(recoveryDelayMs);

  Serial.print("MAX_RETRIES=");
  Serial.println(MAX_RECOVERY_ATTEMPTS);

  Serial.print("FAULT_QUALIFY_MS=");
  Serial.println(FAULT_QUALIFY_MS);

  Serial.print("VOLTAGE_RECOVERY_LIMIT=");
  Serial.println(
      overVoltageLimit - VOLTAGE_HYSTERESIS, 1
  );

  Serial.print("CURRENT_RECOVERY_LIMIT=");
  Serial.println(
      overCurrentLimit - CURRENT_HYSTERESIS, 2
  );
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("HELP");
  Serial.println("STATUS");
  Serial.println("GET");
  Serial.println("SET_OV <20..290>");
  Serial.println("SET_OC <0.5..9.5>");
  Serial.println("SET_DELAY <1000..60000>");
  Serial.println("RESET");
}

// --------------------------------------------------
// Command parsing and validation
// --------------------------------------------------

bool parseNumber(const char* text, float* result) {
  if (*text == '\0') return false;

  char* endPtr;
  float value = strtof(text, &endPtr);

  if (endPtr == text || *endPtr != '\0') {
    return false;
  }

  if (!isfinite(value)) {
    return false;
  }

  *result = value;
  return true;
}

void processCommand(char* line) {
  // Split the command name from its optional argument.
  char* argument = line;

  while (*argument != '\0' &&
         *argument != ' ' &&
         *argument != '\t') {
    argument++;
  }

  if (*argument != '\0') {
    *argument++ = '\0';

    while (*argument == ' ' || *argument == '\t') {
      argument++;
    }
  }

  // Uppercase command name only.
  for (char* p = line; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p -= ('a' - 'A');
    }
  }

  if (strcmp(line, "HELP") == 0) {
    if (*argument != '\0') {
      Serial.println("ERROR: HELP takes no argument.");
      return;
    }

    printHelp();
    return;
  }

  if (strcmp(line, "STATUS") == 0) {
    if (*argument != '\0') {
      Serial.println("ERROR: STATUS takes no argument.");
      return;
    }

    printStatus();
    return;
  }

  if (strcmp(line, "GET") == 0) {
    if (*argument != '\0') {
      Serial.println("ERROR: GET takes no argument.");
      return;
    }

    printSettings();
    return;
  }

  if (strcmp(line, "RESET") == 0) {
    if (*argument != '\0') {
      Serial.println("ERROR: RESET takes no argument.");
      return;
    }

    if (state != LOCKOUT) {
      Serial.println("ERROR: RESET requires LOCKOUT.");
      return;
    }

    if (!readingsSafeForRecovery()) {
      Serial.println("ERROR: readings are unsafe.");
      return;
    }

    recoveryAttempts = 0;
    state = RECOVERY_WAIT;
    recoveryStart = millis();
    setLoad(false);

    Serial.println("OK: lockout cleared; recovery waiting.");
    return;
  }

  // All SET commands require exactly one numeric argument.
  if (strcmp(line, "SET_OV") == 0 ||
      strcmp(line, "SET_OC") == 0 ||
      strcmp(line, "SET_DELAY") == 0) {

    if (*argument == '\0') {
      Serial.println("ERROR: missing value.");
      return;
    }

    // Reject extra arguments.
    for (char* p = argument; *p; p++) {
      if (*p == ' ' || *p == '\t') {
        Serial.println("ERROR: provide one value only.");
        return;
      }
    }

    float value;

    if (!parseNumber(argument, &value)) {
      Serial.println("ERROR: invalid numeric value.");
      return;
    }

    if (strcmp(line, "SET_OV") == 0) {
      if (value < 20.0f || value > 290.0f) {
        Serial.println("ERROR: SET_OV range is 20..290.");
        return;
      }

      overVoltageLimit = value;

      Serial.print("OK: SET_OV=");
      Serial.println(overVoltageLimit, 1);
      return;
    }

    if (strcmp(line, "SET_OC") == 0) {
      if (value < 0.5f || value > 9.5f) {
        Serial.println("ERROR: SET_OC range is 0.5..9.5.");
        return;
      }

      overCurrentLimit = value;

      Serial.print("OK: SET_OC=");
      Serial.println(overCurrentLimit, 2);
      return;
    }

    if (value < 1000.0f || value > 60000.0f ||
        floorf(value) != value) {
      Serial.println(
          "ERROR: SET_DELAY must be an integer from 1000 to 60000."
      );
      return;
    }

    recoveryDelayMs = (unsigned long)value;

    Serial.print("OK: SET_DELAY=");
    Serial.println(recoveryDelayMs);
    return;
  }

  Serial.println("ERROR: unknown command; type HELP.");
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      serialBuffer[serialLength] = '\0';

      if (serialLength > 0) {
        processCommand(serialBuffer);
      }

      serialLength = 0;
    } else if (serialLength < sizeof(serialBuffer) - 1) {
      serialBuffer[serialLength++] = c;
    } else {
      serialLength = 0;
      Serial.println("ERROR: command too long.");
    }
  }
}


// --------------------------------------------------
// Local menu implementation
// --------------------------------------------------

bool buttonPressed(Button &button) {
  int reading = digitalRead(button.pin);
  unsigned long now = millis();

  if (reading != button.lastReading) {
    button.lastChange = now;
    button.lastReading = reading;
  }

  if (now - button.lastChange >= 30) {
    if (reading != button.stableState) {
      button.stableState = reading;

      // Buttons use INPUT_PULLUP: LOW means pressed.
      if (button.stableState == LOW) {
        return true;
      }
    }
  }

  return false;
}

void writeLcdLine(uint8_t row, const char *text) {
  lcd.setCursor(0, row);

  uint8_t count = 0;
  while (text[count] != '\0' && count < 16) {
    lcd.print(text[count]);
    count++;
  }

  while (count < 16) {
    lcd.print(' ');
    count++;
  }
}

void displayMenu() {
  char line1[17];
  char line2[17];

  if (selectedItem == 0) {
    snprintf(line1, sizeof(line1), "%cOver Voltage%s",
             selectedItem == 0 ? '>' : ' ',
             editingSetting ? "*" : "");
    snprintf(line2, sizeof(line2), "Limit: %.0f V",
             overVoltageLimit);
  } else if (selectedItem == 1) {
    snprintf(line1, sizeof(line1), "%cOver Current%s",
             selectedItem == 1 ? '>' : ' ',
             editingSetting ? "*" : "");
    snprintf(line2, sizeof(line2), "Limit: %.1f A",
             overCurrentLimit);
  } else {
    snprintf(line1, sizeof(line1), "%cRecovery Delay%s",
             selectedItem == 2 ? '>' : ' ',
             editingSetting ? "*" : "");
    snprintf(line2, sizeof(line2), "Delay: %lus",
             recoveryDelayMs / 1000UL);
  }

  writeLcdLine(0, line1);
  writeLcdLine(1, line2);
}

void changeSetting(int direction) {
  if (selectedItem == 0) {
    overVoltageLimit += direction * 5.0f;

    if (overVoltageLimit < 20.0f) overVoltageLimit = 20.0f;
    if (overVoltageLimit > 290.0f) overVoltageLimit = 290.0f;
  } else if (selectedItem == 1) {
    overCurrentLimit += direction * 0.5f;

    if (overCurrentLimit < 0.5f) overCurrentLimit = 0.5f;
    if (overCurrentLimit > 9.5f) overCurrentLimit = 9.5f;
  } else {
    long delayValue = (long)recoveryDelayMs + direction * 1000L;

    if (delayValue < 1000L) delayValue = 1000L;
    if (delayValue > 60000L) delayValue = 60000L;

    recoveryDelayMs = (unsigned long)delayValue;
  }

  Serial.println("OK: setting changed locally.");
  printSettings();
}

void handleLocalUI() {
  bool up = buttonPressed(upButton);
  bool down = buttonPressed(downButton);
  bool select = buttonPressed(selectButton);

  bool changed = up || down || select;

  if (editingSetting) {
    if (up) changeSetting(1);
    if (down) changeSetting(-1);

    if (select) {
      editingSetting = false;
    }
  } else {
    if (up) {
      selectedItem = (selectedItem + 2) % 3;
    }

    if (down) {
      selectedItem = (selectedItem + 1) % 3;
    }

    if (select) {
      editingSetting = true;
    }
  }

  unsigned long now = millis();
  if (changed || now - lastUiRefresh >= UI_REFRESH_INTERVAL_MS) {
    displayMenu();
    lastUiRefresh = now;
  }
}

// --------------------------------------------------
// Arduino entry points
// --------------------------------------------------

void setup() {
  // Default simulated load output to OFF.
  digitalWrite(LOAD_SWITCH_PIN, LOW);
  pinMode(LOAD_SWITCH_PIN, OUTPUT);

  pinMode(LED_NORMAL, OUTPUT);
  pinMode(LED_FAULT, OUTPUT);
  pinMode(LED_RECOVERY, OUTPUT);

  digitalWrite(LED_NORMAL, LOW);
  digitalWrite(LED_FAULT, LOW);
  digitalWrite(LED_RECOVERY, LOW);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  displayMenu();

  Serial.begin(115200);
  analogReadResolution(12);

  readSensors();

  Serial.println();
  Serial.println("FSMB Voltage Protector - Milestone 5");
  Serial.println("Simulation-only sensor readings.");
  printHelp();
  printSettings();

  if (readingsSafeForRecovery()) {
    state = RECOVERY_WAIT;
    recoveryStart = millis();
    Serial.println("Startup: waiting for stable readings.");
  } else {
    state = FAULT_OVERVOLTAGE;
    lastFault = FAULT_OVERVOLTAGE;
    Serial.println("Startup: unsafe readings; load OFF.");
  }

  updateIndicators();
}

void loop() {
  unsigned long now = millis();

  readSensors();
  pollSerial();
  handleLocalUI();
  updateProtection(now);
  updateIndicators();

  if (now - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = now;
    printStatus();
  }
}