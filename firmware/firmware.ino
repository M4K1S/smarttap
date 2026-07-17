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

AsyncWebServer server(80);

#define PN532_IRQ (5)
#define PN532_RESET (2)
#define I2C_ADDR (0x27)  // Check I2C of your adapter
#define LCD_COLS (20)
#define LCD_ROWS (4)
#define SENSOR_PIN 34
#define SENSOR_PIN2 35

// Mac (server) IP address
const char* host = DASHBOARD_HOST;  // Defined in secrets.h
const uint16_t port = 12345;        // Port number, ensure it matches the Python server

WiFiClient client;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;  // Adjust for your timezone
const int daylightOffset_sec = 3600;   // Adjust for daylight saving time if necessary

int selection = 0; // 0 for ROBINET 1, 1 for ROBINET 2
unsigned long buttonPressTime = 0;
unsigned long holdTime = 750; // 0.75 seconds to select
int tap = 0;

volatile int pulseCount = 0;
volatile int pulseCount2 = 0;
float flowRate = 0.0;
unsigned long oldTime = 0;

//Firebase
bool readyToDelete = false;
bool readyToAdd = false;
bool readyToEdit = false;
bool readyToRefresh = false;
int timePassed = 0;
int message1 = 0;
bool readyToWrite = false;
int timePassed1 = 0;
int personToRead = -1;
bool continue1 = false;
//Define Firebase Data object
FirebaseData fbdo;

int syncNames = 0;
int syncPours = 0;
bool syncCalibration = false;
bool syncAmount = false;
bool syncFlowName = false;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
const int numOfUsers = 200;
int numOfNames = 0;
int numOfPours = 0;
String editingUser = "0";
String consommations = "0";

const char *wled_ip = WLED_HOST;  // Defined in secrets.h

int lastNameRead = 0;
int lastPourRead = 0;

const int buttonResume = 4;
const int buttonStop = 18;
bool readyToPour = false;
bool okToRead = true;

// Delete Variables
String userToDelete = "";
String pourToDelete = "";

// Add Varaibles
String newFirstName = "";
String newLastName = "";
String newUID = "";
String newPhone = "";
String newAccountStatus = "";
String newBeerCoupons = "";
String newWineCoupons = "";

// Edit Variables
String userToEdit = "";
String userFirstName = "";
String userLastName = "";
String userUID = "";
String userPhone = "";
String accountStatus = "";
String beerCouponsEdit = "";
String wineCouponsEdit = "";
String beerUsesEdit = "";
String wineUsesEdit = "";

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

bool signupOK = false;
int count = 0;
bool nfcWorking = false;

int valve_pin = 23;
int pump_pin = 13;
int valve_pin2 = 14;
int pump_pin2 = 33;
int pumpVolume = 30;
int pumpVolume2 = 30;
bool pumpRunning = false;
int startPulse = 0;


volatile long pulse1 = 0;
volatile long pulse2 = 0;

volatile long* pulses[] = { &pulse1, &pulse2 };


// Calibration variables
int calibratingMeter = -1;  // -1 indicates no ongoing calibration
long calibrationPulses = 0;
long calibrationStart = 0;
float calibrationAmount = 0;
float mlpumped = 0;
float flowMeterCalibrations[2] = { 1, 1 };  // Calibration values for each flow meter (pulses/mL)


// Flow Meter names
String flowMeterNames[2] = { "Flow Meter 1", "Flow Meter 2" };
bool useOunces = false;  // Default to milliliters
float mlOZ = 1.0;        // Default to milliliters

bool valveOpen = true;
bool pumpOn = true;
bool valveOpen2 = true;
bool pumpOn2 = true;

bool compareUIDs(const uint8_t *uid1, uint8_t len1, const uint8_t *uid2, uint8_t len2) {
  if (len1 != len2) return false;
  for (uint8_t i = 0; i < len1; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);  

String flowMeterData() {
  String unit = useOunces ? "Ounces" : "Milliliters";
  String data = "\
Flow Meter Data:<br>\
" + flowMeterNames[0]
                + ": " + String(pulse1 * flowMeterCalibrations[0] * mlOZ) + " " + unit + "<br>\
" + flowMeterNames[1]
                + ": " + String(pulse2 * flowMeterCalibrations[1] * mlOZ) + " " + unit + "<br><br>";
  return data;
}

String generateMainPage() {
  String unit = useOunces ? "Ounces" : "Milliliters";
  String valveStatus = valveOpen ? "Closed" : "Open";
  String pumpStatus = pumpOn ? "Off" : "On";
  String valveStatus2 = valveOpen2 ? "Closed" : "Open";
  String pumpStatus2 = pumpOn2 ? "Off" : "On";
  String mainPage = "\
<html>\
<head>\
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
  <style>\
    * {\
      box-sizing: border-box;\
    }\
    body {\
      text-align: center;\
      font-family: Arial, sans-serif;\
      font-size: 100%;\
      margin: 0;\
      padding: 10px;\
      background-color: #f2f2f2;\
    }\
    h1 {\
      font-size: 1.8em;\
      margin: 5px 0;\
    }\
    h2 {\
      font-size: 1.3em;\
      margin: 8px 0;\
    }\
    h3 {\
      font-size: 1em;\
      margin: 5px 0;\
      font-weight: normal;\
      color: #555;\
    }\
    h4 {\
      font-size: 1em;\
      margin: 5px 0;\
      font-weight: normal;\
    }\
    .top-actions {\
      display: flex;\
      gap: 10px;\
      justify-content: center;\
      flex-wrap: wrap;\
      margin-bottom: 15px;\
    }\
    .top-actions button {\
      width: auto;\
      min-width: 160px;\
    }\
    .totals-box {\
      background: white;\
      border-radius: 8px;\
      padding: 10px 15px;\
      margin: 0 auto 15px auto;\
      max-width: 700px;\
      box-shadow: 0 1px 3px rgba(0,0,0,0.15);\
    }\
    .meters-grid {\
      display: grid;\
      grid-template-columns: 1fr 1fr;\
      gap: 15px;\
      max-width: 900px;\
      margin: 0 auto;\
    }\
    @media (max-width: 700px) {\
      .meters-grid {\
        grid-template-columns: 1fr;\
      }\
    }\
    .meter-card {\
      background: white;\
      border-radius: 8px;\
      padding: 12px;\
      box-shadow: 0 1px 3px rgba(0,0,0,0.15);\
    }\
    button {\
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
    button:hover {\
      background-color: #4d4d4d;\
    }\
    button.start {\
      background-color: #2e7d32;\
    }\
    button.start:hover {\
      background-color: #1b5e20;\
    }\
    p.ip {\
      font-size: 0.85em;\
      color: #777;\
      margin-top: 15px;\
    }\
  </style>\
</head>\
<body>\
<h1>Wine Machine</h1>\
<h3>v0.50</h3>\
<div class=\"top-actions\">\
  <button onclick=\"submitUserInput(10)\">Manage Users</button>\
  <button onclick=\"submitUserInput(11)\">Previous Pours</button>\
</div>\
<div class=\"totals-box\">\
  <h2>Current Totals</h2>\
  <h4>" + flowMeterData() + "</h4>\
</div>\
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
</body>\
</html>\
<script>\
function submitUserInput(option) {\
  if (option == 1) {\
    var meterToCalibrate = 1;\
    if (meterToCalibrate != null && meterToCalibrate >= 1 && meterToCalibrate <= 2) {\
      meterToCalibrate = parseInt(meterToCalibrate);\
      window.location.href = '/calibrateMeter?meter=' + (meterToCalibrate - 1);\
    } else {\
      alert('Invalid Flow Meter number');\
    }\
  } else if (option == 2) {\
    var newSSID = prompt('Enter New SSID');\
    var newPASS = prompt('Enter New Password');\
    window.location.href = '/changeWifi?newSSID=' + encodeURIComponent(newSSID) + '&newPASS=' + encodeURIComponent(newPASS);\
  } else if (option == 3) {\
    if (confirm('Are you sure you want to reset the totalizers?')) {\
      window.location.href = '/resetAll';\
    }\
  } else if (option == 5) {\
    window.location.href = '/toggleValve';\
  } else if (option == 15) {\
    window.location.href = '/toggleValve2';\
  } else if (option == 13) {\
    var meterToCalibrate = 2;\
    if (meterToCalibrate != null && meterToCalibrate >= 1 && meterToCalibrate <= 2) {\
      meterToCalibrate = parseInt(meterToCalibrate);\
      window.location.href = '/calibrateMeter?meter=' + (meterToCalibrate - 1);\
    } else {\
      alert('Invalid Flow Meter number');\
    }\
  } else if (option == 6) {\
    window.location.href = '/togglePump';\
  } else if (option == 16) {\
    window.location.href = '/togglePump2';\
  } else if (option == 8) {\
    window.location.href = '/startPump';\
  } else if (option == 17) {\
    window.location.href = '/startPump2';\
  } else if (option == 7) {\
    var _pumpVolume = prompt('Enter Pump Volume (ml):');\
    window.location.href = '/pumpVolume?_pumpVolume=' + encodeURIComponent(_pumpVolume);\
  } else if (option == 4) {\
    var _pumpVolume2 = prompt('Enter Pump Volume (ml):');\
    window.location.href = '/pumpVolume2?_pumpVolume2=' + encodeURIComponent(_pumpVolume2);\
  } else if (option == 10) {\
    window.location.href = '/manageUsers';\
  } else if (option == 11) {\
    window.location.href = '/pours';\
  } else if (option == 14) {\
    var meterToRename = 2;\
    var newName = prompt('Enter New Name:');\
    window.location.href = '/rename?renameMeter=' + meterToRename + '&newName=' + encodeURIComponent(newName);\
  } else if (option == 9) {\
    var meterToRename = 1;\
    var newName = prompt('Enter New Name:');\
    window.location.href = '/rename?renameMeter=' + meterToRename + '&newName=' + encodeURIComponent(newName);\
  }\
}\
</script>";
  return mainPage;
}

String userPageHeaderHTML() {
  return String("\
<html>\
<head>\
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
  <style>\
    * { box-sizing: border-box; }\
    body {\
      text-align: center;\
      font-family: Arial, sans-serif;\
      font-size: 100%;\
      margin: 0;\
      padding: 10px;\
      background-color: #f2f2f2;\
    }\
    h1 { font-size: 1.8em; margin: 5px 0; }\
    h2 { font-size: 1.3em; margin: 8px 0; }\
    h3 { font-size: 1em; margin: 5px 0; font-weight: normal; color: #555; }\
    .top-actions {\
      display: flex; gap: 10px; justify-content: center;\
      flex-wrap: wrap; margin-bottom: 15px;\
    }\
    .top-actions button { width: auto; min-width: 160px; }\
    .users-grid {\
      display: grid;\
      grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));\
      gap: 15px;\
      max-width: 1100px;\
      margin: 0 auto;\
      text-align: left;\
    }\
    .user-card {\
      background: white;\
      border-radius: 8px;\
      padding: 14px;\
      box-shadow: 0 1px 3px rgba(0,0,0,0.15);\
    }\
    .user-card .name { font-size: 1.1em; font-weight: bold; margin-bottom: 8px; }\
    .user-card .detail { font-size: 0.85em; color: #444; margin: 3px 0; }\
    .user-card .actions { display: flex; gap: 8px; margin-top: 10px; }\
    .user-card .actions button { margin: 0; flex: 1; font-size: 0.85em; padding: 8px; }\
    button {\
      display: block; width: 100%; padding: 12px; margin: 8px 0;\
      font-size: 1em; background-color: #6c6c6c; color: white;\
      border: none; border-radius: 5px; cursor: pointer;\
    }\
    button:hover { background-color: #4d4d4d; }\
    button.add {\
      background-color: #2e7d32; max-width: 300px; margin: 20px auto 0 auto;\
    }\
    button.add:hover { background-color: #1b5e20; }\
    button.delete-button { background-color: #b71c1c; }\
    button.delete-button:hover { background-color: #7f1313; }\
    p.ip { font-size: 0.85em; color: #777; margin-top: 20px; }\
  </style>\
</head>\
<body>\
<h1>Manage Users</h1>\
<h3>v0.01e</h3>\
<div class=\"top-actions\">\
  <button onclick=\"submitUserInput(5)\">Go Back To Main Page</button>\
</div>\
<h2>All Users</h2>\
<div class=\"users-grid\">");
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
  footer += "</div>\
<button class=\"add\" onclick=\"submitUserInput(1)\">Add User</button>\
<p class=\"ip\">IP Address: " + WiFi.localIP().toString() + "</p>\
</body>\
</html>\
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
 
// Shared state for one in-progress chunked response.
// A shared_ptr keeps it alive across the multiple callback invocations
// the server will make while streaming this one page to one client.
struct UserPageStreamState {
  bool headerSent = false;
  bool footerSent = false;
  bool done = false;
  int i = 1;          // current index into first[]/last[]/etc
  String buffer = ""; // holds whatever partial content is left to flush
  size_t bufferPos = 0;
};
 
void handleUserPage(AsyncWebServerRequest *request) {
  auto state = std::make_shared<UserPageStreamState>();
 
  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "text/html",
    [state](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t written = 0;
 
      while (written < maxLen) {
        // If our internal buffer is empty, generate the next piece of content.
        if (state->bufferPos >= state->buffer.length()) {
          state->buffer = "";
          state->bufferPos = 0;
 
          if (!state->headerSent) {
            state->buffer = userPageHeaderHTML();
            state->headerSent = true;
          } else if (!state->done) {
            // Find the next valid (non-deleted, non-empty) user, if any.
            bool found = false;
            while (state->i <= numOfUsers - 1) {
              int idx = state->i;
              state->i++;
              if (idx <= numOfNames + 50 /* generous slack for skipped/deleted slots */
                  && first[idx] != "deleted" && !first[idx].isEmpty()) {
                state->buffer = userCardHTML(idx);
                found = true;
                break;
              }
              if (idx > numOfNames + 50) break; // safety stop
            }
            if (!found) {
              state->done = true;
            }
          } else if (!state->footerSent) {
            state->buffer = userPageFooterHTML();
            state->footerSent = true;
          } else {
            // Nothing left to send.
            break;
          }
        }
 
        size_t remaining = state->buffer.length() - state->bufferPos;
        if (remaining == 0) continue; // loop back to generate next piece
        size_t toCopy = (remaining < (maxLen - written)) ? remaining : (maxLen - written);
        memcpy(buffer + written, state->buffer.c_str() + state->bufferPos, toCopy);
        state->bufferPos += toCopy;
        written += toCopy;
      }
 
      return written;
    });
 
  request->send(response);
}

String generatePoursPage() {
  String poursPage = "\
<html>\
<head>\
<style>\
  .pour-container {\
    border: 1px solid #ccc;\
    padding: 10px;\
    margin-bottom: 10px;\
  }\
  .name {\
    font-size: 24px;\
    font-weight: bold;\
    margin-bottom: 5px;\
  }\
  .details {\
    font-size: 14px;\
  }\
  .delete-button {\
    margin-top: 10px;\
  }\
</style>\
</head>\
<body>\
<center>\
<h1>View Past Pours</h1>\
<h3>v0.01e<br></h3>\
</center>\
<h2>All Pours</h2>\
<h3>\
<button onclick=\"submitUserInput(5)\">Go Back To Main Page</button><br>\
</h3>";
    int j = 0;
  // Loop over all pours and add their details to the webpage
  for (int i = 1; i <= numOfPours + j; i++) {  // Corrected loop condition
    // Split the poursString[i] into its components
    String pourEntry = poursString[i];
    if (pourEntry == "DeletePours") {
      j++;
      continue; // Skip this iteration if pourEntry is "!"
    }
    if (poursString[i].isEmpty()) {
      j++;
      continue; // Skip this iteration if pourEntry is "!"
    }
    
    int firstUnderscore = pourEntry.indexOf('_');
    int secondUnderscore = pourEntry.indexOf('_', firstUnderscore + 1);
    int thirdUnderscore = pourEntry.indexOf('_', secondUnderscore + 1);
    int fourthUnderscore = pourEntry.indexOf('_', thirdUnderscore + 1);

    // Extract components in the correct order
    String time = pourEntry.substring(0, firstUnderscore);
    String date = pourEntry.substring(firstUnderscore + 1, secondUnderscore);
    String typeOfDrink = pourEntry.substring(secondUnderscore + 1, thirdUnderscore);
    String amountPoured = pourEntry.substring(thirdUnderscore + 1, fourthUnderscore);
    String firstName = pourEntry.substring(fourthUnderscore + 1);
    int lastNameIndex = firstName.lastIndexOf('_');  // Find last underscore for last name
    String lastName = firstName.substring(lastNameIndex + 1);
    firstName = firstName.substring(0, lastNameIndex);

    // Build the HTML for each pour entry
    poursPage += "\
<div class=\"pour-container\">\
  <div class=\"name\">"
                 + firstName + " " + lastName + "</div>\
  <div class=\"details\">\
    <b>Time:</b> "
                 + time + "<br>\
    <b>Date:</b> "
                 + date + "<br>\
    <b>Type of Drink:</b> "
                 + typeOfDrink + "<br>\
    <b>Amount Poured:</b> "
                 + amountPoured + " ml\
  </div>\
  <button class=\"delete-button\" onclick=\"submitUserInput(7, "
                 + String(i) + ")\">Delete Pour</button>\
</div>";
  }

  poursPage += "\
<h3>\
</h3>\
<p>IP Address: "
               + WiFi.localIP().toString() + "</p>\
</body>\
</html>\
<script>\
function submitUserInput(option, pourIndex) {\
  if (option == 5) {\
    window.location.href = '/';\
  } else if (option == 7) {\
    window.location.href = '/deletePour?pourToDelete=' + pourIndex;\
  }\
}\
</script>";
  return poursPage;
}



String generateNewUserPage() {
  String newUserPage = "\
<html>\
<body>\
<center>\
<h1>New User</h1>\
</center>\
<form id=\"newUserForm\">\
  <label for=\"firstName\">First Name:</label><br>\
  <input type=\"text\" id=\"firstName\" name=\"userFirstName\"><br>\
  <label for=\"lastName\">Last Name:</label><br>\
  <input type=\"text\" id=\"lastName\" name=\"userLastName\"><br>\
  <label for=\"nfcUid\">NFC UID:</label><br>\
  <input type=\"text\" id=\"nfcUid\" name=\"userUID\"><br>\
  <label for=\"phoneNum\">Phone Number:</label><br>\
  <input type=\"text\" id=\"phoneNum\" name=\"userPhone\"><br>\
  <label for=\"beerCoupon\">Beer (Coupons):</label><br>\
  <select id=\"beerCoupon\" name=\"beerCoupons\">";

  for (int i = 0; i <= 100; i++) {
    newUserPage += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
  }
  newUserPage += "<option value=\"1000\">Unlimited</option>";

  newUserPage += "\
  </select><br>\
  <label for=\"wineCoupon\">Wine (Coupons):</label><br>\
  <select id=\"wineCoupon\" name=\"wineCoupons\">";

  for (int i = 0; i <= 100; i++) {
    newUserPage += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
  }
  newUserPage += "<option value=\"1000\">Unlimited</option>";

  newUserPage += "\
  </select><br>\
  <label for=\"accStatus\">Account Status:</label><br>\
  <select id=\"accStatus\" name=\"accountStatus\">\
    <option value=\"Active\">Active</option>\
    <option value=\"Inactive\">Inactive</option>\
  </select><br><br>\
  <input type=\"button\" value=\"Submit\" onclick=\"submitForm()\">\
  <input type=\"button\" value=\"Go Back\" onclick=\"goBack()\">\
</form>\
</body>\
</html>\
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
  return newUserPage;
}

String generateEditUserPage() {
  String userToEdit = editingUser;
  String editUserPage = "\
<html>\
<body>\
<center>\
<h1>Edit User # " + userToEdit
                        + "</h1>\
</center>\
<form id=\"editUserForm\">\
  <label for=\"firstName\">First Name:</label><br>\
  <input type=\"text\" id=\"firstName\" name=\"userFirstName\" value=\""
                        + first[userToEdit.toInt()] + "\"><br>\
  <label for=\"lastName\">Last Name:</label><br>\
  <input type=\"text\" id=\"lastName\" name=\"userLastName\" value=\""
                        + last[userToEdit.toInt()] + "\"><br>\
  <label for=\"nfcUid\">NFC UID:</label><br>\
  <input type=\"text\" id=\"nfcUid\" name=\"userUID\" value=\""
                        + uid[userToEdit.toInt()] + "\"><br>\
  <label for=\"phoneNum\">Phone Number:</label><br>\
  <input type=\"text\" id=\"phoneNum\" name=\"userPhone\" value=\""
                        + phone[userToEdit.toInt()] + "\"><br>\
  <label for=\"beerCoupon\">Beer (Coupons):</label><br>\
  <select id=\"beerCoupon\" name=\"beerCoupons\">";

  for (int i = 0; i <= 100; i++) {
    if (i == beerCoupons[userToEdit.toInt()].toInt()) {
      editUserPage += "<option value=\"" + String(i) + "\" selected>" + String(i) + "</option>";
    } else {
      editUserPage += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
    }
  }
  if (beerCoupons[userToEdit.toInt()].toInt() == 1000) {
    editUserPage += "<option value=\"1000\" selected>Unlimited</option>";
  } else {
    editUserPage += "<option value=\"1000\">Unlimited</option>";
  }

  editUserPage += "\
  </select><br>\
  <label for=\"wineCoupon\">Wine (Coupons):</label><br>\
  <select id=\"wineCoupon\" name=\"wineCoupons\">";

  for (int i = 0; i <= 100; i++) {
    if (i == wineCoupons[userToEdit.toInt()].toInt()) {
      editUserPage += "<option value=\"" + String(i) + "\" selected>" + String(i) + "</option>";
    } else {
      editUserPage += "<option value=\"" + String(i) + "\">" + String(i) + "</option>";
    }
  }
  if (wineCoupons[userToEdit.toInt()].toInt() == 1000) {
    editUserPage += "<option value=\"1000\" selected>Unlimited</option>";
  } else {
    editUserPage += "<option value=\"1000\">Unlimited</option>";
  }

  editUserPage += "\
  </select><br>\
  <label for=\"accStatus\">Account Status:</label><br>\
  <select id=\"accStatus\" name=\"accountStatus\">";

  if (status[userToEdit.toInt()] == "Active") {
    editUserPage += "<option value=\"Active\" selected>Active</option>\
    <option value=\"Inactive\">Inactive</option>";
  } else {
    editUserPage += "<option value=\"Active\">Active</option>\
    <option value=\"Inactive\" selected>Inactive</option>";
  }

  editUserPage += "\
  </select><br><br>\
  <label for=\"beerUses\">Beer Uses:</label><br>\
  <input type=\"text\" id=\"beerUses\" name=\"beerUses\" value=\""
                  + beerUses[userToEdit.toInt()] + "\"><br>\
  <label for=\"wineUses\">Wine Uses:</label><br>\
  <input type=\"text\" id=\"wineUses\" name=\"wineUses\" value=\""
                  + wineUses[userToEdit.toInt()] + "\"><br><br>\
  <input type=\"button\" value=\"Submit\" onclick=\"submitForm()\">\
  <input type=\"button\" value=\"Go Back\" onclick=\"goBack()\">\
</form>\
</body>\
</html>\
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
    window.location.href = '/editUser?userToEdit="
                  + userToEdit + "&userFirstName=' + encodeURIComponent(userFirstName) + '&userLastName=' + encodeURIComponent(userLastName) + '&userUID=' + encodeURIComponent(userUID) + '&userPhone=' + encodeURIComponent(userPhone) + '&accountStatus=' + encodeURIComponent(accountStatus) + '&beerCoupons=' + encodeURIComponent(beerCoupons) + '&wineCoupons=' + encodeURIComponent(wineCoupons) + '&beerUses=' + encodeURIComponent(beerUses) + '&wineUses=' + encodeURIComponent(wineUses);\
}\
function goBack() {\
    window.location.href = '/manageUsers';\
}\
document.getElementById('firstName').focus();\
</script>";
  return editUserPage;
}

String generateCalibratePage(int meterToCalibrate) {
  String calibratePage = "\
<html>\
<body>\
<h2>Calibrate Flow Meter "
                         + String(meterToCalibrate + 1) + "</h2>\
Select START, pump a metered quantity from Choosen tap. Select STOP and enter quantity dispensed.\
<form id=\"calibrateForm\">\
  <button type=\"button\" onclick=\"startCalibration("
                         + meterToCalibrate + ")\" id=\"startButton\">Start Calibration</button>\
  <button type=\"button\" onclick=\"stopCalibration("
                         + meterToCalibrate + ")\" id=\"stopButton\" disabled>Stop Calibration</button>\
</form>\
<script>\
var startButton = document.getElementById('startButton');\
var stopButton = document.getElementById('stopButton');\
\
function startCalibration(meterToCalibrate) {\
  startButton.disabled = true;\
  stopButton.disabled = false;\
  fetch('/startCalibration?meter=' + meterToCalibrate, {\
    method: 'POST'\
  });\
}\
\
function stopCalibration(meterToCalibrate) {\
  var mlpumped = prompt('Enter the amount of liquid pumped (ml):');\
  if (mlpumped !== null) {\
    fetch('/stopCalibration?meter=' + meterToCalibrate + '&mlpumped=' + mlpumped, {\
      method: 'POST'\
    }).then(function(response) {\
      return response.text();\
    }).then(function(data) {\
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
      stopCalibration(" + meterToCalibrate
                         + ");\
    } else {\
      startCalibration(" + meterToCalibrate
                         + ");\
    }\
  }\
});\
</script>\
</body>\
</html>";
  return calibratePage;
}

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
    // Only render the calibration page here. Starting the pump happens only
    // after the operator presses START on the calibration page.
    String calibratePage = generateCalibratePage(meterToCalibrate);
    request->send(200, "text/html", calibratePage);
  } else if (request->method() == HTTP_POST) {
    stopCalibration(request);
  } else {
    request->send(405, "text/plain", "Method Not Allowed");
  }
}

void handleResetAll(AsyncWebServerRequest *request) {
  for (int i = 0; i < 2; i++) {
    resetFlowMeter(i);
  }
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume(AsyncWebServerRequest *request) {
  Serial.println(request->arg("_pumpVolume"));
  int requestedVolume = request->arg("_pumpVolume").toInt();
  if (requestedVolume <= 0) {
    request->send(400, "text/plain", "Pump volume must be greater than 0");
    return;
  }
  pumpVolume = requestedVolume;
  Serial.println(pumpVolume);
  syncAmount = true;
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume2(AsyncWebServerRequest *request) {
  Serial.println(request->arg("_pumpVolume2"));
  int requestedVolume = request->arg("_pumpVolume2").toInt();
  if (requestedVolume <= 0) {
    request->send(400, "text/plain", "Pump volume must be greater than 0");
    return;
  }
  pumpVolume2 = requestedVolume;
  Serial.println(pumpVolume2);
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
  if (valveOpen == true) {
    digitalWrite(valve_pin, LOW);  // Close the valve
  } else {
    digitalWrite(valve_pin, HIGH);  // Open the valve
  }
  request->send(200, "text/html", generateMainPage());
}

void handleToggleValve2(AsyncWebServerRequest *request) {
  valveOpen2 = !valveOpen2;
  if (valveOpen2 == true) {
    digitalWrite(valve_pin2, LOW);  // Close the valve
  } else {
    digitalWrite(valve_pin2, HIGH);  // Open the valve
  }
  request->send(200, "text/html", generateMainPage());
}

void handleTogglePump(AsyncWebServerRequest *request) {
  pumpOn = !pumpOn;
  if (pumpOn == true) {
    digitalWrite(pump_pin, LOW);  // Close the valve
  } else {
    digitalWrite(pump_pin, HIGH);  // Close the valve
  }
  request->send(200, "text/html", generateMainPage());
}

void handleTogglePump2(AsyncWebServerRequest *request) {
  pumpOn2 = !pumpOn2;
  if (pumpOn2 == true) {
    digitalWrite(pump_pin2, LOW);  // Close the valve
  } else {
    digitalWrite(pump_pin2, HIGH);  // Close the valve
  }
  request->send(200, "text/html", generateMainPage());
}

void handleFirebase(String path, String value, AsyncWebServerRequest *request) {
  if (!Firebase.RTDB.setString(&fbdo, path, value)) {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
    request->send(200, "text/plain", "Firebase write failed: " + fbdo.errorReason());
    return;
  }
  Serial.println("PASSED");
  Serial.println("PATH: " + fbdo.dataPath());
  Serial.println("TYPE: " + fbdo.dataType());
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void wled(int red, int green, int blue) {
  HTTPClient http;
  String url = String("http://") + wled_ip + "/json/state";

  // Create JSON data with the specified color
  String jsonData = "{\"on\":true, \"seg\":[{\"start\":0, \"stop\":810, \"col\":[[" + String(red) + "," + String(green) + "," + String(blue) + "],[0,0,0],[0,0,0]]}]}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.println("HTTP Response code: " + String(httpResponseCode));
    Serial.println("Payload: " + payload);
  } else {
    Serial.println("Error on sending POST: " + String(httpResponseCode));
  }

  http.end();
}



void handleNewUser(AsyncWebServerRequest *request) {

  // Read request parameters
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
  handleUserPage(request);  // streams the updated user list back
}


void handleEditUser(AsyncWebServerRequest *request) {
  // Read request parameters
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
  Serial.println("DELETING POUR");
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
  Serial.println("Starting Calibration");
  int meterToCalibrate = request->arg("meter").toInt();
  if (meterToCalibrate < 0 || meterToCalibrate >= 2) {
    request->send(400, "text/plain", "Invalid Flow Meter number");
    return;
  }

  calibratingMeter = meterToCalibrate;
  calibrationStart = *pulses[meterToCalibrate];
  Serial.println(meterToCalibrate);
  calibrationPulses = 0;

  if (meterToCalibrate == 0) {
    digitalWrite(pump_pin, HIGH);
  } else {
    digitalWrite(pump_pin2, HIGH);
  }

  request->send(200, "text/plain", "Calibration started");
}

void stopCalibration(AsyncWebServerRequest *request) {
  digitalWrite(pump_pin, LOW);
  digitalWrite(pump_pin2, LOW);
  if (calibratingMeter != -1) {
    int meterToCalibrate = calibratingMeter;
    calibrationPulses = *pulses[meterToCalibrate];

    // Retrieve the mlpumped value from the POST request
    String mlpumpedStr = request->arg("mlpumped");
    mlpumped = mlpumpedStr.toFloat();

    long pulseDelta = calibrationPulses - calibrationStart;

    if (mlpumped > 0 && pulseDelta > 0) {
      Serial.println(meterToCalibrate + 1);
      Serial.println(calibrationPulses);
      Serial.println(pulseDelta);
      calibrationAmount = mlpumped / pulseDelta;
      flowMeterCalibrations[calibratingMeter] = calibrationAmount;
      Serial.println("Calibration for Flow Meter " + String(meterToCalibrate + 1) + " complete with " + flowMeterCalibrations[calibratingMeter] + " ml/pulse");
      syncCalibration = true;
      request->send(200, "text/html", generateMainPage());
    } else {
      Serial.println("Calibration for Flow Meter " + String(meterToCalibrate + 1) + " aborted");
      request->send(400, "text/plain", "Invalid calibration amount or no pulses detected");
    }
    calibratingMeter = -1;  // Reset calibration state
    calibrationPulses = 0;
    calibrationStart = 0;
  } else {
    request->send(400, "text/plain", "No ongoing calibration");
  }
}

void resetFlowMeter(int meterIndex) {
  switch (meterIndex) {
    case 0:
      pulse1 = 0;
      break;
    case 1:
      pulse2 = 0;
      break;
  }
}

void readCalibrationValues() {
  String path = "calibration";
 
  if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
    if (fbdo.dataType() == "string") {
      String calibrationData = fbdo.stringData();
      int underscoreIndex = calibrationData.indexOf('_');
 
      if (underscoreIndex != -1) {
        String calibration1 = calibrationData.substring(0, underscoreIndex);
        String calibration2 = calibrationData.substring(underscoreIndex + 1);
 
        flowMeterCalibrations[0] = calibration1.toFloat();
        flowMeterCalibrations[1] = calibration2.toFloat();
 
        Serial.println("Read calibration data successfully!");
        Serial.print("Calibration 1: ");
        Serial.println(flowMeterCalibrations[0]);
        Serial.print("Calibration 2: ");
        Serial.println(flowMeterCalibrations[1]);
      } else {
        Serial.println("Error: Calibration data format is incorrect.");
      }
    }
  } else {
    Serial.println("Calibration path not found, creating with defaults: " + fbdo.errorReason());
    String defaultCal = String(flowMeterCalibrations[0]) + "_" + String(flowMeterCalibrations[1]);
    if (Firebase.RTDB.setString(&fbdo, path.c_str(), defaultCal)) {
      Serial.println("Created default calibration node: " + defaultCal);
    } else {
      Serial.println("Failed to create default calibration node: " + fbdo.errorReason());
    }
  }
}

void readFlowNameString() {
  String path = "flowname";
 
  if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
    if (fbdo.dataType() == "string") {
      String flownameData = fbdo.stringData();
      int underscoreIndex = flownameData.indexOf('_');
 
      if (underscoreIndex != -1) {
        String flowname1 = flownameData.substring(0, underscoreIndex);
        String flowname2 = flownameData.substring(underscoreIndex + 1);
 
        flowMeterNames[0] = flowname1;
        flowMeterNames[1] = flowname2;
 
        Serial.println("Read flow name data successfully!");
        Serial.print("Flow Meter Name 1: ");
        Serial.println(flowMeterNames[0]);
        Serial.print("Flow Meter Name 2: ");
        Serial.println(flowMeterNames[1]);
      } else {
        Serial.println("Error: flow name data format is incorrect.");
      }
    }
  } else {
    Serial.println("Flow name path not found, creating with defaults: " + fbdo.errorReason());
    String defaultNames = flowMeterNames[0] + "_" + flowMeterNames[1];
    if (Firebase.RTDB.setString(&fbdo, path.c_str(), defaultNames)) {
      Serial.println("Created default flowname node: " + defaultNames);
    } else {
      Serial.println("Failed to create default flowname node: " + fbdo.errorReason());
    }
  }
}
 
void readAmountValues() {
  String path = "amount";
 
  if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
    if (fbdo.dataType() == "string") {
      String amountData = fbdo.stringData();
      int underscoreIndex = amountData.indexOf('_');
 
      if (underscoreIndex != -1) {
        pumpVolume = amountData.substring(0, underscoreIndex).toInt();
        pumpVolume2 = amountData.substring(underscoreIndex + 1).toInt();
 
        Serial.println("Read Pump Volume data successfully!");
        Serial.print("Pump Volume 1: ");
        Serial.println(pumpVolume);
        Serial.print("Pump Volume 2: ");
        Serial.println(pumpVolume2);
      } else {
        Serial.println("Invalid data format: underscore not found.");
      }
    }
  } else {
    Serial.println("Amount path not found, creating with defaults: " + fbdo.errorReason());
    String defaultAmount = String(pumpVolume) + "_" + String(pumpVolume2);
    if (Firebase.RTDB.setString(&fbdo, path.c_str(), defaultAmount)) {
      Serial.println("Created default amount node: " + defaultAmount);
    } else {
      Serial.println("Failed to create default amount node: " + fbdo.errorReason());
    }
  }
}
 
void readPeopleString() {
  // Read numOfNames from Firebase
  if (Firebase.RTDB.getString(&fbdo, "numOfNames")) {
    if (fbdo.dataType() == "string") {
      numOfNames = fbdo.stringData().toInt();
      Serial.print("Number of Names: ");
      Serial.println(numOfNames);
    }
  } else {
    Serial.println("numOfNames path not found, creating with default 0: " + fbdo.errorReason());
    if (Firebase.RTDB.setString(&fbdo, "numOfNames", String(numOfNames))) {
      Serial.println("Created default numOfNames node: 0");
    } else {
      Serial.println("Failed to create default numOfNames node: " + fbdo.errorReason());
    }
    return; // Nothing else to read yet - no people exist
  }
 
  // Fetch the ENTIRE "people" node in one call
  if (!Firebase.RTDB.getJSON(&fbdo, "people")) {
    Serial.println("People node not found (no users yet): " + fbdo.errorReason());
    return; // Not an error - just means no users have been added yet.
            // The node itself gets created automatically the first time
            // handleNewUser() writes a person to it.
  }
 
  FirebaseJson *json = fbdo.jsonObjectPtr();
  size_t count = json->iteratorBegin();
  Serial.print("People node child count: ");
  Serial.println(count);
 
  int foundNames = 0;
  int maxIndexSeen = 0;
 
  FirebaseJson::IteratorValue value;
  for (size_t i = 0; i < count; i++) {
    value = json->valueAt(i);
    String key = value.key;
    String numPart = key.substring(6); // strip "people"
    int idx = numPart.toInt();
    if (idx <= 0 || idx >= numOfUsers) continue;
 
    String val = value.value;
    if (val.length() >= 2 && val.startsWith("\"") && val.endsWith("\"")) {
      val = val.substring(1, val.length() - 1);
    }
 
    peopleString[foundNames + 1] = val;
    peopleNumber[foundNames] = idx;
    if (idx > maxIndexSeen) maxIndexSeen = idx;
    foundNames++;
 
    if ((i & 0x0F) == 0) {
      vTaskDelay(1);
    }
  }
  json->iteratorEnd();
 
  lastNameRead = maxIndexSeen;
 
  for (int j = 1; j <= foundNames; j++) {
    int underscoreIndex1 = peopleString[j].indexOf('_');
    int underscoreIndex2 = peopleString[j].indexOf('_', underscoreIndex1 + 1);
    int underscoreIndex3 = peopleString[j].indexOf('_', underscoreIndex2 + 1);
    int underscoreIndex4 = peopleString[j].indexOf('_', underscoreIndex3 + 1);
    int underscoreIndex5 = peopleString[j].indexOf('_', underscoreIndex4 + 1);
    int underscoreIndex6 = peopleString[j].indexOf('_', underscoreIndex5 + 1);
    int underscoreIndex7 = peopleString[j].indexOf('_', underscoreIndex6 + 1);
    int underscoreIndex8 = peopleString[j].indexOf('_', underscoreIndex7 + 1);
 
    first[j] = peopleString[j].substring(0, underscoreIndex1);
    last[j] = peopleString[j].substring(underscoreIndex1 + 1, underscoreIndex2);
    uid[j] = peopleString[j].substring(underscoreIndex2 + 1, underscoreIndex3);
    phone[j] = peopleString[j].substring(underscoreIndex3 + 1, underscoreIndex4);
    beerUses[j] = peopleString[j].substring(underscoreIndex4 + 1, underscoreIndex5);
    wineUses[j] = peopleString[j].substring(underscoreIndex5 + 1, underscoreIndex6);
    status[j] = peopleString[j].substring(underscoreIndex6 + 1, underscoreIndex7);
    wineCoupons[j] = peopleString[j].substring(underscoreIndex7 + 1, underscoreIndex8);
    beerCoupons[j] = peopleString[j].substring(underscoreIndex8 + 1);
  }
 
  lastNameRead++;
  Serial.print("Last Name Read Index: ");
  Serial.println(lastNameRead);
}
 
void readPoursString() {
  // Read numOfPours from Firebase
  if (Firebase.RTDB.getString(&fbdo, "numOfPours")) {
    if (fbdo.dataType() == "string") {
      numOfPours = fbdo.stringData().toInt();
      Serial.print("Number of Pours: ");
      Serial.println(numOfPours);
    }
  } else {
    Serial.println("numOfPours path not found, creating with default 0: " + fbdo.errorReason());
    if (Firebase.RTDB.setString(&fbdo, "numOfPours", String(numOfPours))) {
      Serial.println("Created default numOfPours node: 0");
    } else {
      Serial.println("Failed to create default numOfPours node: " + fbdo.errorReason());
    }
    return; // Nothing else to read yet - no pours exist
  }
 
  // Fetch the ENTIRE "pours" node in one call
  if (!Firebase.RTDB.getJSON(&fbdo, "pours")) {
    Serial.println("Pours node not found (no pours yet): " + fbdo.errorReason());
    return; // Not an error - node gets created automatically on the first pour
  }
 
  FirebaseJson *json = fbdo.jsonObjectPtr();
  size_t count = json->iteratorBegin();
  Serial.print("Pours node child count: ");
  Serial.println(count);
 
  int foundPours = 0;
  int maxIndexSeen = 0;
 
  FirebaseJson::IteratorValue value;
  for (size_t i = 0; i < count; i++) {
    value = json->valueAt(i);
    String key = value.key;
    String numPart = key.substring(4); // strip "pour"
    int idx = numPart.toInt();
    if (idx <= 0 || idx >= 200) continue;
 
    String val = value.value;
    if (val.length() >= 2 && val.startsWith("\"") && val.endsWith("\"")) {
      val = val.substring(1, val.length() - 1);
    }
 
    poursString[foundPours + 1] = val;
    pourNumber[foundPours] = idx;
    if (idx > maxIndexSeen) maxIndexSeen = idx;
    foundPours++;
 
    if ((i & 0x0F) == 0) {
      vTaskDelay(1);
    }
  }
  json->iteratorEnd();
 
  lastPourRead = maxIndexSeen;
  lastPourRead++;
  Serial.print("Last Pour Read Index: ");
  Serial.println(lastPourRead);
}

void nfcCode() {
  timePassed = 0;
  uint8_t nfc_uid[7] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  // Reading an NFC card
  uint8_t success = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, nfc_uid, &uidLength, 500);
    if (!success) {
      esp_task_wdt_reset();
      delay(50);
    }
  }

  if (success) {
    // Convert the UID into a string for comparison
    String uidString = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (nfc_uid[i] < 0x10) uidString += "0";  // Append leading zero if necessary
      uidString += String(nfc_uid[i], HEX);
    }

    Serial.print("Found an NFC card with UID: ");
    Serial.println(uidString);

    // Check if the UID matches any of the user UIDs
    bool uidMatchFound = false;
    for (int i = 1; i <= numOfNames; i++) {
      Serial.println("UID Read: " + uidString + " UID Checked: " + uid[i]);
      if (uidString == uid[i]) {
        Serial.println("Person Found: ");
        Serial.println(first[i]);
        personToRead = i;
        // UID match found, print "Bonjour" with the first name
        uidMatchFound = true;

        
        // Determine the appropriate message for beer coupons
        if (beerCoupons[i].toInt() == 1000) {
          consommations = ("CONSOMMATION ILLIMITEE");
        } else if (beerCoupons[i].toInt() == 1) {
          consommations = (beerCoupons[i] + " CONSOMMATION RESTANTE");
        } else if (beerCoupons[i].toInt() > 1) {
          consommations = (beerCoupons[i] + " CONSOMMATIONS RESTANTE");
        } else if (beerCoupons[i].toInt() == 0) {
          consommations = (beerCoupons[i] + " CONSOMMATION RESTANTE");
          for (int i = 0; i < 4000; i += 250) {
            esp_task_wdt_reset();
            delay(250);
          }
          break;
        }
        continue1 = true;
        if (continue1) {
          if (client.connected()) {
              // Send the message, consommations, and flow meter names in one string
              String message = first[i] + "|" + consommations + "|" + flowMeterNames[0] + "|" + flowMeterNames[1];
              client.println(message);
              Serial.println("Sent: " + message);
          } else {
              Serial.println("Disconnected from server, retrying...");
              client.connect(host, port);
          }

          while (true) {
            esp_task_wdt_reset();
            // Check if the button is pressed
            if (digitalRead(buttonResume) == LOW) {
              // Debounce delay
              delay(50);

              // If button is still pressed
              if (digitalRead(buttonResume) == LOW) {
                // Check for hold
                buttonPressTime = millis();
                while (digitalRead(buttonResume) == LOW) {
                  esp_task_wdt_reset(); 
                  if (millis() - buttonPressTime > holdTime) {
                    // Select the current option if button is held for the holdTime duration
                    delay(50); // Additional debounce delay
                    if (true) {

                      readyToPour = true;
                      okToRead = false;
                      return; // Exit the function since an option is selected
                    }
                  }
                  delay(10);
                }

                // If button was not held, toggle selection
                selection = !selection;

                // Update LCD display
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("CHOISIS UN ROBINET");
                lcd.setCursor(0, 1);
                if (selection == 0) {
                  lcd.print("> " + flowMeterNames[0]);

                } else {
                  lcd.print("  " + flowMeterNames[0]);
                }
                lcd.setCursor(0, 2);
                if (selection == 1) {
                  lcd.print("> " + flowMeterNames[1]);
                } else {
                  lcd.print("  " + flowMeterNames[1]);
                }
              }
            }

            if (digitalRead(buttonStop) == LOW) {
              break;
            }
          }
          break;  // Exit the loop since we found a match
        }
      }
    }

    if (!uidMatchFound) {
      // No matching UID found, display the UID on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UID: ");
      lcd.print(uidString);
    }
  }
}

void setPeopleStrings() {
  for (int i = 1; i <= lastNameRead; i++) {
    peopleString[i] = first[i] + "_" + last[i] + "_" + uid[i] + "_" + phone[i] + "_" + beerUses[i] + "_" + wineUses[i] + "_" + status[i] + "_" + wineCoupons[i] + "_" + beerCoupons[i];
    Serial.println(peopleString[i]);
  }
}

TaskHandle_t nfcTaskHandle = NULL;

void nfcTask(void *parameter) {
  esp_task_wdt_add(NULL);
  for (;;) {
    if (nfcWorking && okToRead && timePassed > 1) {
      nfcCode();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Adjust the delay as needed
  }
}

TaskHandle_t writeTaskHandle = NULL;

void writeTask(void *parameter) {
  for (;;) {
    if (Firebase.ready()) {
      if (syncNames > 0) {
        if (Firebase.RTDB.setString(&fbdo, "numOfNames", String(numOfNames))) {
          Serial.println("Written Num of names Data Successfully");
          Serial.println(String(numOfNames));
        } else {
          Serial.println("Error writing data to Firebase" + fbdo.errorReason());
        }
        String path = "people/people" + String(peopleNumber[syncNames - 1]);
        Serial.println("WRITING PERSON DATA TO FIREBASE. PERSON STRING: ");
        Serial.println(peopleString[syncNames]);
        Serial.println("WRITING TO NUMBER: ");
        Serial.println(peopleNumber[syncNames - 1]);
        Serial.println(String(syncNames));

        if (peopleString[syncNames] == "deleted________") {
            if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                Serial.println("Deleted Person Data Successfully");
            } else {
                Serial.println("Error deleting data from Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                vTaskDelay(5000 / portTICK_PERIOD_MS); // FIX: Replace delay() with vTaskDelay inside RTOS Tasks
                if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                    Serial.println("Deleted Person Data Successfully");
                } else {
                    Serial.println("Error deleting data from Firebase after second try: " + fbdo.errorReason());
                }
            }
        } else {
            if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(peopleString[syncNames]))) {
                Serial.println("Written Person Data Successfully");
                Serial.println(String(peopleString[syncNames]));
            } else {
                Serial.println("Error writing data to Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                vTaskDelay(5000 / portTICK_PERIOD_MS); // FIX: Replace delay() with vTaskDelay inside RTOS Tasks
                if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(peopleString[syncNames]))) {
                    Serial.println("Written Person Data Successfully");
                    Serial.println(String(peopleString[syncNames]));
                } else {
                    Serial.println("Error writing data to Firebase after second try: " + fbdo.errorReason());
                }
            }
        }

        syncNames = 0;
      }
      
      if (syncPours > 0) {
        Serial.println("SETTING POUR DATA");
        Serial.println(String(syncPours));
        if (Firebase.RTDB.setString(&fbdo, "numOfPours", String(numOfPours))) {
          Serial.println("Written Num of names Data Successfully");
          Serial.println(String(numOfPours));
        } else {
          Serial.println("Error writing data to Firebase" + fbdo.errorReason());
        }
        int index = syncPours - 1;
        while (index >= 0 && pourNumber[index] == 0) {
            index--;
        }

        int pourValue = (index >= 0) ? pourNumber[index] : 0;
        String path = "pours/pour" + String(pourValue + 1);
        Serial.println(path);  

        Serial.println("WRITING POUR DATA TO FIREBASE. POUR STRING: ");
        Serial.println(String(syncPours));
        Serial.println(poursString[syncPours]);
        Serial.println(pourNumber[syncPours - 1]);

        if (poursString[syncPours] == "DeletePours") {
            if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                Serial.println("Deleted Pour Data Successfully");
            } else {
                Serial.println("Error deleting data from Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                vTaskDelay(5000 / portTICK_PERIOD_MS); // FIX: Replace delay() with vTaskDelay inside RTOS Tasks
                if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                    Serial.println("Deleted Pour Data Successfully");
                } else {
                    Serial.println("Error deleting data from Firebase after second try: " + fbdo.errorReason());
                }
            }
        } else {
            if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(poursString[syncPours]))) {
                Serial.println("Written Pour Data Successfully");
            } else {
                Serial.println("Error writing data to Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                vTaskDelay(5000 / portTICK_PERIOD_MS); // FIX: Replace delay() with vTaskDelay inside RTOS Tasks
                if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(poursString[syncPours]))) {
                    Serial.println("Written Pour Data Successfully");
                } else {
                    Serial.println("Error writing data to Firebase after second try: " + fbdo.errorReason());
                }
            }
        }

        syncPours = 0;
      }

      if (syncCalibration) {
        String calpath = "calibration";
        String calstring = String(flowMeterCalibrations[0]) + "_" + String(flowMeterCalibrations[1]);
        if (Firebase.RTDB.setString(&fbdo, calpath.c_str(), calstring)) {
          Serial.println("Calibration set successfully!");
        } else {
          Serial.println("Error marking user data as deleted in Firebase: " + fbdo.errorReason());
        }
        syncCalibration = false;
      }

      if (syncFlowName) {
        String flownamepath = "flowname";
        String flownamestring = String(flowMeterNames[0]) + "_" + String(flowMeterNames[1]);
        if (Firebase.RTDB.setString(&fbdo, flownamepath.c_str(), flownamestring)) {
          Serial.println("Flow Names set successfully!");
        } else {
          Serial.println("Error marking user data as deleted in Firebase: " + fbdo.errorReason());
        }
        syncFlowName = false;
      }

      if (syncAmount) {
        String amountpath = "amount";
        String amountstring = String(pumpVolume) + "_" + String(pumpVolume2);
        Serial.print("STRING FOR AMOUNT: ");
        Serial.println(amountstring);
        if (Firebase.RTDB.setString(&fbdo, amountpath.c_str(), amountstring)) {
          Serial.println("Pump Volume set successfully!");
        } else {
          Serial.println("Error marking user data as deleted in Firebase: " + fbdo.errorReason());
        }
        syncAmount = false;
      }

      // FIX 1: Slow down the task when Firebase is connected but nothing needs syncing (1-second sleep)
      vTaskDelay(1000 / portTICK_PERIOD_MS); 

    } else {
      Serial.println("Firebase not ready, retrying...");
      vTaskDelay(5000 / portTICK_PERIOD_MS); // FIX 2: Never use delay() inside FreeRTOS tasks. Use vTaskDelay.
    }
  }
}


void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void IRAM_ATTR pulseCounter2() {
  pulseCount2++;
}

void setup() {
  Serial.begin(115200);
  
  // --- Configure Watchdog Timer to prevent boot crashes during initialization ---
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 30000, // Stretch threshold to 30 seconds for heavy string allocations
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
  Serial.println();

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  auth.user.email = "esp@smarttap.com";
  auth.user.password = FIREBASE_PASSWORD;

  // Assign the callback function for token generation status tracking
  config.token_status_callback = tokenStatusCallback; // Uses TokenHelper.h

  // Initialize the connection and handle automatic background login
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Authenticating with Firebase...");
  unsigned long timeout = millis();

  if (Firebase.ready()) {
      Serial.println("Connected to Firebase Successfully via Login!");
      signupOK = true; 
  } else {
      Serial.println("Firebase authentication failed or timed out.");
      // It will still try to connect dynamically in your void loop() via Firebase.ready()
  }

  Serial.println("A: about to Wire.begin"); Serial.flush();
Wire.begin();
Serial.println("B: after Wire.begin, before server routes"); Serial.flush();

// Route for root / web page
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

Serial.println("C: routes registered, before server.begin"); Serial.flush();

server.begin();

Serial.println("D: after server.begin, before pinMode block"); Serial.flush();

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

Serial.println("E: before attachInterrupt"); Serial.flush();
attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseCounter, FALLING);
attachInterrupt(digitalPinToInterrupt(SENSOR_PIN2), pulseCounter2, FALLING);
oldTime = millis();

Serial.println("F: before readCalibrationValues"); Serial.flush();
esp_task_wdt_reset();
readCalibrationValues();
Serial.println("G: after readCalibrationValues, before readAmountValues"); Serial.flush();
esp_task_wdt_reset();
readAmountValues();
Serial.println("H: after readAmountValues, before readPeopleString"); Serial.flush();
esp_task_wdt_reset();
readPeopleString();
Serial.println("I: after readPeopleString, before readPoursString"); Serial.flush();
esp_task_wdt_reset();
readPoursString();
Serial.println("J: after readPoursString, before readFlowNameString"); Serial.flush();
esp_task_wdt_reset();
readFlowNameString();
Serial.println("K: after readFlowNameString"); Serial.flush();
esp_task_wdt_reset();

  // Initialize the NFC module
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.print("Didn't find PN53x board");
    nfcWorking = false;
  } else {
    nfc.SAMConfig();
    Serial.println("Waiting for an NFC card...");
    nfcWorking = true;
  }

  // --- Add short timeout to prevent client.connect from blocking forever if host is offline ---
  client.setTimeout(2); 
  if (!client.connect(host, port)) {
    Serial.println("Connection to server failed");
    // Removed the 'return;' statement so setup can finish initializing tasks even if server is down
  } else {
    Serial.println("Connected to the server");
  }

  wled(0, 25, 255);

  xTaskCreatePinnedToCore(
    nfcTask,         // Task function
    "NFC Task",      // Task name
    4096,            // Stack size
    NULL,            // Parameter
    1,               // Task priority
    &nfcTaskHandle,  // Task handle
    1                // Core (0 or 1) to run the task on
  );

  // Create the Write task
  xTaskCreatePinnedToCore(
    writeTask,         // Task function
    "Write Task",      // Task name
    8192,              // Stack size
    NULL,              // Parameter
    1,                 // Task priority
    &writeTaskHandle,  // Task handle
    0                  // Core (0 or 1) to run the task on
  );

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // --- Restore normal watchdog limit for standard execution ---
  twdt_config.timeout_ms = 15000;
  esp_task_wdt_reconfigure(&twdt_config);
  Serial.println("Setup Complete");
}

void loop() {
  esp_task_wdt_reset();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  pulse1 = pulseCount;
  pulse2 = pulseCount2;
  if (pumpRunning && tap == 0 ) {
    int percentage = (((pulse1 * flowMeterCalibrations[0]) - startPulse) / pumpVolume) * 100;
    String formattedpercentage = String(percentage) + "%";
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    lcd.setCursor(0, 2);
    lcd.print(formattedpercentage);
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
        int beerCouponsValue = beerCoupons[personToRead].toInt();
        Serial.print("beercouponsvalue: ");
        Serial.println(String(beerCouponsValue));
        // Perform the decrement operation
        
        beerCouponsValue--;
        Serial.print("beercouponsvalue: ");
        Serial.println(String(beerCouponsValue));

        // Convert the integer back to string
        beerCoupons[personToRead] = String(beerCouponsValue);
        
      }
      int beerUsesValue = beerUses[personToRead].toInt();
      beerUsesValue++;
      beerUses[personToRead] = String(beerUsesValue);
      Serial.print("Coupons Remaining: ");
      Serial.println(beerCoupons[personToRead]);
      Serial.print("Uses");
      Serial.println(beerUses[personToRead]);
      setPeopleStrings();
      syncNames = personToRead;


      // Format the time as hh/mm
      char timeBuf[6];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

      // Store the formatted time in a String
      String formattedTime = String(timeBuf);

      // Format the date as yyyy/mm/dd
      char dateBuf[11];
      snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", 1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday);

      // Store the formatted date in a String
      String formattedDate = String(dateBuf);

      String formattedPourSave = formattedTime + "_" + formattedDate + "_" + flowMeterNames[0] + "_" + String(amountPoured) + "_" + first[personToRead] + "_" + last[personToRead];
      Serial.print("Formatted Save: ");
      Serial.print(formattedPourSave);
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
    Serial.println((pulse1 * flowMeterCalibrations[0]) - startPulse);
  }

if (pumpRunning && tap == 1) {
    int percentage = (((pulse2 * flowMeterCalibrations[1]) - startPulse) / pumpVolume2) * 100;
    String formattedpercentage = String(percentage) + "%";
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    lcd.setCursor(0, 2);
    lcd.print(formattedpercentage);
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
        int wineCouponsValue = wineCoupons[personToRead].toInt();
        Serial.print("winecouponsvalue: ");
        Serial.println(String(wineCouponsValue));
        // Perform the decrement operation
        wineCouponsValue--;

        Serial.print("winecouponsvalue: ");
        Serial.println(String(wineCouponsValue));
        // Convert the integer back to string
        wineCoupons[personToRead] = String(wineCouponsValue);

      }
      int wineUsesValue = wineUses[personToRead].toInt();
      wineUsesValue++;
      wineUses[personToRead] = String(wineUsesValue);
      Serial.print("Wine Coupons Remaining: ");
      Serial.println(wineCoupons[personToRead]);
      Serial.print("Wine Uses Remaining: ");
      Serial.println(wineUses[personToRead]);
      setPeopleStrings();
      syncNames = personToRead;


      // Format the time as hh/mm
      char timeBuf[6];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

      // Store the formatted time in a String
      String formattedTime = String(timeBuf);

      // Format the date as yyyy/mm/dd
      char dateBuf[11];
      snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", 1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday);

      // Store the formatted date in a String
      String formattedDate = String(dateBuf);

      String formattedPourSave = formattedTime + "_" + formattedDate + "_" + flowMeterNames[1] + "_" + String(amountPoured) + "_" + first[personToRead] + "_" + last[personToRead];
      Serial.print("Formatted Save: ");
      Serial.print(formattedPourSave);
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
    Serial.println((pulse1 * flowMeterCalibrations[0]) - startPulse);
  }

  if (readyToPour == true && digitalRead(buttonResume) == LOW) {
    // Start pouring process
    Serial.print("Selection: ");
    Serial.println(String(selection));
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
    

    // Display "Pouring" on the LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSEMENT");
    lcd.setCursor(0, 1);
    lcd.print("EN COURS");
    readyToPour = false;
    okToRead = true;
  }

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


  Serial.print("Flow Meter 1: ");
  Serial.print(pulse1 * flowMeterCalibrations[0]);
  Serial.println(" ml");

  Serial.print("Flow Meter 2: ");
  Serial.print(pulse2 * flowMeterCalibrations[1]);
  Serial.println(" ml");

  Serial.println();
  Serial.print("Button Status: ");
  if (digitalRead(buttonResume) == LOW) {
    Serial.println("OK Pressed");
  } else {
    Serial.println("OK Not Pressed");
  }
  if (digitalRead(buttonStop) == LOW) {
    Serial.println("Stop Pressed");
  } else {
    Serial.println("Stop Not Pressed");
  }
  Serial.print("Ready to Pour: ");
  Serial.println(readyToPour);
  timePassed++;
  Serial.println(timePassed);

  if (message1 == 0) {
    Serial.println("Ready To Scan!");
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
  delay(250);
  timePassed1++;
}