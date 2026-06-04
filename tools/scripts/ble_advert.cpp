// ble_advert.cpp
// BLE LE Advertisement using HCI commands (hcitool)
// This approach directly sets advertising data via HCI, bypassing BlueZ D-Bus API
// which has proven unreliable for custom manufacturer data.
//
// Compile: g++ -std=c++11 ble_advert.cpp -o ble_advert
// Run: sudo ./ble_advert

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <signal.h>

static volatile bool keep_running = true;

void signal_handler(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== BLE Advertisement for Radxa Rock 3C ===\n");
    printf("Using HCI commands for reliable advertising\n\n");
    
    // Step 1: Configure adapter
    printf("--- Step 1: Configure adapter ---\n");
    system("btmgmt power off 2>&1");
    system("btmgmt le on 2>&1");
    system("btmgmt bredr off 2>&1");
    system("btmgmt power on 2>&1");
    printf("  Adapter configured\n");
    
    // Step 2: Set advertising data via hcitool
    // hcitool cmd <ogf> <ocf> <hex bytes...>
    // OGF=0x08 (LE Controller), OCF=0x0008 (LE Set Advertising Data)
    //
    // Advertising data (max 31 bytes):
    // 02 01 1a           - Flags: LE General Disc + BR/EDR not supported
    // 0a 09 72 6f 63 6b 33 63 2d 74 65 73 74  - Name: "rock3c-test"
    // 11 ff ff ff 01 01 00 52 4f 43 4b 33 43 2d 54 45 53 54 2d 30 01
    //    ^type ^company  ^prt|serv_uuid |---trap_id "ROCK3C-TEST-0"--| |wifi|
    //    |mfg(17)|
    
    printf("\n--- Step 2: Set advertising data ---\n");
    int ret = system(
        "hcitool -i hci0 cmd 0x08 0x0008 "
        "1e "                    // length = 30 bytes
        "02 01 1a "              // Flags
        "0a 09 72 6f 63 6b 33 63 2d 74 65 73 74 "  // "rock3c-test"
        "11 ff ff ff "           // Manufacturer data: len=17, type=0xFF, company=0xFFFF
        "01 "                    // Protocol version = 1
        "01 00 "                 // Service UUID = 0x0001
        "52 4f 43 4b 33 43 2d 54 45 53 54 2d 30 "  // "ROCK3C-TEST-0"
        "01 "                    // WiFi state: WIFI_STATION
        "2>&1"
    );
    printf("  Set advertising data: %d\n", ret);
    
    // Step 3: Enable advertising
    printf("\n--- Step 3: Enable advertising ---\n");
    ret = system("hcitool -i hci0 cmd 0x08 0x000A 01 2>&1");
    printf("  Enable advertising: %d\n", ret);
    
    printf("\n=== ADVERTISING AS 'rock3c-test' ===\n");
    printf("  BD Address: 98:03:CF:D2:38:39\n");
    printf("  Manufacturer ID: 0xFFFF\n");
    printf("  Trap ID: ROCK3C-TEST-0\n");
    printf("  WiFi State: WIFI_STATION\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Keep running until Ctrl+C
    while (keep_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Cleanup
    printf("\n--- Stopping advertising ---\n");
    system("hcitool -i hci0 cmd 0x08 0x000A 00 2>&1");
    system("btmgmt power off 2>&1");
    
    printf("Done.\n");
    return 0;
}
