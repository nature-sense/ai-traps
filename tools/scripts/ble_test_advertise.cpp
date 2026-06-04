// Minimal BLE LE Advertisement test for ROCK 3C
// Uses hcitool for LE advertising
// Compile: g++ -std=c++20 ble_test_advertise.cpp -o ble_test_advertise -lsystemd
// Run: sudo ./ble_test_advertise

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <unistd.h>

int main() {
    printf("=== BLE LE Advertisement Test (hcitool) ===\n\n");

    // Step 1: Configure adapter via btmgmt
    printf("--- Step 1: Configure adapter ---\n");
    {
        int ret = system("btmgmt power off 2>&1");
        printf("  btmgmt power off: %d\n", ret);
        
        ret = system("btmgmt le on 2>&1");
        printf("  btmgmt le on: %d\n", ret);
        
        ret = system("btmgmt bredr off 2>&1");
        printf("  btmgmt bredr off: %d\n", ret);
        
        ret = system("btmgmt power on 2>&1");
        printf("  btmgmt power on: %d\n", ret);
    }

    // Step 2: Set advertising data via hcitool (adapter must be ON)
    printf("\n--- Step 2: Set advertising data ---\n");
    // LE Set Advertising Data (OGF=0x08, OCF=0x0008)
    {
        int ret = system("hcitool -i hci0 cmd 0x08 0x0008 1e 02 01 06 0c 09 72 6f 63 6b 33 63 2d 74 65 73 74 05 ff ff ff 01 01 00 00 00 00 00 00 00 00 00 00 2>&1");
        printf("  Set advertising data: %d\n", ret);
    }

    // Step 3: Enable advertising
    printf("\n--- Step 3: Enable advertising ---\n");
    {
        int ret = system("hcitool -i hci0 cmd 0x08 0x000A 01 2>&1");
        printf("  Enable advertising: %d\n", ret);
    }

    printf("\n=== ADVERTISING AS 'rock3c-test' ===\n");
    printf("Press Ctrl+C to stop\n\n");

    // Keep running for 60 seconds
    for (int i = 0; i < 60; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    printf("\n--- Stopping advertising ---\n");
    system("hcitool -i hci0 cmd 0x08 0x000A 00 2>&1");
    system("btmgmt power off 2>&1");

    printf("Done.\n");
    return 0;
}
