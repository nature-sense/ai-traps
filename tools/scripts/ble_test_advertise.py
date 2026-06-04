#!/usr/bin/env python3
"""
Minimal BLE LE Advertisement test for ROCK 3C
Uses D-Bus directly via pydbus or dbus-next
Run: sudo python3 ble_test_advertise.py
"""

import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
import time
import signal
import sys

from gi.repository import GLib

BLUEZ_SERVICE = 'org.bluez'
ADAPTER_IFACE = 'org.bluez.Adapter1'
LE_ADVERTISING_MANAGER_IFACE = 'org.bluez.LEAdvertisingManager1'
LE_ADVERTISEMENT_IFACE = 'org.bluez.LEAdvertisement1'
DBUS_OM_IFACE = 'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE = 'org.freedesktop.DBus.Properties'
ADAPTER_PATH = '/org/bluez/hci0'

class TestAdvertisement(dbus.service.Object):
    PATH_BASE = '/org/bluez/example/advertisement'
    
    def __init__(self, bus, index):
        self.path = self.PATH_BASE + str(index)
        self.bus = bus
        self.ad_type = 'peripheral'
        self.service_uuids = None
        self.manufacturer_data = {0xFFFF: [0x01, 0x01]}
        self.solicit_uuids = None
        self.service_data = None
        self.local_name = 'rock3c-test'
        self.include_tx_power = False
        self.data = None
        
        dbus.service.Object.__init__(self, bus, self.path)
    
    def get_properties(self):
        properties = dict()
        properties['Type'] = self.ad_type
        
        if self.service_uuids is not None:
            properties['ServiceUUIDs'] = dbus.Array(self.service_uuids, signature='s')
        if self.manufacturer_data is not None:
            f = dbus.Dictionary({}, signature='qv')
            for key, val in self.manufacturer_data.items():
                f[dbus.UInt16(key)] = dbus.Array(val, signature='y')
            properties['ManufacturerData'] = f
        if self.solicit_uuids is not None:
            properties['SolicitUUIDs'] = dbus.Array(self.solicit_uuids, signature='s')
        if self.service_data is not None:
            f = dbus.Dictionary({}, signature='sv')
            for key, val in self.service_data.items():
                f[key] = dbus.Array(val, signature='y')
            properties['ServiceData'] = f
        if self.local_name is not None:
            properties['LocalName'] = self.local_name
        if self.include_tx_power:
            properties['IncludeTxPower'] = dbus.Boolean(True)
        if self.data is not None:
            f = dbus.Dictionary({}, signature='sv')
            for key, val in self.data.items():
                f[key] = dbus.Array(dbus.Byte(v) for v in val)
            properties['Data'] = f
        
        return {LE_ADVERTISEMENT_IFACE: properties}
    
    def get_path(self, _):
        return dbus.ObjectPath(self.path)
    
    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != LE_ADVERTISEMENT_IFACE:
            raise dbus.exceptions.DBusException(
                'org.freedesktop.DBus.Error.InvalidArgs',
                f'No such interface: {interface}')
        return self.get_properties()[LE_ADVERTISEMENT_IFACE]
    
    @dbus.service.method(LE_ADVERTISEMENT_IFACE, in_signature='', out_signature='')
    def Release(self):
        print('Advertisement released')
    
    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
    def Get(self, interface, prop):
        if interface != LE_ADVERTISEMENT_IFACE:
            raise dbus.exceptions.DBusException(
                'org.freedesktop.DBus.Error.InvalidArgs',
                f'No such interface: {interface}')
        if prop not in self.get_properties()[LE_ADVERTISEMENT_IFACE]:
            raise dbus.exceptions.DBusException(
                'org.freedesktop.DBus.Error.InvalidArgs',
                f'No such property {prop}')
        return self.get_properties()[LE_ADVERTISEMENT_IFACE][prop]


def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    
    bus = dbus.SystemBus()
    
    # Get the adapter properties
    adapter_props = dbus.Interface(
        bus.get_object(BLUEZ_SERVICE, ADAPTER_PATH),
        DBUS_PROP_IFACE)
    
    # Power off first
    adapter_props.Set(ADAPTER_IFACE, 'Powered', dbus.Boolean(0))
    time.sleep(0.5)
    
    # Configure adapter
    adapter_props.Set(ADAPTER_IFACE, 'Powered', dbus.Boolean(1))
    time.sleep(0.5)
    
    # Get advertising manager
    om = dbus.Interface(bus.get_object(BLUEZ_SERVICE, '/'), DBUS_OM_IFACE)
    objects = om.GetManagedObjects()
    
    ad_manager = None
    for path, ifaces in objects.items():
        if LE_ADVERTISING_MANAGER_IFACE in ifaces:
            ad_manager = dbus.Interface(
                bus.get_object(BLUEZ_SERVICE, path),
                LE_ADVERTISING_MANAGER_IFACE)
            print(f"Found LEAdvertisingManager at {path}")
            break
    
    if not ad_manager:
        print("ERROR: No LEAdvertisingManager found")
        sys.exit(1)
    
    # Create and register advertisement
    test_ad = TestAdvertisement(bus, 0)
    
    mainloop = GLib.MainLoop()
    
    def register_ad_cb():
        print('Advertisement registered OK')
    
    def register_ad_error_cb(error):
        print(f'Failed to register advertisement: {error}')
        mainloop.quit()
    
    # Register the advertisement
    ad_manager.RegisterAdvertisement(
        test_ad.path,
        {},
        reply_handler=register_ad_cb,
        error_handler=register_ad_error_cb)
    
    print("\n=== ADVERTISING AS 'rock3c-test' ===")
    print("Press Ctrl+C to stop\n")
    
    # Handle Ctrl+C
    def sig_handler(signum, frame):
        print("\nStopping...")
        try:
            ad_manager.UnregisterAdvertisement(test_ad.get_path())
        except:
            pass
        mainloop.quit()
    
    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)
    
    mainloop.run()
    print("Done.")


if __name__ == '__main__':
    main()
