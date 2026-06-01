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

import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../models/trap_status.dart';
import '../models/session.dart';
import '../models/detection.dart';
import '../models/system_metrics.dart';

/// API client for the NatureSense Camera Trap v1 REST API.
///
/// Communicates with the camera trap device over LAN on port 8080.
/// All endpoints are defined in the OpenAPI 3.0.3 spec at docs/api/openapi.yaml.
class CameraTrapApi {
  final String host;
  final int port;
  final String baseUrl;
  final http.Client _client;

  CameraTrapApi({required this.host, this.port = 8080, http.Client? client})
    : baseUrl = 'http://$host:$port',
      _client = client ?? http.Client();

  // ── Provisioning ────────────────────────────────────────────────────────

  /// Provision the trap with a unique ID (one-time operation).
  Future<Map<String, dynamic>> provisionTrap(String trapId) async {
    final url = '$baseUrl/v1/provision';
    final body = jsonEncode({'trapId': trapId});
    debugPrint('[CameraTrapApi] POST $url body=$body');
    try {
      final resp = await _client.post(
        Uri.parse(url),
        headers: {'Content-Type': 'application/json'},
        body: body,
      );
      debugPrint(
        '[CameraTrapApi] POST $url -> ${resp.statusCode} ${resp.body}',
      );
      if (resp.statusCode == 201) {
        return jsonDecode(resp.body) as Map<String, dynamic>;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] POST $url FAILED: $e');
      rethrow;
    }
  }

  // ── Trap Status ─────────────────────────────────────────────────────────

  /// Get trap status and active session info.
  Future<TrapStatus> getTrapStatus(String trapId) async {
    final url = '$baseUrl/v1/traps/$trapId';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return TrapStatus.fromJson(
          jsonDecode(resp.body) as Map<String, dynamic>,
        );
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  // ── Sessions ────────────────────────────────────────────────────────────

  /// Start a new monitoring session.
  Future<Session> startSession(String trapId) async {
    final url = '$baseUrl/v1/traps/$trapId/sessions';
    debugPrint('[CameraTrapApi] POST $url');
    try {
      final resp = await _client.post(Uri.parse(url));
      debugPrint(
        '[CameraTrapApi] POST $url -> ${resp.statusCode} ${resp.body}',
      );
      if (resp.statusCode == 201) {
        return Session.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] POST $url FAILED: $e');
      rethrow;
    }
  }

  /// List all sessions (most recent first).
  Future<List<Session>> listSessions(
    String trapId, {
    int limit = 50,
    int offset = 0,
  }) async {
    final url =
        '$baseUrl/v1/traps/$trapId/sessions?limit=$limit&offset=$offset';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        final data = jsonDecode(resp.body) as Map<String, dynamic>;
        final sessions = (data['sessions'] as List)
            .map((s) => Session.fromJson(s as Map<String, dynamic>))
            .toList();
        return sessions;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  /// Get the currently active session (if any).
  Future<Map<String, dynamic>> getActiveSession(String trapId) async {
    final url = '$baseUrl/v1/traps/$trapId/sessions/active';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return jsonDecode(resp.body) as Map<String, dynamic>;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  /// Get session details.
  Future<Session> getSession(String trapId, int sessionId) async {
    final url = '$baseUrl/v1/traps/$trapId/sessions/$sessionId';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return Session.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  /// Stop a monitoring session.
  Future<Session> stopSession(String trapId, int sessionId) async {
    final url = '$baseUrl/v1/traps/$trapId/sessions/$sessionId/stop';
    debugPrint('[CameraTrapApi] PUT $url');
    try {
      final resp = await _client.put(Uri.parse(url));
      debugPrint('[CameraTrapApi] PUT $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return Session.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] PUT $url FAILED: $e');
      rethrow;
    }
  }

  // ── Detections ──────────────────────────────────────────────────────────

  /// List detections for a session.
  Future<List<Detection>> listDetections(
    String trapId,
    int sessionId, {
    int limit = 50,
    int offset = 0,
  }) async {
    final url =
        '$baseUrl/v1/traps/$trapId/sessions/$sessionId/detections'
        '?limit=$limit&offset=$offset';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        final data = jsonDecode(resp.body) as Map<String, dynamic>;
        final detections = (data['detections'] as List)
            .map((d) => Detection.fromJson(d as Map<String, dynamic>))
            .toList();
        return detections;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  /// Get a single detection.
  Future<Detection> getDetection(String trapId, int detectionId) async {
    final url = '$baseUrl/v1/traps/$trapId/detections/$detectionId';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return Detection.fromJson(
          jsonDecode(resp.body) as Map<String, dynamic>,
        );
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  // ── Crop Images ─────────────────────────────────────────────────────────

  /// Get the full URL for a crop image.
  String getCropImageUrl(String date, String filename) {
    return '$baseUrl/v1/crops/$date/$filename';
  }

  // ── MJPEG Streams ───────────────────────────────────────────────────────

  /// Get the URL for the standard MJPEG stream (640×480 with overlays).
  String get mjpegStreamUrl => '$baseUrl/stream.mjpg';

  /// Get the URL for the hi-res MJPEG stream (1920×1080 raw).
  String get hiResMjpegStreamUrl => '$baseUrl/stream_hires.mjpg';

  // ── Server Status ───────────────────────────────────────────────────────

  /// Get server status.
  Future<Map<String, dynamic>> getServerStatus() async {
    final url = '$baseUrl/status';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return jsonDecode(resp.body) as Map<String, dynamic>;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  // ── System Metrics ──────────────────────────────────────────────────────

  /// Get real-time system metrics (CPU, memory, disk, temperature, pipeline).
  ///
  /// Note: The backend does not currently implement this endpoint (returns 501).
  /// Returns null when the endpoint is not available.
  Future<SystemMetrics?> getSystemMetrics(String trapId) async {
    final url = '$baseUrl/v1/traps/$trapId/system';
    debugPrint('[CameraTrapApi] GET $url');
    try {
      final resp = await _client.get(Uri.parse(url));
      debugPrint('[CameraTrapApi] GET $url -> ${resp.statusCode} ${resp.body}');
      if (resp.statusCode == 200) {
        return SystemMetrics.fromJson(
          jsonDecode(resp.body) as Map<String, dynamic>,
        );
      }
      if (resp.statusCode == 501) {
        debugPrint(
          '[CameraTrapApi] System metrics endpoint not implemented (501)',
        );
        return null;
      }
      throw ApiException(resp.statusCode, resp.body);
    } catch (e) {
      debugPrint('[CameraTrapApi] GET $url FAILED: $e');
      rethrow;
    }
  }

  // ── SSE ─────────────────────────────────────────────────────────────────

  /// Get the URL for the SSE event stream.
  /// The backend serves SSE at /events (not /v1/events).
  String get sseUrl => '$baseUrl/events';

  // ── WebSocket ───────────────────────────────────────────────────────────

  /// Get the WebSocket URL.
  /// Note: The backend does not currently expose a WebSocket endpoint.
  /// SSE at /events is the supported real-time channel.
  String get wsUrl => '$baseUrl/events'.replaceFirst('http', 'ws');

  /// Dispose the HTTP client.
  void dispose() {
    _client.close();
  }
}

/// Exception thrown when the API returns a non-success status code.
class ApiException implements Exception {
  final int statusCode;
  final String body;

  ApiException(this.statusCode, this.body);

  @override
  String toString() => 'ApiException($statusCode): $body';
}
