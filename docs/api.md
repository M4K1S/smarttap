# SmartTap — Web / Local API Notes

All ESP32 endpoints are served over local HTTP on port `80`.

```text
http://<ESP32-IP>/
```

These endpoints are intended for trusted local networks only.

---

## Main Pages

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/` | Main admin/control page |
| `GET` | `/manageUsers` | User management page |
| `GET` | `/pours` | Pour history page |
| `GET` | `/newUserPage` | New user form |
| `GET` | `/editUserPage?userToEdit=<index>` | Edit user form |
| `GET` | `/updatePage` | Refresh user page data |

---

## User Management

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/newUser?userFirstName=<first>&userLastName=<last>&userUID=<uid>&userPhone=<phone>&accountStatus=<status>&beerCoupons=<n>&wineCoupons=<n>` | Add a new NFC user |
| `GET` | `/editUser?userToEdit=<index>&userFirstName=<first>&userLastName=<last>&userUID=<uid>&userPhone=<phone>&accountStatus=<status>&beerCoupons=<n>&wineCoupons=<n>&beerUses=<n>&wineUses=<n>` | Edit an existing user |
| `GET` | `/deleteUser?userToDelete=<index>` | Mark a user as deleted and sync the deletion |

---

## Pour History

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/deletePour?pourToDelete=<index>` | Mark a pour record for deletion |

---

## Flow Meter Calibration

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/calibrateMeter?meter=<0-1>` | Open calibration page for tap 1 or tap 2 |
| `POST` | `/startCalibration?meter=<0-1>` | Start a calibration run and turn on the selected pump |
| `POST` | `/stopCalibration?meter=<0-1>&mlpumped=<volume>` | Stop calibration, calculate mL-per-pulse factor, and sync it to Firebase |

---

## Tap Configuration

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/pumpVolume?_pumpVolume=<mL>` | Set tap 1 target pour volume |
| `GET` | `/pumpVolume2?_pumpVolume2=<mL>` | Set tap 2 target pour volume |
| `GET` | `/rename?renameMeter=<1-2>&newName=<name>` | Rename tap 1 or tap 2 |

---

## Manual Hardware Control

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/toggleValve` | Toggle tap 1 valve output |
| `GET` | `/togglePump` | Toggle tap 1 pump output |
| `GET` | `/startPump` | Start tap 1 pump manually |
| `GET` | `/toggleValve2` | Toggle tap 2 valve output |
| `GET` | `/togglePump2` | Toggle tap 2 pump output |
| `GET` | `/startPump2` | Start tap 2 pump manually |
| `GET` | `/resetAll` | Reset both flow pulse counters |

---

## Python Dashboard TCP Payload

The ESP32 sends live status to the Python dashboard over TCP port `12345`.

Expected message format:

```text
firstName|consommations|flowMeter1Name|flowMeter2Name
```

Example:

```text
Maxim|2 CONSOMMATIONS RESTANTE|Beer|Wine
```
