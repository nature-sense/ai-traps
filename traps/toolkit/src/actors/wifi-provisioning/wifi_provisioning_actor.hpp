/*
 * Copyright 2026 Nature Sense
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// actors/wifi-provisioning/wifi_provisioning_actor.hpp
#pragma once

#include <ramen.hpp>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace ct {

// ============================================================================
// WiFi State Enum
// ============================================================================
//
// Maps directly to the GATT WiFi State characteristic values (0x00-0x04).
//
enum class WifiState : uint8_t {
    OFF       = 0,  ///< WiFi disabled (default for battery/solar mode)
    STATION   = 1,  ///< WiFi station mode, connected to a network
    AP        = 2,  ///< WiFi access-point mode
    CONNECTING = 3, ///< Station mode attempting to connect
    FAILED    = 4,  ///< Station mode failed to connect
};

// ============================================================================
// WifiProvisionCommand
// ============================================================================
//
// Carries SSID and password for provisioning. Used both for station and AP
// provisioning commands.
//
struct WifiProvisionCommand {
    std::string ssid;
    std::string password;
};

// ============================================================================
// CommandResponse
// ============================================================================
//
// Status codes and optional message sent back to the client after a
// provisioning write. Maps to the GATT Command Response characteristic.
//
struct CommandResponse {
    uint8_t     status;   ///< Status code (0=SUCCESS, 1=ACCEPTED, etc.)
    std::string message;  ///< Human-readable message (0-19 bytes)

    // Predefined status codes matching the GATT spec
    static constexpr uint8_t SUCCESS           = 0x00;
    static constexpr uint8_t ACCEPTED          = 0x01;
    static constexpr uint8_t INVALID_FORMAT    = 0x02;
    static constexpr uint8_t SSID_TOO_LONG     = 0x03;
    static constexpr uint8_t PASSWORD_TOO_LONG = 0x04;
    static constexpr uint8_t CONFIG_FAILED     = 0x05;
    static constexpr uint8_t CONNECTION_FAILED = 0x06;
    static constexpr uint8_t INTERNAL_ERROR    = 0x07;
};

// ============================================================================
// GATT UUIDs (matching docs/ble-gatt-service.md)
// ============================================================================
//
// Base UUID: A1C00000-0001-4A54-8000-0A4D4943414D
// Service UUID: A1C00001-0001-4A54-8000-0A4D4943414D
//
namespace gatt_uuids {

constexpr const char* SERVICE             = "A1C00001-0001-4A54-8000-0A4D4943414D";
constexpr const char* TRAP_IDENTITY       = "A1C00002-0001-4A54-8000-0A4D4943414D";
constexpr const char* WIFI_STATE          = "A1C00003-0001-4A54-8000-0A4D4943414D";
constexpr const char* WIFI_PROV_STATION   = "A1C00004-0001-4A54-8000-0A4D4943414D";
constexpr const char* WIFI_PROV_AP        = "A1C00005-0001-4A54-8000-0A4D4943414D";
constexpr const char* CMD_RESPONSE        = "A1C00006-0001-4A54-8000-0A4D4943414D";

} // namespace gatt_uuids

// ============================================================================
// WifiProvisioningActor
// ============================================================================
//
// A Ramen actor that exposes a BLE GATT service for WiFi provisioning.
//
// Features:
//   - Advertises trap identity via BLE advertisement (manufacturer data)
//   - Exposes 5 GATT characteristics for discovery and provisioning
//   - Maintains internal WiFi state (no real WiFi hardware control yet)
//   - Provides Ramen ports for future actor wiring
//
// BlueZ D-Bus Integration:
//   Uses sd-bus (libsystemd) to register a GATT application with BlueZ.
//   The actor connects to the system bus and registers:
//     - A GATT service with 5 characteristics
//     - A LE advertisement with manufacturer data
//
// Thread Safety:
//   The actor runs its own D-Bus event loop thread. All state access is
//   protected by a mutex. Ramen ports are safe to call from any thread.
//
// Usage:
//   WifiProvisioningActor actor;
//   actor.init("trap-singapore-01");
//   // ... actor is now advertising and accepting GATT connections ...
//   actor.shutdown();
//
// ============================================================================

struct WifiProvisioningActor {
    // ── Ramen Input Ports ──────────────────────────────────────────────────────
    // Receive a station provisioning command via actor wiring.
    ramen::Pushable<WifiProvisionCommand> in_provision_station{
        [this](const WifiProvisionCommand& cmd) { handleProvisionStation(cmd); }
    };

    // Receive an AP provisioning command via actor wiring.
    ramen::Pushable<WifiProvisionCommand> in_provision_ap{
        [this](const WifiProvisionCommand& cmd) { handleProvisionAp(cmd); }
    };

    // ── Ramen Output Ports ─────────────────────────────────────────────────────
    // Emitted when the WiFi state changes.
    ramen::Pusher<WifiState> out_wifi_state{};

    // Emitted when a command response is generated.
    ramen::Pusher<CommandResponse> out_command_response{};

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    // Initialize the actor with the given trap identity.
    // Sets up the BlueZ GATT server and begins advertising.
    // Returns true on success, false on failure.
    bool init(const std::string& trap_id);

    // Shutdown the actor: stop advertising, unregister GATT, close D-Bus.
    void shutdown();

    // ── Public Getters (for testing / external query) ──────────────────────────
    // Returns the current WiFi state.
    WifiState getWifiState() const;

    // Returns the trap identity string.
    std::string getTrapId() const;

    // Returns the last provisioned SSID (empty if none).
    std::string getStoredSsid() const;

    // Returns the last provisioned password (empty if none).
    std::string getStoredPassword() const;

    // Returns true if the actor is initialized and the D-Bus connection is active.
    bool isRunning() const;

private:
    // ── Internal State ─────────────────────────────────────────────────────────
    std::string trap_id_;
    std::atomic<WifiState> wifi_state_{WifiState::OFF};
    std::string stored_ssid_;
    std::string stored_password_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;

    // ── BlueZ D-Bus Integration ────────────────────────────────────────────────
    // D-Bus connection (sd-bus)
    struct sd_bus* bus_{nullptr};

    // D-Bus object paths for our GATT application
    std::string app_path_;       // e.g., /com/naturesense/trap0
    std::string service_path_;   // e.g., /com/naturesense/trap0/service/provisioning
    std::string adv_path_;       // e.g., /com/naturesense/trap0/advertisement

    // D-Bus event loop thread
    std::thread dbus_thread_;
    void dbusEventLoop();

    // ── BlueZ D-Bus Setup Helpers ──────────────────────────────────────────────
    // Connect to the system bus and register the GATT application.
    bool setupBluezGatt();

    // Register the LE advertisement with manufacturer data.
    bool setupAdvertisement();

    // Unregister GATT application and advertisement.
    void teardownBluez();

    // Build the manufacturer data blob for advertisement.
    std::vector<uint8_t> buildManufacturerData() const;

    // ── GATT Characteristic Value Helpers ──────────────────────────────────────
    // Encode the trap ID as a 16-byte null-padded string.
    std::vector<uint8_t> encodeTrapIdentity() const;

    // Encode the current WiFi state as a 1-byte value.
    std::vector<uint8_t> encodeWifiState() const;

    // Encode a command response as status byte + optional message.
    std::vector<uint8_t> encodeCommandResponse(const CommandResponse& resp) const;

    // Decode a provision write value (SSID length + SSID + password length + password).
    // Returns true on success, false on invalid format.
    bool decodeProvisionValue(const std::vector<uint8_t>& value,
                              std::string& out_ssid,
                              std::string& out_password) const;

    // ── Provisioning Logic ─────────────────────────────────────────────────────
    // Handle a station provisioning command (from Ramen port or GATT write).
    void handleProvisionStation(const WifiProvisionCommand& cmd);

    // Handle an AP provisioning command (from Ramen port or GATT write).
    void handleProvisionAp(const WifiProvisionCommand& cmd);

    // Validate and process a provisioning command.
    void processProvision(const WifiProvisionCommand& cmd, bool is_ap);

    // Set the WiFi state and emit notifications.
    void setWifiState(WifiState new_state);

    // Send a command response via GATT notification and Ramen port.
    void sendCommandResponse(uint8_t status, const std::string& message);

    // ── D-Bus GATT Method Handlers (static, called by sd-bus) ──────────────────
    // Handler for ReadValue method calls on our GATT characteristics.
    static int onReadValue(sd_bus_message* msg, void* userdata, sd_bus_error* ret_error);

    // Handler for WriteValue method calls on our GATT characteristics.
    static int onWriteValue(sd_bus_message* msg, void* userdata, sd_bus_error* ret_error);

    // Generic handler for org.freedesktop.DBus.Properties.Get on our objects.
    static int onGetProperty(sd_bus_message* msg, void* userdata, sd_bus_error* ret_error);

    // Generic handler for org.freedesktop.DBus.Properties.GetAll on our objects.
    static int onGetAllProperties(sd_bus_message* msg, void* userdata, sd_bus_error* ret_error);

    // ── Helpers ────────────────────────────────────────────────────────────────
    // Send a PropertiesChanged signal for a GATT characteristic.
    bool emitPropertiesChanged(const std::string& object_path,
                               const std::string& interface,
                               const std::string& property_name,
                               const std::vector<uint8_t>& value);
};

} // namespace ct
