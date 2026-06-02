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
              << " \u2014 " << (err.message ? err.message : "no message") << "\n";
}

// ============================================================================
// Static D-Bus property getter for GATT service UUID
// ============================================================================

static int _gatt_service_get_uuid(sd_bus* bus, const char* path,
    const char* interface, const char* property,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "v", "s", ct::gatt_uuids::SERVICE);
}

// ============================================================================
// Static D-Bus property getter for GATT characteristic UUID
// ============================================================================

static int _gatt_char_get_uuid(sd_bus* bus, const char* path,
    const char* interface, const char* property,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* ret_error)
{
    (void)bus; (void)interface; (void)property;
    (void)userdata; (void)ret_error;

    // Determine which characteristic based on the object path
    std::string p(path ? path : "");
    const char* uuid = nullptr;

    if (p.find("char/trap_identity") != std::string::npos) {
        uuid = ct::gatt_uuids::TRAP_IDENTITY;
    } else if (p.find("char/wifi_state") != std::string::npos) {
        uuid = ct::gatt_uuids::WIFI_STATE;
    } else if (p.find("char/wifi_provision_station") != std::string::npos) {
        uuid = ct::gatt_uuids::WIFI_PROV_STATION;
    } else if (p.find("char/wifi_provision_ap") != std::string::npos) {
        uuid = ct::gatt_uuids::WIFI_PROV_AP;
    } else if (p.find("char/command_response") != std::string::npos) {
        uuid = ct::gatt_uuids::CMD_RESPONSE;
    } else {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments", "Unknown characteristic");
    }

    return sd_bus_message_append(reply, "v", "s", uuid);
}

// ============================================================================
// Static D-Bus property getter for GATT characteristic flags
// ============================================================================

static int _gatt_char_get_flags(sd_bus* bus, const char* path,
    const char* interface, const char* property,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* ret_error)
{
    (void)bus; (void)interface; (void)property;
    (void)userdata; (void)ret_error;

    std::string p(path ? path : "");

    // Each characteristic has different flags
    if (p.find("char/trap_identity") != std::string::npos) {
        // Read-only
        return sd_bus_message_append(reply, "v", "as", 1, "read");
    } else if (p.find("char/wifi_state") != std::string::npos) {
        // Read + Notify
        const char* flags[] = {"read", "notify"};
        return sd_bus_message_append(reply, "v", "as", 2, flags[0], flags[1]);
    } else if (p.find("char/wifi_provision_station") != std::string::npos) {
        // Write-only
        return sd_bus_message_append(reply, "v", "as", 1, "write");
    } else if (p.find("char/wifi_provision_ap") != std::string::npos) {
        // Write-only
        return sd_bus_message_append(reply, "v", "as", 1, "write");
    } else if (p.find("char/command_response") != std::string::npos) {
        // Notify-only
        return sd_bus_message_append(reply, "v", "as", 1, "notify");
    }

    return sd_bus_error_set_const(ret_error,
        "org.bluez.Error.InvalidArguments", "Unknown characteristic");
}

// ============================================================================
// Static D-Bus property getter for GATT descriptor UUID
// ============================================================================

static int _gatt_desc_get_uuid(sd_bus* bus, const char* path,
    const char* interface, const char* property,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)ret_error;
    // Client Characteristic Configuration UUID
    return sd_bus_message_append(reply, "v", "s", "00002902-0000-1000-8000-00805f9b34fb");
}

// ============================================================================
// Static D-Bus property getter for GATT descriptor flags
// ============================================================================

static int _gatt_desc_get_flags(sd_bus* bus, const char* path,
    const char* interface, const char* property,
    sd_bus_message* reply, void* userdata,
    sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)ret_error;
    const char* flags[] = {"read", "write"};
    return sd_bus_message_append(reply, "v", "as", 2, flags[0], flags[1]);
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

    std::cout << "[WifiProvisioningActor] ready \u2014 advertising as \""
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
    std::string safe_id = trap_id_;
    for (auto& c : safe_id) {
        if (!std::isalnum(c) && c != '_') c = '_';
    }
    app_path_     = "/com/naturesense/" + safe_id;
    service_path_ = app_path_ + "/service/provisioning";
    adv_path_     = app_path_ + "/advertisement";

    // ── Register GATT Application via GattManager1 ─────────────────────────
    //
    // Object hierarchy:
    //   /com/naturesense/<trap_id>/
    //     service/provisioning/              (GattService1)
    //       char/trap_identity               (GattCharacteristic1 — read)
    //       char/wifi_state                  (GattCharacteristic1 — read, notify)
    //       char/wifi_provision_station      (GattCharacteristic1 — write)
    //       char/wifi_provision_ap           (GattCharacteristic1 — write)
    //       char/command_response            (GattCharacteristic1 — notify)
    //       desc/client_char_config          (GattDescriptor1 — read, write)

    // ── 1. Register GATT Service ───────────────────────────────────────────
    // GattService1 has one property: UUID (string)
    static const sd_bus_vtable gatt_service_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("UUID", "s", _gatt_service_get_uuid, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_VTABLE_END
    };

    ret = sd_bus_add_object_vtable(
        bus_, nullptr,
        service_path_.c_str(),
        bluez::GATT_SERVICE,
        gatt_service_vtable,
        this
    );
    if (ret < 0) {
        logDbusError("sd_bus_add_object_vtable (GATT service)", ret);
        return false;
    }
    std::cout << "[WifiProvisioningActor] GATT service registered at "
              << service_path_ << "\n";

    // ── 2. Register GATT Characteristics ──────────────────────────────────
    // GattCharacteristic1 has properties: UUID (string), Flags (array of strings),
    // and Value (array of bytes — handled via ReadValue/WriteValue methods)

    // Helper vtable for characteristics (UUID + Flags properties)
    auto register_char = [&](const std::string& char_name,
                             const sd_bus_vtable* vtable) -> bool {
        std::string char_path = service_path_ + "/" + char_name;
        ret = sd_bus_add_object_vtable(
            bus_, nullptr,
            char_path.c_str(),
            bluez::GATT_CHARACTERISTIC,
            vtable,
            this
        );
        if (ret < 0) {
            logDbusError(("sd_bus_add_object_vtable (" + char_name + ")").c_str(), ret);
            return false;
        }
        std::cout << "[WifiProvisioningActor] characteristic registered: "
                  << char_name << "\n";
        return true;
    };

    // Characteristic vtable: UUID + Flags properties
    // ReadValue and WriteValue are handled via method handlers registered
    // separately using sd_bus_add_object_vtable's method support.
    //
    // BlueZ calls the GattCharacteristic1.ReadValue and WriteValue methods
    // on our objects. We need to register method handlers for these.
    //
    // sd_bus_add_object_vtable supports both properties and methods.
    // We use SD_BUS_METHOD entries for ReadValue and WriteValue.

    // ── Characteristic: trap_identity (read) ───────────────────────────────
    {
        static const sd_bus_vtable char_trap_id_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_char_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_char_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_METHOD("ReadValue", "a{sv}", "ay", onReadValue, 0),
            SD_BUS_VTABLE_END
        };
        if (!register_char("char/trap_identity", char_trap_id_vtable))
            return false;
    }

    // ── Characteristic: wifi_state (read, notify) ─────────────────────────
    {
        static const sd_bus_vtable char_wifi_state_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_char_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_char_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_METHOD("ReadValue", "a{sv}", "ay", onReadValue, 0),
            SD_BUS_VTABLE_END
        };
        if (!register_char("char/wifi_state", char_wifi_state_vtable))
            return false;
    }

    // ── Characteristic: wifi_provision_station (write) ────────────────────
    {
        static const sd_bus_vtable char_prov_sta_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_char_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_char_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_METHOD("WriteValue", "aya{sv}", "", onWriteValue, 0),
            SD_BUS_VTABLE_END
        };
        if (!register_char("char/wifi_provision_station", char_prov_sta_vtable))
            return false;
    }

    // ── Characteristic: wifi_provision_ap (write) ─────────────────────────
    {
        static const sd_bus_vtable char_prov_ap_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_char_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_char_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_METHOD("WriteValue", "aya{sv}", "", onWriteValue, 0),
            SD_BUS_VTABLE_END
        };
        if (!register_char("char/wifi_provision_ap", char_prov_ap_vtable))
            return false;
    }

    // ── Characteristic: command_response (notify) ─────────────────────────
    {
        static const sd_bus_vtable char_cmd_resp_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_char_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_char_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_VTABLE_END
        };
        if (!register_char("char/command_response", char_cmd_resp_vtable))
            return false;
    }

    // ── 3. Register GATT Descriptor: Client Characteristic Configuration ───
    {
        static const sd_bus_vtable desc_ccc_vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_PROPERTY("UUID", "s", _gatt_desc_get_uuid, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_PROPERTY("Flags", "as", _gatt_desc_get_flags, 0,
                            SD_BUS_VTABLE_PROPERTY_CONST),
            SD_BUS_VTABLE_END
        };

        std::string desc_path = service_path_ + "/desc/client_char_config";
        ret = sd_bus_add_object_vtable(
            bus_, nullptr,
            desc_path.c_str(),
            bluez::GATT_DESCRIPTOR,
            desc_ccc_vtable,
            this
        );
        if (ret < 0) {
            logDbusError("sd_bus_add_object_vtable (desc/client_char_config)", ret);
            return false;
        }
        std::cout << "[WifiProvisioningActor] descriptor registered: "
                  << "desc/client_char_config\n";
    }

    // ── 4. Start D-Bus event loop thread BEFORE calling RegisterApplication ─
    // BlueZ's RegisterApplication internally calls ObjectManager.GetManagedObjects
    // on our application to enumerate all GATT objects. If the event loop isn't
    // running, this call can deadlock or timeout because BlueZ's response to our
    // object enumeration can't be processed.
    //
    // IMPORTANT: We use sd_bus_call_async instead of sd_bus_call to avoid a
    // multi-threading race condition. sd_bus_call (synchronous) internally
    // processes messages on the bus connection while waiting for the reply.
    // If the event loop thread is also calling sd_bus_process on the same
    // connection, they can interfere. sd_bus_call_async queues the method call
    // and lets the event loop thread handle both the outgoing call and the
    // incoming reply, avoiding the race.
    running_.store(true);
    dbus_thread_ = std::thread([this] { dbusEventLoop(); });

    // Give the event loop a moment to start processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ── 5. Call GattManager1.RegisterApplication (async) ───────────────────
    // This tells BlueZ to enumerate our objects via ObjectManager and
    // register them as a GATT application.
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* call_msg = nullptr;

    ret = sd_bus_message_new_method_call(
        bus_, &call_msg,
        bluez::SERVICE,                    // org.bluez
        bluez::DEFAULT_ADAPTER,            // /org/bluez/hci0
        bluez::GATT_MANAGER,               // org.bluez.GattManager1
        "RegisterApplication"              // method
    );
    if (ret < 0) {
        logDbusError("sd_bus_message_new_method_call (RegisterApplication)", ret);
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    // Append application root object path
    ret = sd_bus_message_append(call_msg, "o", app_path_.c_str());
    if (ret < 0) {
        logDbusError("sd_bus_message_append (app path)", ret);
        sd_bus_message_unref(call_msg);
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    // Append empty options dict
    ret = sd_bus_message_open_container(call_msg, 'a', "{sv}");
    if (ret < 0) {
        logDbusError("sd_bus_message_open_container (options)", ret);
        sd_bus_message_unref(call_msg);
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }
    ret = sd_bus_message_close_container(call_msg);
    if (ret < 0) {
        logDbusError("sd_bus_message_close_container (options)", ret);
        sd_bus_message_unref(call_msg);
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    // Use a promise/future to synchronize the async result back to this thread
    auto reg_promise = std::make_shared<std::promise<bool>>();
    auto reg_future = reg_promise->get_future();

    // Slot callback for the async call
    auto slot = [](sd_bus_message* reply, void* userdata, sd_bus_error* ret_error) -> int {
        auto* promise = static_cast<std::promise<bool>*>(userdata);
        if (ret_error && sd_bus_error_is_set(ret_error)) {
            std::cerr << "[WifiProvisioningActor] RegisterApplication failed: "
                      << (ret_error->name ? ret_error->name : "unknown")
                      << " \u2014 " << (ret_error->message ? ret_error->message : "no message") << "\n";
            promise->set_value(false);
        } else {
            std::cout << "[WifiProvisioningActor] GATT application registered with BlueZ\n";
            promise->set_value(true);
        }
        return 0;
    };

    // Send the method call asynchronously
    ret = sd_bus_call_async(
        bus_, nullptr, call_msg,
        slot, reg_promise.get(),
        30000000  // 30 second timeout
    );
    sd_bus_message_unref(call_msg);

    if (ret < 0) {
        logDbusError("sd_bus_call_async (RegisterApplication)", ret);
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    // Wait for the async call to complete (with timeout)
    if (reg_future.wait_for(std::chrono::seconds(35)) != std::future_status::ready) {
        std::cerr << "[WifiProvisioningActor] RegisterApplication timed out (35s)\n";
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    // Get the result
    bool reg_ok = reg_future.get();
    if (!reg_ok) {
        running_.store(false);
        if (dbus_thread_.joinable()) dbus_thread_.join();
        return false;
    }

    std::cout << "[WifiProvisioningActor] BlueZ GATT setup complete\n";
    return true;

}

bool WifiProvisioningActor::setupAdvertisement() {
    if (bus_ == nullptr) {
        std::cerr << "[WifiProvisioningActor] no D-Bus connection\n";
        return false;
    }

    // Build manufacturer data
    auto manu_data = buildManufacturerData();

    // Build the vtable for the LEAdvertisement1 interface
    static const sd_bus_vtable adv_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Type", "s", onAdvGetType, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("ServiceUUIDs", "as", onAdvGetServiceUUIDs, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("ManufacturerData", "a{qay}", onAdvGetManufacturerData, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("LocalName", "s", onAdvGetLocalName, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_VTABLE_END
    };

    // Register the advertisement object
    int ret = sd_bus_add_object_vtable(
        bus_,
        nullptr,
        adv_path_.c_str(),
        bluez::LE_ADVERTISEMENT,
        adv_vtable,
        this
    );
    if (ret < 0) {
        logDbusError("sd_bus_add_object_vtable (advertisement)", ret);
        return false;
    }

    std::cout << "[WifiProvisioningActor] advertisement object registered at "
              << adv_path_ << "\n";

    // ── Call LEAdvertisingManager1.RegisterAdvertisement (async) ──────────
    // IMPORTANT: We use sd_bus_call_async instead of sd_bus_call to avoid a
    // multi-threading race condition with the D-Bus event loop thread that
    // was started in setupBluezGatt(). sd_bus_call (synchronous) internally
    // processes messages on the bus connection while waiting for the reply.
    // If the event loop thread is also calling sd_bus_process on the same
    // connection, they can interfere. sd_bus_call_async queues the method call
    // and lets the event loop thread handle both the outgoing call and the
    // incoming reply, avoiding the race.
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* call_msg = nullptr;

    ret = sd_bus_message_new_method_call(
        bus_, &call_msg,
        bluez::SERVICE,
        bluez::DEFAULT_ADAPTER,
        bluez::LE_ADVERTISING_MGR,
        "RegisterAdvertisement"
    );
    if (ret < 0) {
        logDbusError("sd_bus_message_new_method_call (RegisterAdvertisement)", ret);
        return false;
    }

    ret = sd_bus_message_append(call_msg, "o", adv_path_.c_str());
    if (ret < 0) {
        logDbusError("sd_bus_message_append (advertisement path)", ret);
        sd_bus_message_unref(call_msg);
        return false;
    }

    ret = sd_bus_message_open_container(call_msg, 'a', "{sv}");
    if (ret < 0) {
        logDbusError("sd_bus_message_open_container (options)", ret);
        sd_bus_message_unref(call_msg);
        return false;
    }
    ret = sd_bus_message_close_container(call_msg);
    if (ret < 0) {
        logDbusError("sd_bus_message_close_container (options)", ret);
        sd_bus_message_unref(call_msg);
        return false;
    }

    // Use a promise/future to synchronize the async result back to this thread
    auto adv_promise = std::make_shared<std::promise<bool>>();
    auto adv_future = adv_promise->get_future();

    // Slot callback for the async call
    auto slot = [](sd_bus_message* reply, void* userdata, sd_bus_error* ret_error) -> int {
        auto* promise = static_cast<std::promise<bool>*>(userdata);
        if (ret_error && sd_bus_error_is_set(ret_error)) {
            std::cerr << "[WifiProvisioningActor] RegisterAdvertisement failed: "
                      << (ret_error->name ? ret_error->name : "unknown")
                      << " \u2014 " << (ret_error->message ? ret_error->message : "no message") << "\n";
            promise->set_value(false);
        } else {
            std::cout << "[WifiProvisioningActor] advertisement registered with BlueZ\n";
            promise->set_value(true);
        }
        return 0;
    };

    // Send the method call asynchronously
    ret = sd_bus_call_async(
        bus_, nullptr, call_msg,
        slot, adv_promise.get(),
        30000000  // 30 second timeout
    );
    sd_bus_message_unref(call_msg);

    if (ret < 0) {
        logDbusError("sd_bus_call_async (RegisterAdvertisement)", ret);
        return false;
    }

    // Wait for the async call to complete (with timeout)
    if (adv_future.wait_for(std::chrono::seconds(35)) != std::future_status::ready) {
        std::cerr << "[WifiProvisioningActor] RegisterAdvertisement timed out (35s)\n";
        return false;
    }

    // Get the result
    bool adv_ok = adv_future.get();
    if (!adv_ok) {
        return false;
    }

    std::cout << "[WifiProvisioningActor] advertisement registered with BlueZ\n";

    // ── Set adapter Discoverable to true (with infinite timeout) ────────────
    // Without this, the adapter won't be discoverable by BLE scanners even
    // though the advertisement is registered with BlueZ. We also set
    // DiscoverableTimeout to 0 (infinite) so BlueZ doesn't automatically turn
    // Discoverable back off after the default 180-second timeout.
    {
        // Helper lambda to set a property asynchronously
        auto set_adapter_property = [&](const char* property, const char* type_sig, ...) -> bool {
            sd_bus_message* msg = nullptr;
            int r = sd_bus_message_new_method_call(
                bus_, &msg,
                bluez::SERVICE,
                bluez::DEFAULT_ADAPTER,
                bluez::PROPERTIES,
                "Set"
            );
            if (r < 0) {
                logDbusError(("sd_bus_message_new_method_call (Set " + std::string(property) + ")").c_str(), r);
                return false;
            }

            // Append interface name
            r = sd_bus_message_append(msg, "s", bluez::ADAPTER_INTERFACE);
            if (r < 0) { logDbusError("sd_bus_message_append (interface)", r); sd_bus_message_unref(msg); return false; }

            // Append property name
            r = sd_bus_message_append(msg, "s", property);
            if (r < 0) { logDbusError("sd_bus_message_append (property)", r); sd_bus_message_unref(msg); return false; }

            // Open variant container
            r = sd_bus_message_open_container(msg, 'v', type_sig);
            if (r < 0) { logDbusError("sd_bus_message_open_container (variant)", r); sd_bus_message_unref(msg); return false; }

            // Append the variadic arguments inside the variant
            {
                va_list args;
                va_start(args, type_sig);
                r = sd_bus_message_appendv(msg, type_sig, args);
                va_end(args);
            }

            if (r < 0) {
                logDbusError(("sd_bus_message_appendv (Set " + std::string(property) + ")").c_str(), r);
                sd_bus_message_unref(msg);
                return false;
            }

            // Close variant container
            r = sd_bus_message_close_container(msg);
            if (r < 0) { logDbusError("sd_bus_message_close_container (variant)", r); sd_bus_message_unref(msg); return false; }

            auto promise = std::make_shared<std::promise<bool>>();
            auto future = promise->get_future();

            auto slot = [](sd_bus_message* reply, void* userdata, sd_bus_error* ret_error) -> int {
                auto* p = static_cast<std::promise<bool>*>(userdata);
                if (ret_error && sd_bus_error_is_set(ret_error)) {
                    std::cerr << "[WifiProvisioningActor] Set property failed: "
                              << (ret_error->name ? ret_error->name : "unknown")
                              << " \u2014 " << (ret_error->message ? ret_error->message : "no message") << "\n";
                    p->set_value(false);
                } else {
                    p->set_value(true);
                }
                return 0;
            };

            r = sd_bus_call_async(bus_, nullptr, msg, slot, promise.get(), 10000000);
            sd_bus_message_unref(msg);

            if (r < 0) {
                logDbusError(("sd_bus_call_async (Set " + std::string(property) + ")").c_str(), r);
                return false;
            }

            if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
                std::cerr << "[WifiProvisioningActor] Set " << property << " timed out\n";
                return false;
            }
            return future.get();
        };

        // First set DiscoverableTimeout to 0 (infinite) so BlueZ doesn't
        // automatically turn Discoverable off after the default timeout.
        set_adapter_property("DiscoverableTimeout", "u", (uint32_t)0);

        // Then set Discoverable to true
        bool ok = set_adapter_property("Discoverable", "b", 1);
        if (ok) {
            std::cout << "[WifiProvisioningActor] adapter set discoverable (infinite timeout)\n";
        }
    }

    std::cout << "[WifiProvisioningActor] advertisement setup complete\n";
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
    // Manufacturer data format (matching tools/scripts/ble_scan_traps.py):
    //   Byte  0:   Protocol Version (0x01)
    //   Bytes 1-2: Service UUID (0x0001, little-endian)
    //   Bytes 3-18: Trap ID (16 bytes, null-padded)
    //   Byte  19:  WiFi State
    //
    // Total: 20 bytes

    std::vector<uint8_t> data;
    data.reserve(20);

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

    // Append message (max 19 bytes to keep total <= 20)
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
        sd_bus_message* msg = nullptr;
        int ret = sd_bus_process(bus_, &msg);
        if (ret < 0) {
            logDbusError("sd_bus_process", ret);
            break;
        }

        if (ret > 0) {
            sd_bus_message_unref(msg);
            continue;
        }

        ret = sd_bus_wait(bus_, 1000);
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
    if (!self || !self->bus_) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    // Read the options dict (we ignore it for now)
    const char* object_path = sd_bus_message_get_path(msg);

    // Determine which characteristic is being read
    std::string path(object_path ? object_path : "");
    std::vector<uint8_t> value;

    if (path.find("char/trap_identity") != std::string::npos) {
        value = self->encodeTrapIdentity();
    } else if (path.find("char/wifi_state") != std::string::npos) {
        value = self->encodeWifiState();
    } else {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments",
            "Characteristic does not support ReadValue");
    }

    // Append the value as a byte array
    int ret = sd_bus_message_append(msg, "ay", value.size(), value.data());
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Failed to append value");
    }

    return 0;
}

// ── WriteValue Handler ─────────────────────────────────────────────────────

int WifiProvisioningActor::onWriteValue(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (!self || !self->bus_) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    // Read the byte array value
    const void* data = nullptr;
    size_t data_len = 0;
    int ret = sd_bus_message_read_array(msg, 'y', &data, &data_len);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments", "Failed to read value");
    }

    // Read the options dict (we ignore it for now)
    // The options dict follows the byte array in the message

    // Determine which characteristic is being written
    const char* object_path = sd_bus_message_get_path(msg);
    std::string path(object_path ? object_path : "");

    // Convert to vector
    std::vector<uint8_t> value(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + data_len
    );

    if (path.find("char/wifi_provision_station") != std::string::npos) {
        // Decode SSID + password and handle provisioning
        std::string ssid, password;
        if (!self->decodeProvisionValue(value, ssid, password)) {
            self->sendCommandResponse(CommandResponse::INVALID_FORMAT,
                                      "Invalid provision format");
            return 0;
        }
        WifiProvisionCommand cmd{ssid, password};
        self->handleProvisionStation(cmd);
    } else if (path.find("char/wifi_provision_ap") != std::string::npos) {
        // Decode SSID + password and handle AP provisioning
        std::string ssid, password;
        if (!self->decodeProvisionValue(value, ssid, password)) {
            self->sendCommandResponse(CommandResponse::INVALID_FORMAT,
                                      "Invalid provision format");
            return 0;
        }
        WifiProvisionCommand cmd{ssid, password};
        self->handleProvisionAp(cmd);
    } else {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments",
            "Characteristic does not support WriteValue");
    }

    return 0;
}

// ── Properties.Get Handler ─────────────────────────────────────────────────

int WifiProvisioningActor::onGetProperty(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (!self) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    const char* interface_name = nullptr;
    const char* property_name = nullptr;

    int ret = sd_bus_message_read(msg, "ss", &interface_name, &property_name);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments", "Failed to read property request");
    }

    const char* object_path = sd_bus_message_get_path(msg);
    std::string path(object_path ? object_path : "");

    // Route to the appropriate property getter based on interface and object path
    if (strcmp(interface_name, bluez::GATT_SERVICE) == 0) {
        if (strcmp(property_name, "UUID") == 0) {
            return _gatt_service_get_uuid(self->bus_, object_path,
                interface_name, property_name, msg, self, ret_error);
        }
    } else if (strcmp(interface_name, bluez::GATT_CHARACTERISTIC) == 0) {
        if (strcmp(property_name, "UUID") == 0) {
            return _gatt_char_get_uuid(self->bus_, object_path,
                interface_name, property_name, msg, self, ret_error);
        }
        if (strcmp(property_name, "Flags") == 0) {
            return _gatt_char_get_flags(self->bus_, object_path,
                interface_name, property_name, msg, self, ret_error);
        }
    } else if (strcmp(interface_name, bluez::GATT_DESCRIPTOR) == 0) {
        if (strcmp(property_name, "UUID") == 0) {
            return _gatt_desc_get_uuid(self->bus_, object_path,
                interface_name, property_name, msg, self, ret_error);
        }
        if (strcmp(property_name, "Flags") == 0) {
            return _gatt_desc_get_flags(self->bus_, object_path,
                interface_name, property_name, msg, self, ret_error);
        }
    }

    return sd_bus_error_set_const(ret_error,
        "org.freedesktop.DBus.Error.UnknownProperty",
        "Unknown property or interface");
}

// ── Properties.GetAll Handler ──────────────────────────────────────────────

int WifiProvisioningActor::onGetAllProperties(
    sd_bus_message* msg, void* userdata, sd_bus_error* ret_error)
{
    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (!self) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    const char* interface_name = nullptr;
    int ret = sd_bus_message_read(msg, "s", &interface_name);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.InvalidArguments", "Failed to read interface name");
    }

    const char* object_path = sd_bus_message_get_path(msg);
    std::string path(object_path ? object_path : "");

    // Open the array of property variants
    ret = sd_bus_message_open_container(msg, 'a', "{sv}");
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Failed to open container");
    }

    if (strcmp(interface_name, bluez::GATT_SERVICE) == 0) {
        // Return UUID
        ret = sd_bus_message_append(msg, "{sv}", "UUID", "s", ct::gatt_uuids::SERVICE);
        if (ret < 0) goto fail;
    } else if (strcmp(interface_name, bluez::GATT_CHARACTERISTIC) == 0) {
        // Return UUID and Flags
        const char* uuid = nullptr;
        if (path.find("char/trap_identity") != std::string::npos) {
            uuid = ct::gatt_uuids::TRAP_IDENTITY;
        } else if (path.find("char/wifi_state") != std::string::npos) {
            uuid = ct::gatt_uuids::WIFI_STATE;
        } else if (path.find("char/wifi_provision_station") != std::string::npos) {
            uuid = ct::gatt_uuids::WIFI_PROV_STATION;
        } else if (path.find("char/wifi_provision_ap") != std::string::npos) {
            uuid = ct::gatt_uuids::WIFI_PROV_AP;
        } else if (path.find("char/command_response") != std::string::npos) {
            uuid = ct::gatt_uuids::CMD_RESPONSE;
        }

        if (uuid) {
            ret = sd_bus_message_append(msg, "{sv}", "UUID", "s", uuid);
            if (ret < 0) goto fail;
        }

        // Return flags
        if (path.find("char/trap_identity") != std::string::npos) {
            const char* flags[] = {"read"};
            ret = sd_bus_message_append(msg, "{sv}", "Flags", "as", 1, flags[0]);
        } else if (path.find("char/wifi_state") != std::string::npos) {
            const char* flags[] = {"read", "notify"};
            ret = sd_bus_message_append(msg, "{sv}", "Flags", "as", 2, flags[0], flags[1]);
        } else if (path.find("char/wifi_provision_station") != std::string::npos ||
                   path.find("char/wifi_provision_ap") != std::string::npos) {
            const char* flags[] = {"write"};
            ret = sd_bus_message_append(msg, "{sv}", "Flags", "as", 1, flags[0]);
        } else if (path.find("char/command_response") != std::string::npos) {
            const char* flags[] = {"notify"};
            ret = sd_bus_message_append(msg, "{sv}", "Flags", "as", 1, flags[0]);
        }
        if (ret < 0) goto fail;
    } else if (strcmp(interface_name, bluez::GATT_DESCRIPTOR) == 0) {
        // Return CCCD UUID and flags
        ret = sd_bus_message_append(msg, "{sv}", "UUID", "s",
            "00002902-0000-1000-8000-00805f9b34fb");
        if (ret < 0) goto fail;
        const char* flags[] = {"read", "write"};
        ret = sd_bus_message_append(msg, "{sv}", "Flags", "as", 2, flags[0], flags[1]);
        if (ret < 0) goto fail;
    }

    ret = sd_bus_message_close_container(msg);
    if (ret < 0) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Failed to close container");
    }

    return 0;

fail:
    return sd_bus_error_set_const(ret_error,
        "org.bluez.Error.Failed", "Failed to append properties");
}

// ── LE Advertisement Property Getters ──────────────────────────────────────

int WifiProvisioningActor::onAdvGetType(
    sd_bus* bus, const char* path, const char* interface,
    const char* property, sd_bus_message* reply,
    void* userdata, sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "v", "s", bluez::ADV_TYPE_PERIPHERAL);
}

int WifiProvisioningActor::onAdvGetServiceUUIDs(
    sd_bus* bus, const char* path, const char* interface,
    const char* property, sd_bus_message* reply,
    void* userdata, sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "v", "as", 1, ct::gatt_uuids::SERVICE);
}

int WifiProvisioningActor::onAdvGetManufacturerData(
    sd_bus* bus, const char* path, const char* interface,
    const char* property, sd_bus_message* reply,
    void* userdata, sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)ret_error;

    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (!self) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    auto manu_data = self->buildManufacturerData();

    // ManufacturerData in BlueZ LEAdvertisement1 is a{qay} where the key is
    // the manufacturer ID as a uint16 (q) and the value is a byte array (ay).
    int ret = sd_bus_message_open_container(reply, 'a', "{qay}");
    if (ret < 0) return ret;

    // Append manufacturer data: key = 0xFFFF (65535), value = byte array
    ret = sd_bus_message_append(reply, "{qay}", (uint16_t)0xFFFF,
        manu_data.size(), manu_data.data());
    if (ret < 0) return ret;

    return sd_bus_message_close_container(reply);
}

int WifiProvisioningActor::onAdvGetLocalName(
    sd_bus* bus, const char* path, const char* interface,
    const char* property, sd_bus_message* reply,
    void* userdata, sd_bus_error* ret_error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)ret_error;

    auto* self = static_cast<WifiProvisioningActor*>(userdata);
    if (!self) {
        return sd_bus_error_set_const(ret_error,
            "org.bluez.Error.Failed", "Internal error");
    }

    return sd_bus_message_append(reply, "v", "s", self->trap_id_.c_str());
}

// ── Emit PropertiesChanged Signal ──────────────────────────────────────────

bool WifiProvisioningActor::emitPropertiesChanged(
    const std::string& object_path,
    const std::string& interface,
    const std::string& property_name,
    const std::vector<uint8_t>& value)
{
    if (!bus_) {
        return false;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* msg = nullptr;

    int ret = sd_bus_message_new_signal(
        bus_, &msg,
        object_path.c_str(),
        bluez::PROPERTIES,
        "PropertiesChanged"
    );
    if (ret < 0) {
        logDbusError("sd_bus_message_new_signal (PropertiesChanged)", ret);
        return false;
    }

    // Append interface name
    ret = sd_bus_message_append(msg, "s", interface.c_str());
    if (ret < 0) {
        logDbusError("sd_bus_message_append (interface)", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    // Append changed properties dict
    ret = sd_bus_message_open_container(msg, 'a', "{sv}");
    if (ret < 0) {
        logDbusError("sd_bus_message_open_container (changed props)", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_message_append(msg, "{sv}", property_name.c_str(), "ay",
        value.size(), value.data());
    if (ret < 0) {
        logDbusError("sd_bus_message_append (property value)", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_message_close_container(msg);
    if (ret < 0) {
        logDbusError("sd_bus_message_close_container (changed props)", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    // Append invalidated properties array (empty)
    ret = sd_bus_message_append(msg, "as", 0);
    if (ret < 0) {
        logDbusError("sd_bus_message_append (invalidated)", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_send(bus_, msg, nullptr);
    sd_bus_message_unref(msg);

    if (ret < 0) {
        logDbusError("sd_bus_send (PropertiesChanged)", ret);
        return false;
    }

    return true;
}

} // namespace ct
