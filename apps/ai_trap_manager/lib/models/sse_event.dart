import 'dart:convert';

/// Represents an SSE (Server-Sent Events) event from the camera trap.
class SseEvent {
  final String type;
  final Map<String, dynamic> data;

  SseEvent({
    required this.type,
    required this.data,
  });

  /// Parses an SSE event string into an [SseEvent].
  ///
  /// The raw event format is:
  ///   event: session_started
  ///   data: {"sessionId": 1, "startedAt": 1715500000000}
  factory SseEvent.parse(String eventType, String rawData) {
    try {
      final decoded = Map<String, dynamic>.from(
        const JsonDecoder().convert(rawData),
      );
      return SseEvent(type: eventType, data: decoded);
    } catch (_) {
      return SseEvent(type: eventType, data: {'raw': rawData});
    }
  }

  // Convenience getters for common event fields
  int? get sessionId => data['sessionId'] as int?;
  int? get detectionId => data['detectionId'] as int?;
  int? get trackId => data['trackId'] as int?;
  int? get classId => data['classId'] as int?;
  double? get confidence => (data['confidence'] as num?)?.toDouble();
  int? get timestamp => data['timestamp'] as int?;
  String? get imageUrl => data['imageUrl'] as String?;
  int? get startedAt => data['startedAt'] as int?;
  int? get stoppedAt => data['stoppedAt'] as int?;
  int? get detectionCount => data['detectionCount'] as int?;
}

/// SSE event type constants matching the OpenAPI spec.
class SseEventType {
  static const sessionStarted = 'session_started';
  static const sessionStopped = 'session_stopped';
  static const trackDetected = 'track_detected';
  static const cropSaved = 'crop_saved';
}

