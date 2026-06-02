// ble_advert.cpp
// Compile: g++ -std=c++11 ble_advert.cpp -o ble_advert `pkg-config --cflags --libs glib-2.0 gio-2.0`

#include <gio/gio.h>
#include <glib.h>
#include <iostream>
#include <string>
#include <vector>

static GMainLoop *main_loop = nullptr;
static GDBusConnection *system_bus = nullptr;
static const char *adapter_path = "/org/bluez/hci0";
static const char *advertisement_path = "/org/bluez/hci0/advertisement0";

// Structure to hold our advertisement data
struct AdvertisementData {
    std::vector<std::string> uuids;
    std::vector<uint8_t> manufacturer_data;
    std::string local_name;
};

// Callback when advertisement is registered
static void on_advertisement_registered(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = nullptr;
    g_variant_unref(g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error));
    
    if (error) {
        g_printerr("Failed to register advertisement: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(main_loop);
    } else {
        g_print("✓ Advertisement registered successfully!\n");
        g_print("  Broadcasting as 'Bee Device' on adapter %s\n", adapter_path);
    }
}

// Method handler for org.bluez.LEAdvertisement1.Release
static void handle_release(GDBusConnection *connection, const char *sender, const char *object_path,
                          const char *interface_name, const char *method_name,
                          GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    g_print("Advertisement released by BlueZ\n");
    g_dbus_method_invocation_return_value(invocation, NULL);
    g_main_loop_quit(main_loop);
}

// Method handler for org.bluez.LEAdvertisement1 (empty - just need to exist)
static void handle_method_call(GDBusConnection *connection, const char *sender, const char *object_path,
                              const char *interface_name, const char *method_name,
                              GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    if (g_strcmp0(method_name, "Release") == 0) {
        handle_release(connection, sender, object_path, interface_name, method_name, 
                      parameters, invocation, user_data);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Method %s not implemented", method_name);
    }
}

// Properties for the advertisement
static GVariant *get_advertisement_properties() {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    
    // Type: peripheral (broadcaster also works)
    g_variant_builder_add(&builder, "{sv}", "Type", g_variant_new_string("peripheral"));
    
    // Service UUIDs to advertise
    GVariantBuilder uuids_builder;
    g_variant_builder_init(&uuids_builder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&uuids_builder, "s", "180f");  // Battery Service
    g_variant_builder_add(&uuids_builder, "s", "1822");  // Example custom service
    g_variant_builder_add(&uuids_builder, "s", "feed");  // Bee-related custom UUID
    g_variant_builder_add(&builder, "{sv}", "ServiceUUIDs", 
                         g_variant_builder_end(&uuids_builder));
    
    // Manufacturer data (0x004C = Apple, 0x0000 = generic)
    GVariantBuilder mfg_builder;
    g_variant_builder_init(&mfg_builder, G_VARIANT_TYPE("ay"));
    g_variant_builder_add(&mfg_builder, "y", 0x00);  // Company ID LSB
    g_variant_builder_add(&mfg_builder, "y", 0x00);  // Company ID MSB
    g_variant_builder_add(&mfg_builder, "y", 0x42);  // 'B' for Bee
    g_variant_builder_add(&mfg_builder, "y", 0x45);  // 'E'
    g_variant_builder_add(&mfg_builder, "y", 0x45);  // 'E'
    g_variant_builder_add(&builder, "{sv}", "ManufacturerData", 
                         g_variant_new_dict_entry(g_variant_new_uint16(0x0000),
                                                  g_variant_builder_end(&mfg_builder)));
    
    // Include TX power
    GVariantBuilder includes_builder;
    g_variant_builder_init(&includes_builder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&includes_builder, "s", "tx-power");
    g_variant_builder_add(&includes_builder, "s", "appearance");
    g_variant_builder_add(&builder, "{sv}", "Includes", 
                         g_variant_builder_end(&includes_builder));
    
    return g_variant_builder_end(&builder);
}

// Export the advertisement object on D-Bus
static void export_advertisement_object(GDBusConnection *connection) {
    static const GDBusInterfaceVTable interface_vtable = {
        handle_method_call,  // method_call
        nullptr,             // get_property
        nullptr              // set_property
    };
    
    // Properties for the advertisement interface
    GVariant *props = get_advertisement_properties();
    
    GError *error = nullptr;
    g_dbus_connection_export_object(connection,
                                   advertisement_path,
                                   g_dbus_interface_info_lookup(G_DBUS_INTERFACE_INFO(g_dbus_node_info_lookup_interface(
                                       g_dbus_node_info_new_for_xml(
                                           "<node>"
                                           "  <interface name='org.bluez.LEAdvertisement1'>"
                                           "    <method name='Release'/>"
                                           "  </interface>"
                                           "</node>", nullptr), "org.bluez.LEAdvertisement1")),
                                   props,
                                   &interface_vtable,
                                   nullptr, &error);
    
    if (error) {
        g_printerr("Failed to export advertisement: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(main_loop);
    } else {
        g_print("✓ Exported advertisement object at %s\n", advertisement_path);
    }
    
    g_variant_unref(props);
}

// Get the LE Advertising Manager for the adapter
static void register_advertisement() {
    GError *error = nullptr;
    
    // Construct the advertising manager path
    char *manager_path = g_strdup_printf("%s", adapter_path);
    GDBusProxy *advertising_manager = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        "org.bluez",
        manager_path,
        "org.bluez.LEAdvertisingManager1",
        nullptr,
        &error);
    
    if (error) {
        g_printerr("Failed to create advertising manager proxy: %s\n", error->message);
        g_error_free(error);
        g_free(manager_path);
        g_main_loop_quit(main_loop);
        return;
    }
    
    // Register the advertisement with options
    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE("a{sv}"));
    // Set timeout to 0 for indefinite advertising
    g_variant_builder_add(&options_builder, "{sv}", "Discoverable", 
                         g_variant_new_boolean(true));
    
    g_dbus_proxy_call(advertising_manager,
                     "RegisterAdvertisement",
                     g_variant_new("(o@a{sv})", advertisement_path, 
                                  g_variant_builder_end(&options_builder)),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     nullptr,
                     on_advertisement_registered,
                     nullptr);
    
    g_free(manager_path);
    g_object_unref(advertising_manager);
}

// Power on the Bluetooth adapter
static void power_on_adapter() {
    GError *error = nullptr;
    GDBusProxy *adapter_proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        "org.bluez",
        adapter_path,
        "org.bluez.Adapter1",
        nullptr,
        &error);
    
    if (error) {
        g_printerr("Failed to create adapter proxy: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(main_loop);
        return;
    }
    
    // Set Powered property to true
    g_dbus_proxy_call_sync(adapter_proxy,
                          "org.freedesktop.DBus.Properties.Set",
                          g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered",
                                       g_variant_new_boolean(true)),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          nullptr,
                          &error);
    
    if (error) {
        g_printerr("Failed to power on adapter: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("✓ Bluetooth adapter powered on\n");
    }
    
    // Set Discoverable property
    g_dbus_proxy_call_sync(adapter_proxy,
                          "org.freedesktop.DBus.Properties.Set",
                          g_variant_new("(ssv)", "org.bluez.Adapter1", "Discoverable",
                                       g_variant_new_boolean(true)),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          nullptr,
                          &error);
    
    if (error) {
        g_print("Note: Could not set discoverable (not critical): %s\n", error->message);
        g_error_free(error);
    }
    
    g_object_unref(adapter_proxy);
}

// Check if adapter exists
static bool check_adapter() {
    GError *error = nullptr;
    GDBusProxy *adapter_proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        "org.bluez",
        adapter_path,
        "org.freedesktop.DBus.Properties",
        nullptr,
        &error);
    
    if (error) {
        g_printerr("✗ Bluetooth adapter not found at %s\n", adapter_path);
        g_printerr("  Make sure Bluetooth is enabled: sudo hciconfig hci0 up\n");
        g_error_free(error);
        return false;
    }
    
    g_object_unref(adapter_proxy);
    g_print("✓ Found Bluetooth adapter at %s\n", adapter_path);
    return true;
}

int main(int argc, char *argv[]) {
    g_print("\n=== BLE Advertisement for Radxa Rock 3C ===\n");
    g_print("Advertising as 'Bee Device' with custom data\n\n");
    
    // Initialize GLib
    main_loop = g_main_loop_new(nullptr, FALSE);
    
    // Connect to system D-Bus
    system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!system_bus) {
        g_printerr("Failed to connect to system bus\n");
        return 1;
    }
    
    // Check if adapter exists
    if (!check_adapter()) {
        return 1;
    }
    
    // Power on the adapter
    power_on_adapter();
    
    // Export advertisement object
    export_advertisement_object(system_bus);
    
    // Small delay to ensure export completes
    g_usleep(500000);
    
    // Register advertisement with BlueZ
    register_advertisement();
    
    g_print("\nAdvertising... Press Ctrl+C to stop\n\n");
    
    // Run the main loop
    g_main_loop_run(main_loop);
    
    // Cleanup
    g_main_loop_unref(main_loop);
    g_object_unref(system_bus);
    
    return 0;
}
