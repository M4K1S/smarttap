// ============================================================
// secrets_template.h — Configuration template
// ============================================================
// 1. Copy this file and rename it to secrets.h
// 2. Fill in your actual credentials below
// 3. secrets.h is listed in .gitignore and will NOT be committed
// ============================================================

#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// Firebase project API key (found in Project Settings > General)
#define API_KEY       "your-firebase-api-key"

// Firebase Realtime Database URL
#define DATABASE_URL  "https://your-project-id.firebaseio.com/"

// Local IP of the machine running esp32_dashboard_server.py
#define DASHBOARD_HOST "192.168.x.x"

// Local IP of the WLED controller
#define WLED_HOST      "192.168.x.x"
