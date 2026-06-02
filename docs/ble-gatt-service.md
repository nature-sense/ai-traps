# BLE GATT Service — AI Camera Trap Provisioning

## Overview

This document defines the **Bluetooth Low Energy (BLE) GATT service** for the AI Camera Trap. The service enables a mobile client (e.g., the Flutter management app) to:

1. **Discover** the trap via BLE advertisement (including its identity)
2. **Read** the current WiFi state (off / station mode / access-point mode)
3. **Provision** the trap to connect to a WiFi network as a station (SSID + password)
4. **Provision** the trap to operate as a WiFi access point (SSID + password)

The BLE GATT service is implemented by a standalone Ramen actor (`BleGattActor`) that runs independently of the main detection pipeline. It does **not** connect to any other actors — it is designed for isolated testing.

---

## Service UUID

| Field | Value |
|-------|-------|
| **Service Name** | AI Camera Trap Provisioning Service |
| **Service UUID** | `A1C0 0001-####-####-####-#############` (16-bit UUID allocated from the vendor range) |
| **Short UUID** | `0x0001` (when using a custom 128-bit base UUID) |

The full 128-bit UUID is:

```
A1C00001-0001-4A54-8000-0A4D4943414D
```

> **Note:** The base UUID `A1C00000-0001-4A54-8000-0A4D4943414D` is used for all characteristics in this service, with the 16-bit characteristic UUID substituted into the first two bytes (e.g., `A1C0XXXX-0001-4A54-8000-0A4D4943414D`).

---

## Advertisement Data Format

The trap continuously advertises its presence using **BLE Advertising** (non-connectable scannable advertisement, or connectable advertisement when GATT is active). The advertisement payload contains:

### Advertising Flags

| Byte Offset | Field | Value | Description |
|-------------|-------|-------|-------------|
| 0 | AD Length | `0x02` | 2 bytes follow |
| 1 | AD Type | `0x01` | Flags |
| 2 | Flags | `0x06` | LE General Discoverable + BR/EDR Not Supported |

### Manufacturer Specific Data (Company ID: Nature Sense / 0xFFFF for development)

| Byte Offset | Field | Value | Description |
|-------------|-------|-------|-------------|
| 0 | AD Length | `0x??` | Length varies |
| 1 | AD Type | `0xFF` | Manufacturer Specific Data |
| 2-3 | Company ID | `0xFFFF` | Development / vendor-assigned company ID |
| 4 | Protocol Version | `0x01` | Version of this advertisement format |
| 5-6 | Service UUID | `0x0001` | Short UUID of the provisioning service |
| 7-22 | Trap ID | 16 bytes | Unique trap identifier (ASCII string, null-padded) |
| 23 | WiFi State | `0x00`–`0x02` | Current WiFi state (see §WiFi State characteristic) |

**Total advertisement payload:** ~24 bytes (excluding AD structure overhead).

### Example Advertisement (hex dump)

```
02 01 06 15 FF FF FF 01 00 01 74 72 61 70 2D 73 69 6E 67 61 70 6F 72 65 2D 30 31 00 00 00 00 00 00 00
```

Decoded:
- `02 01 06` — Flags: LE General Discoverable
- `15 FF FF FF 01 00 01` — Manufacturer data: Company 0xFFFF, Proto v1, Service 0x0001
- `74 72 61 70 2D 73 69 6E 67 61 70 6F 72 65 2D 30 31` — "trap-singapore-01"
- `00 00 00 00 00 00 00` — WiFi State = 0x00 (off)

---

## GATT Characteristics

All characteristics use the **128-bit base UUID** `A1C00000-0001-4A54-8000-0A4D4943414D` with the 16-bit characteristic UUID substituted.

### Characteristic Table

| # | Name | UUID (16-bit) | Properties | Permissions | Value Size | Description |
|---|------|---------------|------------|-------------|------------|-------------|
| 1 | **Trap Identity** | `0x0002` | Read | Open (no auth) | 16 bytes | ASCII trap ID, null-padded |
| 2 | **WiFi State** | `0x0003` | Read, Notify | Open (no auth) | 1 byte | Current WiFi mode |
| 3 | **WiFi Provision Station** | `0x0004` | Write | Encrypted (MITM) | ≤ 66 bytes | SSID + password for station mode |
| 4 | **WiFi Provision AP** | `0x0005` | Write | Encrypted (MITM) | ≤ 66 bytes | SSID + password for AP mode |
| 5 | **Command Response** | `0x0006` | Notify | Open (no auth) | 1–20 bytes | Acknowledgment / error response |

---

### Characteristic 1: Trap Identity (UUID `0x0002`)

**Properties:** Read  
**Permissions:** Open (no authentication required)  
**Value Size:** 16 bytes (fixed)

This characteristic exposes the trap's unique identity. It is readable by any BLE client without pairing, allowing discovery apps to identify the trap before connecting.

#### Value Encoding

| Byte Offset | Field | Type | Description |
|-------------|-------|------|-------------|
| 0–15 | Trap ID | UTF-8 string | 16-byte ASCII trap identifier, null-padded on the right |

The trap ID is a human-readable string (e.g., `"trap-singapore-01"`, `"trap-forest-07"`). If the ID is shorter than 16 bytes, the remaining bytes are filled with `0x00`.

#### Example

```
74 72 61 70 2D 73 69 6E 67 61 70 6F 72 65 2D 30 31 00 00 00 00 00 00 00
```
→ `"trap-singapore-01"` (16 bytes, null-padded)

---

### Characteristic 2: WiFi State (UUID `0x0003`)

**Properties:** Read, Notify  
**Permissions:** Open (no authentication required)  
**Value Size:** 1 byte

This characteristic reports the current WiFi state of the trap. The client can read it at any time, and the trap will send notifications when the state changes.

#### Value Encoding

| Value | Name | Description |
|-------|------|-------------|
| `0x00` | WIFI_OFF | WiFi is disabled (default for battery/solar mode) |
| `0x01` | WIFI_STATION | WiFi is enabled in station mode (connected to a network) |
| `0x02` | WIFI_AP | WiFi is enabled in access-point mode |
| `0x03` | WIFI_STATION_CONNECTING | WiFi station mode is attempting to connect |
| `0x04` | WIFI_STATION_FAILED | WiFi station mode failed to connect |
| `0x05`–`0xFF` | Reserved | Reserved for future use |

#### Notification Behavior

The trap sends a notification on this characteristic whenever the WiFi state changes:
- After a successful station connection (`0x01`)
- After a failed station connection attempt (`0x04`)
- After AP mode is activated (`0x02`)
- After WiFi is turned off (`0x00`)
- During connection attempts (`0x03`)

---

### Characteristic 3: WiFi Provision Station (UUID `0x0004`)

**Properties:** Write  
**Permissions:** Encrypted (MITM protection required — BLE pairing with numeric comparison or passkey)  
**Value Size:** 2–66 bytes (variable)

Writing to this characteristic instructs the trap to configure its WiFi in **station mode** and attempt to connect to the specified network.

#### Value Encoding

| Byte Offset | Field | Type | Description |
|-------------|-------|------|-------------|
| 0 | SSID Length | uint8 | Length of the SSID in bytes (1–32) |
| 1..N | SSID | UTF-8 string | WiFi network SSID (N = SSID Length) |
| N+1 | Password Length | uint8 | Length of the password in bytes (0–32; 0 = open network) |
| N+2..M | Password | UTF-8 string | WiFi password (M = N+1+Password Length) |

**Maximum value size:** 1 + 32 + 1 + 32 = 66 bytes.

#### Write Procedure

1. Client writes the encoded SSID + password to this characteristic.
2. The trap immediately writes a **Command Response** notification (Characteristic 5) with status `0x01` (Accepted).
3. The trap attempts to configure WiFi and connect.
4. On success: WiFi State changes to `WIFI_STATION` (`0x01`) with a notification.
5. On failure: WiFi State changes to `WIFI_STATION_FAILED` (`0x04`) with a notification.

#### Example Write

To connect to SSID `"HomeNet"` with password `"secret123"`:

```
08 48 6F 6D 65 4E 65 74 09 73 65 63 72 65 74 31 32 33
```

Decoded:
- `08` — SSID length = 8
- `48 6F 6D 65 4E 65 74` — "HomeNet"
- `09` — Password length = 9
- `73 65 63 72 65 74 31 32 33` — "secret123"

---

### Characteristic 4: WiFi Provision AP (UUID `0x0005`)

**Properties:** Write  
**Permissions:** Encrypted (MITM protection required — BLE pairing with numeric comparison or passkey)  
**Value Size:** 2–66 bytes (variable)

Writing to this characteristic instructs the trap to start its WiFi interface in **access-point (AP) mode** with the specified SSID and password.

#### Value Encoding

| Byte Offset | Field | Type | Description |
|-------------|-------|------|-------------|
| 0 | SSID Length | uint8 | Length of the AP SSID in bytes (1–32) |
| 1..N | SSID | UTF-8 string | AP network SSID (N = SSID Length) |
| N+1 | Password Length | uint8 | Length of the AP password in bytes (8–32; 0 = open AP) |
| N+2..M | Password | UTF-8 string | AP password (M = N+1+Password Length) |

**Maximum value size:** 1 + 32 + 1 + 32 = 66 bytes.

#### Write Procedure

1. Client writes the encoded SSID + password to this characteristic.
2. The trap immediately writes a **Command Response** notification (Characteristic 5) with status `0x01` (Accepted).
3. The trap configures WiFi in AP mode.
4. On success: WiFi State changes to `WIFI_AP` (`0x02`) with a notification.
5. On failure: WiFi State changes to `WIFI_OFF` (`0x00`) with a notification, and the Command Response characteristic sends an error code.

---

### Characteristic 5: Command Response (UUID `0x0006`)

**Properties:** Notify  
**Permissions:** Open (no authentication required)  
**Value Size:** 1–20 bytes (variable)

This characteristic is used by the trap to send acknowledgment and error responses to the client after a write operation. The client must enable notifications on this characteristic to receive responses.

#### Value Encoding

| Byte Offset | Field | Type | Description |
|-------------|-------|------|-------------|
| 0 | Status | uint8 | Response status code |
| 1..N | Message | UTF-8 string | Optional human-readable message (0–19 bytes) |

#### Status Codes

| Code | Name | Description |
|------|------|-------------|
| `0x00` | SUCCESS | Operation completed successfully |
| `0x01` | ACCEPTED | Operation accepted, processing (async) |
| `0x02` | INVALID_FORMAT | Malformed write value |
| `0x03` | SSID_TOO_LONG | SSID exceeds 32 bytes |
| `0x04` | PASSWORD_TOO_LONG | Password exceeds 32 bytes |
| `0x05` | CONFIG_FAILED | WiFi configuration failed (driver error) |
| `0x06` | CONNECTION_FAILED | WiFi connection attempt failed |
| `0x07` | INTERNAL_ERROR | Internal actor error |
| `0x08`–`0xFF` | Reserved | Reserved for future use |

#### Response Flow

```
Client                          Trap
  │                               │
  │── Write(WiFiProvisionStation)─>│
  │                               │
  │<── Notify(CommandResponse:    │
  │     0x01, "Accepted")         │  ← Immediate acknowledgment
  │                               │
  │        ... (WiFi configures)  │
  │                               │
  │<── Notify(WiFiState: 0x01)    │  ← State change notification
  │                               │
  │<── Notify(CommandResponse:    │
  │     0x00, "Connected")        │  ← Final success response
```

---

## Security Model

### Advertisement & Discovery

- **Trap Identity** and **WiFi State** characteristics are readable without pairing. This allows any BLE scanner to discover traps and read their identity/state without authentication.
- The advertisement payload includes the trap ID and WiFi state, so even passive scanning reveals basic information.

### Provisioning (Write Characteristics)

- **WiFi Provision Station** (0x0004) and **WiFi Provision AP** (0x0005) require **encrypted BLE connections with MITM protection**.
- The trap must initiate **BLE pairing** with the client before accepting writes to these characteristics.
- Recommended pairing method: **Numeric Comparison** (Just Works is acceptable for development, but Numeric Comparison or Passkey Entry is preferred for production).
- Pairing is **bonded** — once paired, the client can reconnect without re-pairing.

### Why This Matters

Since the trap operates in remote locations with WiFi off by default, the BLE interface is the **only** way to configure it. Without encryption, an attacker within BLE range could:
- Reprovision the trap to connect to a malicious network
- Read the WiFi credentials of the configured network

---

## Client Integration Guide

### Discovery Flow

1. **Scan for BLE devices** advertising the service UUID `A1C00001-0001-4A54-8000-0A4D4943414D` (or the 16-bit short UUID `0x0001`).
2. **Parse manufacturer-specific data** in the advertisement to extract the trap ID and WiFi state without connecting.
3. **Optionally connect** to read the full Trap Identity characteristic.

### Provisioning Flow (Station Mode)

```
1. Scan → discover trap with ID "trap-singapore-01"
2. Connect to trap
3. Pair (Numeric Comparison) → encrypted connection established
4. Enable notifications on Command Response (0x0006)
5. Enable notifications on WiFi State (0x0003)
6. Write SSID + password to WiFi Provision Station (0x0004)
7. Receive Command Response notification: ACCEPTED
8. Wait for WiFi State notification: WIFI_STATION (0x01)
9. Optionally disconnect from BLE — trap is now on WiFi
```

### Provisioning Flow (AP Mode)

```
1. Scan → discover trap with ID "trap-forest-07"
2. Connect to trap
3. Pair (Numeric Comparison) → encrypted connection established
4. Enable notifications on Command Response (0x0006)
5. Enable notifications on WiFi State (0x0003)
6. Write SSID + password to WiFi Provision AP (0x0005)
7. Receive Command Response notification: ACCEPTED
8. Wait for WiFi State notification: WIFI_AP (0x02)
9. Disconnect from BLE
10. Connect your phone/computer to the trap's WiFi AP
```

### Flutter (Dart) Example

```dart
// Using the flutter_blue_plus package
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

// Service & Characteristic UUIDs
const String serviceUuid = 'A1C00001-0001-4A54-8000-0A4D4943414D';
const String trapIdCharUuid = 'A1C00002-0001-4A54-8000-0A4D4943414D';
const String wifiStateCharUuid = 'A1C00003-0001-4A54-8000-0A4D4943414D';
const String wifiProvStationCharUuid = 'A1C00004-0001-4A54-8000-0A4D4943414D';
const String wifiProvApCharUuid = 'A1C00005-0001-4A54-8000-0A4D4943414D';
const String cmdResponseCharUuid = 'A1C00006-0001-4A54-8000-0A4D4943414D';

// Read trap identity
Future<String> readTrapId(BluetoothDevice device) async {
  final services = await device.discoverServices();
  final service = services.firstWhere((s) => s.uuid.toString() == serviceUuid);
  final char = service.characteristics.firstWhere(
    (c) => c.uuid.toString() == trapIdCharUuid,
  );
  final value = await char.read();
  return String.fromCharCodes(value).replaceAll('\x00', '');
}

// Provision WiFi (station mode)
Future<void> provisionWifiStation(
  BluetoothDevice device,
  String ssid,
  String password,
) async {
  final services = await device.discoverServices();
  final service = services.firstWhere((s) => s.uuid.toString() == serviceUuid);

  // Enable notifications
  final cmdResp = service.characteristics.firstWhere(
    (c) => c.uuid.toString() == cmdResponseCharUuid,
  );
  await cmdResp.setNotifyValue(true);
  cmdResp.onValueReceived.listen((value) {
    final status = value[0];
    final message = String.fromCharCodes(value.sublist(1));
    print('Response: status=$status message=$message');
  });

  // Encode SSID + password
  final data = BytesBuilder()
    ..addByte(ssid.length)
    ..add(utf8.encode(ssid))
    ..addByte(password.length)
    ..add(utf8.encode(password));

  // Write to provision characteristic
  final provChar = service.characteristics.firstWhere(
    (c) => c.uuid.toString() == wifiProvStationCharUuid,
  );
  await provChar.write(data.toBytes());
}
```

---

## References

- **BLE Core Specification 5.x** — Bluetooth SIG
- **BlueZ D-Bus GATT API** — `org.bluez.GattManager1`, `org.bluez.GattService1`, `org.bluez.GattCharacteristic1`
- **Flutter Blue Plus** — `flutter_blue_plus` package for Flutter BLE integration
- **Ramen Actor Framework** — `traps/toolkit/src/actors/ramen.hpp`
- **EventPublisherActor** — Reference actor implementation at `traps/toolkit/src/actors/event-publisher/`
