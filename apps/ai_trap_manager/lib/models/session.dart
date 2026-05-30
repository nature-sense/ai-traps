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
