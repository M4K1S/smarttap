#include <Wire.h>
#include <ESPAsyncWebSrv.h>
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

#include "secrets.h"

AsyncWebServer server(80);

#define PN532_IRQ (5)
#define PN532_RESET (2)
#define I2C_ADDR (0x27)  // Check I2C of your adapter
#define LCD_COLS (20)
#define LCD_ROWS (4)
#define SENSOR_PIN 27
#define SENSOR_PIN2 26

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


String people[numOfUsers];  // Array of names
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
int valve_pin2 = 32;
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
  <style>\
    body {\
      text-align: center;\
      font-family: Arial, sans-serif;\
      font-size: 250%; /* Moderate size increase */\
      margin: 0;\
      padding: 0;\
    }\
    h1 {\
      font-size: 3em;\
    }\
    h2 {\
      font-size: 2.5em;\
    }\
    h3 {\
      font-size: 2em;\
    }\
    h4 {\
      font-size: 1.8em;\
    }\
    button {\
      display: block;\
      width: 80%;\
      max-width: 400px;\
      padding: 25px;\
      margin: 15px auto;\
      font-size: 1.8em;\
      background-color: #808080; /* Gray background */\
      color: white;\
      border: none;\
      border-radius: 5px;\
      cursor: pointer;\
    }\
    button:hover {\
      background-color: #666666;\
    }\
    p {\
      font-size: 2em;\
    }\
  </style>\
</head>\
<body>\
<h1>Wine Machine</h1>\
<h3>v0.50<br></h3>\
<button onclick=\"submitUserInput(10)\">Manage Users</button><br>\
<button onclick=\"submitUserInput(11)\">Previous Pours</button><br>\
<h2>Current totals</h2>\
<h4>" + flowMeterData() + "</h4>\
<h2>" + flowMeterNames[0] + "</h2>\
<button onclick=\"submitUserInput(7)\">Change Pump Volume (Current: " + String(pumpVolume) + ")</button><br>\
<button onclick=\"submitUserInput(1)\">Calibrate Flow Meter</button><br>\
<button onclick=\"submitUserInput(9)\">Rename Flow Meter</button><br>\
<button onclick=\"submitUserInput(5)\">Toggle Valve (Current Status: " + valveStatus + ")</button><br>\
<button onclick=\"submitUserInput(6)\">Toggle Pump (Current Status: " + pumpStatus + ")</button><br>\
<button onclick=\"submitUserInput(8)\">Start Pump</button><br>\
<h2>" + flowMeterNames[1] + "</h2>\
<button onclick=\"submitUserInput(4)\">Change Pump Volume (Current: " + String(pumpVolume2) + ")</button><br>\
<button onclick=\"submitUserInput(13)\">Calibrate Flow Meter</button><br>\
<button onclick=\"submitUserInput(14)\">Rename Flow Meter</button><br>\
<button onclick=\"submitUserInput(15)\">Toggle Valve (Current Status: " + valveStatus2 + ")</button><br>\
<button onclick=\"submitUserInput(16)\">Toggle Pump (Current Status: " + pumpStatus2 + ")</button><br>\
<button onclick=\"submitUserInput(17)\">Start Pump</button><br>\
<p>IP Address: " + WiFi.localIP().toString() + "</p>\
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
document.getElementById('userInput').focus();\
</script>";
  return mainPage;
}



String generateUserPage() {
  String userPage = "\
<html>\
<body>\
<center>\
<h1>Manage Users</h1>\
<h3>v0.01e<br></h3>\
</center>\
<h2>All Users</h2>\
<h3>\
<button onclick=\"submitUserInput(5)\">Go Back To Main Page</button><br>\
</h3>";

  int j = 0;
  for (int i = 1; i <= numOfNames + j; i++) {
    // Check if the user has been deleted or if the first name is null or empty
    if (first[i] != "deleted" && !first[i].isEmpty()) {
      String firstName = first[i];
      String lastName = last[i];
      String nfcUid = uid[i];
      String phoneNum = phone[i];
      String numWineUses = wineUses[i];
      String numBeerUses = beerUses[i];
      String accStatus = status[i];
      String numBeerCoupons = beerCoupons[i];
      String numWineCoupons = wineCoupons[i];
      String numUses = String(numWineUses.toInt() + numBeerUses.toInt());

      userPage += "<b>" + firstName + " " + lastName + "</b>\
            <button onclick=\"submitUserInput(2, "
                  + String(i) + ")\">Edit User</button>\
            <button onclick=\"submitUserInput(7, "
                  + String(i) + ")\">Delete User</button><br>";
      userPage += "<small>NFC UID: " + nfcUid + "<br>";
      userPage += "Phone Number: " + phoneNum + "<br>";
      userPage += "Number of uses: " + numUses + " (Beer Uses: " + numBeerUses + " Wine Uses: " + numWineUses + ")" + "<br>";
      userPage += "Account Status: " + accStatus + "<br>";
      userPage += "Beer Coupons Remaining: " + numBeerCoupons + "<br>";
      userPage += "Wine Coupons Remaining: " + numWineCoupons + "</small><br><br>";
    } else {
      j++;
    }
  }

  userPage += "\
<button onclick=\"submitUserInput(1)\">Add User</button><br>\
</h3>\
<p>IP Address: "
              + WiFi.localIP().toString() + "</p>\
</body>\
</html>\
<script>\
function submitUserInput(option, userIndex) {\
  if (option == 1) {\
    window.location.href = '/newUserPage';\
  } else if (option == 2) {\
    window.location.href = '/editUserPage?userToEdit=' + userIndex;\
  } else if (option == 6) {\
    var couponType = prompt('Type of Coupon (B = Beer, W = Wine)');\
    var numOfCoupons = prompt('Enter Amount of Coupons');\
    window.location.href = '/addCoupons?userToCoupons=' + userIndex + '&couponType=' + encodeURIComponent(couponType) + '&numOfCoupons=' + encodeURIComponent(numOfCoupons);\
  } else if (option == 4) {\
    window.location.href = '/updatePage';\
  } else if (option == 5) {\
    window.location.href = '/';\
  } else if (option == 7) {\
    window.location.href = '/deleteUser?userToDelete=' + userIndex;\
  }\
}\
document.getElementById('userInput').focus();\
</script>";
  return userPage;
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
document.getElementById('userInput').focus();\
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

void handleUserPage(AsyncWebServerRequest *request) {
  request->send(200, "text/html", generateUserPage());
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
  request->send(200, "text/html", generateUserPage());
  readyToRefresh = true;
}

void handleCalibrateMeter(AsyncWebServerRequest *request) {
  int meterToCalibrate = request->arg("meter").toInt();
  if (request->method() == HTTP_GET) {
    startCalibration(request);
    String calibratePage = generateCalibratePage(meterToCalibrate);
    request->send(200, "text/html", calibratePage);
  } else if (request->method() == HTTP_POST) {
    float mlpumped = request->arg("mlpumped").toFloat();
    stopCalibration(request);
  } else {
    request->send(405, "text/plain", "Method Not Allowed");
  }
}

void handleResetAll(AsyncWebServerRequest *request) {
  for (int i = 0; i < 8; i++) {
    resetFlowMeter(i);
  }
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume(AsyncWebServerRequest *request) {
  Serial.println(request->arg("_pumpVolume"));
  pumpVolume = request->arg("_pumpVolume").toInt();
  Serial.println(pumpVolume);
  syncAmount = true;
  request->send(200, "text/html", generateMainPage());
}

void handlepumpVolume2(AsyncWebServerRequest *request) {
  Serial.println(request->arg("_pumpVolume2"));
  pumpVolume2 = request->arg("_pumpVolume2").toInt();
  Serial.println(pumpVolume2);
  syncAmount = true;
  request->send(200, "text/html", generateMainPage());
}

void handleFlowMeterRename(AsyncWebServerRequest *request) {
  String meterToRename = request->arg("renameMeter");
  String newName = request->arg("newName");
  int meterIndex = meterToRename.toInt() - 1;

  if (meterIndex >= 0 && meterIndex < 8) {
    flowMeterNames[meterIndex] = newName;

  } else {
    request->send(400, "text/plain", "Invalid Flow Meter number");
  }
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
    request->send(200, "text/html", generateUserPage());
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
  request->send(200, "text/html", generateUserPage());
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

  first[userToEdit.toInt()] = userFirstName;
  last[userToEdit.toInt()] = userLastName;
  uid[userToEdit.toInt()] = userUID;
  phone[userToEdit.toInt()] = userPhone;
  status[userToEdit.toInt()] = accountStatus;
  beerCoupons[userToEdit.toInt()] = beerCouponsEdit;
  wineCoupons[userToEdit.toInt()] = wineCouponsEdit;
  beerUses[userToEdit.toInt()] = beerUsesEdit;
  wineUses[userToEdit.toInt()] = wineUsesEdit;

  setPeopleStrings();
  syncNames = userToEdit.toInt();

  request->send(200, "text/html", generateUserPage());
}

void handleDeleteUser(AsyncWebServerRequest *request) {
  userToDelete = request->arg("userToDelete");
  first[userToDelete.toInt()] = "deleted";
  last[userToDelete.toInt()] = "";
  uid[userToDelete.toInt()] = "";
  phone[userToDelete.toInt()] = "";
  status[userToDelete.toInt()] = "";
  beerCoupons[userToDelete.toInt()] = "";
  wineCoupons[userToDelete.toInt()] = "";
  beerUses[userToDelete.toInt()] = "";
  wineUses[userToDelete.toInt()] = "";
  setPeopleStrings();
  syncNames = userToDelete.toInt();
  numOfNames--;
  request->send(200, "text/html", generateUserPage());
}

void handleDeletePour(AsyncWebServerRequest *request) {
  Serial.println("DELETING USER");
  pourToDelete = request->arg("pourToDelete");
  poursString[pourToDelete.toInt()] = "DeletePours";
  syncPours = pourToDelete.toInt();
  numOfPours--;
  request->send(200, "text/html", generatePoursPage());
}
void startCalibration(AsyncWebServerRequest *request) {
  Serial.println("Starting Calibration");
  int meterToCalibrate = request->arg("meter").toInt();
  calibratingMeter = meterToCalibrate;
  calibrationStart = *pulses[meterToCalibrate];
  Serial.println(meterToCalibrate);
  calibrationPulses = 0;
  if (meterToCalibrate == 0){
  digitalWrite(pump_pin, HIGH);
  } else {
  digitalWrite(pump_pin2, HIGH);
  }
  String calibratePage = generateCalibratePage(meterToCalibrate);
  request->send(200, "text/html", calibratePage);
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

    if (mlpumped > 0) {
      Serial.println(meterToCalibrate + 1);
      Serial.println(calibrationPulses);
      Serial.println(calibrationPulses - calibrationStart);
      calibrationAmount = mlpumped / (calibrationPulses - calibrationStart);
      flowMeterCalibrations[calibratingMeter] = calibrationAmount;
      Serial.println("Calibration for Flow Meter " + String(meterToCalibrate + 1) + " complete with " + flowMeterCalibrations[calibratingMeter] + " pulse/ml");
      syncCalibration = true;
      request->send(200, "text/html", generateMainPage());
    } else {
      Serial.println("Calibration for Flow Meter " + String(meterToCalibrate + 1) + " aborted");
      request->send(200, "text/html", generateMainPage());
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
    Serial.println("Error reading data from Firebase: " + fbdo.errorReason());
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
    Serial.println("Error reading data from Firebase: " + fbdo.errorReason());
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
    Serial.println("Error reading data from Firebase: " + fbdo.errorReason());
  }
}

void readPeopleString() {
    // Read numOfNames from Firebase
    if (Firebase.RTDB.getString(&fbdo, "numOfNames")) {
        if (fbdo.dataType() == "string") {
            numOfNames = fbdo.stringData().toInt();  // Convert the string to an integer
            Serial.print("Number of Names: ");
            Serial.println(numOfNames);
        }
    } else {
        // If there's an error reading numOfNames, print the error and return
        Serial.println("Error reading numOfNames from Firebase: " + fbdo.errorReason());
        return;
    }

    int foundNames = 0;
    int i = 1;

    while (foundNames < numOfNames) {
        String path = "people/people" + String(i);

        if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
            if (fbdo.dataType() == "string") {
                peopleString[foundNames + 1] = fbdo.stringData();
                Serial.print("Person: ");
                Serial.println(peopleString[foundNames + 1]);
                peopleNumber[foundNames] = i;
                Serial.println(peopleNumber[foundNames]);
                foundNames++;
                lastNameRead = i;  // Update lastNameRead to the current index
            }
        } else {
            // Print error but continue to the next index
            Serial.println("Error reading data from Firebase: " + fbdo.errorReason());
        }

        i++;
    }

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
            numOfPours = fbdo.stringData().toInt();  // Convert the string to an integer
            Serial.print("Number of Pours: ");
            Serial.println(numOfPours);
        }
    } else {
        // If there's an error reading numOfPours, print the error and return
        Serial.println("Error reading numOfPours from Firebase: " + fbdo.errorReason());
        return;
    }

    int foundPours = 0;
    int i = 1;

    while (foundPours < numOfPours) {
        String path = "pours/pour" + String(i);

        if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
            if (fbdo.dataType() == "string") {
                poursString[foundPours + 1] = fbdo.stringData();
                Serial.print("Pour: ");
                Serial.println(poursString[foundPours + 1]);
                pourNumber[foundPours] = i;
                Serial.println(pourNumber[foundPours]);
                foundPours++;
                lastPourRead = i;  // Update lastPourRead to the current index
            }
        } else {
            // Print error but continue to the next index
            Serial.println("Error reading data from Firebase: " + fbdo.errorReason());
        }

        i++;
    }
    lastPourRead++;
    Serial.print("Last Pour Read Index: ");
    Serial.println(lastPourRead);
}


void nfcCode() {
  timePassed = 0;
  uint8_t success;
  uint8_t nfc_uid[7] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  // Reading an NFC card
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, nfc_uid, &uidLength);

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
          delay(4000);
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
            // Check if the button is pressed
            if (digitalRead(buttonResume) == LOW) {
              // Debounce delay
              delay(50);

              // If button is still pressed
              if (digitalRead(buttonResume) == LOW) {
                // Check for hold
                buttonPressTime = millis();
                while (digitalRead(buttonResume) == LOW) {
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
            // Deleting the data from Firebase
            if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                Serial.println("Deleted Person Data Successfully");
            } else {
                Serial.println("Error deleting data from Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                delay(5000);
                if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                    Serial.println("Deleted Person Data Successfully");
                } else {
                    Serial.println("Error deleting data from Firebase after second try: " + fbdo.errorReason());
                }
            }
        } else {
            // Writing the data to Firebase
            if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(peopleString[syncNames]))) {
                Serial.println("Written Person Data Successfully");
                Serial.println(String(peopleString[syncNames]));
            } else {
                Serial.println("Error writing data to Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                delay(5000);
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
        Serial.println(path);  // To verify the output

          Serial.println("WRITING POUR DATA TO FIREBASE. POUR STRING: ");
          Serial.println(String(syncPours));
          Serial.println(poursString[syncPours]);
          Serial.println(pourNumber[syncPours - 1]);

          if (poursString[syncPours] == "DeletePours") {
              // Deleting the data from Firebase
              if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                  Serial.println("Deleted Pour Data Successfully");
              } else {
                  Serial.println("Error deleting data from Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                  delay(5000);
                  if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
                      Serial.println("Deleted Pour Data Successfully");
                  } else {
                      Serial.println("Error deleting data from Firebase after second try: " + fbdo.errorReason());
                  }
              }
          } else {
              // Writing the data to Firebase
              if (Firebase.RTDB.setString(&fbdo, path.c_str(), String(poursString[syncPours]))) {
                  Serial.println("Written Pour Data Successfully");
              } else {
                  Serial.println("Error writing data to Firebase, trying again in 5 seconds: " + fbdo.errorReason());
                  delay(5000);
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
        // Update the user data in Firebase by marking it as deleted
        if (Firebase.RTDB.setString(&fbdo, calpath.c_str(), flowMeterCalibrations[0])) {
          Serial.println("Calibration set successfully!");
        } else {
          Serial.println("Error marking user data as deleted in Firebase: " + fbdo.errorReason());
        }
        syncCalibration = false;
      }

      if (syncFlowName) {
        String flownamepath = "flowname";
        String flownamestring = String(flowMeterNames[0]) + "_" + String(flowMeterNames[1]);
        // Update the user data in Firebase by marking it as deleted
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
        // Update the user data in Firebase by marking it as deleted
        if (Firebase.RTDB.setString(&fbdo, amountpath.c_str(), amountstring)) {
          Serial.println("Pump Volume set successfully!");
        } else {
          Serial.println("Error marking user data as deleted in Firebase: " + fbdo.errorReason());
        }
        syncAmount = false;
      }
    } else {
      Serial.println("Firebase not ready, retrying...");
      delay(5000);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Adjust the delay as needed
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

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Connected to Firebase Successfully");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback;  //see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Wire.begin();

  // Route for root / web page
  server.on("/", HTTP_GET, handleMainPage);
  server.on("/calibrateMeter", HTTP_GET, handleCalibrateMeter);
  server.on("/startCalibration", HTTP_GET, startCalibration);
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

  // Start server
  server.begin();

  pinMode(valve_pin, OUTPUT);
  digitalWrite(valve_pin, LOW);  // Initially, the valve is closed
  pinMode(pump_pin, OUTPUT);
  digitalWrite(pump_pin, LOW);  // Initially, the valve is closed
    pinMode(valve_pin2, OUTPUT);
  digitalWrite(valve_pin2, LOW);  // Initially, the valve is closed
  pinMode(pump_pin2, OUTPUT);
  digitalWrite(pump_pin2, LOW);  // Initially, the valve is closed
  pinMode(buttonResume, INPUT_PULLUP);
  pinMode(buttonStop, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseCounter, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN2), pulseCounter2, FALLING);
  // Initialize the time variable
  oldTime = millis();

  readCalibrationValues();
  readAmountValues();
  readPeopleString();
  readPoursString();
  readFlowNameString();

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

  if (!client.connect(host, port)) {
    Serial.println("Connection to server failed");
    return;
  }

  Serial.println("Connected to the server");

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
    1                  // Core (0 or 1) to run the task on
  );

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
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
      delay(4000);
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
      int wineUsesValue = wineUses[personToRead].toInt();
      wineUsesValue++;
      wineUses[personToRead] = String(wineUsesValue);
      Serial.print("Coupons Remaining: ");
      Serial.println(beerCoupons[personToRead]);
      Serial.print("Wine Uses Remaining: ");
      Serial.println(wineUses[personToRead]);


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
      poursString[numOfPours] = formattedPourSave;
      syncPours = numOfPours;
      delay(4000);
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