# 🍷🍺 SmartTap — Automated Beverage Dispensing System

> A full-stack IoT system built for real-world event deployment, combining NFC authentication, cloud-connected user management, real-time flow control, and a live web dashboard — all running on an ESP32.

---

## 📖 System Overview

SmartTap is an automated beverage dispenser designed for events. Users tap their NFC wristband or card to authenticate, choose their drink (wine or beer), and receive a precisely measured pour — all tracked in Firebase, logged to an SD card, and controllable from a web interface.

---

## ✨ Features

### 🔐 NFC Authentication
- PN532 module reads NFC UIDs and matches them to Firebase user profiles
- Each user has a name, phone number, account status, and per-drink coupon balance
- Unauthorized or depleted-coupon users are denied service

### 🍷🍺 Dual Dispensing Control
- **Tap 1 (Beer):** Solenoid valve + pump controlled via GPIO
- **Tap 2 (Wine):** Independent pump + valve on separate GPIO pins
- Each dispenser calibrated to a configurable pour volume (default: 250 mL)
- Real-time flow measurement via pulse-counting sensors on GPIO 26 & 27

### 📊 Firebase Cloud Backend
- User profiles: first/last name, UID, phone, coupon balances, usage history
- Pour logs: timestamp, drink type, volume, and user identity
- All data readable and editable from the admin dashboard

### 🌐 Embedded Async Web Server
- Runs directly on the ESP32 via `ESPAsyncWebServer`
- Admin interface for: user management, calibration, device renaming, WiFi config, valve/pump toggles, totalizer resets, and historical pour review

### 💡 WLED Visual Feedback
- HTTP calls to a WLED controller display color-coded system states:
  - Standby → Authorized → Dispensing → Complete

### 🖥️ Python Live Dashboard
- TCP server receives real-time data from the ESP32
- Flask web app displays live user identity, coupon count, and flow meter readings
- Optional PyWebView wrapper bundles it as a standalone desktop application

### 🛡️ Reliability & Logging
- ESP32 watchdog timer (WDT) for fault recovery
- SD card logging as local backup
- NTP time sync for accurate pour timestamps
- FreeRTOS multi-core task architecture (NFC on Core 1, write operations on Core 1)

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        ESP32                            │
│                                                         │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────┐    │
│  │ PN532    │   │ Flow     │   │ AsyncWebServer   │    │
│  │ NFC Read │   │ Sensors  │   │ Admin Dashboard  │    │
│  └────┬─────┘   └────┬─────┘   └────────┬─────────┘    │
│       │              │                  │               │
│  ┌────▼──────────────▼──────────────────▼─────────┐     │
│  │             Main Control Logic                  │     │
│  │  (Auth → Select → Dispense → Log → Sync)        │     │
│  └────┬──────────────┬──────────────┬─────────────┘     │
│       │              │              │                    │
│  ┌────▼──┐    ┌──────▼────┐  ┌─────▼──────┐            │
│  │ Pump  │    │ Firebase  │  │ SD Card    │            │
│  │ Valve │    │ RTDB Sync │  │ Local Log  │            │
│  │ GPIO  │    └──────┬────┘  └────────────┘            │
│  └───────┘           │                                  │
└─────────────────────┼──────────────────────────────────┘
                      │ WiFi
        ┌─────────────▼────────────┐
        │     Firebase RTDB        │
        │  Users / Pours / Config  │
        └──────────────────────────┘
                      │ TCP (port 12345)
        ┌─────────────▼────────────┐
        │   Python Dashboard       │
        │  Flask + TCP + PyWebView │
        └──────────────────────────┘
```

---

## 🔧 Hardware — Bill of Materials

| Component | Notes |
|---|---|
| ESP32 Dual Core Module | Main microcontroller |
| Peristaltic Pump 500 mL/min × 2 | Liquid delivery |
| DC 12V Solenoid Valve × 2 | Flow gate |
| Flow Sensor × 2 | Pulse-based volume measurement |
| 12V Relay Module (multi-channel) | Controls pumps & valves from 3.3V GPIO |
| Mini TF SD Card Module | Local data logging (with 32 GB card) |
| DFPlayer YX5200 | Audio feedback (with 32 GB card) |
| 30 mm Speaker 8Ω | Audio output for DFPlayer |
| LCD 20×4 with I2C Adapter | User-facing display |
| PN532 NFC Reader | NFC tag authentication |
| NFC NTAG215 Tags | User wristbands / cards |
| WS2811 LED Strip | Status lighting (controlled via WLED) |
| Wemos D1 Mini (WLED controller) | Drives the LED strip via WLED firmware |
| Push Button × 2 (2-pin) | Resume & Cancel actions |

---

## 📁 Repository Structure

```
(repo root)
├── firmware/
│   ├── nfc_tap.ino                  # ESP32 firmware (Arduino IDE)
│   └── secrets_template.h           # Copy to secrets.h and fill in your credentials
├── dashboard/
│   ├── esp32_dashboard_server.py    # Python TCP/Flask live dashboard
│   └── requirements.txt             # Python dependencies
├── photos/                          # System photos and screenshots
├── LICENSE
└── README.md
```

---

## 🚀 Getting Started

### Firmware (ESP32)

**Prerequisites:**
- Arduino IDE with ESP32 board support
- Libraries: `Adafruit_PN532`, `ESPAsyncWebSrv`, `Firebase_ESP_Client`, `LiquidCrystal_I2C`, `SD`

**Configuration:**
1. Copy `secrets_template.h` and rename it `secrets.h`
2. Fill in your credentials:
```cpp
#define WIFI_SSID      "your-wifi-name"
#define WIFI_PASSWORD  "your-wifi-password"
#define API_KEY        "your-firebase-api-key"
#define DATABASE_URL   "https://your-project-id.firebaseio.com/"
#define DASHBOARD_HOST "192.168.x.x"  // IP of the machine running the Python dashboard
#define WLED_HOST      "192.168.x.x"  // IP of your WLED controller
```
3. Place `secrets.h` in the same `firmware/` folder as `nfc_tap.ino`
4. Flash to your ESP32 via Arduino IDE

---

### Python Dashboard

**Prerequisites:**
```bash
pip install -r requirements.txt
```

**Run:**
```bash
python esp32_dashboard_server.py
```

The dashboard opens as a desktop window and listens on TCP port `12345` for the ESP32 connection. It auto-refreshes every second with the current user name, coupon count, and flow meter readings.

---

### Firebase Setup

1. Create a Firebase project and enable **Realtime Database**
2. Set up the following data structure:

```json
{
  "users": {
    "0": {
      "firstName": "Jane",
      "lastName": "Doe",
      "uid": "AA:BB:CC:DD",
      "phone": "5551234567",
      "accountStatus": "active",
      "beerCoupons": "5",
      "wineCoupons": "3",
      "beerUses": "0",
      "wineUses": "0"
    }
  },
  "pours": {}
}
```

3. Add your **API key** and **database URL** to `secrets.h`

---

## 🎛️ Admin Web Interface

Once the ESP32 is connected to WiFi, open `http://<ESP32-IP>` in a browser on the same network. Available actions:

- **Manage Users** — add, remove, edit users and coupon balances
- **Previous Pours** — full pour history with time, drink type, volume, and user
- **Calibrate Flow Meters** — run calibration to set pulses-per-mL
- **Toggle Valve / Pump** — manual override for testing
- **Rename Flow Meters** — custom labels for each tap
- **Change Pour Volume** — configurable per tap
- **WiFi Settings** — update credentials over-the-air
- **Reset Totalizers** — clear cumulative flow counts

---

## 📐 Flow Meter Calibration

1. Open the admin interface and select **Calibrate Flow Meter**
2. Enter the known volume you're about to dispense
3. Run the pump and stop when done
4. The system computes and stores pulses/mL automatically

---

## 🔌 Pin Mapping

### Dispensing

| GPIO | Component | Notes |
|---|---|---|
| 13 | Pump 1 (Beer) | Via relay module |
| 23 | Valve 1 (Beer) | DC 12V solenoid via relay |
| 27 | Flow Sensor 1 | Pulse input |
| 33 | Pump 2 (Wine) | Via relay module |
| 32 | Valve 2 (Wine) | DC 12V solenoid via relay |
| 26 | Flow Sensor 2 | Pulse input |

### SD Card (SPI)

| GPIO | Function |
|---|---|
| 15 | CS |
| 18 | SCK |
| 23 | MOSI |
| 19 | MISO |

> ⚠️ **GPIO 23 conflict:** GPIO 23 is shared between SD card MOSI and Valve 1. The firmware avoids concurrent use — SD writes occur outside of active pour sequences — but be mindful of this if extending the codebase.

### NFC — PN532 (I2C)

| GPIO | Function |
|---|---|
| 21 | SDA |
| 22 | SCL |

### LCD 20×4 (I2C, shared bus)

| GPIO | Function |
|---|---|
| 21 | SDA |
| 22 | SCL |

### DFPlayer YX5200 (UART)

| GPIO | Function |
|---|---|
| GPIO3 (RX0) | RX |
| GPIO1 (TX0) | TX |

### Buttons

| GPIO | Function |
|---|---|
| 2 | Cancel (ANNULER) |
| 4 | Resume (REPRENDRE) |

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (Arduino framework) |
| NFC | PN532 via I2C (Adafruit library) |
| Web Server | ESPAsyncWebServer |
| Cloud Backend | Firebase Realtime Database |
| Time Sync | NTP (`pool.ntp.org`) |
| RTOS | FreeRTOS (dual-core task pinning) |
| Fault Recovery | ESP32 Hardware Watchdog (WDT) |
| Dashboard Backend | Python + Flask |
| Dashboard Transport | TCP socket |
| Desktop Wrapper | PyWebView |
| LED Effects | WLED (HTTP API) |

---

## 🔒 Security Notes

- All credentials (WiFi, Firebase, device IPs) are stored in `secrets.h`, which is never committed to the repo. Copy `secrets_template.h` to get started.
- The admin web interface has no authentication by default — deploy only on trusted local networks.

---

## 📄 License

MIT License — free to use, modify, and deploy.

---

## 🙋 Author

Built as a real-world client deployment for automated event beverage management.
Demonstrates end-to-end integration of embedded systems, IoT networking, cloud backends, real-time control, and full-stack web development.
