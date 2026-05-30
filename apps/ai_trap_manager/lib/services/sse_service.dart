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
import 'package:http/http.dart' as http;
import '../models/sse_event.dart';

/// Service for consuming Server-Sent Events (SSE) from the camera trap.
///
/// Connects to GET /v1/events and emits typed [SseEvent] objects.
/// Events: session_started, session_stopped, track_detected, crop_saved
class SseService {
  http.Client? _client;
  StreamSubscription<String>? _subscription;
  final StreamController<SseEvent> _eventController =
      StreamController<SseEvent>.broadcast();

  /// Stream of all SSE events from the trap.
  Stream<SseEvent> get events => _eventController.stream;

  /// Whether the service is currently connected.
  bool get isConnected => _subscription != null;

  /// Connect to the SSE stream at the given [url].
  ///
  /// The [url] should be something like `http://192.168.1.100:8080/v1/events`.
  void connect(String url) {
    disconnect();
    _client = http.Client();

    _client!
        .send(
          http.Request('get', Uri.parse(url))
            ..headers['Accept'] = 'text/event-stream',
        )
        .then((response) {
          final stream = response.stream
              .transform(utf8.decoder)
              .transform(const LineSplitter());

          String? currentEvent;

          _subscription = stream.listen(
            (line) {
              if (line.startsWith('event: ')) {
                currentEvent = line.substring(7).trim();
              } else if (line.startsWith('data: ')) {
                final data = line.substring(6).trim();
                if (currentEvent != null) {
                  _eventController.add(
                    SseEvent.parse(currentEvent!, data),
                  );
                  currentEvent = null;
                }
              }
              // Ignore empty lines (event separators) and comments
            },
            onError: (error) {
              _eventController.addError(error);
            },
            onDone: () {
              // Auto-reconnect after a short delay
              Future.delayed(const Duration(seconds: 3), () {
                if (_client != null) {
                  connect(url);
                }
              });
            },
          );
        })
        .catchError((error) {
          _eventController.addError(error);
          // Auto-reconnect after a short delay
          Future.delayed(const Duration(seconds: 5), () {
            if (_client != null) {
              connect(url);
            }
          });
        });
  }

  /// Disconnect from the SSE stream.
  void disconnect() {
    _subscription?.cancel();
    _subscription = null;
    _client?.close();
    _client = null;
  }

  /// Dispose the service and release resources.
  void dispose() {
    disconnect();
    _eventController.close();
  }
}
