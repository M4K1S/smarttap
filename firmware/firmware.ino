#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <esp_task_wdt.h>
#include <HTTPClient.h>
#include "time.h"
#include <functional>
#include <memory>

#include "secrets.h"

// ============================================================================
// Hardware configuration
// ============================================================================
#define PN532_IRQ (5)
#define PN532_RESET (2)
#define I2C_ADDR (0x27)
#define LCD_COLS (20)
#define LCD_ROWS (4)
#define SENSOR_PIN 34
#define SENSOR_PIN2 35

int valve_pin = 23;
int pump_pin = 13;
int valve_pin2 = 14;
int pump_pin2 = 33;
const int buttonResume = 4;
const int buttonStop = 18;

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);
AsyncWebServer server(80);

// ============================================================================
// Network / Firebase / dashboard configuration
// ============================================================================
const char* host = DASHBOARD_HOST;
const uint16_t port = 12345;
const char *wled_ip = WLED_HOST;
WiFiClient client;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;
const int daylightOffset_sec = 3600;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// ============================================================================
// User / pour data storage
// ============================================================================
const int numOfUsers = 200;
int numOfNames = 0;
int numOfPours = 0;
int lastNameRead = 0;
int lastPourRead = 0;

String editingUser = "0";
String consommations = "0";

String first[numOfUsers];
String last[numOfUsers];
String uid[numOfUsers];
String phone[numOfUsers];
String beerUses[numOfUsers];
String wineUses[numOfUsers];
String status[numOfUsers];
String wineCoupons[numOfUsers];
String beerCoupons[numOfUsers];
String peopleString[numOfUsers];
int peopleNumber[numOfUsers];

int pourNumber[200];
String poursString[200];

// Form field scratch variables
String newFirstName, newLastName, newUID, newPhone, newAccountStatus, newBeerCoupons, newWineCoupons;
String userToEdit, userFirstName, userLastName, userUID, userPhone, accountStatus;
String beerCouponsEdit, wineCouponsEdit, beerUsesEdit, wineUsesEdit;
String userToDelete, pourToDelete;

// ============================================================================
// Firebase sync flags (set by handlers, consumed by writeTask)
// ============================================================================
int syncNames = 0;
int syncPours = 0;
bool syncCalibration = false;
bool syncAmount = false;
bool syncFlowName = false;
bool readyToRefresh = false;
bool readyToWrite = false;

// ============================================================================
// Flow meter / pump / valve state
// ============================================================================
volatile int pulseCount = 0;
volatile int pulseCount2 = 0;
volatile long pulse1 = 0;
volatile long pulse2 = 0;
volatile long* pulses[] = { &pulse1, &pulse2 };

int pumpVolume = 30;
int pumpVolume2 = 30;
bool pumpRunning = false;
int startPulse = 0;
int tap = 0;

int calibratingMeter = -1;
long calibrationPulses = 0;
long calibrationStart = 0;
float calibrationAmount = 0;
float mlpumped = 0;
float flowMeterCalibrations[2] = { 1, 1 };

String flowMeterNames[2] = { "Flow Meter 1", "Flow Meter 2" };
bool useOunces = false;
float mlOZ = 1.0;

bool valveOpen = true;
bool pumpOn = true;
bool valveOpen2 = true;
bool pumpOn2 = true;

// ============================================================================
// Pour flow / button state
// ============================================================================
int selection = 0;
unsigned long buttonPressTime = 0;
unsigned long holdTime = 750;
bool readyToPour = false;
bool okToRead = true;
bool continue1 = false;
int personToRead = -1;
int timePassed = 0;
int timePassed1 = 0;
int message1 = 0;
unsigned long oldTime = 0;
bool nfcWorking = false;

TaskHandle_t nfcTaskHandle = NULL;
TaskHandle_t writeTaskHandle = NULL;

// ============================================================================
// Helpers
// ============================================================================
bool compareUIDs(const uint8_t *uid1, uint8_t len1, const uint8_t *uid2, uint8_t len2) {
  if (len1 != len2) return false;
  for (uint8_t i = 0; i < len1; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

void setPeopleStrings() {
  for (int i = 1; i <= lastNameRead; i++) {
    peopleString[i] = first[i] + "_" + last[i] + "_" + uid[i] + "_" + phone[i] + "_" + beerUses[i] + "_" + wineUses[i] + "_" + status[i] + "_" + wineCoupons[i] + "_" + beerCoupons[i];
  }
}

void resetFlowMeter(int meterIndex) {
  if (meterIndex == 0) pulse1 = 0;
  else if (meterIndex == 1) pulse2 = 0;
}

void wled(int red, int green, int blue) {
  HTTPClient http;
  String url = String("http://") + wled_ip + "/json/state";
  String jsonData = "{\"on\":true, \"seg\":[{\"start\":0, \"stop\":810, \"col\":[[" + String(red) + "," + String(green) + "," + String(blue) + "],[0,0,0],[0,0,0]]}]}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonData);
  if (httpResponseCode <= 0) {
    Serial.println("WLED request failed");
  }
  http.end();
}

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void IRAM_ATTR pulseCounter2() {
  pulseCount2++;
}

// ============================================================================
// Shared page style used by all form/list pages
// ============================================================================
String sharedPageStyle() {
  return "\
<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
<style>\
  * { box-sizing: border-box; }\
  body {\
    text-align: center;\
    font-family: Arial, sans-serif;\
    margin: 0;\
    padding: 10px;\
    background-color: #f2f2f2;\
  }\
  h1 { font-size: 1.8em; margin: 5px 0; }\
  h2 { font-size: 1.3em; margin: 8px 0; }\
  h3 { font-size: 1em; margin: 5px 0; font-weight: normal; color: #555; }\
  .card {\
    background: white;\
    border-radius: 8px;\
    padding: 20px;\
    max-width: 480px;\
    margin: 0 auto 15px auto;\
    box-shadow: 0 1px 3px rgba(0,0,0,0.15);\
    text-align: left;\
  }\
  .top-actions {\
    display: flex; gap: 10px; justify-content: center;\
    flex-wrap: wrap; margin-bottom: 15px;\
  }\
  .top-actions button { width: auto; min-width: 160px; }\
  label { font-size: 0.9em; color: #444; display: block; margin-top: 12px; }\
  input[type=text], select {\
    width: 100%;\
    padding: 10px;\
    margin-top: 4px;\
    border: 1px solid #ccc;\
    border-radius: 5px;\
    font-size: 1em;\
  }\
  button, input[type=button] {\
    display: block;\
    width: 100%;\
    padding: 12px;\
    margin: 8px 0;\
    font-size: 1em;\
    background-color: #6c6c6c;\
    color: white;\
    border: none;\
    border-radius: 5px;\
    cursor: pointer;\
  }\
  button:hover, input[type=button]:hover { background-color: #4d4d4d; }\
  button.submit, input.submit { background-color: #2e7d32; }\
  button.submit:hover, input.submit:hover { background-color: #1b5e20; }\
  .pours-grid {\
    display: grid;\
    grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));\
    gap: 15px;\
    max-width: 1100px;\
    margin: 0 auto;\
    text-align: left;\
  }\
  .pour-card {\
    background: white;\
    border-radius: 8px;\
    padding: 14px;\
    box-shadow: 0 1px 3px rgba(0,0,0,0.15);\
  }\
  .pour-card .name { font-size: 1.1em; font-weight: bold; margin-bottom: 8px; }\
  .pour-card .detail { font-size: 0.85em; color: #444; margin: 3px 0; }\
  button.delete-button { background-color: #b71c1c; }\
  button.delete-button:hover { background-color: #7f1313; }\
  p.ip { font-size: 0.85em; color: #777; margin-top: 20px; }\
</style>";
}

// ============================================================================
// Main page
// ============================================================================
String flowMeterData() {
  String unit = useOunces ? "Ounces" : "Milliliters";
  return "Flow Meter Data:<br>" + flowMeterNames[0] + ": " + String(pulse1 * flowMeterCalibrations[0] * mlOZ) + " " + unit + "<br>" + flowMeterNames[1] + ": " + String(pulse2 * flowMeterCalibrations[1] * mlOZ) + " " + unit + "<br><br>";
}

String generateMainPage() {
  String valveStatus = valveOpen ? "Closed" : "Open";
  String pumpStatus = pumpOn ? "Off" : "On";
  String valveStatus2 = valveOpen2 ? "Closed" : "Open";
  String pumpStatus2 = pumpOn2 ? "Off" : "On";

  String page = "\
<html><head>\
<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
<style>\
  * { box-sizing: border-box; }\
  body { text-align: center; font-family: Arial, sans-serif; margin: 0; padding: 10px; background-color: #f2f2f2; }\
  h1 { font-size: 1.8em; margin: 5px 0; }\
  h2 { font-size: 1.3em; margin: 8px 0; }\
  h3 { font-size: 1em; margin: 5px 0; font-weight: normal; color: #555; }\
  h4 { font-size: 1em; margin: 5px 0; font-weight: normal; }\
  .top-actions { display: flex; gap: 10px; justify-content: center; flex-wrap: wrap; margin-bottom: 15px; }\
  .top-actions button { width: auto; min-width: 160px; }\
  .totals-box { background: white; border-radius: 8px; padding: 10px 15px; margin: 0 auto 15px auto; max-width: 700px; box-shadow: 0 1px 3px rgba(0,0,0,0.15); }\
  .meters-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; max-width: 900px; margin: 0 auto; }\
  @media (max-width: 700px) { .meters-grid { grid-template-columns: 1fr; } }\
  .meter-card { background: white; border-radius: 8px; padding: 12px; box-shadow: 0 1px 3px rgba(0,0,0,0.15); }\
  button { display: block; width: 100%; padding: 12px; margin: 8px 0; font-size: 1em; background-color: #6c6c6c; color: white; border: none; border-radius: 5px; cursor: pointer; }\
  button:hover { background-color: #4d4d4d; }\
  button.start { background-color: #2e7d32; }\
  button.start:hover { background-color: #1b5e20; }\
  p.ip { font-size: 0.85em; color: #777; margin-top: 15px; }\
</style>\
</head><body>\
<h1>Wine Machine</h1><h3>v0.51</h3>\
<div class=\"top-actions\">\
  <button onclick=\"submitUserInput(10)\">Manage Users</button>\
  <button onclick=\"submitUserInput(11)\">Previous Pours</button>\
</div>\
<div class=\"totals-box\"><h2>Current Totals</h2><h4>" + flowMeterData() + "</h4></div>\
<div class=\"meters-grid\">\
  <div class=\"meter-card\">\
    <h2>" + flowMeterNames[0] + "</h2>\
    <button onclick=\"submitUserInput(7)\">Change Pump Volume (Current: " + String(pumpVolume) + ")</button>\
    <button onclick=\"submitUserInput(1)\">Calibrate Flow Meter</button>\
    <button onclick=\"submitUserInput(9)\">Rename Flow Meter</button>\
    <button onclick=\"submitUserInput(5)\">Toggle Valve (Status: " + valveStatus + ")</button>\
    <button onclick=\"submitUserInput(6)\">Toggle Pump (Status: " + pumpStatus + ")</button>\
    <button class=\"start\" onclick=\"submitUserInput(8)\">Start Pump</button>\
  </div>\
  <div class=\"meter-card\">\
    <h2>" + flowMeterNames[1] + "</h2>\
    <button onclick=\"submitUserInput(4)\">Change Pump Volume (Current: " + String(pumpVolume2) + ")</button>\
    <button onclick=\"submitUserInput(13)\">Calibrate Flow Meter</button>\
    <button onclick=\"submitUserInput(14)\">Rename Flow Meter</button>\
    <button onclick=\"submitUserInput(15)\">Toggle Valve (Status: " + valveStatus2 + ")</button>\
    <button onclick=\"submitUserInput(16)\">Toggle Pump (Status: " + pumpStatus2 + ")</button>\
    <button class=\"start\" onclick=\"submitUserInput(17)\">Start Pump</button>\
  </div>\
</div>\
<p class=\"ip\">IP Address: " + WiFi.localIP().toString() + "</p>\
</body></html>\
<script>\
function submitUserInput(option) {\
  if (option == 1) {\
    window.location.href = '/calibrateMeter?meter=0';\
  } else if (option == 13) {\
    window.location.href = '/calibrateMeter?meter=1';\
  } else if (option == 5) {\
    window.location.href = '/toggleValve';\
  } else if (option == 15) {\
    window.location.href = '/toggleValve2';\
  } else if (option == 6) {\
    window.location.href = '/togglePump';\
  } else if (option == 16) {\
    window.location.href = '/togglePump2';\
  } else if (option == 8) {\
    window.location.href = '/startPump';\
  } else if (option == 17) {\
    window.location.href = '/startPump2';\
  } else if (option == 7) {\
    var v = prompt('Enter Pump Volume (ml):');\
    window.location.href = '/pumpVolume?_pumpVolume=' + encodeURIComponent(v);\
  } else if (option == 4) {\
    var v = prompt('Enter Pump Volume (ml):');\
    window.location.href = '/pumpVolume2?_pumpVolume2=' + encodeURIComponent(v);\
  } else if (option == 10) {\
    window.location.href = '/manageUsers';\
  } else if (option == 11) {\
    window.location.href = '/pours';\
  } else if (option == 14) {\
    var n = prompt('Enter New Name:');\
    window.location.href = '/rename?renameMeter=2&newName=' + encodeURIComponent(n);\
  } else if (option == 9) {\
    var n = prompt('Enter New Name:');\
    window.location.href = '/rename?renameMeter=1&newName=' + encodeURIComponent(n);\
  }\
}\
</script>";
  return page;
}

// ============================================================================
// Manage Users page (streamed to avoid large heap allocations)
// ============================================================================
String userPageHeaderHTML() {
  return "\
<html><head>" + sharedPageStyle() + "\
<style>\
  .users-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 15px; max-width: 1100px; margin: 0 auto; text-align: left; }\
  .user-card { background: white; border-radius: 8px; padding: 14px; box-shadow: 0 1px 3px rgba(0,0,0,0.15); }\
  .user-card .name { font-size: 1.1em; font-weight: bold; margin-bottom: 8px; }\
  .user-card .detail { font-size: 0.85em; color: #444; margin: 3px 0; }\
  .user-card .actions { display: flex; gap: 8px; margin-top: 10px; }\
  .user-card .actions button { margin: 0; flex: 1; font-size: 0.85em; padding: 8px; }\
  button.add { background-color: #2e7d32; max-width: 300px; margin: 20px auto 0 auto; }\
  button.add:hover { background-color: #1b5e20; }\
</style>\
</head><body>\
<h1>Manage Users</h1><h3>v0.51</h3>\
<div class=\"top-actions\"><button onclick=\"submitUserInput(5)\">Go Back To Main Page</button></div>\
<h2>All Users</h2>\
<div class=\"users-grid\">";
}

String userCardHTML(int i) {
  String card;
  card.reserve(500);
  String numUses = String(wineUses[i].toInt() + beerUses[i].toInt());
  card += "<div class=\"user-card\">";
  card += "<div class=\"name\">" + first[i] + " " + last[i] + "</div>";
  card += "<div class=\"detail\">NFC UID: " + uid[i] + "</div>";
  card += "<div class=\"detail\">Phone: " + phone[i] + "</div>";
  card += "<div class=\"detail\">Uses: " + numUses + " (Beer: " + beerUses[i] + ", Wine: " + wineUses[i] + ")</div>";
  card += "<div class=\"detail\">Status: " + status[i] + "</div>";
  card += "<div class=\"detail\">Beer Coupons: " + beerCoupons[i] + "</div>";
  card += "<div class=\"detail\">Wine Coupons: " + wineCoupons[i] + "</div>";
  card += "<div class=\"actions\">";
  card += "<button onclick=\"submitUserInput(2, " + String(i) + ")\">Edit</button>";
  card += "<button class=\"delete-button\" onclick=\"submitUserInput(7, " + String(i) + ")\">Delete</button>";
  card += "</div></div>";
  return card;
}

String userPageFooterHTML() {
  String footer;
  footer.reserve(1200);
  footer += "\
</div>\
<button class=\"add\" onclick=\"submitUserInput(1)\">Add User</button>\
<p class=\"ip\">IP Address: " + WiFi.localIP().toString() + "</p>\
</body></html>\
<script>\
function submitUserInput(option, userIndex) {\
  if (option == 1) {\
    window.location.href = '/newUserPage';\
  } else if (option == 2) {\
    window.location.href = '/editUserPage?userToEdit=' + userIndex;\
  } else if (option == 5) {\
    window.location.href = '/';\
  } else if (option == 7) {\
    window.location.href = '/deleteUser?userToDelete=' + userIndex;\
  }\
}\
</script>";
  return footer;
}

// State for one in-progress chunked Manage Users response. Kept alive via
// shared_ptr across the multiple callback invocations the server makes
// while streaming the page to a client.
struct UserPageStreamState {
  bool headerSent = false;
  bool footerSent = false;
  bool done = false;
  int i = 1;
  String buffer = "";
  size_t bufferPos = 0;
};

void handleUserPage(AsyncWebServerRequest *request) {
  auto state = std::make_shared<UserPageStreamState>();

  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "text/html",
    [state](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t written = 0;

      while (written < maxLen) {
        if (state->bufferPos >= state->buffer.length()) {
          state->buffer = "";
          state->bufferPos = 0;

          if (!state->headerSent) {
            state->buffer = userPageHeaderHTML();
            state->headerSent = true;
          } else if (!state->done) {
            bool found = false;
            while (state->i <= numOfUsers - 1) {
              int idx = state->i;
              state->i++;
              if (idx <= numOfNames + 50 && first[idx] != "deleted" && !first[idx].isEmpty()) {
                state->buffer = userCardHTML(idx);
                found = true;
                break;
              }
              if (idx > numOfNames + 50) break;
            }
            if (!found) state->done = true;
          } else if (!state->footerSent) {
            state->buffer = userPageFooterHTML();
            state->footerSent = true;
          } else {
            break;
          }
        }

        size_t remaining = state->buffer.length() - state->bufferPos;
        if (remaining == 0) continue;
        size_t toCopy = (remaining < (maxLen - written)) ? remaining : (maxLen - written);
        memcpy(buffer + written, state->buffer.c_str() + state->bufferPos, toCopy);
        state->bufferPos += toCopy;
        written += toCopy;
      }

      return written;
    });

  request->send(response);
}

// ============================================================================
// Previous Pours page
// ============================================================================
String generatePoursPage() {
  String page = "\
<html><head>" + sharedPageStyle() + "</head><body>\
<h1>Previous Pours</h1>\
<div class=\"top-actions\"><button onclick=\"submitUserInput(5)\">Go Back To Main Page</button></div>\
<div class=\"pours-grid\">";

  int j = 0;
  for (int i = 1; i <= numOfPours + j; i++) {
    String pourEntry = poursString[i];
    if (pourEntry == "DeletePours" || pourEntry.isEmpty()) {
      j++;
      continue;
    }

    int firstUnderscore = pourEntry.indexOf('_');
    int secondUnderscore = pourEntry.indexOf('_', firstUnderscore + 1);
    int thirdUnderscore = pourEntry.indexOf('_', secondUnderscore + 1);
    int fourthUnderscore = pourEntry.indexOf('_', thirdUnderscore + 1);

    String time = pourEntry.substring(0, firstUnderscore);
    String date = pourEntry.substring(firstUnderscore + 1, secondUnderscore);
    String typeOfDrink = pourEntry.substring(secondUnderscore + 1, thirdUnderscore);
    String amountPoured = pourEntry.substring(thirdUnderscore + 1, fourthUnderscore);
    String pourFirstName = pourEntry.substring(fourthUnderscore + 1);
    int lastNameIndex = pourFirstName.lastIndexOf('_');
    String pourLastName = pourFirstName.substring(lastNameIndex + 1);
    pourFirstName = pourFirstName.substring(0, lastNameIndex);

    page += "<div class=\"pour-card\">";
    page += "<div class=\"name\">" + pourFirstName + " " + pourLastName + "</div>";
    page += "<div class=\"detail\">Time: " + time + "</div>";
    page += "<div class=\"detail\">Date: " + date + "</div>";
    page += "<div class=\"detail\">Drink: " + typeOfDrink + "</div>";
    page += "<div class=\"detail\">Amount: " + amountPoured + " ml</div>";
    page += "<button class=\"delete-button\" onclick=\"submitUserInput(7, " + String(i) + ")\">Delete Pour</button>";
    page += "</div>";
  }

  page += "\
</div>\
<p class=\"ip\">IP Address: " + WiFi.localIP().toString() + "</p>\
</body></html>\
<script>\
function submitUserInput(option, pourIndex) {\
  if (option == 5) {\
    window.location.href = '/';\
  } else if (option == 7) {\
    window.location.href = '/deletePour?pourToDelete=' + pourIndex;\
  }\
}\
</script>";
  return page;
}

// ============================================================================
// New User page
// ============================================================================
String generateNewUserPage() {
  String page = "\
<html><head>" + sharedPageStyle() + "</head><body>\
<h1>New User</h1>\
<div class=\"top-actions\"><button onclick=\"goBack()\">Go Back</button></div>\
<div class=\"card\">\
<form id=\"newUserForm\">\
  <label for=\"firstName\">First Name</label>\
  <input type=\"text\" id=\"firstName\" name=\"userFirstName\">\
  <label for=\"lastName\">Last Name</label>\
  <input type=\"text\" id=\"lastName\" name=\"userLastName\">\
  <label for=\"nfcUid\">NFC UID</label>\
  <input type=\"text\" id=\"nfcUid\" name=\"userUID\">\
  <label for=\"phoneNum\">Phone Number</label>\
  <input type=\"text\" id=\"phoneNum\" name=\"userPhone\">\
  <label for=\"beerCoupon\">Beer Coupons</label>\
  <select id=\"beerCoupon\" name=\"beerCoupons\">";

  for (int i = 0; i <= 100; i++) {
    page += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
  }
  page += "<option value=\"1000\">Unlimited</option></select>";

  page += "\
  <label for=\"wineCoupon\">Wine Coupons</label>\
  <select id=\"wineCoupon\" name=\"wineCoupons\">";

  for (int i = 0; i <= 100; i++) {
    page += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
  }
  page += "<option value=\"1000\">Unlimited</option></select>";

  page += "\
  <label for=\"accStatus\">Account Status</label>\
  <select id=\"accStatus\" name=\"accountStatus\">\
    <option value=\"Active\">Active</option>\
    <option value=\"Inactive\">Inactive</option>\
  </select>\
  <button class=\"submit\" type=\"button\" onclick=\"submitForm()\">Submit</button>\
</form>\
</div>\
</body></html>\
<script>\
function submitForm() {\
    var userFirstName = document.getElementById('firstName').value;\
    var userLastName = document.getElementById('lastName').value;\
    var userUID = document.getElementById('nfcUid').value;\
    var userPhone = document.getElementById('phoneNum').value;\
    var accountStatus = document.getElementById('accStatus').value;\
    var beerCoupons = document.getElementById('beerCoupon').value;\
    var wineCoupons = document.getElementById('wineCoupon').value;\
    window.location.href = '/newUser?userFirstName=' + encodeURIComponent(userFirstName) + '&userLastName=' + encodeURIComponent(userLastName) + '&userUID=' + encodeURIComponent(userUID) + '&userPhone=' + encodeURIComponent(userPhone) + '&accountStatus=' + encodeURIComponent(accountStatus) + '&beerCoupons=' + encodeURIComponent(beerCoupons) + '&wineCoupons=' + encodeURIComponent(wineCoupons);\
}\
function goBack() {\
    window.location.href = '/manageUsers';\
}\
document.getElementById('firstName').focus();\
</script>";
  return page;
}

// ============================================================================
// Edit User page
// ============================================================================
String generateEditUserPage() {
  String editIdx = editingUser;
  int idx = editIdx.toInt();

  String page = "\
<html><head>" + sharedPageStyle() + "</head><body>\
<h1>Edit User #" + editIdx + "</h1>\
<div class=\"top-actions\"><button onclick=\"goBack()\">Go Back</button></div>\
<div class=\"card\">\
<form id=\"editUserForm\">\
  <label for=\"firstName\">First Name</label>\
  <input type=\"text\" id=\"firstName\" name=\"userFirstName\" value=\"" + first[idx] + "\">\
  <label for=\"lastName\">Last Name</label>\
  <input type=\"text\" id=\"lastName\" name=\"userLastName\" value=\"" + last[idx] + "\">\
  <label for=\"nfcUid\">NFC UID</label>\
  <input type=\"text\" id=\"nfcUid\" name=\"userUID\" value=\"" + uid[idx] + "\">\
  <label for=\"phoneNum\">Phone Number</label>\
  <input type=\"text\" id=\"phoneNum\" name=\"userPhone\" value=\"" + phone[idx] + "\">\
  <label for=\"beerCoupon\">Beer Coupons</label>\
  <select id=\"beerCoupon\" name=\"beerCoupons\">";

  for (int i = 0; i <= 100; i++) {
    String sel = (i == beerCoupons[idx].toInt()) ? " selected" : "";
    page += "<option value=\"" + String(i) + "\"" + sel + ">" + String(i) + "</option>";
  }
  String selUnlimBeer = (beerCoupons[idx].toInt() == 1000) ? " selected" : "";
  page += "<option value=\"1000\"" + selUnlimBeer + ">Unlimited</option></select>";

  page += "<label for=\"wineCoupon\">Wine Coupons</label><select id=\"wineCoupon\" name=\"wineCoupons\">";
  for (int i = 0; i <= 100; i++) {
    String sel = (i == wineCoupons[idx].toInt()) ? " selected" : "";
    page += "<option value=\"" + String(i) + "\"" + sel + ">" + String(i) + "</option>";
  }
  String selUnlimWine = (wineCoupons[idx].toInt() == 1000) ? " selected" : "";
  page += "<option value=\"1000\"" + selUnlimWine + ">Unlimited</option></select>";

  bool active = (status[idx] == "Active");
  page += "\
  <label for=\"accStatus\">Account Status</label>\
  <select id=\"accStatus\" name=\"accountStatus\">\
    <option value=\"Active\"" + String(active ? " selected" : "") + ">Active</option>\
    <option value=\"Inactive\"" + String(!active ? " selected" : "") + ">Inactive</option>\
  </select>\
  <label for=\"beerUses\">Beer Uses</label>\
  <input type=\"text\" id=\"beerUses\" name=\"beerUses\" value=\"" + beerUses[idx] + "\">\
  <label for=\"wineUses\">Wine Uses</label>\
  <input type=\"text\" id=\"wineUses\" name=\"wineUses\" value=\"" + wineUses[idx] + "\">\
  <button class=\"submit\" type=\"button\" onclick=\"submitForm()\">Submit</button>\
</form>\
</div>\
</body></html>\
<script>\
function submitForm() {\
    var userFirstName = document.getElementById('firstName').value;\
    var userLastName = document.getElementById('lastName').value;\
    var userUID = document.getElementById('nfcUid').value;\
    var userPhone = document.getElementById('phoneNum').value;\
    var accountStatus = document.getElementById('accStatus').value;\
    var beerCoupons = document.getElementById('beerCoupon').value;\
    var wineCoupons = document.getElementById('wineCoupon').value;\
    var beerUses = document.getElementById('beerUses').value;\
    var wineUses = document.getElementById('wineUses').value;\
    window.location.href = '/editUser?userToEdit=" + editIdx + "&userFirstName=' + encodeURIComponent(userFirstName) + '&userLastName=' + encodeURIComponent(userLastName) + '&userUID=' + encodeURIComponent(userUID) + '&userPhone=' + encodeURIComponent(userPhone) + '&accountStatus=' + encodeURIComponent(accountStatus) + '&beerCoupons=' + encodeURIComponent(beerCoupons) + '&wineCoupons=' + encodeURIComponent(wineCoupons) + '&beerUses=' + encodeURIComponent(beerUses) + '&wineUses=' + encodeURIComponent(wineUses);\
}\
function goBack() {\
    window.location.href = '/manageUsers';\
}\
document.getElementById('firstName').focus();\
</script>";
  return page;
}

// ============================================================================
// Calibrate Flow Meter page
// ============================================================================
String generateCalibratePage(int meterToCalibrate) {
  String page = "\
<html><head>" + sharedPageStyle() + "</head><body>\
<h1>Calibrate Flow Meter " + String(meterToCalibrate + 1) + "</h1>\
<div class=\"card\">\
<h3>Select START, pump a metered quantity from the chosen tap, then select STOP and enter the quantity dispensed.</h3>\
<button id=\"startButton\" onclick=\"startCalibration(" + String(meterToCalibrate) + ")\">Start Calibration</button>\
<button id=\"stopButton\" class=\"delete-button\" onclick=\"stopCalibration(" + String(meterToCalibrate) + ")\" disabled>Stop Calibration</button>\
</div>\
</body></html>\
<script>\
var startButton = document.getElementById('startButton');\
var stopButton = document.getElementById('stopButton');\
\
function startCalibration(meterToCalibrate) {\
  startButton.disabled = true;\
  stopButton.disabled = false;\
  fetch('/startCalibration?meter=' + meterToCalibrate, { method: 'POST' });\
}\
\
function stopCalibration(meterToCalibrate) {\
  var mlpumped = prompt('Enter the amount of liquid pumped (ml):');\
  if (mlpumped !== null) {\
    fetch('/stopCalibration?meter=' + meterToCalibrate + '&mlpumped=' + mlpumped, { method: 'POST' })\
      .then(function(response) { return response.text(); })\
      .then(function(data) {\
        startButton.disabled = false;\
        stopButton.disabled = true;\
        window.location.href = '/';\
      });\
  } else {\
    alert('Calibration aborted. Please enter a valid amount of liquid.');\
  }\
}\
\
document.addEventListener('keydown', function(event) {\
  if (event.key === 'Enter') {\
    if (startButton.disabled) {\
      stopCalibration(" + String(meterToCalibrate) + ");\
    } else {\
      startCalibration(" + String(meterToCalibrate) + ");\
    }\
  }\
});\
</script>";
  return page;
}

// ============================================================================
// HTTP route handlers
// ============================================================================
void handleMainPage(AsyncWebServerRequest *request) {
  request->send(200, "text/html", generateMainPage());
}

void handlePoursPage(AsyncWebServerRequest *request) {
  request->send(200, "text/html", generatePoursPage());
}

void handleNewUserPage(AsyncWebServerRequest *request) {
  request->send(200, "text/html", generateNewUserPage());
}

void handleEditUserPage(AsyncWebServerRequest *request) {
  editingUser = request->arg("userToEdit");
  request->send(200, "text/html", generateEditUserPage());
}

void handleUpdatePage(AsyncWebServerRequest *request) {
  readyToRefresh = true;
  handleUserPage(request);
}

void handleCalibrateMeter(AsyncWebServerRequest *request) {
  int meterToCalibrate = request->arg("meter").toInt();
  if (meterToCalibrate < 0 || meterToCalibrate >= 2) {
    request->send(400, "text/plain", "Invalid Flow Meter number");
    return;
  }

  if (request->method() == HTTP_GET) {
    request->send(200, "text/html", generateCalibratePage(meterToCalibrate));
  } else if (request->method() == HTTP_POST) {
    stopCalibration(request);
  } else {
    request->send(405, "text/plain", "Method Not Allowed");
  }
}

void handleResetAll(AsyncWebServerRequest *request) {
  resetFlowMeter(0);
  resetFlowMeter(1);
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume(AsyncWebServerRequest *request) {
  int requestedVolume = request->arg("_pumpVolume").toInt();
  if (requestedVolume <= 0) {
    request->send(400, "text/plain", "Pump volume must be greater than 0");
    return;
  }
  pumpVolume = requestedVolume;
  syncAmount = true;
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume2(AsyncWebServerRequest *request) {
  int requestedVolume = request->arg("_pumpVolume2").toInt();
  if (requestedVolume <= 0) {
    request->send(400, "text/plain", "Pump volume must be greater than 0");
    return;
  }
  pumpVolume2 = requestedVolume;
  syncAmount = true;
  request->send(200, "text/html", generateMainPage());
}

void handleFlowMeterRename(AsyncWebServerRequest *request) {
  String meterToRename = request->arg("renameMeter");
  String newName = request->arg("newName");
  int meterIndex = meterToRename.toInt() - 1;

  if (meterIndex < 0 || meterIndex >= 2 || newName.length() == 0) {
    request->send(400, "text/plain", "Invalid Flow Meter number");
    return;
  }

  flowMeterNames[meterIndex] = newName;
  syncFlowName = true;
  request->send(200, "text/html", generateMainPage());
}

void handleStartPump(AsyncWebServerRequest *request) {
  digitalWrite(pump_pin, HIGH);
  pumpRunning = true;
  startPulse = (pulse1 * flowMeterCalibrations[0]);
  request->send(200, "text/html", generateMainPage());
}

void handleStartPump2(AsyncWebServerRequest *request) {
  digitalWrite(pump_pin2, HIGH);
  pumpRunning = true;
  startPulse = (pulse2 * flowMeterCalibrations[1]);
  request->send(200, "text/html", generateMainPage());
}

void handleToggleValve(AsyncWebServerRequest *request) {
  valveOpen = !valveOpen;
  digitalWrite(valve_pin, valveOpen ? LOW : HIGH);
  request->send(200, "text/html", generateMainPage());
}

void handleToggleValve2(AsyncWebServerRequest *request) {
  valveOpen2 = !valveOpen2;
  digitalWrite(valve_pin2, valveOpen2 ? LOW : HIGH);
  request->send(200, "text/html", generateMainPage());
}

void handleTogglePump(AsyncWebServerRequest *request) {
  pumpOn = !pumpOn;
  digitalWrite(pump_pin, pumpOn ? LOW : HIGH);
  request->send(200, "text/html", generateMainPage());
}

void handleTogglePump2(AsyncWebServerRequest *request) {
  pumpOn2 = !pumpOn2;
  digitalWrite(pump_pin2, pumpOn2 ? LOW : HIGH);
  request->send(200, "text/html", generateMainPage());
}

void handleNewUser(AsyncWebServerRequest *request) {
  newFirstName = request->arg("userFirstName");
  newLastName = request->arg("userLastName");
  newUID = request->arg("userUID");
  newPhone = request->arg("userPhone");
  newAccountStatus = request->arg("accountStatus");
  newBeerCoupons = request->arg("beerCoupons");
  newWineCoupons = request->arg("wineCoupons");

  if (lastNameRead + 1 >= numOfUsers) {
    request->send(400, "text/plain", "User limit reached");
    return;
  }

  numOfNames++;
  lastNameRead++;
  first[lastNameRead] = newFirstName;
  last[lastNameRead] = newLastName;
  uid[lastNameRead] = newUID;
  phone[lastNameRead] = newPhone;
  status[lastNameRead] = newAccountStatus;
  beerCoupons[lastNameRead] = newBeerCoupons;
  wineCoupons[lastNameRead] = newWineCoupons;
  beerUses[lastNameRead] = "0";
  wineUses[lastNameRead] = "0";
  peopleNumber[numOfNames] = lastNameRead;
  setPeopleStrings();
  syncNames = lastNameRead;
  handleUserPage(request);
}

void handleEditUser(AsyncWebServerRequest *request) {
  userToEdit = request->arg("userToEdit");
  userFirstName = request->arg("userFirstName");
  userLastName = request->arg("userLastName");
  userUID = request->arg("userUID");
  userPhone = request->arg("userPhone");
  accountStatus = request->arg("accountStatus");
  beerCouponsEdit = request->arg("beerCoupons");
  wineCouponsEdit = request->arg("wineCoupons");
  beerUsesEdit = request->arg("beerUses");
  wineUsesEdit = request->arg("wineUses");

  int editIndex = userToEdit.toInt();
  if (editIndex <= 0 || editIndex >= numOfUsers) {
    request->send(400, "text/plain", "Invalid user index");
    return;
  }

  first[editIndex] = userFirstName;
  last[editIndex] = userLastName;
  uid[editIndex] = userUID;
  phone[editIndex] = userPhone;
  status[editIndex] = accountStatus;
  beerCoupons[editIndex] = beerCouponsEdit;
  wineCoupons[editIndex] = wineCouponsEdit;
  beerUses[editIndex] = beerUsesEdit;
  wineUses[editIndex] = wineUsesEdit;

  setPeopleStrings();
  syncNames = editIndex;
  handleUserPage(request);
}

void handleDeleteUser(AsyncWebServerRequest *request) {
  userToDelete = request->arg("userToDelete");
  int deleteIndex = userToDelete.toInt();
  if (deleteIndex <= 0 || deleteIndex >= numOfUsers) {
    request->send(400, "text/plain", "Invalid user index");
    return;
  }

  first[deleteIndex] = "deleted";
  last[deleteIndex] = "";
  uid[deleteIndex] = "";
  phone[deleteIndex] = "";
  status[deleteIndex] = "";
  beerCoupons[deleteIndex] = "";
  wineCoupons[deleteIndex] = "";
  beerUses[deleteIndex] = "";
  wineUses[deleteIndex] = "";
  setPeopleStrings();
  syncNames = deleteIndex;
  numOfNames--;
  handleUserPage(request);
}

void handleDeletePour(AsyncWebServerRequest *request) {
  pourToDelete = request->arg("pourToDelete");
  int deletePourIndex = pourToDelete.toInt();
  if (deletePourIndex <= 0 || deletePourIndex >= 200) {
    request->send(400, "text/plain", "Invalid pour index");
    return;
  }

  poursString[deletePourIndex] = "DeletePours";
  syncPours = deletePourIndex;
  numOfPours--;
  request->send(200, "text/html", generatePoursPage());
}

void startCalibration(AsyncWebServerRequest *request) {
  int meterToCalibrate = request->arg("meter").toInt();
  if (meterToCalibrate < 0 || meterToCalibrate >= 2) {
    request->send(400, "text/plain", "Invalid Flow Meter number");
    return;
  }

  calibratingMeter = meterToCalibrate;
  calibrationStart = *pulses[meterToCalibrate];
  calibrationPulses = 0;

  digitalWrite(meterToCalibrate == 0 ? pump_pin : pump_pin2, HIGH);
  request->send(200, "text/plain", "Calibration started");
}

void stopCalibration(AsyncWebServerRequest *request) {
  digitalWrite(pump_pin, LOW);
  digitalWrite(pump_pin2, LOW);

  if (calibratingMeter == -1) {
    request->send(400, "text/plain", "No ongoing calibration");
    return;
  }

  int meterToCalibrate = calibratingMeter;
  calibrationPulses = *pulses[meterToCalibrate];
  mlpumped = request->arg("mlpumped").toFloat();
  long pulseDelta = calibrationPulses - calibrationStart;

  if (mlpumped > 0 && pulseDelta > 0) {
    calibrationAmount = mlpumped / pulseDelta;
    flowMeterCalibrations[calibratingMeter] = calibrationAmount;
    syncCalibration = true;
    request->send(200, "text/html", generateMainPage());
  } else {
    request->send(400, "text/plain", "Invalid calibration amount or no pulses detected");
  }

  calibratingMeter = -1;
  calibrationPulses = 0;
  calibrationStart = 0;
}

// ============================================================================
// Firebase reads (run once at boot). Missing paths are initialized with
// sensible defaults so subsequent boots read cleanly.
// ============================================================================
void readCalibrationValues() {
  if (Firebase.RTDB.getString(&fbdo, "calibration")) {
    if (fbdo.dataType() == "string") {
      String data = fbdo.stringData();
      int us = data.indexOf('_');
      if (us != -1) {
        flowMeterCalibrations[0] = data.substring(0, us).toFloat();
        flowMeterCalibrations[1] = data.substring(us + 1).toFloat();
      }
    }
  } else {
    String defaultCal = String(flowMeterCalibrations[0]) + "_" + String(flowMeterCalibrations[1]);
    Firebase.RTDB.setString(&fbdo, "calibration", defaultCal);
  }
}

void readFlowNameString() {
  if (Firebase.RTDB.getString(&fbdo, "flowname")) {
    if (fbdo.dataType() == "string") {
      String data = fbdo.stringData();
      int us = data.indexOf('_');
      if (us != -1) {
        flowMeterNames[0] = data.substring(0, us);
        flowMeterNames[1] = data.substring(us + 1);
      }
    }
  } else {
    String defaultNames = flowMeterNames[0] + "_" + flowMeterNames[1];
    Firebase.RTDB.setString(&fbdo, "flowname", defaultNames);
  }
}

void readAmountValues() {
  if (Firebase.RTDB.getString(&fbdo, "amount")) {
    if (fbdo.dataType() == "string") {
      String data = fbdo.stringData();
      int us = data.indexOf('_');
      if (us != -1) {
        pumpVolume = data.substring(0, us).toInt();
        pumpVolume2 = data.substring(us + 1).toInt();
      }
    }
  } else {
    String defaultAmount = String(pumpVolume) + "_" + String(pumpVolume2);
    Firebase.RTDB.setString(&fbdo, "amount", defaultAmount);
  }
}

void readPeopleString() {
  if (Firebase.RTDB.getString(&fbdo, "numOfNames")) {
    if (fbdo.dataType() == "string") {
      numOfNames = fbdo.stringData().toInt();
    }
  } else {
    Firebase.RTDB.setString(&fbdo, "numOfNames", String(numOfNames));
    return;
  }

  if (!Firebase.RTDB.getJSON(&fbdo, "people")) {
    return; // No users yet - the node is created on first write.
  }

  FirebaseJson *json = fbdo.jsonObjectPtr();
  size_t count = json->iteratorBegin();
  int foundNames = 0;
  int maxIndexSeen = 0;

  FirebaseJson::IteratorValue value;
  for (size_t i = 0; i < count; i++) {
    value = json->valueAt(i);
    String key = value.key;
    int idx = key.substring(6).toInt(); // strip "people"
    if (idx <= 0 || idx >= numOfUsers) continue;

    String val = value.value;
    if (val.length() >= 2 && val.startsWith("\"") && val.endsWith("\"")) {
      val = val.substring(1, val.length() - 1);
    }

    peopleString[foundNames + 1] = val;
    peopleNumber[foundNames] = idx;
    if (idx > maxIndexSeen) maxIndexSeen = idx;
    foundNames++;

    if ((i & 0x0F) == 0) vTaskDelay(1);
  }
  json->iteratorEnd();

  lastNameRead = maxIndexSeen;

  for (int j = 1; j <= foundNames; j++) {
    int u1 = peopleString[j].indexOf('_');
    int u2 = peopleString[j].indexOf('_', u1 + 1);
    int u3 = peopleString[j].indexOf('_', u2 + 1);
    int u4 = peopleString[j].indexOf('_', u3 + 1);
    int u5 = peopleString[j].indexOf('_', u4 + 1);
    int u6 = peopleString[j].indexOf('_', u5 + 1);
    int u7 = peopleString[j].indexOf('_', u6 + 1);
    int u8 = peopleString[j].indexOf('_', u7 + 1);

    first[j] = peopleString[j].substring(0, u1);
    last[j] = peopleString[j].substring(u1 + 1, u2);
    uid[j] = peopleString[j].substring(u2 + 1, u3);
    phone[j] = peopleString[j].substring(u3 + 1, u4);
    beerUses[j] = peopleString[j].substring(u4 + 1, u5);
    wineUses[j] = peopleString[j].substring(u5 + 1, u6);
    status[j] = peopleString[j].substring(u6 + 1, u7);
    wineCoupons[j] = peopleString[j].substring(u7 + 1, u8);
    beerCoupons[j] = peopleString[j].substring(u8 + 1);
  }

  lastNameRead++;
}

void readPoursString() {
  if (Firebase.RTDB.getString(&fbdo, "numOfPours")) {
    if (fbdo.dataType() == "string") {
      numOfPours = fbdo.stringData().toInt();
    }
  } else {
    Firebase.RTDB.setString(&fbdo, "numOfPours", String(numOfPours));
    return;
  }

  if (!Firebase.RTDB.getJSON(&fbdo, "pours")) {
    return; // No pours yet - the node is created on first write.
  }

  FirebaseJson *json = fbdo.jsonObjectPtr();
  size_t count = json->iteratorBegin();
  int foundPours = 0;
  int maxIndexSeen = 0;

  FirebaseJson::IteratorValue value;
  for (size_t i = 0; i < count; i++) {
    value = json->valueAt(i);
    String key = value.key;
    int idx = key.substring(4).toInt(); // strip "pour"
    if (idx <= 0 || idx >= 200) continue;

    String val = value.value;
    if (val.length() >= 2 && val.startsWith("\"") && val.endsWith("\"")) {
      val = val.substring(1, val.length() - 1);
    }

    poursString[foundPours + 1] = val;
    pourNumber[foundPours] = idx;
    if (idx > maxIndexSeen) maxIndexSeen = idx;
    foundPours++;

    if ((i & 0x0F) == 0) vTaskDelay(1);
  }
  json->iteratorEnd();

  lastPourRead = maxIndexSeen;
  lastPourRead++;
}

// ============================================================================
// NFC scanning
// ============================================================================
void nfcCode() {
  timePassed = 0;
  uint8_t nfc_uid[7] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  uint8_t success = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, nfc_uid, &uidLength, 500);
    if (!success) {
      esp_task_wdt_reset();
      vTaskDelay(1);
      delay(50);
    }
  }

  if (!success) return;

  String uidString = "";
  for (uint8_t i = 0; i < uidLength; i++) {
    if (nfc_uid[i] < 0x10) uidString += "0";
    uidString += String(nfc_uid[i], HEX);
  }

  Serial.print("NFC card scanned, UID: ");
  Serial.println(uidString);

  bool uidMatchFound = false;
  for (int i = 1; i <= numOfNames; i++) {
    if (uidString != uid[i]) continue;

    Serial.print("Match found: ");
    Serial.print(first[i]);
    Serial.print(" ");
    Serial.println(last[i]);

    personToRead = i;
    uidMatchFound = true;

    if (beerCoupons[i].toInt() == 1000) {
      consommations = "CONSOMMATION ILLIMITEE";
    } else if (beerCoupons[i].toInt() == 1) {
      consommations = beerCoupons[i] + " CONSOMMATION RESTANTE";
    } else if (beerCoupons[i].toInt() > 1) {
      consommations = beerCoupons[i] + " CONSOMMATIONS RESTANTE";
    } else {
      consommations = beerCoupons[i] + " CONSOMMATION RESTANTE";
      Serial.println("No coupons remaining");
      for (int t = 0; t < 4000; t += 250) {
        esp_task_wdt_reset();
        delay(250);
      }
      break;
    }

    continue1 = true;
    if (client.connected()) {
      String message = first[i] + "|" + consommations + "|" + flowMeterNames[0] + "|" + flowMeterNames[1];
      client.println(message);
      Serial.println("Sent to dashboard: " + message);
    } else {
      Serial.println("Dashboard client disconnected, reconnecting...");
      client.connect(host, port);
    }

  while (true) {
      esp_task_wdt_reset();
      vTaskDelay(1);  // yield to the scheduler so IDLE1 can run

      if (digitalRead(buttonResume) == LOW) {
        delay(50);
        if (digitalRead(buttonResume) == LOW) {
          buttonPressTime = millis();
          while (digitalRead(buttonResume) == LOW) {
            esp_task_wdt_reset();
            vTaskDelay(1);  // same reasoning - keep yielding during the hold-check loop
            if (millis() - buttonPressTime > holdTime) {
              delay(50);
              readyToPour = true;
              okToRead = false;
              return;
            }
            delay(10);
          }

          selection = !selection;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("CHOISIS UN ROBINET");
          lcd.setCursor(0, 1);
          lcd.print((selection == 0 ? "> " : "  ") + flowMeterNames[0]);
          lcd.setCursor(0, 2);
          lcd.print((selection == 1 ? "> " : "  ") + flowMeterNames[1]);
        }
      }

      if (digitalRead(buttonStop) == LOW) break;
    }
    break;
  }

  if (!uidMatchFound) {
    Serial.println("No matching user for this UID");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UID: ");
    lcd.print(uidString);
  }
}

// ============================================================================
// Background tasks
// ============================================================================
void nfcTask(void *parameter) {
  esp_task_wdt_add(NULL);
  for (;;) {
    if (nfcWorking && okToRead && timePassed > 1) {
      nfcCode();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void writeTask(void *parameter) {
  for (;;) {
    if (!Firebase.ready()) {
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }

    if (syncNames > 0) {
      Firebase.RTDB.setString(&fbdo, "numOfNames", String(numOfNames));
      String path = "people/people" + String(peopleNumber[syncNames - 1]);

      if (peopleString[syncNames] == "deleted________") {
        if (!Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
          vTaskDelay(5000 / portTICK_PERIOD_MS);
          Firebase.RTDB.deleteNode(&fbdo, path.c_str());
        }
      } else {
        if (!Firebase.RTDB.setString(&fbdo, path.c_str(), String(peopleString[syncNames]))) {
          vTaskDelay(5000 / portTICK_PERIOD_MS);
          Firebase.RTDB.setString(&fbdo, path.c_str(), String(peopleString[syncNames]));
        }
      }
      syncNames = 0;
    }

    if (syncPours > 0) {
      Firebase.RTDB.setString(&fbdo, "numOfPours", String(numOfPours));

      int index = syncPours - 1;
      while (index >= 0 && pourNumber[index] == 0) index--;
      int pourValue = (index >= 0) ? pourNumber[index] : 0;
      String path = "pours/pour" + String(pourValue + 1);

      if (poursString[syncPours] == "DeletePours") {
        if (!Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
          vTaskDelay(5000 / portTICK_PERIOD_MS);
          Firebase.RTDB.deleteNode(&fbdo, path.c_str());
        }
      } else {
        if (!Firebase.RTDB.setString(&fbdo, path.c_str(), String(poursString[syncPours]))) {
          vTaskDelay(5000 / portTICK_PERIOD_MS);
          Firebase.RTDB.setString(&fbdo, path.c_str(), String(poursString[syncPours]));
        }
      }
      syncPours = 0;
    }

    if (syncCalibration) {
      String calstring = String(flowMeterCalibrations[0]) + "_" + String(flowMeterCalibrations[1]);
      Firebase.RTDB.setString(&fbdo, "calibration", calstring);
      syncCalibration = false;
    }

    if (syncFlowName) {
      String flownamestring = flowMeterNames[0] + "_" + flowMeterNames[1];
      Firebase.RTDB.setString(&fbdo, "flowname", flownamestring);
      syncFlowName = false;
    }

    if (syncAmount) {
      String amountstring = String(pumpVolume) + "_" + String(pumpVolume2);
      Firebase.RTDB.setString(&fbdo, "amount", amountstring);
      syncAmount = false;
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);

  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 30000, // generous during boot; tightened after setup
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&twdt_config);
  esp_task_wdt_add(NULL);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "esp@smarttap.com";
  auth.user.password = FIREBASE_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (Firebase.ready()) {
    Serial.println("Connected to Firebase successfully");
    signupOK = true;
  } else {
    Serial.println("Firebase authentication failed or timed out");
  }

  Wire.begin();
  Wire.setTimeOut(50);  // abort any I2C transaction that stalls past 50ms

  server.on("/", HTTP_GET, handleMainPage);
  server.on("/calibrateMeter", HTTP_GET, handleCalibrateMeter);
  server.on("/startCalibration", HTTP_POST, startCalibration);
  server.on("/stopCalibration", HTTP_POST, stopCalibration);
  server.on("/resetAll", HTTP_GET, handleResetAll);
  server.on("/rename", HTTP_GET, handleFlowMeterRename);
  server.on("/toggleValve", HTTP_GET, handleToggleValve);
  server.on("/togglePump", HTTP_GET, handleTogglePump);
  server.on("/pumpVolume", HTTP_GET, handlepumpVolume);
  server.on("/toggleValve2", HTTP_GET, handleToggleValve2);
  server.on("/togglePump2", HTTP_GET, handleTogglePump2);
  server.on("/pumpVolume2", HTTP_GET, handlepumpVolume2);
  server.on("/startPump", HTTP_GET, handleStartPump);
  server.on("/startPump2", HTTP_GET, handleStartPump2);
  server.on("/manageUsers", HTTP_GET, handleUserPage);
  server.on("/pours", HTTP_GET, handlePoursPage);
  server.on("/newUser", HTTP_GET, handleNewUser);
  server.on("/editUser", HTTP_GET, handleEditUser);
  server.on("/deleteUser", HTTP_GET, handleDeleteUser);
  server.on("/deletePour", HTTP_GET, handleDeletePour);
  server.on("/updatePage", HTTP_GET, handleUpdatePage);
  server.on("/newUserPage", HTTP_GET, handleNewUserPage);
  server.on("/editUserPage", HTTP_GET, handleEditUserPage);
  server.begin();

  pinMode(valve_pin, OUTPUT);
  digitalWrite(valve_pin, LOW);
  pinMode(pump_pin, OUTPUT);
  digitalWrite(pump_pin, LOW);
  pinMode(valve_pin2, OUTPUT);
  digitalWrite(valve_pin2, LOW);
  pinMode(pump_pin2, OUTPUT);
  digitalWrite(pump_pin2, LOW);
  pinMode(buttonResume, INPUT_PULLUP);
  pinMode(buttonStop, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseCounter, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN2), pulseCounter2, FALLING);
  oldTime = millis();

  esp_task_wdt_reset();
  readCalibrationValues();
  esp_task_wdt_reset();
  readAmountValues();
  esp_task_wdt_reset();
  readPeopleString();
  esp_task_wdt_reset();
  readPoursString();
  esp_task_wdt_reset();
  readFlowNameString();
  esp_task_wdt_reset();

  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.println("Didn't find PN53x board");
    nfcWorking = false;
  } else {
    nfc.SAMConfig();
    Serial.println("Waiting for an NFC card...");
    nfcWorking = true;
  }

  client.setTimeout(2);
  if (!client.connect(host, port)) {
    Serial.println("Connection to dashboard server failed");
  } else {
    Serial.println("Connected to the dashboard server");
  }

  wled(0, 25, 255);

  xTaskCreatePinnedToCore(nfcTask, "NFC Task", 4096, NULL, 1, &nfcTaskHandle, 1);
  xTaskCreatePinnedToCore(writeTask, "Write Task", 8192, NULL, 1, &writeTaskHandle, 0);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  twdt_config.timeout_ms = 15000; // normal operating timeout
  esp_task_wdt_reconfigure(&twdt_config);
  Serial.println("Setup complete");
}

// ============================================================================
// Main loop
// ============================================================================
void loop() {
  esp_task_wdt_reset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  pulse1 = pulseCount;
  pulse2 = pulseCount2;

  // --- Tap 1 pour in progress ---
  if (pumpRunning && tap == 0) {
    int percentage = (((pulse1 * flowMeterCalibrations[0]) - startPulse) / pumpVolume) * 100;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    lcd.setCursor(0, 2);
    lcd.print(String(percentage) + "%");

    if ((pulse1 * flowMeterCalibrations[0]) - startPulse >= pumpVolume) {
      float amountPoured = (pulse1 * flowMeterCalibrations[0]) - startPulse;
      digitalWrite(pump_pin, LOW);
      digitalWrite(valve_pin, LOW);
      digitalWrite(pump_pin2, LOW);
      digitalWrite(valve_pin2, LOW);
      pumpRunning = false;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PRET!");
      lcd.setCursor(0, 1);
      lcd.print("DEGUSTEZ!");

      if (beerCoupons[personToRead].toInt() != 1000) {
        beerCoupons[personToRead] = String(beerCoupons[personToRead].toInt() - 1);
      }
      beerUses[personToRead] = String(beerUses[personToRead].toInt() + 1);
      setPeopleStrings();
      syncNames = personToRead;

      char timeBuf[6];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      char dateBuf[11];
      snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", 1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday);

      String formattedPourSave = String(timeBuf) + "_" + String(dateBuf) + "_" + flowMeterNames[0] + "_" + String(amountPoured) + "_" + first[personToRead] + "_" + last[personToRead];
      numOfPours++;
      lastPourRead++;
      poursString[lastPourRead] = formattedPourSave;
      syncPours = lastPourRead;
      pourNumber[numOfPours] = lastPourRead;

      for (int i = 0; i < 4000; i += 250) {
        esp_task_wdt_reset();
        delay(250);
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PASSEZ VOTRE TAG");
      lcd.setCursor(0, 1);
      lcd.print("SUR LE LECTEUR");
      readyToPour = false;
      okToRead = true;
    }
  }

  // --- Tap 2 pour in progress ---
  if (pumpRunning && tap == 1) {
    int percentage = (((pulse2 * flowMeterCalibrations[1]) - startPulse) / pumpVolume2) * 100;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    lcd.setCursor(0, 2);
    lcd.print(String(percentage) + "%");

    if ((pulse2 * flowMeterCalibrations[1]) - startPulse >= pumpVolume2) {
      float amountPoured = (pulse2 * flowMeterCalibrations[1]) - startPulse;
      digitalWrite(pump_pin, LOW);
      digitalWrite(valve_pin, LOW);
      digitalWrite(pump_pin2, LOW);
      digitalWrite(valve_pin2, LOW);
      pumpRunning = false;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PRET!");
      lcd.setCursor(0, 1);
      lcd.print("DEGUSTEZ!");

      if (wineCoupons[personToRead].toInt() != 1000) {
        wineCoupons[personToRead] = String(wineCoupons[personToRead].toInt() - 1);
      }
      wineUses[personToRead] = String(wineUses[personToRead].toInt() + 1);
      setPeopleStrings();
      syncNames = personToRead;

      char timeBuf[6];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      char dateBuf[11];
      snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", 1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday);

      String formattedPourSave = String(timeBuf) + "_" + String(dateBuf) + "_" + flowMeterNames[1] + "_" + String(amountPoured) + "_" + first[personToRead] + "_" + last[personToRead];
      numOfPours++;
      lastPourRead++;
      poursString[lastPourRead] = formattedPourSave;
      syncPours = lastPourRead;
      pourNumber[numOfPours] = lastPourRead;

      for (int i = 0; i < 4000; i += 250) {
        esp_task_wdt_reset();
        delay(250);
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PASSEZ VOTRE TAG");
      lcd.setCursor(0, 1);
      lcd.print("SUR LE LECTEUR");
      readyToPour = false;
      okToRead = true;
    }
  }

  // --- Start pour when operator confirms tap selection ---
  if (readyToPour && digitalRead(buttonResume) == LOW) {
    if (selection == 0) {
      digitalWrite(pump_pin, HIGH);
      digitalWrite(valve_pin, HIGH);
      tap = 0;
      startPulse = (pulse1 * flowMeterCalibrations[0]);
    } else {
      digitalWrite(pump_pin2, HIGH);
      digitalWrite(valve_pin2, HIGH);
      tap = 1;
      startPulse = (pulse2 * flowMeterCalibrations[1]);
    }
    selection = 0;
    pumpRunning = true;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    readyToPour = false;
    okToRead = true;
  }

  // --- Stop button aborts a pour / resets to idle ---
  if (digitalRead(buttonStop) == LOW) {
    continue1 = false;
    digitalWrite(pump_pin, LOW);
    digitalWrite(valve_pin, LOW);
    digitalWrite(pump_pin2, LOW);
    digitalWrite(valve_pin2, LOW);
    pumpRunning = false;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PASSEZ VOTRE TAG");
    lcd.setCursor(0, 1);
    lcd.print("SUR LE LECTEUR");
    readyToPour = false;
    okToRead = true;
  }

  if (message1 == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PASSEZ VOTRE TAG");
    lcd.setCursor(0, 1);
    lcd.print("SUR LE LECTEUR");
    message1 = 1;
  }

  if (timePassed1 > 500) {
    setPeopleStrings();
    readyToWrite = true;
    timePassed1 = 0;
  }

  timePassed++;
  timePassed1++;
  delay(250);
}
