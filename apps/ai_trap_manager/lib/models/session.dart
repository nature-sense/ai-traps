/// Represents a monitoring session on a camera trap.
class Session {
  final int id;
  final int startedAt;
  final int? stoppedAt;
  final int detectionCount;

  Session({
    required this.id,
    required this.startedAt,
    this.stoppedAt,
    required this.detectionCount,
  });

  factory Session.fromJson(Map<String, dynamic> json) => Session(
        id: json['id'] as int,
        startedAt: json['startedAt'] as int,
        stoppedAt: json['stoppedAt'] as int?,
        detectionCount: json['detectionCount'] as int,
      );

  Map<String, dynamic> toJson() => {
        'id': id,
        'startedAt': startedAt,
        'stoppedAt': stoppedAt,
        'detectionCount': detectionCount,
      };
}
