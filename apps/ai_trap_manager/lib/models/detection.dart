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

/// Represents a detection (insect sighting) captured by the camera trap.
class Detection {
  final int id;
  final int timestamp;
  final int trackId;
  final int classId;
  final double confidence;
  final int sessionId;
  final String imageUrl;
  final String imageFormat; // "jpg" or "h264"

  Detection({
    required this.id,
    required this.timestamp,
    required this.trackId,
    required this.classId,
    required this.confidence,
    required this.sessionId,
    required this.imageUrl,
    this.imageFormat = 'h264',
  });

  factory Detection.fromJson(Map<String, dynamic> json) => Detection(
    id: json['id'] as int,
    timestamp: json['timestamp'] as int,
    trackId: json['trackId'] as int,
    classId: json['classId'] as int,
    confidence: (json['confidence'] as num).toDouble(),
    sessionId: json['sessionId'] as int,
    imageUrl: json['imageUrl'] as String,
    imageFormat: (json['imageFormat'] as String?) ?? 'h264',
  );

  Map<String, dynamic> toJson() => {
    'id': id,
    'timestamp': timestamp,
    'trackId': trackId,
    'classId': classId,
    'confidence': confidence,
    'sessionId': sessionId,
    'imageUrl': imageUrl,
    'imageFormat': imageFormat,
  };
}
