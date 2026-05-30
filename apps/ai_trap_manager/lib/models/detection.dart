/// Represents a detection (insect sighting) captured by the camera trap.
class Detection {
  final int id;
  final int timestamp;
  final int trackId;
  final int classId;
  final double confidence;
  final int sessionId;
  final String imageUrl;

  Detection({
    required this.id,
    required this.timestamp,
    required this.trackId,
    required this.classId,
    required this.confidence,
    required this.sessionId,
    required this.imageUrl,
  });

  factory Detection.fromJson(Map<String, dynamic> json) => Detection(
        id: json['id'] as int,
        timestamp: json['timestamp'] as int,
        trackId: json['trackId'] as int,
        classId: json['classId'] as int,
        confidence: (json['confidence'] as num).toDouble(),
        sessionId: json['sessionId'] as int,
        imageUrl: json['imageUrl'] as String,
      );

  Map<String, dynamic> toJson() => {
        'id': id,
        'timestamp': timestamp,
        'trackId': trackId,
        'classId': classId,
        'confidence': confidence,
        'sessionId': sessionId,
        'imageUrl': imageUrl,
      };
}
