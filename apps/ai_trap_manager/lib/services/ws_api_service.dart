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
import 'dart:convert';
import 'dart:io';
import '../models/sse_event.dart';

/// Service for communicating with the camera trap via a single WebSocket.
///
/// Connects to ws://host:port/ws and provides:
///   - JSON-RPC request/response pairs (via send method + response stream)
///   - Push event stream (text frames with {"event":"...","data":{...}})
///   - Binary frame stream (JPEG images, via raw frame callback)
///
/// Protocol:
///   Client → Server: {"id":1, "method":"list_sessions", "params":{...}}
///   Server → Client: {"id":1, "result":{...}}
///   Server → Client: {"event":"session_started","data":{"sessionId":1}}
///   Server → Client: [binary JPEG bytes]
///
/// This replaces both CameraTrapApi (REST HTTP) and SseService (separate WS).
class WsApiService {
  WebSocket? _ws;
  StreamSubscription? _subscription;
  final StreamController<SseEvent> _eventController =
      StreamController<SseEvent>.broadcast();
  Timer? _reconnectTimer;
  bool _disposed = false;

  // JSON-RPC request tracking
  int _nextId = 1;
  final Map<int, Completer<Map<String, dynamic>>> _pending = {};

  // Stream controller for binary frames (JPEG images)
  final StreamController<List<int>> _binaryFrameController =
      StreamController<List<int>>.broadcast();

  // Stream controller for connection state
  final StreamController<bool> _connectionController =
      StreamController<bool>.broadcast();

  /// Stream of SseEvent objects from push notifications.
  Stream<SseEvent> get events => _eventController.stream;

  /// Stream of binary frame data (JPEG images).
  Stream<List<int>> get binaryFrames => _binaryFrameController.stream;

  /// Stream of connection state changes (true = connected, false = disconnected).
  Stream<bool> get connectionState => _connectionController.stream;

  /// Whether the service is currently connected.
  bool get isConnected => _ws != null;

  /// Completer that resolves when the WebSocket connection is established.
  Completer<void>? _connectCompleter;

  /// Connect to the WebSocket at the given [url].
  /// Returns a Future that completes when the connection is established
  /// or fails if the connection cannot be made.
  Future<void> connect(String url) async {
    disconnect();
    final completer = Completer<void>();
    _connectCompleter = completer;

    try {
      final ws = await WebSocket.connect(url);

      if (_disposed) {
        ws.close();
        return;
      }

      _ws = ws;
      _connectionController.add(true);

      _subscription = ws.listen(
        (message) {
          if (message is String) {
            // Text frame — either JSON-RPC response or push event
            try {
              final decoded = jsonDecode(message) as Map<String, dynamic>;

              // Check if this is a JSON-RPC response (has "id" + "result" or "id" + "error")
              if (decoded.containsKey('id') &&
                  (decoded.containsKey('result') ||
                      decoded.containsKey('error'))) {
                final id = decoded['id'] as int?;
                if (id != null && _pending.containsKey(id)) {
                  final completer = _pending.remove(id)!;
                  if (decoded.containsKey('error')) {
                    completer.completeError(
                      WsApiException(
                        (decoded['error'] as Map)['code'] as int? ?? -1,
                        (decoded['error'] as Map)['message'] as String? ??
                            'Unknown error',
                      ),
                    );
                  } else {
                    completer.complete(
                      decoded['result'] as Map<String, dynamic>,
                    );
                  }
                }
                return;
              }

              // Check if this is a push event (has "event" + "data")
              final eventType = decoded['event'] as String?;
              final data = decoded['data'] as Map<String, dynamic>?;

              if (eventType != null) {
                _eventController.add(
                  SseEvent(type: eventType, data: data ?? <String, dynamic>{}),
                );
              }
            } catch (_) {
              // Ignore malformed text frames
            }
          } else if (message is List<int>) {
            // Binary frame — JPEG image data
            _binaryFrameController.add(message);
          }
        },
        onError: (error) {
          _connectionController.add(false);
          _pending.forEach((id, pending) {
            if (!pending.isCompleted) {
              pending.completeError(error);
            }
          });
          _pending.clear();
          _eventController.addError(error);
          _scheduleReconnect(url);
        },
        onDone: () {
          _ws = null;
          _subscription = null;
          _connectionController.add(false);
          // Fail any pending requests
          _pending.forEach((id, pending) {
            if (!pending.isCompleted) {
              pending.completeError(
                WsApiException(0, 'WebSocket disconnected'),
              );
            }
          });
          _pending.clear();
          _scheduleReconnect(url);
        },
      );
    } catch (error) {
      _connectionController.add(false);
      _eventController.addError(error);
      _scheduleReconnect(url);
      rethrow;
    }
  }

  /// Disconnect from the WebSocket.
  void disconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _subscription?.cancel();
    _subscription = null;
    _ws?.close();
    _ws = null;
    _connectionController.add(false);
    // Fail any pending requests
    _pending.forEach((id, completer) {
      if (!completer.isCompleted) {
        completer.completeError(WsApiException(0, 'WebSocket disconnected'));
      }
    });
    _pending.clear();
  }

  /// Send a JSON-RPC request and wait for the response.
  ///
  /// Returns the "result" object on success, throws [WsApiException] on error.
  Future<Map<String, dynamic>> send(
    String method, {
    Map<String, dynamic> params = const {},
    Duration timeout = const Duration(seconds: 10),
  }) async {
    if (_ws == null) {
      throw WsApiException(0, 'Not connected to WebSocket');
    }

    final id = _nextId++;
    final completer = Completer<Map<String, dynamic>>();
    _pending[id] = completer;

    final request = jsonEncode({'id': id, 'method': method, 'params': params});

    _ws!.add(request);

    // Wait for response with timeout
    return completer.future.timeout(timeout);
  }

  /// Schedule an automatic reconnection after a short delay.
  void _scheduleReconnect(String url) {
    if (_disposed) return;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      if (!_disposed) {
        connect(url);
      }
    });
  }

  /// Dispose the service and release resources.
  void dispose() {
    _disposed = true;
    disconnect();
    _eventController.close();
    _binaryFrameController.close();
    _connectionController.close();
  }
}

/// Exception thrown when a WebSocket API call returns an error response.
class WsApiException implements Exception {
  final int code;
  final String message;

  WsApiException(this.code, this.message);

  @override
  String toString() => 'WsApiException($code): $message';
}
