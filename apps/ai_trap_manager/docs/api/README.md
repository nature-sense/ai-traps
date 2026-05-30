# Camera Trap API — Flutter/Dart Integration Guide

## Overview

The camera trap exposes a REST API + SSE (Server-Sent Events) + MJPEG streams on port 8080. This document explains how to consume it from a Flutter/Dart mobile app.

## Quick Start

### 1. Generate a Dart client from OpenAPI spec

The file `openapi.yaml` in this directory is a complete OpenAPI 3.0.3 specification. Use the [openapi-generator](https://openapi-generator.tech/) to generate a Dart client:

```bash
# Install openapi-generator
brew install openapi-generator

# Generate Dart client
openapi-generator generate \
  -i docs/api/openapi.yaml \
  -g dart \
  -o /tmp/camera_trap_api \
  --additional-properties=pubName=camera_trap_api
```

Or use the [Dart OpenAPI Generator](https://pub.dev/packages/openapi_generator) directly in your Flutter project:

```yaml
# pubspec.yaml
dev_dependencies:
  openapi_generator: ^5.0.0
```

Then run:
```bash
dart run openapi_generator generate -i docs/api/openapi.yaml -o lib/api
```

### 2. Manual Dart client (no codegen)

If you prefer not to use code generation, here's a minimal Dart client:

```dart
import 'dart:convert';
import 'dart:io';
import 'package:http/http.dart' as http;

class CameraTrapClient {
  final String baseUrl;
  final http.Client _client = http.Client();

  CameraTrapClient(this.baseUrl);

  // ── Provisioning ────────────────────────────────────────────────────────

  Future<Map<String, dynamic>> provision(String trapId) async {
    final resp = await _client.post(
      Uri.parse('$baseUrl/v1/provision'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'trapId': trapId}),
    );
    return jsonDecode(resp.body);
  }

  // ── Trap Status ─────────────────────────────────────────────────────────

  Future<Map<String, dynamic>> getTrapStatus(String trapId) async {
    final resp = await _client.get(
      Uri.parse('$baseUrl/v1/traps/$trapId'),
    );
    return jsonDecode(resp.body);
  }

  // ── Sessions ────────────────────────────────────────────────────────────

  Future<Map<String, dynamic>> startSession(String trapId) async {
    final resp = await _client.post(
      Uri.parse('$baseUrl/v1/traps/$trapId/sessions'),
    );
    return jsonDecode(resp.body);
  }

  Future<Map<String, dynamic>> stopSession(String trapId, int sessionId) async {
    final resp = await _client.put(
      Uri.parse('$baseUrl/v1/traps/$trapId/sessions/$sessionId/stop'),
    );
    return jsonDecode(resp.body);
  }

  Future<List<Map<String, dynamic>>> listSessions(
    String trapId, {
    int limit = 50,
    int offset = 0,
  }) async {
    final resp = await _client.get(
      Uri.parse('$baseUrl/v1/traps/$trapId/sessions?limit=$limit&offset=$offset'),
    );
    final data = jsonDecode(resp.body);
    return List<Map<String, dynamic>>.from(data['sessions']);
  }

  Future<Map<String, dynamic>> getActiveSession(String trapId) async {
    final resp = await _client.get(
      Uri.parse('$baseUrl/v1/traps/$trapId/sessions/active'),
    );
    return jsonDecode(resp.body);
  }

  // ── Detections ──────────────────────────────────────────────────────────

  Future<List<Map<String, dynamic>>> listDetections(
    String trapId,
    int sessionId, {
    int limit = 50,
    int offset = 0,
  }) async {
    final resp = await _client.get(
      Uri.parse('$baseUrl/v1/traps/$trapId/sessions/$sessionId/detections?limit=$limit&offset=$offset'),
    );
    final data = jsonDecode(resp.body);
    return List<Map<String, dynamic>>.from(data['detections']);
  }

  Future<Map<String, dynamic>> getDetection(String trapId, int detectionId) async {
    final resp = await _client.get(
      Uri.parse('$baseUrl/v1/traps/$trapId/detections/$detectionId'),
    );
    return jsonDecode(resp.body);
  }

  void dispose() {
    _client.close();
  }
}
```

## SSE (Server-Sent Events)

For real-time events, use the `eventsource` package:

```yaml
# pubspec.yaml
dependencies:
  eventsource: ^1.1.0
```

```dart
import 'dart:async';
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:eventsource/eventsource.dart';

class SseService {
  EventSource? _eventSource;
  final StreamController<Map<String, dynamic>> _cropController =
      StreamController<Map<String, dynamic>>.broadcast();
  final StreamController<Map<String, dynamic>> _sessionController =
      StreamController<Map<String, dynamic>>.broadcast();

  Stream<Map<String, dynamic>> get onCropSaved => _cropController.stream;
  Stream<Map<String, dynamic>> get onSessionEvent => _sessionController.stream;

  void connect(String baseUrl) {
    final client = http.Client();
    _eventSource = EventSource(
      Uri.parse('$baseUrl/v1/events'),
      client: client,
    );

    _eventSource!.addEventListener('crop_saved', (event) {
      if (event.data != null) {
        _cropController.add(jsonDecode(event.data!));
      }
    });

    _eventSource!.addEventListener('session_started', (event) {
      if (event.data != null) {
        _sessionController.add(jsonDecode(event.data!));
      }
    });

    _eventSource!.addEventListener('session_stopped', (event) {
      if (event.data != null) {
        _sessionController.add(jsonDecode(event.data!));
      }
    });
  }

  void disconnect() {
    _eventSource?.close();
    _eventSource = null;
  }

  void dispose() {
    disconnect();
    _cropController.close();
    _sessionController.close();
  }
}
```

## MJPEG Streams

For displaying the MJPEG stream in Flutter, use the `mjpeg` package:

```yaml
# pubspec.yaml
dependencies:
  mjpeg: ^1.2.0
```

```dart
import 'package:flutter/material.dart';
import 'package:mjpeg/mjpeg.dart';

class CameraStreamWidget extends StatelessWidget {
  final String streamUrl;

  const CameraStreamWidget({super.key, required this.streamUrl});

  @override
  Widget build(BuildContext context) {
    return Mjpeg(
      stream: streamUrl,
      isLive: true,
      error: (context, error, stackTrace) {
        return const Center(
          child: Text('Stream unavailable'),
        );
      },
    );
  }
}
```

## Crop Images

Detection crop images are served at `/v1/crops/{date}/{filename}`. Use `Image.network()` in Flutter:

```dart
Image.network(
  'http://192.168.1.100:8080/v1/crops/2026-05-12/1715500000_3.jpg',
  loadingBuilder: (context, child, progress) {
    if (progress == null) return child;
    return const CircularProgressIndicator();
  },
  errorBuilder: (context, error, stackTrace) {
    return const Icon(Icons.broken_image);
  },
)
```

## Typical App Flow

```dart
class CameraTrapApp extends StatefulWidget {
  @override
  State<CameraTrapApp> createState() => _CameraTrapAppState();
}

class _CameraTrapAppState extends State<CameraTrapApp> {
  final _client = CameraTrapClient('http://192.168.1.100:8080');
  final _sse = SseService();
  String? _trapId;
  int? _activeSessionId;

  @override
  void initState() {
    super.initState();
    _sse.onCropSaved.listen((crop) {
      // Show notification for new detection
      print('New detection: ${crop['imageUrl']}');
    });
  }

  Future<void> _startMonitoring() async {
    // 1. Provision (first time only)
    if (_trapId == null) {
      final result = await _client.provision('CT-${DateTime.now().year}-001');
      _trapId = result['trapId'];
    }

    // 2. Connect SSE
    _sse.connect('http://192.168.1.100:8080');

    // 3. Start session
    final session = await _client.startSession(_trapId!);
    _activeSessionId = session['id'];
  }

  Future<void> _stopMonitoring() async {
    if (_trapId != null && _activeSessionId != null) {
      await _client.stopSession(_trapId!, _activeSessionId!);
      _activeSessionId = null;
    }
    _sse.disconnect();
  }

  @override
  void dispose() {
    _sse.dispose();
    _client.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Camera Trap')),
      body: Column(
        children: [
          // MJPEG stream
          Expanded(
            child: CameraStreamWidget(
              streamUrl: 'http://192.168.1.100:8080/stream.mjpg',
            ),
          ),
          // Controls
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              ElevatedButton(
                onPressed: _activeSessionId == null ? _startMonitoring : null,
                child: const Text('Start Monitoring'),
              ),
              const SizedBox(width: 16),
              ElevatedButton(
                onPressed: _activeSessionId != null ? _stopMonitoring : null,
                child: const Text('Stop'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
```

## API Summary

| Method | Path | Description |
|--------|------|-------------|
| GET | `/stream.mjpg` | MJPEG stream (640×480, with overlays) |
| GET | `/stream_hires.mjpg` | Hi-res MJPEG stream (1920×1080, raw) |
| GET | `/status` | Server status JSON |
| GET | `/v1/events` | SSE real-time events |
| POST | `/v1/provision` | One-time trap provisioning |
| GET | `/v1/traps/{trapId}` | Trap status |
| POST | `/v1/traps/{trapId}/sessions` | Start session |
| GET | `/v1/traps/{trapId}/sessions` | List sessions |
| GET | `/v1/traps/{trapId}/sessions/active` | Get active session |
| GET | `/v1/traps/{trapId}/sessions/{id}` | Get session |
| PUT | `/v1/traps/{trapId}/sessions/{id}/stop` | Stop session |
| GET | `/v1/traps/{trapId}/sessions/{id}/detections` | List detections |
| GET | `/v1/traps/{trapId}/detections/{id}` | Get detection |
| GET | `/v1/crops/{date}/{filename}` | Serve crop JPEG |

## SSE Events

| Event | Payload |
|-------|---------|
| `session_started` | `{sessionId, startedAt}` |
| `session_stopped` | `{sessionId, stoppedAt, detectionCount}` |
| `track_detected` | `{sessionId, trackId, classId, confidence, timestamp}` |
| `crop_saved` | `{sessionId, detectionId, trackId, classId, confidence, timestamp, imageUrl}` |

## Notes

- **No authentication** — the API is intended for LAN use only
- **No HTTPS** — HTTP only (the device is on a local network)
- **Port 8080** — configurable in `config.toml` under `[network] server_port`
- **Trap discovery** — use mDNS/Bonjour or a simple UDP broadcast for device discovery
