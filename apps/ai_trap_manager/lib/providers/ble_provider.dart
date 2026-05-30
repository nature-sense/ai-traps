// Copyright 2026 Nature Sense
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleProvider with ChangeNotifier {
  bool _isScanning = false;
  List<BluetoothDevice> _devices = [];
  BluetoothDevice? _connectedDevice;
  String _connectionStatus = 'Disconnected';

  bool get isScanning => _isScanning;
  List<BluetoothDevice> get devices => _devices;
  BluetoothDevice? get connectedDevice => _connectedDevice;
  String get connectionStatus => _connectionStatus;

  Future<void> startScan() async {
    _isScanning = true;
    notifyListeners();
    
    _devices.clear();
    
    // Listen for scan results
    FlutterBluePlus.scanResults.listen((results) {
      for (ScanResult result in results) {
        if (!_devices.contains(result.device)) {
          _devices.add(result.device);
          notifyListeners();
        }
      }
    });
    
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    
    _isScanning = false;
    notifyListeners();
  }

  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
    _isScanning = false;
    notifyListeners();
  }

  Future<void> connectToDevice(BluetoothDevice device) async {
    _connectionStatus = 'Connecting...';
    notifyListeners();
    
    try {
      await device.connect();
      _connectedDevice = device;
      _connectionStatus = 'Connected';
      notifyListeners();
    } catch (e) {
      _connectionStatus = 'Connection Failed: $e';
      notifyListeners();
    }
  }

  Future<void> disconnectDevice() async {
    if (_connectedDevice != null) {
      await _connectedDevice!.disconnect();
      _connectedDevice = null;
      _connectionStatus = 'Disconnected';
      notifyListeners();
    }
  }

  Future<void> sendWifiCredentials(String ssid, String password) async {
    // This would send WiFi credentials via BLE to the trap
    // Implementation depends on your trap's BLE service
    if (_connectedDevice != null) {
      // Find the service and characteristic
      // Write the credentials
      _connectionStatus = 'Sending WiFi credentials...';
      notifyListeners();
      
      // Simulate sending
      await Future.delayed(const Duration(seconds: 2));
      
      _connectionStatus = 'WiFi credentials sent';
      notifyListeners();
    }
  }
}
