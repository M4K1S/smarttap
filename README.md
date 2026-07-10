# SmartTap — NFC-Authenticated Beverage Dispensing System

> A full-stack embedded IoT system that uses an ESP32, NFC authentication, calibrated flow sensors, relay-controlled pumps/valves, Firebase Realtime Database, and a live dashboard to automate controlled beverage dispensing at events.

SmartTap was built as a real-world hardware/software deployment, not just a bench demo. A user taps an NFC wristband or card, the ESP32 checks their stored account data, the operator selects a tap, and the system dispenses a measured pour while updating the cloud backend and local dashboard.

---

## Project Snapshot

| Area | Details |
|---|---|
| Project type | Embedded IoT + hardware automation |
| Main MCU | ESP32 using the Arduino framework |
| Core inputs | PN532 NFC reader, push buttons, flow sensors |
| Core outputs | Pumps, solenoid valves, LCD, WLED status lighting, live dashboard |
| Backend | Firebase Realtime Database |
| Local UI | ESP32-hosted admin web interface |
| Desktop/live UI | Python TCP + Flask + PyWebView dashboard |
| Main skills shown | Embedded firmware, hardware control, cloud sync, networking, real-time flow tracking, client-style deployment |

---

## Demo Media

| Full System | Wiring |
|---|---|
| ![WLEDs](photos/WLEDs.jpeg) | ![Wiring](photos/wiring.jpeg) |

| NFC Scan | Flow Meter Wiring |
|---|---|
| ![NFC Scan](photos/nfc_scan1.jpeg) | ![Flow Meter](photos/flow_meter_wiring.jpeg) |

### Videos

- [NFC scan demo](photos/nfc_scan_vid.mov)
- [Pour progress demo](photos/pour_%25_vid.mov)

---

## System Overview

SmartTap connects physical beverage hardware to a cloud-backed user management system.

1. The user taps an NFC tag.
2. The ESP32 reads the tag UID through a PN532 NFC module.
3. Firebase is queried for the matching user profile and coupon values.
4. The LCD and Python dashboard update with the current user and tap labels.
5. The operator selects the tap and confirms the pour.
6. The ESP32 activates the correct relay-controlled pump/valve pair.
7. Flow sensor pulses are counted and converted into volume using calibration factors.
8. Once the target volume is reached, the pumps and valves shut off.
9. User usage and pour history are synchronized back to Firebase.

---

## What I Built

This was a solo engineering project. I handled the firmware, wiring, system design, backend structure, dashboard, and deployment support.

### Embedded Firmware

- Implemented ESP32 firmware in Arduino/C++.
- Integrated PN532 NFC authentication over I2C.
- Controlled two independent relay-driven pump/valve dispensing channels.
- Counted flow sensor pulses using interrupt service routines.
- Added per-tap calibration so raw pulses can be converted into approximate poured volume.
- Built LCD user prompts for NFC scan, tap selection, dispensing state, and completion state.
- Used ESP32 FreeRTOS tasks to separate NFC scanning from Firebase write operations.
- Added NTP time synchronization for timestamped pour records.
- Integrated WLED status lighting over HTTP.

### Cloud + Data Layer

- Designed Firebase Realtime Database fields for users, NFC UIDs, coupon balances, pour history, calibration values, tap names, and pour amounts.
- Implemented ESP32 read/write synchronization between local firmware state and Firebase.
- Added user management flows for adding, editing, deleting, and syncing user records.

### Web + Dashboard Layer

- Built an ESP32-hosted admin interface using `ESPAsyncWebSrv`.
- Built a Python TCP listener that receives live ESP32 state.
- Built a Flask/PyWebView desktop dashboard for user-facing status during events.

### Hardware Integration

- Wired and tested pumps, solenoid valves, flow meters, PN532 NFC module, LCD, buttons, relays, and WLED controller.
- Designed the pin map and event flow so the firmware can safely stop all pumps/valves through the cancel button.

---

## Engineering Highlights

- End-to-end embedded IoT system: hardware → firmware → cloud database → web/dashboard UI.
- Real-time physical control of pumps and solenoid valves from ESP32 GPIO.
- NFC-based identity system using real tag UIDs instead of manual user selection.
- Interrupt-driven pulse counting for flow measurement.
- Calibration workflow for converting sensor pulses into poured volume.
- ESP32-hosted web UI for local administration without needing a separate server.
- Python TCP bridge for a live external dashboard.
- Safety behavior: cancel input immediately stops pump/valve outputs and returns the LCD to idle.

---

## Architecture

```text
                 ┌──────────────────────────────┐
                 │          NFC User            │
                 │      Wristband / Card        │
                 └───────────────┬──────────────┘
                                 │
                                 ▼
┌──────────────────────────────────────────────────────────┐
│                          ESP32                           │
│                                                          │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐  │
│  │ PN532 NFC    │   │ Flow Sensors │   │ LCD + Buttons│  │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘  │
│         │                  │                  │          │
│         ▼                  ▼                  ▼          │
│  ┌────────────────────────────────────────────────────┐  │
│  │          Main Control Logic / State Flow           │  │
│  │     Auth → Select Tap → Dispense → Log/Sync        │  │
│  └──────────────┬─────────────────────┬───────────────┘  │
│                 │                     │                  │
│                 ▼                     ▼                  │
│       Relay-Controlled Pumps       WLED HTTP             │
│       + Solenoid Valves            Status Lighting        │
└─────────────────┬────────────────────────────────────────┘
                  │ Wi-Fi
                  ▼
        ┌───────────────────────┐
        │ Firebase Realtime DB   │
        │ Users / Coupons / Logs │
        └───────────────────────┘
                  │
                  │ TCP status stream
                  ▼
        ┌───────────────────────┐
        │ Python Live Dashboard  │
        │ Flask + PyWebView      │
        └───────────────────────┘
```

---

## System Flow

```text
IDLE
  ↓
NFC TAG DETECTED
  ↓
READ UID
  ↓
LOOK UP USER IN FIREBASE
  ↓
LOAD USER + COUPON DATA
  ↓
DISPLAY USER / TAP OPTIONS
  ↓
OPERATOR SELECTS TAP
  ↓
PUMP + VALVE ACTIVATE
  ↓
FLOW SENSOR PULSES COUNTED
  ↓
TARGET VOLUME REACHED
  ↓
PUMP + VALVE STOP
  ↓
POUR RECORD + USER USAGE SYNCED
  ↓
RETURN TO IDLE
```

The cancel button can interrupt the dispensing flow and turn off all pump/valve outputs.

---

## Hardware

| Component | Purpose |
|---|---|
| ESP32 dual-core module | Main controller |
| PN532 NFC reader | Reads user tag UIDs |
| NTAG215 NFC tags | User wristbands/cards |
| Flow sensor × 2 | Pulse-based volume measurement |
| Peristaltic pump × 2 | Liquid delivery |
| 12V solenoid valve × 2 | Flow gating |
| Relay module | Switches pump/valve loads from ESP32 GPIO |
| LCD 20×4 with I2C adapter | User-facing instructions/status |
| Push buttons × 2 | Resume/select and cancel inputs |
| Wemos D1 Mini + WLED | Status lighting controller |
| WS2811 LED strip | Event/state lighting |

---

## Pin Mapping

### Dispensing Outputs + Flow Inputs

| GPIO | Component | Notes |
|---|---|---|
| 13 | Pump 1 / Beer | Relay-controlled output |
| 23 | Valve 1 / Beer | Relay-controlled output |
| 27 | Flow Sensor 1 | Interrupt pulse input |
| 33 | Pump 2 / Wine | Relay-controlled output |
| 32 | Valve 2 / Wine | Relay-controlled output |
| 26 | Flow Sensor 2 | Interrupt pulse input |

### I2C Bus

| GPIO | Function | Devices |
|---|---|---|
| 21 | SDA | PN532 NFC + LCD |
| 22 | SCL | PN532 NFC + LCD |

### Buttons

| GPIO | Function |
|---|---|
| 4 | Resume / select |
| 18 | Cancel / stop |

### Other Defined Pins

| GPIO | Function |
|---|---|
| 5 | PN532 IRQ |
| 2 | PN532 reset |

---

## Repository Structure

```text
smarttap/
├── firmware/
│   ├── main.ino              # ESP32 firmware
│   └── secrets_template.h    # Safe config template; copy to secrets.h
├── dashboard/
│   ├── esp32_dashboard_server.py
│   └── requirements.txt
├── docs/
│   └── api.md
├── hardware/
│   └── wiring.md
├── photos/
├── LICENSE
├── .gitignore
└── README.md
```

---

## Setup

### 1. Firmware

Install Arduino IDE or PlatformIO with ESP32 board support.

Required Arduino libraries:

| Library | Purpose |
|---|---|
| `Adafruit_PN532` | NFC reader |
| `ESPAsyncWebSrv` | ESP32-hosted web dashboard |
| `Firebase_ESP_Client` | Firebase RTDB sync |
| `LiquidCrystal_I2C` | LCD display |
| `WiFi` / `HTTPClient` | Networking + WLED HTTP calls |

Copy the template credentials file:

```bash
cp firmware/secrets_template.h firmware/secrets.h
```

Fill in your local values:

```cpp
#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define API_KEY       "your-firebase-api-key"
#define DATABASE_URL  "https://your-project-id.firebaseio.com/"
#define DASHBOARD_HOST "192.168.x.x"
#define WLED_HOST      "192.168.x.x"
```

Then flash `firmware/main.ino` to the ESP32.

### 2. Python Dashboard

```bash
cd dashboard
pip install -r requirements.txt
python esp32_dashboard_server.py
```

The dashboard listens for ESP32 TCP messages on port `12345` and opens a local desktop window through PyWebView.

### 3. Firebase

Create a Firebase project, enable Realtime Database, and initialize the base counters:

```text
numOfNames: "0"
numOfPours: "0"
```

User records, pour records, calibration data, pump volumes, and tap names are created/updated by the ESP32 firmware.

---

## Interface / API

The ESP32 hosts its admin interface on:

```text
http://<ESP32-IP>/
```

Common actions:

| Area | Examples |
|---|---|
| User management | Add/edit/delete NFC users and coupon values |
| Pour history | View and delete saved pour records |
| Flow calibration | Calibrate each flow meter with a known volume |
| Tap configuration | Rename taps and update target pour volume |
| Hardware testing | Toggle pumps/valves manually |

Detailed endpoint notes are in [`docs/api.md`](docs/api.md).

---

## Security Notes

- `firmware/secrets.h` is intentionally ignored and should never be committed.
- Firebase API keys and Wi-Fi credentials should stay local to the deployment.
- The ESP32 admin interface is intended for trusted local networks.
- The Python dashboard TCP listener is intended for LAN use with the ESP32 only.
- For a public or production network, the next security step would be adding admin authentication and stronger Firebase rules.

---

## Future Improvements

- Add authentication to the ESP32 admin interface.
- Add stronger Firebase security rules and role separation.
- Move generated HTML/CSS/JS into SPIFFS/LittleFS files.
- Add local SD fallback logging for offline use.
- Add OTA firmware updates.
- Add a custom PCB or wiring harness for cleaner deployment.
- Add a formal hardware test checklist for pumps, valves, sensors, NFC, LCD, WLED, and dashboard connectivity.

---

## Skills Demonstrated

- Embedded C++ / Arduino on ESP32
- GPIO relay control
- Interrupt-driven pulse counting
- I2C devices
- NFC authentication
- Real-time user interaction with LCD/buttons
- Firebase Realtime Database integration
- ESP32 web server development
- TCP socket communication
- Python Flask dashboard development
- Hardware/software integration
- Debugging real physical systems
- Project documentation and deployment planning

---

## License

MIT License.

---

## Author

Built by Maxim Kisselev as a real-world embedded IoT beverage automation project.
