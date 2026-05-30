import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import '../models/sse_event.dart';
import '../services/sse_service.dart';

/// Provider for real-time event streaming from the camera trap.
///
/// Supports both WebSocket and SSE (Server-Sent Events) channels.
/// The SSE channel connects to GET /v1/events per the OpenAPI spec.
class WebSocketProvider with ChangeNotifier {
  // ── WebSocket ───────────────────────────────────────────────────────────
  WebSocketChannel? _channel;
  StreamSubscription? _wsSubscription;

  // ── SSE ─────────────────────────────────────────────────────────────────
  final SseService _sseService = SseService();
  StreamSubscription<SseEvent>? _sseSubscription;

  // ── State ───────────────────────────────────────────────────────────────
  String _lastEvent = 'No events yet';
  List<String> _eventHistory = [];
  SseEvent? _lastSseEvent;
  String _connectionType = 'none'; // 'websocket', 'sse', or 'none'

  // ── Getters ─────────────────────────────────────────────────────────────
  String get lastEvent => _lastEvent;
  List<String> get eventHistory => _eventHistory;
  SseEvent? get lastSseEvent => _lastSseEvent;
  String get connectionType => _connectionType;
  bool get isConnected => _connectionType != 'none';
  SseService get sseService => _sseService;

  // ── WebSocket Connection ────────────────────────────────────────────────

  /// Connect to the trap's WebSocket at the given [url].
  void connectToWebSocket(String url) {
    disconnectAll();

    try {
      _connectionType = 'websocket';
      _channel = WebSocketChannel.connect(Uri.parse(url));

      _wsSubscription = _channel!.stream.listen(
        (message) {
          _lastEvent = message.toString();
          _eventHistory.add('${DateTime.now()}: $_lastEvent');
          notifyListeners();
        },
        onError: (error) {
          _lastEvent = 'WebSocket Error: $error';
          notifyListeners();
        },
        onDone: () {
          _lastEvent = 'WebSocket disconnected';
          _connectionType = 'none';
          notifyListeners();
        },
      );
      notifyListeners();
    } catch (e) {
      _lastEvent = 'WebSocket connection error: $e';
      _connectionType = 'none';
      notifyListeners();
    }
  }

  // ── SSE Connection ──────────────────────────────────────────────────────

  /// Connect to the trap's SSE stream at the given [url].
  ///
  /// The [url] should be something like `http://192.168.1.100:8080/v1/events`.
  void connectToSse(String url) {
    disconnectAll();

    try {
      _connectionType = 'sse';
      _sseService.connect(url);

      _sseSubscription = _sseService.events.listen(
        (event) {
          _lastSseEvent = event;
          _lastEvent = 'SSE ${event.type}: ${event.data}';
          _eventHistory.add('${DateTime.now()}: $_lastEvent');
          notifyListeners();
        },
        onError: (error) {
          _lastEvent = 'SSE Error: $error';
          notifyListeners();
        },
      );
      notifyListeners();
    } catch (e) {
      _lastEvent = 'SSE connection error: $e';
      _connectionType = 'none';
      notifyListeners();
    }
  }

  // ── Common ──────────────────────────────────────────────────────────────

  /// Disconnect from both WebSocket and SSE.
  void disconnectAll() {
    // Disconnect WebSocket
    _wsSubscription?.cancel();
    _wsSubscription = null;
    _channel?.sink.close();
    _channel = null;

    // Disconnect SSE
    _sseSubscription?.cancel();
    _sseSubscription = null;
    _sseService.disconnect();

    if (_connectionType != 'none') {
      _connectionType = 'none';
      _lastEvent = 'Disconnected';
      notifyListeners();
    }
  }

  /// Send a message via WebSocket (only works if connected via WebSocket).
  void sendMessage(String message) {
    if (_channel != null && _connectionType == 'websocket') {
      _channel!.sink.add(message);
    }
  }

  /// Clear the event history.
  void clearHistory() {
    _eventHistory.clear();
    _lastEvent = 'No events yet';
    _lastSseEvent = null;
    notifyListeners();
  }

  @override
  void dispose() {
    disconnectAll();
    _sseService.dispose();
    super.dispose();
  }
}
