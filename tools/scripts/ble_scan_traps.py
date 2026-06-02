#!/usr/bin/env python3
"""
Scan for AI Camera Traps via BLE advertisements.
Uses the same manufacturer data parsing as trap-mcp-server.py.
"""
import asyncio
import struct
import json
from bleak import BleakScanner

MANUFACTURER_ID = 0xFFFF
BLE_PROTOCOL_VERSION = 0x01
BLE_SERVICE_SHORT_UUID = 0x0001

WIFI_STATE_NAMES = {
    0x00: "WIFI_OFF",
    0x01: "WIFI_STATION",
    0x02: "WIFI_AP",
    0x03: "WIFI_STATION_CONNECTING",
    0x04: "WIFI_STATION_FAILED",
}

def parse_manufacturer_data(manu_data: bytes) -> dict | None:
    if len(manu_data) < 20:
        return None
    proto_ver = manu_data[0]
    service_uuid = struct.unpack("<H", manu_data[1:3])[0]
    if proto_ver != BLE_PROTOCOL_VERSION or service_uuid != BLE_SERVICE_SHORT_UUID:
        return None
    trap_id_bytes = manu_data[3:19]
    trap_id = trap_id_bytes.rstrip(b"\x00").decode("ascii", errors="replace")
    wifi_state = manu_data[19]
    return {
        "trap_id": trap_id,
        "wifi_state": wifi_state,
        "wifi_state_name": WIFI_STATE_NAMES.get(wifi_state, f"UNKNOWN_{wifi_state}"),
    }

async def scan(scan_duration=10):
    discovered_traps = []

    def detection_callback(device, advertisement_data):
        if MANUFACTURER_ID not in (advertisement_data.manufacturer_data or {}):
            return
        manu_data = advertisement_data.manufacturer_data[MANUFACTURER_ID]
        parsed = parse_manufacturer_data(manu_data)
        if parsed is None:
            return
        discovered_traps.append({
            "ble_address": device.address,
            "name": device.name or "Unknown",
            "rssi": advertisement_data.rssi,
            "trap_id": parsed["trap_id"],
            "wifi_state": parsed["wifi_state"],
            "wifi_state_name": parsed["wifi_state_name"],
        })

    scanner = BleakScanner(detection_callback)
    await scanner.start()
    print(f"Scanning for BLE traps for {scan_duration} seconds...")
    await asyncio.sleep(scan_duration)
    await scanner.stop()

    result = {
        "scan_duration_seconds": scan_duration,
        "traps_found": len(discovered_traps),
        "discovered_traps": discovered_traps,
    }
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    asyncio.run(scan(scan_duration=15))
