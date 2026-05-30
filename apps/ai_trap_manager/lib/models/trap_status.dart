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

/// Represents the status of a camera trap device.
class TrapStatus {
  final String trapId;
  final String status;
  final ActiveSession? activeSession;

  TrapStatus({
    required this.trapId,
    required this.status,
    this.activeSession,
  });

  factory TrapStatus.fromJson(Map<String, dynamic> json) {
    ActiveSession? session;
    if (json['activeSession'] != null && json['activeSession']['active'] == true) {
      session = ActiveSession.fromJson(json['activeSession']);
    }
    return TrapStatus(
      trapId: json['trapId'] as String,
      status: json['status'] as String,
      activeSession: session,
    );
  }

  Map<String, dynamic> toJson() => {
        'trapId': trapId,
        'status': status,
        if (activeSession != null) 'activeSession': activeSession!.toJson(),
      };
}

/// Represents an active monitoring session.
class ActiveSession {
  final bool active;
  final int sessionId;
  final int startedAt;
  final int detectionCount;

  ActiveSession({
    required this.active,
    required this.sessionId,
    required this.startedAt,
    required this.detectionCount,
  });

  factory ActiveSession.fromJson(Map<String, dynamic> json) => ActiveSession(
        active: json['active'] as bool,
        sessionId: json['sessionId'] as int,
        startedAt: json['startedAt'] as int,
        detectionCount: json['detectionCount'] as int,
      );

  Map<String, dynamic> toJson() => {
        'active': active,
        'sessionId': sessionId,
        'startedAt': startedAt,
        'detectionCount': detectionCount,
      };
}
