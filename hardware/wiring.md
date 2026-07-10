# SmartTap — Wiring Reference

## ESP32 Pin Assignments

### Dispensing Outputs + Flow Inputs

| Function | ESP32 GPIO | Notes |
|---|---|---|
| Pump 1 / Beer | GPIO 13 | Relay-controlled output |
| Valve 1 / Beer | GPIO 23 | Relay-controlled output |
| Flow Sensor 1 | GPIO 27 | Interrupt pulse input |
| Pump 2 / Wine | GPIO 33 | Relay-controlled output |
| Valve 2 / Wine | GPIO 32 | Relay-controlled output |
| Flow Sensor 2 | GPIO 26 | Interrupt pulse input |

### I2C Bus

| Signal | ESP32 GPIO | Devices |
|---|---|---|
| SDA | GPIO 21 | PN532 NFC reader, LCD backpack |
| SCL | GPIO 22 | PN532 NFC reader, LCD backpack |

### Buttons

| Function | ESP32 GPIO | Notes |
|---|---|---|
| Resume / select | GPIO 4 | `INPUT_PULLUP`, active LOW |
| Cancel / stop | GPIO 18 | `INPUT_PULLUP`, active LOW |

### PN532 Control Pins

| Function | ESP32 GPIO |
|---|---|
| IRQ | GPIO 5 |
| Reset | GPIO 2 |

---

## Electrical Notes

- Relay modules should share ground with the ESP32 control side.
- Pump and solenoid power should be sized for the actual 12V load current.
- Flyback protection should be present on inductive loads if the relay board does not already include it.
- Flow sensor outputs must be 3.3V-compatible before connecting to ESP32 GPIO pins.
- Keep sensor wires and pump/valve power wiring separated where possible to reduce noise.
