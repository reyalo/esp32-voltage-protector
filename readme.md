# ESP32 Surge Protector — Wokwi Simulation

## 1. How to Run the Project

1. Extract the downloaded ZIP file.
2. Open [Wokwi](https://wokwi.com/) in your browser.
3. Create an ESP32 project. Open sketch.ino in Wokwi and replace its contents with the code from the extracted sketch.ino, diagram.json, library.txt file.
4. Click **Start Simulation**.
5. The LCD displays the configuration menu, and the Serial Monitor displays system readings and messages.

## 2. Testing the Features

### A. Simulate Voltage and Current

- Adjust the voltage potentiometer to simulate **0–300 V**.
- Adjust the current potentiometer to simulate **0–10 A**.
- These are simulated values, not real electrical measurements.

### B. Test Overvoltage Protection

1. Increase simulated voltage above the configured limit (default: **250 V**).
2. Keep it above the limit for at least **500 ms**.
3. Verify that the load switches OFF and the fault indicator turns ON.

### C. Test Overcurrent Protection

1. Increase simulated current above the configured limit (default: **5 A**).
2. Keep it above the limit for at least **500 ms**.
3. Verify that the load switches OFF.

### D. Test Local Configuration

Use the LCD and buttons:

- **UP:** Previous menu item in Normal Mode; increase value in Configuration Mode.
- **DOWN:** Next menu item in Normal Mode; decrease value in Configuration Mode.
- **SET / MENU:** Enter or exit Configuration Mode.

You can change the overvoltage limit (20–290 V), overcurrent limit (0.5–9.5 A), and recovery delay (1–60 seconds).

### E. Test Serial Commands

In the Serial Monitor, type a command and press Enter.

| Command | Function |
|---|---|
| `HELP` | Show available commands |
| `STATUS` | Show readings and system state |
| `GET` | Show protection settings |
| `SET_OV 260` | Set overvoltage limit to 260 V |
| `SET_OC 6.0` | Set overcurrent limit to 6 A |
| `SET_DELAY 10000` | Set recovery delay to 10 seconds |
| `RESET` | Clear lockout when readings are safe |

### F. Test Automatic Recovery and Lockout

1. After a fault, return both potentiometers to safe readings: voltage at or below the configured limit minus 10 V, and current at or below the configured limit minus 0.5 A.
2. Wait for the configured recovery delay and verify that the load reconnects.
3. Repeat fault and recovery cycles. After three successful automatic recovery attempts, trigger another fault to test lockout.
4. Return readings to safe levels and send `RESET`.