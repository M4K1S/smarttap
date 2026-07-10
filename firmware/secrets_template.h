/**
 * SmartTap — Secrets Template
 * ===========================
 * Copy this file, rename it to secrets.h, and fill in your values.
 * secrets.h is listed in .gitignore and should never be committed.
 */

#ifndef SECRETS_H
#define SECRETS_H

// ─────────────────────────────────────────────
// Wi-Fi credentials
// ─────────────────────────────────────────────
#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// ─────────────────────────────────────────────
// Firebase Realtime Database
// API key: Firebase Project Settings > General
// Database URL: Firebase Realtime Database URL
// ─────────────────────────────────────────────
#define API_KEY       "your-firebase-api-key"
#define DATABASE_URL  "https://your-project-id.firebaseio.com/"

// ─────────────────────────────────────────────
// Local network services
// DASHBOARD_HOST: IP of the computer running dashboard server
// WLED_HOST: IP of the WLED controller
// ─────────────────────────────────────────────
#define DASHBOARD_HOST "192.168.x.x"
#define WLED_HOST      "192.168.x.x"

#endif // SECRETS_H
