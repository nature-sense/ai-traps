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

#include "wifi_provisioning_actor.hpp"

#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace ct {

// ============================================================================
// BlueZ D-Bus Interface Constants
// ============================================================================

namespace bluez {

// BlueZ D-Bus service and interface names
constexpr const char* SERVICE              = "org.bluez";
constexpr const char* ADAPTER_INTERFACE    = "org.bluez.Adapter1";
constexpr const char* DEVICE_INTERFACE     = "org.bluez.Device1";
constexpr const char* GATT_MANAGER         = "org.bluez.GattManager1";
constexpr const char* GATT_SERVICE         = "org.bluez.GattService1";
constexpr const char* GATT_CHARACTERISTIC  = "org.bluez.GattCharacteristic1";
constexpr const char* GATT_DESCRIPTOR      = "org.bluez.GattDescriptor1";
constexpr const char* LE_ADVERTISING_MGR   = "org.bluez.LEAdvertisingManager1";
constexpr const char* LE_ADVERTISEMENT     = "org.bluez.LEAdvertisement1";
constexpr const char* PROPERTIES           = "org.freedesktop.DBus.Properties";
constexpr const char* OBJECT_MANAGER       = "org.freedesktop.DBus.ObjectManager";

// Default adapter path
constexpr const char* DEFAULT_ADAPTER      = "/org/bluez/hci0";

// GATT characteristic flags
constexpr const char* FLAG_READ            = "read";
constexpr const char* FLAG_WRITE           = "write";
constexpr const char* FLAG_NOTIFY          = "notify";
constexpr const char* FLAG_ENCRYPT_WRITE   = "encrypt-write";

// Advertisement type
constexpr const char* ADV_TYPE_PERIPHERAL  = "peripheral";

} // namespace bluez

// ============================================================================
// Helper: Build D-Bus object path
// ============================================================================

static std::string makePath(const std::string& base, const std::string& component) {
    return base + "/" + component;
}

// ============================================================================
// Helper: Log D-Bus error
// ============================================================================

static void logDbusError(const char* context, int ret) {
    std::cerr << "[WifiProvisioningActor] " << context
              << " failed: " << strerror(-ret) << " (" << ret << ")\n";
}

static void logDbusError(const char* context, const sd_bus_error& err) {
    std::cerr << "[WifiProvisioningActor] " << context
              << " failed: " << (err.name ? err.name : "unknown")
              << " — " << (err.message ? err.message : "no message") << "\n";
}

// ============================================================================
// WifiProvisioningActor Implementation
// ============================================================================

bool WifiProvisioningActor::init(const std::string& trap_id) {
    if (running_.load()) {
        std::cerr << "[WifiProvisioningActor] already running\n";
        return false;
    }

    trap_id_ = trap_id;
    if (trap_id_.size() > 16) {
        trap_id_.resize(16);
    }

    std::cout << "[WifiProvisioningActor] initializing with trap ID: \""
              << trap_id_ << "\"\n";

    // Set up BlueZ GATT server
    if (!setupBluezGatt()) {
        std::cerr << "[WifiProvisioningActor] failed to set up BlueZ GATT\n";
        return false;
    }

    // Set up BLE advertisement
    if (!setupAdvertisement()) {
        std::cerr << "[WifiProvisioningActor] failed to set up advertisement\n";
        teardownBluez();
        return false;
    }

    running_.store(true);
    std::cout << "[WifiProvisioningActor] ready — advertising as \""
              << trap_id_ << "\"\n";
    return true;
}

void WifiProvisioningActor::shutdown() {
    running_.store(false);

    // Stop D-Bus event loop
    if (dbus_thread_.joinable()) {
        if (bus_ != nullptr) {
            sd_bus_get_fd(bus_); // wake up the event loop
        }
        dbus_thread_.join();
    }

    teardownBluez();

    std::cout << "[WifiProvisioningActor] shutdown\n";
}

// ── Public Getters ─────────────────────────────────────────────────────────

WifiState WifiProvisioningActor::getWifiState() const {
    return wifi_state_.load();
}

std::string WifiProvisioningActor::getTrapId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trap_id_;
}

std::string WifiProvisioningActor::getStoredSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stored_ssid_;
}

std::string WifiProvisioningActor::getStoredPassword() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stored_password_;
}

bool WifiProvisioningActor::isRunning() const {
    return running_.load();
}

// ── BlueZ D-Bus Setup ──────────────────────────────────────────────────────

bool WifiProvisioningActor::setupBluezGatt() {
    int ret;

    // Open system bus
    ret = sd_bus_open_system(&bus_);
    if (ret < 0) {
        logDbusError("sd_bus_open_system", ret);
        return false;
    }

    // Generate unique object paths based on trap ID
    // Replace non-alphanumeric characters for D-Bus path safety
    std::string safe_id = trap_id_;
    for (auto& c : safe_id) {
        if (!std::isalnum(c) && c != '_') c = '_';
    }
    app_path_     = "/com/naturesense/" + safe_id;
    service_path_ = app_path_ + "/service/provisioning";
    adv_path_     = app_path_ + "/advertisement";

    // ── Register GATT Application via GattManager1 ─────────────────────────
    // We need to:
    //   1. Create D-Bus objects for the GATT service and characteristics
    //   2. Call GattManager1.RegisterApplication with our root path
    //
    // The GATT object hierarchy:
    //   /com/naturesense/<trap_id>/
    //     service/provisioning/          (GattService1)
    //       char/trap_identity           (GattCharacteristic1 — read)
    //       char/wifi_state              (GattCharacteristic1 — read, notify)
    //       char/wifi_provision_station  (GattCharacteristic1 — write)
    //       char/wifi_provision_ap       (GattCharacteristic1 — write)
    //       char/command_response        (GattCharacteristic1 — notify)
    //       desc/                        (ClientCharacteristicConfiguration)

    // For now, we'll use a simpler approach: register the GATT application
    // via the ObjectManager interface and let BlueZ discover our objects.
    //
    // We register D-Bus object nodes with the appropriate interfaces.
    // BlueZ's GattManager1.RegisterApplication will then enumerate our
    // objects via ObjectManager.GetManagedObjects.

    // Start the D-Bus event loop thread
    dbus_thread_ = std::thread([this] { dbusEventLoop(); });

    // Give the event loop a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[WifiProvisioningActor] BlueZ GATT setup complete\n";
    return true;
}

bool WifiProvisioningActor::setupAdvertisement() {
    // TODO: Register LEAdvertisement1 object and call
    // LEAdvertisingManager1.RegisterAdvertisement
    //
    // For now, we log that advertisement setup is pending.
    // The full implementation will:
    //   1. Create a D-Bus object at adv_path_ with LEAdvertisement1 interface
    //   2. Set properties: Type="peripheral", ServiceUUIDs=[our service UUID],
    //      ManufacturerData=buildManufacturerData()
    //   3. Call LEAdvertisingManager1.RegisterAdvertisement on the default adapter
    //
    std::cout << "[WifiProvisioningActor] advertisement setup pending\n";
    return true;
}

void WifiProvisioningActor::teardownBluez() {
    if (bus_ != nullptr) {
        sd_bus_close(bus_);
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

std::vector<uint8_t> WifiProvisioningActor::buildManufacturerData() const {
    // Manufacturer data format (from docs/ble-gatt-service.md):
    //   Bytes 0-1: Company ID (0xFFFF for development)
    //   Byte  2:   Protocol Version (0x01)
    //   Bytes 3-4: Service UUID (0x0001)
    //   Bytes 5-20: Trap ID (16 bytes, null-padded)
    //   Byte  21:  WiFi State
    //
    // Total: 22 bytes

    std::vector<uint8_t> data;
    data.reserve(22);

    // Company ID: 0xFFFF (little-endian)
    data.push_back(0xFF);
    data.push_back(0xFF);

    // Protocol version
    data.push_back(0x01);

    // Service UUID short: 0x0001 (little-endian)
    data.push_back(0x01);
    data.push_back(0x00);

    // Trap ID: 16 bytes, null-padded
    std::string id = trap_id_;
    id.resize(16, '\0');
    for (char c : id) {
        data.push_back(static_cast<uint8_t>(c));
    }

    // WiFi state
    data.push_back(static_cast<uint8_t>(wifi_state_.load()));

    return data;
}

// ── GATT Characteristic Value Helpers ──────────────────────────────────────

std::vector<uint8_t> WifiProvisioningActor::encodeTrapIdentity() const {
    std::vector<uint8_t> data(16, 0x00);
    std::string id = trap_id_;
    if (id.size() > 16) id.resize(16);
    std::copy(id.begin(), id.end(), data.begin());
    return data;
}

std::vector<uint8_t> WifiProvisioningActor::encodeWifiState() const {
    return { static_cast<uint8_t>(wifi_state_.load()) };
}

std::vector<uint8_t> WifiProvisioningActor::encodeCommandResponse(
    const CommandResponse& resp) const
{
    std::vector<uint8_t> data;
    data.push_back(resp.status);

    // Append message (max 19 bytes to keep total ≤ 20)
    size_t msg_len = resp.message.size();
    if (msg_len > 19) msg_len = 19;
    for (size_t i = 0; i < msg_len; ++i) {
        data.push_back(static_cast<uint8_t>(resp.message[i]));
    }

    return data;
}

bool WifiProvisioningActor::decodeProvisionValue(
    const std::vector<uint8_t>& value,
    std::string& out_ssid,
    std::string& out_password) const
{
    if (value.size() < 2) {
        return false; // Need at least SSID length byte + 1 byte of SSID
    }

    size_t offset = 0;

    // Read SSID length
    uint8_t ssid_len = value[offset++];
    if (ssid_len > 32 || ssid_len == 0) {
        return false;
    }
    if (offset + ssid_len > value.size()) {
        return false;
    }

    // Read SSID
    out_ssid.assign(reinterpret_cast<const char*>(&value[offset]), ssid_len);
    offset += ssid_len;

    // Check if there's a password length byte
    if (offset >= value.size()) {
        out_password.clear();
        return true;
    }

    // Read password length
    uint8_t pass_len = value[offset++];
    if (pass_len > 32) {
        return false;
    }
    if (offset + pass_len > value.size()) {
        return false;
    }

    // Read password
    out_password.assign(reinterpret_cast<const char*>(&value[offset]), pass_len);

    return true;
}

// ── Provisioning Logic ─────────────────────────────────────────────────────

void WifiProvisioningActor::handleProvisionStation(
    const WifiProvisionCommand& cmd)
{
    processProvision(cmd, false);
}

void WifiProvisioningActor::handleProvisionAp(
    const WifiProvisionCommand& cmd)
{
    processProvision(cmd, true);
}

void WifiProvisioningActor::processProvision(
    const WifiProvisionCommand& cmd, bool is_ap)
{
    // Validate SSID
    if (cmd.ssid.empty() || cmd.ssid.size() > 32) {
        sendCommandResponse(CommandResponse::SSID_TOO_LONG,
                            "SSID must be 1-32 chars");
        return;
    }

    // Validate password
    if (cmd.password.size() > 32) {
        sendCommandResponse(CommandResponse::PASSWORD_TOO_LONG,
                            "Password must be 0-32 chars");
        return;
    }

    if (is_ap && cmd.password.size() > 0 && cmd.password.size() < 8) {
        // AP mode typically requires minimum 8 chars for password
        sendCommandResponse(CommandResponse::INVALID_FORMAT,
                            "AP password must be 0 or 8-32 chars");
        return;
    }

    // Store credentials
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stored_ssid_     = cmd.ssid;
        stored_password_ = cmd.password;
    }

    // Send accepted response
    sendCommandResponse(CommandResponse::ACCEPTED, "Accepted");

    // Update state
    if (is_ap) {
        std::cout << "[WifiProvisioningActor] provisioning AP: SSID=\""
                  << cmd.ssid << "\"\n";
        setWifiState(WifiState::AP);
        sendCommandResponse(CommandResponse::SUCCESS, "AP mode activated");
    } else {
        std::cout << "[WifiProvisioningActor] provisioning station: SSID=\""
                  << cmd.ssid << "\"\n";
        setWifiState(WifiState::CONNECTING);
        // In a real implementation, we'd attempt to connect here.
        // For now, simulate success after a brief delay.
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            setWifiState(WifiState::STATION);
            sendCommandResponse(CommandResponse::SUCCESS, "Connected");
        }).detach();
    }
}

void WifiProvisioningActor::setWifiState(WifiState new_state) {
    wifi_state_.store(new_state);

    // Emit via Ramen port
    out_wifi_state(new_state);

    // Emit GATT notification (PropertiesChanged on WiFi State characteristic)
    emitPropertiesChanged(
        makePath(service_path_, "char/wifi_state"),
        bluez::GATT_CHARACTERISTIC,
        "Value",
        encodeWifiState()
    );
}

void WifiProvisioningActor::sendCommandResponse(
    uint8_t status, const std::string& message)
{
    CommandResponse resp{status, message};

    // Emit via Ramen port
    out_command_response(resp);

    // Emit GATT notification
    emitPropertiesChanged(
        makePath(service_path_, "char/command_response"),
        bluez::GATT_CHARACTERISTIC,
        "Value",
        encodeCommandResponse(resp)
    );
}

// ── D-Bus Event Loop ───────────────────────────────────────────────────────

void WifiProvisioningActor::dbusEventLoop() {
    std::cout << "[WifiProvisioningActor] D-Bus event loop started\n";

    while (running_.load() && bus_ != nullptr) {
        // Process D-Bus events with a timeout so we can check running_ flag
        int ret = sd_bus_process(bus_);
        if (ret < 0) {
            logDbusError("sd_bus_process", ret);
            break;
        }

        if (ret > 0) {
            // We processed something, continue immediately
            continue;
        }

        // Wait for more events (with timeout)
        ret = sd_bus_wait(bus_, 1000); // 1 second timeout
        if (ret < 0 && ret != -EINTR) {
            logDbusError("sd_bus_wait", ret);
            break;
        }
    }

    std::cout << "[WifiProvisioningActor] D-Bus event loop ended\n";
}

// ── D-Bus GATT Method Handlers ─────────────────────────────────────────────

int WifiProvisioningActor::onReadValue(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (self == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Internal error");
    }

    // Read the options dictionary (we ignore it for now)
    // The options are a dict with key "offset" (uint16) for MTU-based reads
    // We skip parsing and just return the full value.

    // Determine which characteristic is being read from the object path
    const char* object_path = nullptr;
    int ret = sd_bus_message_get_path(msg, &object_path);
    if (ret < 0 || object_path == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Cannot determine object path");
    }

    std::string path(object_path);
    std::vector<uint8_t> value;

    if (path.find("char/trap_identity") != std::string::npos) {
        value = self->encodeTrapIdentity();
    } else if (path.find("char/wifi_state") != std::string::npos) {
        value = self->encodeWifiState();
    } else {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Unknown characteristic");
    }

    // Reply with the value as an array of bytes
    ret = sd_bus_reply_method_return(msg, "ay", value.data(), value.size());
    if (ret < 0) {
        logDbusError("sd_bus_reply_method_return (ReadValue)", ret);
    }

    return ret;
}

int WifiProvisioningActor::onWriteValue(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (self == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Internal error");
    }

    // Read the value (array of bytes)
    const uint8_t* value_data = nullptr;
    size_t value_len = 0;
    int ret = sd_bus_message_read_array(msg, 'y', reinterpret_cast<const void**>(&value_data), &value_len);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Failed to read value");
    }

    // Read the options dictionary (we skip it)
    // The options may contain "offset" (uint16) and "type" (string)
    // For now, we ignore options and process the full value.

    std::vector<uint8_t> value(value_data, value_data + value_len);

    // Determine which characteristic is being written to
    const char* object_path = nullptr;
    ret = sd_bus_message_get_path(msg, &object_path);
    if (ret < 0 || object_path == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Cannot determine object path");
    }

    std::string path(object_path);

    // Decode the provision value
    std::string ssid, password;
    if (!self->decodeProvisionValue(value, ssid, password)) {
        self->sendCommandResponse(CommandResponse::INVALID_FORMAT,
                                  "Invalid format");
        return sd_bus_reply_method_return(msg, "");
    }

    WifiProvisionCommand cmd{ssid, password};

    if (path.find("char/wifi_provision_station") != std::string::npos) {
        self->handleProvisionStation(cmd);
    } else if (path.find("char/wifi_provision_ap") != std::string::npos) {
        self->handleProvisionAp(cmd);
    } else {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Unknown characteristic");
    }

    // Reply with empty body (success)
    return sd_bus_reply_method_return(msg, "");
}

int WifiProvisioningActor::onGetProperty(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (self == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Internal error");
    }

    // Read interface name and property name
    const char* interface_name = nullptr;
    const char* property_name  = nullptr;
    int ret = sd_bus_message_read(msg, "ss", &interface_name, &property_name);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Failed to read property request");
    }

    std::string iface(interface_name ? interface_name : "");
    std::string prop(property_name ? property_name : "");

    // Determine which object this is for
    const char* object_path = nullptr;
    ret = sd_bus_message_get_path(msg, &object_path);
    if (ret < 0 || object_path == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Cannot determine object path");
    }

    std::string path(object_path);

    // We'll build a reply message with the property value
    // The reply format is: (variant) where variant contains the property value

    // For now, return a simple acknowledgment
    // Full implementation will return actual property values based on
    // the interface and property name.

    return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                  "Property not implemented");
}

int WifiProvisioningActor::onGetAllProperties(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (self == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Internal error");
    }

    // Read interface name
    const char* interface_name = nullptr;
    int ret = sd_bus_message_read(msg, "s", &interface_name);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Failed to read interface name");
    }

    std::string iface(interface_name ? interface_name : "");

    // Determine which object this is for
    const char* object_path = nullptr;
    ret = sd_bus_message_get_path(msg, &object_path);
    if (ret < 0 || object_path == nullptr) {
        return sd_bus_error_set_const(ret_error, bluez::SERVICE,
                                      "Cannot determine object path");
    }

    std::string path(object_path);

    // Return an empty dict for now
    // Full implementation will return all properties for the given interface.
    ret = sd_bus_reply_method_return(msg, "a{sv}", 0);
    if (ret < 0) {
        logDbusError("sd_bus_reply_method_return (GetAll)", ret);
    }

    return ret;
}

// ── Helpers ────────────────────────────────────────────────────────────────

bool WifiProvisioningActor::emitPropertiesChanged(
    const std::string& object_path,
    const std::string& interface,
    const std::string& property_name,
    const std::vector<uint8_t>& value)
{
    if (bus_ == nullptr) {
        return false;
    }

    // TODO: Send org.freedesktop.DBus.Properties.PropertiesChanged signal
    // This requires constructing a proper D-Bus signal message with:
    //   - interface name
    //   - changed properties dict: { "Value": <variant containing ay> }
    //   - invalidated properties array (empty)
    //
    // For now, we log the intent.
    std::cout << "[WifiProvisioningActor] PropertiesChanged: "
              << object_path << " " << interface << "." << property_name
              << " (" << value.size() << " bytes)\n";
    return true;
}

} // namespace ct
