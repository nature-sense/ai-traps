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

import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/ws_api_service.dart';
import '../models/trap_status.dart';
import '../models/session.dart';
import '../models/detection.dart';
import '../models/sse_event.dart';
import '../models/system_metrics.dart';

/// Provider for camera trap state and communication via WebSocket JSON-RPC.
///
/// Uses a single [WsApiService] connection for all communication:
///   - JSON-RPC calls (send method → await response)
///   - Push events (session_started, classification_saved, etc.)
///   - Connection state monitoring
///
/// Binary frames (JPEG images) are handled by [CameraStreamWidget] via its
/// own WebSocket connection (both can share /ws simultaneously).
///
/// This replaces the previous dual-connection approach (CameraTrapApi REST
/// client + separate SseService WebSocket).
///
/// ## Communication Failure Handling
/// - **WebSocket failures**: Triggers disconnect + reconnect dialog
/// - **Health check**: Monitored via connectionState stream (no polling)
class TrapProvider with ChangeNotifier {
  // ── Connection State ────────────────────────────────────────────────────
  String _trapIp = '192.168.1.100';
  String? _trapId;
  String _connectionStatus = 'Disconnected';
  String? _connectionError;

  // ── Trap State ──────────────────────────────────────────────────────────
  TrapStatus? _trapStatus;
  Session? _activeSession;
  List<Session> _sessions = [];
  List<Detection> _detections = [];
  int? _loadedDetectionsSessionId;
  Map<String, dynamic> _config = {};

  // Track unique track IDs seen during the active session (for display)
  final Set<int> _activeTrackIds = {};

  // ── System Metrics ──────────────────────────────────────────────────────
  SystemMetrics? _systemMetrics;

  // ── WebSocket API Service (replaces CameraTrapApi + SseService) ─────────
  final WsApiService _ws = WsApiService();
  StreamSubscription<SseEvent>? _eventSub;
  StreamSubscription<bool>? _connectionSub;

  // ── Health Check ────────────────────────────────────────────────────────
  Timer? _healthCheckTimer;
  static const Duration _healthCheckInterval = Duration(seconds: 15);

  // ── Connection Lost Flag ────────────────────────────────────────────────
  bool _connectionLost = false;

  // ── Getters ─────────────────────────────────────────────────────────────
  String get trapIp => _trapIp;
  String? get trapId => _trapId;
  String get connectionStatus => _connectionStatus;
  String? get connectionError => _connectionError;
  TrapStatus? get trapStatus => _trapStatus;
  Session? get activeSession => _activeSession;
  List<Session> get sessions => _sessions;
  List<Detection> get detections => _detections;
  Map<String, dynamic> get config => _config;
  bool get isConnected => _connectionStatus == 'Connected';
  bool get hasActiveSession => _activeSession != null;
  WsApiService get ws => _ws;
  SystemMetrics? get systemMetrics => _systemMetrics;
  bool get connectionLost => _connectionLost;

  /// WebSocket URL for the current trap (for CameraStreamWidget).
  String get wsUrl => 'ws://$_trapIp:8080/ws';

  // ── Connection ──────────────────────────────────────────────────────────

  /// Connect to a trap at the given hostname.
  Future<void> connectToTrap(String hostname) async {
    _trapIp = hostname;
    _connectionStatus = 'Connecting...';
    _connectionError = null;
    _connectionLost = false;
    notifyListeners();

    debugPrint('[TrapProvider] ===== CONNECTING TO TRAP =====');
    debugPrint('[TrapProvider] hostname: $hostname');

    try {
      // Start listening to connection state changes
      _connectionSub = _ws.connectionState.listen((connected) {
        if (!connected && _connectionStatus == 'Connected') {
          debugPrint('[TrapProvider] WebSocket connection lost');
          _handleConnectionLost();
        }
      });

      // Connect WebSocket (await ensures _ws is ready before sending)
      await _ws.connect(wsUrl);

      // Send get_status to verify the trap is alive
      final status = await _ws.send(
        'get_status',
        timeout: const Duration(seconds: 10),
      );
      debugPrint('[TrapProvider] Server status response: $status');

      if (status['status'] == 'running') {
        _connectionStatus = 'Connected';
        debugPrint('[TrapProvider] Connection successful');

        // Save to connection history
        await _addToConnectionHistory(hostname);

        // If the server returns a trapId in status, use it
        if (status.containsKey('trapId') && status['trapId'] != null) {
          _trapId = status['trapId'] as String;
          debugPrint('[TrapProvider] Got trapId from server: $_trapId');
        } else {
          debugPrint('[TrapProvider] No trapId in server status');
        }

        // Listen for push events (replaces SSE)
        _eventSub = _ws.events.listen(_onSseEvent);

        // Start periodic health check
        _startHealthCheck();

        // Fetch initial metrics
        fetchSystemMetrics();

        // Fetch sessions list
        await listSessions();

        // If we have a trapId, fetch status
        if (_trapId != null) {
          debugPrint('[TrapProvider] Fetching trap status for $_trapId');
          await _fetchTrapStatus();
        } else {
          debugPrint('[TrapProvider] Skipping trap status - no trapId');
        }
      } else {
        _connectionStatus = 'Connection Failed';
        _connectionError = 'Server status: ${status['status']}';
        debugPrint(
          '[TrapProvider] Unexpected server status: ${status['status']}',
        );
      }
    } on TimeoutException {
      _connectionStatus = 'Connection Failed';
      _connectionError = 'Connection timed out';
      debugPrint('[TrapProvider] WebSocket connection timed out');
      _cleanupConnection();
    } on WsApiException catch (e) {
      _connectionStatus = 'Error';
      _connectionError = e.message;
      debugPrint('[TrapProvider] API error: $e');
      _cleanupConnection();
    } catch (e) {
      _connectionStatus = 'Error';
      _connectionError = e.toString();
      debugPrint('[TrapProvider] Connection error: $e');
      _cleanupConnection();
    }
    notifyListeners();
  }

  /// Disconnect from the current trap.
  void disconnectFromTrap() {
    _stopHealthCheck();
    _cleanupConnection();
    _connectionStatus = 'Disconnected';
    _connectionError = null;
    _connectionLost = false;
    _trapStatus = null;
    _activeSession = null;
    _sessions = [];
    _detections = [];
    notifyListeners();
  }

  /// Clean up WebSocket subscriptions and disconnect.
  void _cleanupConnection() {
    _eventSub?.cancel();
    _eventSub = null;
    _connectionSub?.cancel();
    _connectionSub = null;
    _ws.disconnect();
  }

  // ── Health Check ────────────────────────────────────────────────────────

  void _startHealthCheck() {
    _stopHealthCheck();
    _healthCheckTimer = Timer.periodic(_healthCheckInterval, (_) {
      _performHealthCheck();
    });
  }

  void _stopHealthCheck() {
    _healthCheckTimer?.cancel();
    _healthCheckTimer = null;
  }

  /// Perform a health check by querying the WebSocket connection state.
  Future<void> _performHealthCheck() async {
    if (!_ws.isConnected || !isConnected) return;

    try {
      await _ws.send('get_status');
    } catch (e) {
      if (_isNetworkError(e)) {
        debugPrint('[TrapProvider] Health check failed: $e');
        _handleResponseFailure();
      }
    }
  }

  // ── Network Error Detection ─────────────────────────────────────────────

  bool _isNetworkError(Object error) {
    return error is SocketException ||
        error is WebSocketException ||
        error is TimeoutException ||
        error is HandshakeException;
  }

  /// Handle a connection failure by disconnecting.
  void _handleResponseFailure() {
    if (!isConnected) return;
    debugPrint('[TrapProvider] ===== CONNECTION LOST =====');
    _stopHealthCheck();
    _cleanupConnection();
    _connectionStatus = 'Disconnected';
    _connectionError = null;
    _connectionLost = true;
    _trapStatus = null;
    _activeSession = null;
    _sessions = [];
    _detections = [];
    notifyListeners();
  }

  /// Called when the connectionState stream indicates disconnection.
  void _handleConnectionLost() {
    if (!isConnected) return;
    _stopHealthCheck();
    _cleanupConnection();
    _connectionStatus = 'Disconnected';
    _connectionLost = true;
    notifyListeners();
  }

  // ── Connection History ──────────────────────────────────────────────────

  static const String _historyKey = 'connection_history';
  List<String> _connectionHistory = [];

  List<String> get connectionHistory => List.unmodifiable(_connectionHistory);

  Future<void> loadConnectionHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      _connectionHistory = prefs.getStringList(_historyKey) ?? [];
      notifyListeners();
    } catch (e) {
      debugPrint('[TrapProvider] Failed to load connection history: $e');
    }
  }

  Future<void> _addToConnectionHistory(String hostname) async {
    _connectionHistory.remove(hostname);
    _connectionHistory.insert(0, hostname);
    if (_connectionHistory.length > 20) {
      _connectionHistory = _connectionHistory.sublist(0, 20);
    }
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_historyKey, _connectionHistory);
    } catch (e) {
      debugPrint('[TrapProvider] Failed to save connection history: $e');
    }
    notifyListeners();
  }

  Future<void> removeFromConnectionHistory(String hostname) async {
    _connectionHistory.remove(hostname);
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_historyKey, _connectionHistory);
    } catch (e) {
      debugPrint('[TrapProvider] Failed to save connection history: $e');
    }
    notifyListeners();
  }

  Future<void> clearConnectionHistory() async {
    _connectionHistory.clear();
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove(_historyKey);
    } catch (e) {
      debugPrint('[TrapProvider] Failed to clear connection history: $e');
    }
    notifyListeners();
  }

  // ── Provisioning ────────────────────────────────────────────────────────

  Future<bool> provisionTrap(String trapId) async {
    try {
      final result = await _ws.send('provision', params: {'trapId': trapId});
      _trapId = result['trapId'] as String?;
      notifyListeners();
      return true;
    } on WsApiException catch (e) {
      _connectionError = 'Provisioning failed: $e';
      notifyListeners();
      return false;
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else {
        _connectionError = 'Provisioning failed: $e';
        notifyListeners();
      }
      return false;
    }
  }

  // ── Trap Status ─────────────────────────────────────────────────────────

  Future<void> _fetchTrapStatus() async {
    try {
      final result = await _ws.send('get_status');

      if (result.containsKey('activeSession') &&
          result['activeSession'] is Map) {
        final active = result['activeSession'] as Map<String, dynamic>;
        if (active['active'] == true) {
          final sessionId =
              active['id'] as int? ?? active['sessionId'] as int? ?? -1;
          final serverCount = active['detectionCount'] as int? ?? 0;

          _activeTrackIds.clear();
          _activeSession = Session(
            id: sessionId,
            startedAt: (active['startedAt'] as num?)?.toInt() ?? 0,
            detectionCount: serverCount,
          );
          debugPrint(
            '[TrapProvider] Active session: id=$sessionId count=$serverCount',
          );
        } else {
          _activeSession = null;
          _activeTrackIds.clear();
        }
      } else {
        _activeSession = null;
        _activeTrackIds.clear();
      }
    } on WsApiException catch (e) {
      debugPrint('[TrapProvider] API error fetching trap status: $e');
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else if (kDebugMode) {
        print('Error fetching trap status: $e');
      }
    }
  }

  Future<void> refreshStatus() async {
    await _fetchTrapStatus();
    notifyListeners();
  }

  // ── Sessions ────────────────────────────────────────────────────────────

  Future<bool> startSession() async {
    debugPrint('[TrapProvider] ===== START SESSION =====');
    debugPrint(
      '[TrapProvider] _ws.isConnected=${_ws.isConnected} _trapId=$_trapId',
    );

    if (!_ws.isConnected) {
      debugPrint('[TrapProvider] startSession FAILED: not connected');
      return false;
    }

    try {
      debugPrint('[TrapProvider] Calling start_session');
      final result = await _ws.send('start_session');
      final sessionId = result['sessionId'] as int?;
      if (sessionId == null) {
        debugPrint('[TrapProvider] startSession: no sessionId in response');
        return false;
      }
      debugPrint('[TrapProvider] Session started: id=$sessionId');
      _activeTrackIds.clear();
      _activeSession = Session(
        id: sessionId,
        startedAt:
            (result['startedAt'] as num?)?.toInt() ??
            DateTime.now().millisecondsSinceEpoch,
        detectionCount: 0,
      );
      // Also refresh the sessions list
      await listSessions();
      notifyListeners();
      return true;
    } on WsApiException catch (e) {
      debugPrint('[TrapProvider] startSession API ERROR: $e');
      _connectionError = 'Failed to start session: $e';
      notifyListeners();
      return false;
    } catch (e) {
      debugPrint('[TrapProvider] startSession ERROR: $e');
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else {
        _connectionError = 'Failed to start session: $e';
        notifyListeners();
      }
      return false;
    }
  }

  Future<bool> stopSession() async {
    debugPrint('[TrapProvider] ===== STOP SESSION =====');
    debugPrint(
      '[TrapProvider] _ws.isConnected=${_ws.isConnected} _activeSession=${_activeSession?.id}',
    );

    if (!_ws.isConnected) {
      debugPrint('[TrapProvider] stopSession FAILED: not connected');
      return false;
    }
    if (_activeSession == null) {
      debugPrint('[TrapProvider] stopSession FAILED: no active session');
      return false;
    }

    try {
      debugPrint('[TrapProvider] Calling stop_session(${_activeSession!.id})');
      final result = await _ws.send(
        'stop_session',
        params: {'session_id': _activeSession!.id},
      );
      debugPrint('[TrapProvider] Session stopped: $result');
      _activeSession = null;
      await listSessions();
      notifyListeners();
      return true;
    } on WsApiException catch (e) {
      debugPrint('[TrapProvider] stopSession API ERROR: $e');
      _connectionError = 'Failed to stop session: $e';
      notifyListeners();
      return false;
    } catch (e) {
      debugPrint('[TrapProvider] stopSession ERROR: $e');
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else {
        _connectionError = 'Failed to stop session: $e';
        notifyListeners();
      }
      return false;
    }
  }

  Future<void> listSessions({int limit = 50, int offset = 0}) async {
    debugPrint(
      '[TrapProvider] listSessions: _ws.isConnected=${_ws.isConnected}',
    );
    if (!_ws.isConnected) {
      debugPrint('[TrapProvider] listSessions skipped: not connected');
      return;
    }

    try {
      final result = await _ws.send(
        'list_sessions',
        params: {'limit': limit, 'offset': offset},
      );
      final list = result['sessions'] as List?;
      if (list != null) {
        _sessions = list.map((s) {
          final m = s as Map<String, dynamic>;
          return Session.fromJson(m);
        }).toList();
      }
      debugPrint(
        '[TrapProvider] listSessions returned ${_sessions.length} sessions',
      );
      notifyListeners();
    } on WsApiException catch (e) {
      debugPrint('[TrapProvider] listSessions API ERROR: $e');
    } catch (e) {
      debugPrint('[TrapProvider] listSessions ERROR: $e');
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      }
    }
  }

  // ── Detections ──────────────────────────────────────────────────────────

  Future<void> listDetections(
    int sessionId, {
    int limit = 50,
    int offset = 0,
  }) async {
    if (!_ws.isConnected) return;

    try {
      final result = await _ws.send(
        'list_detections',
        params: {'session_id': sessionId, 'limit': limit, 'offset': offset},
      );
      final list = result['detections'] as List?;
      if (list != null) {
        _detections = list.map((d) {
          final m = d as Map<String, dynamic>;
          return Detection.fromJson(m);
        }).toList();
      }
      _loadedDetectionsSessionId = sessionId;
      notifyListeners();
    } on WsApiException catch (e) {
      if (kDebugMode) {
        print('API error listing detections: $e');
      }
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else if (kDebugMode) {
        print('Error listing detections: $e');
      }
    }
  }

  Future<Detection?> getDetection(int detectionId) async {
    if (!_ws.isConnected) return null;

    try {
      final result = await _ws.send(
        'get_detection',
        params: {'detection_id': detectionId},
      );
      return Detection.fromJson(result);
    } on WsApiException catch (e) {
      if (kDebugMode) {
        print('API error getting detection: $e');
      }
      return null;
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else if (kDebugMode) {
        print('Error getting detection: $e');
      }
      return null;
    }
  }

  // ── Configuration (Legacy) ──────────────────────────────────────────────

  Future<void> fetchConfig() async {
    if (!_ws.isConnected) return;

    try {
      final status = await _ws.send('get_status');
      _config = status;
      notifyListeners();
    } on WsApiException catch (e) {
      if (kDebugMode) {
        print('API error fetching config: $e');
      }
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      } else if (kDebugMode) {
        print('Error fetching config: $e');
      }
    }
  }

  Future<void> updateConfig(Map<String, dynamic> newConfig) async {
    _config = newConfig;
    notifyListeners();
  }

  // ── System Metrics ──────────────────────────────────────────────────────

  Future<void> fetchSystemMetrics() async {
    if (!_ws.isConnected) return;

    try {
      final result = await _ws.send('get_system_metrics');
      if (result.isNotEmpty) {
        _systemMetrics = SystemMetrics.fromJson(result);
      }
      notifyListeners();
    } on WsApiException catch (e) {
      if (e.code == 501) {
        debugPrint('[TrapProvider] System metrics not implemented');
        return;
      }
      debugPrint('[TrapProvider] fetchSystemMetrics ERROR: $e');
    } catch (e) {
      debugPrint('[TrapProvider] fetchSystemMetrics ERROR: $e');
      if (_isNetworkError(e)) {
        _handleResponseFailure();
      }
    }
  }

  // ── Push Events ─────────────────────────────────────────────────────────

  void _onSseEvent(SseEvent event) {
    debugPrint('[TrapProvider] WS event: ${event.type} data=${event.data}');

    switch (event.type) {
      case SseEventType.sessionStarted:
        listSessions();
        _activeTrackIds.clear();
        if (event.sessionId != null) {
          _activeSession = Session(
            id: event.sessionId!,
            startedAt: event.startedAt ?? DateTime.now().millisecondsSinceEpoch,
            detectionCount: 0,
          );
          notifyListeners();
        }
        break;

      case SseEventType.sessionStopped:
        listSessions();
        _activeSession = null;
        _activeTrackIds.clear();
        notifyListeners();
        break;

      case SseEventType.trackDetected:
        if (_activeSession != null && event.trackId != null) {
          _activeTrackIds.add(event.trackId!);
          _activeSession = Session(
            id: _activeSession!.id,
            startedAt: _activeSession!.startedAt,
            detectionCount: _activeTrackIds.length,
          );
          notifyListeners();
        }
        break;

      case SseEventType.classificationSaved:
        if (_activeSession != null && event.data['trackId'] != null) {
          final trackId = event.data['trackId'] as int;
          _activeTrackIds.add(trackId);

          if (event.data['classificationId'] != null) {
            final newDetection = Detection(
              id: event.data['classificationId'] as int,
              timestamp:
                  (event.data['timestamp'] as num?)?.toInt() ??
                  DateTime.now().millisecondsSinceEpoch,
              trackId: trackId,
              classId: (event.data['classId'] as num?)?.toInt() ?? 0,
              confidence: (event.data['confidence'] as num?)?.toDouble() ?? 0.0,
              sessionId: _activeSession!.id,
              imageUrl: event.data['imagePath'] as String? ?? '',
            );

            if (_loadedDetectionsSessionId == _activeSession!.id) {
              final existingIndex = _detections.indexWhere(
                (d) =>
                    d.trackId == trackId && d.sessionId == _activeSession!.id,
              );
              if (existingIndex != -1) {
                _detections[existingIndex] = newDetection;
              } else {
                _detections.add(newDetection);
              }
            }
          }

          _activeSession = Session(
            id: _activeSession!.id,
            startedAt: _activeSession!.startedAt,
            detectionCount: _activeTrackIds.length,
          );
          notifyListeners();
        }
        break;
    }
  }

  // ── Cleanup ─────────────────────────────────────────────────────────────

  @override
  void dispose() {
    _stopHealthCheck();
    _cleanupConnection();
    _ws.dispose();
    super.dispose();
  }
}
