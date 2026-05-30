import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import '../services/camera_trap_api.dart';
import '../services/sse_service.dart';
import '../models/trap_status.dart';
import '../models/session.dart';
import '../models/detection.dart';
import '../models/sse_event.dart';
import '../models/system_metrics.dart';

/// Provider for camera trap state and v1 API communication.
///
/// Manages trap connection, provisioning, session lifecycle, and detection data
/// using the CameraTrapApi client which targets the /v1/* OpenAPI endpoints.
/// Listens to SSE events for real-time session updates.
///
/// ## Communication Failure Handling
/// - **REST API failures** (network errors): Triggers disconnect + reconnect dialog
/// - **SSE failures**: Auto-reconnects (transient, handled by SseService)
/// - **MJPEG failures**: Auto-reconnects (transient, handled by CameraStreamWidget)
/// - **Health check**: Periodically polls GET /status every 15s to detect trap loss
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
  int? _loadedDetectionsSessionId; // Tracks which session's detections are in _detections
  Map<String, dynamic> _config = {};

  // Track unique track IDs seen during the active session (for display)
  final Set<int> _activeTrackIds = {};

  // ── System Metrics ──────────────────────────────────────────────────────
  SystemMetrics? _systemMetrics;

  // ── API Client ──────────────────────────────────────────────────────────
  CameraTrapApi? _api;

  // ── SSE Service ─────────────────────────────────────────────────────────
  final SseService _sseService = SseService();
  StreamSubscription<SseEvent>? _sseSubscription;

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
  CameraTrapApi? get api => _api;
  SystemMetrics? get systemMetrics => _systemMetrics;
  bool get connectionLost => _connectionLost;

  // ── Connection ──────────────────────────────────────────────────────────

  /// Connect to a trap at the given hostname.
  ///
  /// Verifies connectivity by fetching server status, then optionally
  /// fetches trap status if a trapId is known.
  Future<void> connectToTrap(String hostname) async {
    _trapIp = hostname;
    _connectionStatus = 'Connecting...';
    _connectionError = null;
    _connectionLost = false;
    notifyListeners();

    debugPrint('[TrapProvider] ===== CONNECTING TO TRAP =====');
    debugPrint('[TrapProvider] hostname: $hostname');

    try {
      _api = CameraTrapApi(host: hostname);
      debugPrint('[TrapProvider] API base URL: ${_api!.baseUrl}');

      final status = await _api!.getServerStatus();
      debugPrint('[TrapProvider] Server status response: $status');

      if (status['status'] == 'running') {
        _connectionStatus = 'Connected';
        debugPrint('[TrapProvider] Connection successful');

        // Save to connection history
        await _addToConnectionHistory(hostname);

        // If the server returns a trapId in status, use it
        // (the backend may include it in future versions)
        if (status.containsKey('trapId') && status['trapId'] != null) {
          _trapId = status['trapId'] as String;
          debugPrint('[TrapProvider] Got trapId from server status: $_trapId');
        } else {
          debugPrint('[TrapProvider] No trapId in server status. trapId=$_trapId');
          debugPrint('[TrapProvider] Session operations will fail until provisioned!');
        }

        // Connect SSE for real-time events
        _connectSse();

        // Start periodic health check
        _startHealthCheck();

        // Fetch sessions list
        await listSessions();

        // If we have a trapId, fetch the trap status
        if (_trapId != null) {
          debugPrint('[TrapProvider] Fetching trap status for trapId: $_trapId');
          await _fetchTrapStatus();
        } else {
          debugPrint('[TrapProvider] Skipping trap status fetch - no trapId');
        }
      } else {
        _connectionStatus = 'Connection Failed';
        _connectionError = 'Server status: ${status['status']}';
        debugPrint('[TrapProvider] Connection failed - unexpected status: ${status['status']}');
      }
    } catch (e) {
      _connectionStatus = 'Error';
      _connectionError = e.toString();
      _api = null;
      debugPrint('[TrapProvider] Connection error: $e');
    }
    notifyListeners();
  }

  /// Disconnect from the current trap.
  void disconnectFromTrap() {
    _stopHealthCheck();
    _disconnectSse();
    _api?.dispose();
    _api = null;
    _connectionStatus = 'Disconnected';
    _connectionError = null;
    _connectionLost = false;
    _trapStatus = null;
    _activeSession = null;
    _sessions = [];
    _detections = [];
    notifyListeners();
  }

  // ── Health Check ────────────────────────────────────────────────────────

  /// Start the periodic health check timer.
  void _startHealthCheck() {
    _stopHealthCheck();
    _healthCheckTimer = Timer.periodic(_healthCheckInterval, (_) {
      _performHealthCheck();
    });
  }

  /// Stop the periodic health check timer.
  void _stopHealthCheck() {
    _healthCheckTimer?.cancel();
    _healthCheckTimer = null;
  }

  /// Perform a health check by polling the server status endpoint.
  ///
  /// If the request fails with a network error, the trap is considered lost
  /// and we trigger a disconnect + reconnect flow.
  Future<void> _performHealthCheck() async {
    if (_api == null || !isConnected) return;

    try {
      await _api!.getServerStatus();
      // Health check succeeded — trap is still alive
    } catch (e) {
      if (_isNetworkError(e)) {
        debugPrint('[TrapProvider] Health check failed — trap unreachable: $e');
        _handleRestFailure();
      }
      // Non-network errors (e.g. HTTP 4xx/5xx) mean the trap is still alive
    }
  }

  // ── Network Error Detection ─────────────────────────────────────────────

  /// Returns true if [error] is a network-level failure indicating the trap
  /// is unreachable (as opposed to an HTTP API error response).
  bool _isNetworkError(Object error) {
    return error is SocketException ||
        error is http.ClientException ||
        error is TimeoutException ||
        error is HandshakeException;
  }

  /// Handle a REST API network failure by disconnecting and flagging the
  /// connection as lost so the UI can show the reconnect dialog.
  ///
  /// This performs the same cleanup as [disconnectFromTrap] but keeps
  /// [_connectionLost] set to `true` so the UI can react to it.
  void _handleRestFailure() {
    if (!isConnected) return; // avoid double-disconnect
    debugPrint('[TrapProvider] ===== CONNECTION LOST =====');
    _stopHealthCheck();
    _disconnectSse();
    _api?.dispose();
    _api = null;
    _connectionStatus = 'Disconnected';
    _connectionError = null;
    _connectionLost = true; // keep this set so UI can detect it
    _trapStatus = null;
    _activeSession = null;
    _sessions = [];
    _detections = [];
    notifyListeners();
  }

  // ── Connection History (persisted via shared_preferences) ──────────────

  static const String _historyKey = 'connection_history';
  List<String> _connectionHistory = [];

  /// List of previously connected trap hostnames, most recent first.
  List<String> get connectionHistory => List.unmodifiable(_connectionHistory);

  /// Load connection history from shared preferences.
  Future<void> loadConnectionHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      _connectionHistory = prefs.getStringList(_historyKey) ?? [];
      notifyListeners();
    } catch (e) {
      debugPrint('[TrapProvider] Failed to load connection history: $e');
    }
  }

  /// Add a hostname to the connection history (deduped, most recent first).
  Future<void> _addToConnectionHistory(String hostname) async {
    _connectionHistory.remove(hostname);
    _connectionHistory.insert(0, hostname);
    // Keep max 20 entries
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

  /// Remove a hostname from the connection history.
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

  /// Clear all connection history.
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

  /// Provision the trap with a unique ID (one-time operation).
  Future<bool> provisionTrap(String trapId) async {
    if (_api == null) return false;

    try {
      final result = await _api!.provisionTrap(trapId);
      _trapId = result['trapId'] as String?;
      notifyListeners();
      return true;
    } on ApiException catch (e) {
      // Trap responded with an error — log it, don't disconnect
      _connectionError = 'Provisioning failed: $e';
      notifyListeners();
      return false;
    } catch (e) {
      // Network-level error — trap is gone
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else {
        _connectionError = 'Provisioning failed: $e';
        notifyListeners();
      }
      return false;
    }
  }

  // ── Trap Status ─────────────────────────────────────────────────────────

  Future<void> _fetchTrapStatus() async {
    if (_api == null || _trapId == null) return;

    try {
      _trapStatus = await _api!.getTrapStatus(_trapId!);
      if (_trapStatus!.activeSession != null) {
        final sessionId = _trapStatus!.activeSession!.sessionId;
        final serverCount = _trapStatus!.activeSession!.detectionCount;

        // Seed the track ID set by fetching existing detections for this session
        _activeTrackIds.clear();
        try {
          final existingDetections = await _api!.listDetections(
            _trapId!,
            sessionId,
            limit: 1000,
            offset: 0,
          );
          for (final d in existingDetections) {
            _activeTrackIds.add(d.trackId);
          }
          debugPrint('[TrapProvider] Seeded _activeTrackIds with ${_activeTrackIds.length} track IDs from ${existingDetections.length} detections');
        } on ApiException catch (e) {
          debugPrint('[TrapProvider] Could not fetch detections for track ID seeding: $e');
          // Fall back to server's detection count if we can't fetch detections
        } catch (e) {
          if (_isNetworkError(e)) {
            _handleRestFailure();
            return;
          }
          debugPrint('[TrapProvider] Could not fetch detections for track ID seeding: $e');
        }

        _activeSession = Session(
          id: sessionId,
          startedAt: _trapStatus!.activeSession!.startedAt,
          detectionCount: _activeTrackIds.isNotEmpty
              ? _activeTrackIds.length
              : serverCount,
        );
      } else {
        _activeSession = null;
        _activeTrackIds.clear();
      }
    } on ApiException catch (e) {
      debugPrint('[TrapProvider] API error fetching trap status: $e');
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else if (kDebugMode) {
        print('Error fetching trap status: $e');
      }
    }
  }

  /// Refresh the trap status from the device.
  Future<void> refreshStatus() async {
    await _fetchTrapStatus();
    notifyListeners();
  }

  // ── Sessions ────────────────────────────────────────────────────────────

  /// Start a new monitoring session.
  Future<bool> startSession() async {
    debugPrint('[TrapProvider] ===== START SESSION =====');
    debugPrint('[TrapProvider] _api= ${_api != null} _trapId=$_trapId');

    if (_api == null) {
      debugPrint('[TrapProvider] startSession FAILED: _api is null');
      return false;
    }
    if (_trapId == null) {
      debugPrint('[TrapProvider] startSession FAILED: _trapId is null');
      debugPrint('[TrapProvider] You must provision the trap first (POST /v1/provision)');
      _connectionError = 'Trap not provisioned. Please provision first.';
      notifyListeners();
      return false;
    }

    try {
      debugPrint('[TrapProvider] Calling API startSession(trapId=$_trapId)');
      final session = await _api!.startSession(_trapId!);
      debugPrint('[TrapProvider] Session started successfully: id=${session.id} startedAt=${session.startedAt}');
      // Clear track ID tracking for the new session
      _activeTrackIds.clear();
      _activeSession = session;
      _sessions.insert(0, session);
      notifyListeners();
      return true;
    } on ApiException catch (e) {
      debugPrint('[TrapProvider] startSession API ERROR: $e');
      _connectionError = 'Failed to start session: $e';
      notifyListeners();
      return false;
    } catch (e) {
      debugPrint('[TrapProvider] startSession ERROR: $e');
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else {
        _connectionError = 'Failed to start session: $e';
        notifyListeners();
      }
      return false;
    }
  }

  /// Stop the currently active session.
  Future<bool> stopSession() async {
    debugPrint('[TrapProvider] ===== STOP SESSION =====');
    debugPrint('[TrapProvider] _api= ${_api != null} _trapId=$_trapId _activeSession=${_activeSession?.id}');

    if (_api == null) {
      debugPrint('[TrapProvider] stopSession FAILED: _api is null');
      return false;
    }
    if (_trapId == null) {
      debugPrint('[TrapProvider] stopSession FAILED: _trapId is null');
      return false;
    }
    if (_activeSession == null) {
      debugPrint('[TrapProvider] stopSession FAILED: no active session');
      return false;
    }

    try {
      debugPrint('[TrapProvider] Calling API stopSession(trapId=$_trapId, sessionId=${_activeSession!.id})');
      final session = await _api!.stopSession(_trapId!, _activeSession!.id);
      debugPrint('[TrapProvider] Session stopped successfully: id=${session.id}');
      _activeSession = null;
      // Update the session in the list
      final index = _sessions.indexWhere((s) => s.id == session.id);
      if (index != -1) {
        _sessions[index] = session;
      }
      notifyListeners();
      return true;
    } on ApiException catch (e) {
      debugPrint('[TrapProvider] stopSession API ERROR: $e');
      _connectionError = 'Failed to stop session: $e';
      notifyListeners();
      return false;
    } catch (e) {
      debugPrint('[TrapProvider] stopSession ERROR: $e');
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else {
        _connectionError = 'Failed to stop session: $e';
        notifyListeners();
      }
      return false;
    }
  }

  /// List all sessions for the current trap.
  Future<void> listSessions({int limit = 50, int offset = 0}) async {
    debugPrint('[TrapProvider] listSessions: _api= ${_api != null} _trapId=$_trapId');
    if (_api == null || _trapId == null) {
      debugPrint('[TrapProvider] listSessions skipped: api or trapId null');
      return;
    }

    try {
      _sessions = await _api!.listSessions(_trapId!, limit: limit, offset: offset);
      debugPrint('[TrapProvider] listSessions returned ${_sessions.length} sessions');
      notifyListeners();
    } on ApiException catch (e) {
      debugPrint('[TrapProvider] listSessions API ERROR: $e');
    } catch (e) {
      debugPrint('[TrapProvider] listSessions ERROR: $e');
      if (_isNetworkError(e)) {
        _handleRestFailure();
      }
    }
  }

  // ── Detections ──────────────────────────────────────────────────────────

  /// List detections for a given session.
  Future<void> listDetections(int sessionId, {int limit = 50, int offset = 0}) async {
    if (_api == null || _trapId == null) return;

    try {
      _detections = await _api!.listDetections(
        _trapId!,
        sessionId,
        limit: limit,
        offset: offset,
      );
      _loadedDetectionsSessionId = sessionId;
      notifyListeners();
    } on ApiException catch (e) {
      if (kDebugMode) {
        print('API error listing detections: $e');
      }
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else if (kDebugMode) {
        print('Error listing detections: $e');
      }
    }
  }

  /// Get a single detection by ID.
  Future<Detection?> getDetection(int detectionId) async {
    if (_api == null || _trapId == null) return null;

    try {
      return await _api!.getDetection(_trapId!, detectionId);
    } on ApiException catch (e) {
      if (kDebugMode) {
        print('API error getting detection: $e');
      }
      return null;
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else if (kDebugMode) {
        print('Error getting detection: $e');
      }
      return null;
    }
  }

  // ── Configuration (Legacy) ──────────────────────────────────────────────

  /// Fetch configuration from the trap (legacy /api/config endpoint).
  Future<void> fetchConfig() async {
    if (_api == null) return;

    try {
      // Use the server status endpoint for now; config endpoint TBD in v1 spec
      final status = await _api!.getServerStatus();
      _config = status;
      notifyListeners();
    } on ApiException catch (e) {
      if (kDebugMode) {
        print('API error fetching config: $e');
      }
    } catch (e) {
      if (_isNetworkError(e)) {
        _handleRestFailure();
      } else if (kDebugMode) {
        print('Error fetching config: $e');
      }
    }
  }

  /// Update configuration on the trap.
  Future<void> updateConfig(Map<String, dynamic> newConfig) async {
    // Config update via v1 API is not yet defined in the OpenAPI spec.
    // For now, we store locally and mark as pending.
    _config = newConfig;
    notifyListeners();
  }

  // ── System Metrics ──────────────────────────────────────────────────────

  /// Fetch real-time system metrics from the trap.
  Future<void> fetchSystemMetrics() async {
    if (_api == null || _trapId == null) return;

    try {
      _systemMetrics = await _api!.getSystemMetrics(_trapId!);
      notifyListeners();
    } on ApiException catch (e) {
      debugPrint('[TrapProvider] fetchSystemMetrics API ERROR: $e');
    } catch (e) {
      debugPrint('[TrapProvider] fetchSystemMetrics ERROR: $e');
      if (_isNetworkError(e)) {
        _handleRestFailure();
      }
    }
  }

  // ── SSE ─────────────────────────────────────────────────────────────────

  /// Connect to the SSE event stream and listen for session changes.
  void _connectSse() {
    if (_api == null) return;

    _disconnectSse();
    _sseService.connect(_api!.sseUrl);

    _sseSubscription = _sseService.events.listen(_onSseEvent);
    debugPrint('[TrapProvider] SSE connected');
  }

  /// Disconnect from the SSE event stream.
  void _disconnectSse() {
    _sseSubscription?.cancel();
    _sseSubscription = null;
    _sseService.disconnect();
  }

  /// Handle an incoming SSE event.
  void _onSseEvent(SseEvent event) {
    debugPrint('[TrapProvider] SSE event: ${event.type} data=${event.data}');

    switch (event.type) {
      case SseEventType.sessionStarted:
        // A new session was started — refresh the sessions list
        listSessions();
        // Reset track ID tracking for the new session
        _activeTrackIds.clear();
        // Update active session from event data
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
        // A session was stopped — refresh the sessions list
        listSessions();
        // Clear active session and track tracking
        _activeSession = null;
        _activeTrackIds.clear();
        notifyListeners();
        break;

      case SseEventType.trackDetected:
        // Track unique track IDs — report the count of distinct insects seen
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

      case SseEventType.cropSaved:
        // The backend sends crop_saved events when a crop image is saved.
        // If a detection already exists for this (sessionId, trackId), replace it
        // with the new one (which has an updated image URL). Otherwise, add it.
        // Only update _detections if the user is currently viewing this session's detections.
        if (_activeSession != null && event.trackId != null && event.detectionId != null) {
          _activeTrackIds.add(event.trackId!);

          // Build a Detection from the event data
          final newDetection = Detection(
            id: event.detectionId!,
            timestamp: event.timestamp ?? DateTime.now().millisecondsSinceEpoch,
            trackId: event.trackId!,
            classId: event.classId ?? 0,
            confidence: event.confidence ?? 0.0,
            sessionId: event.sessionId ?? _activeSession!.id,
            imageUrl: event.imageUrl ?? '',
          );

          // Only update _detections if the user is viewing this session's detections
          if (_loadedDetectionsSessionId == newDetection.sessionId) {
            // Check if a detection with the same (sessionId, trackId) already exists
            final existingIndex = _detections.indexWhere(
              (d) => d.sessionId == newDetection.sessionId && d.trackId == newDetection.trackId,
            );

            if (existingIndex != -1) {
              // Replace the existing detection with the new one
              _detections[existingIndex] = newDetection;
            } else {
              // Add the new detection to the list
              _detections.add(newDetection);
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
    _disconnectSse();
    _api?.dispose();
    super.dispose();
  }
}
