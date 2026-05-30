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

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/trap_provider.dart';
import '../widgets/camera_stream_widget.dart';
import 'session_detections_screen.dart';

/// The main trap screen showing the live MJPEG stream, session controls,
/// and detection count.
class TrapScreen extends StatelessWidget {
  const TrapScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final api = trapProvider.api;

    return Scaffold(
      body: Column(
        children: [
          // ── Live MJPEG Stream ──────────────────────────────────────────
          Expanded(
            flex: 3,
            child: api != null
                ? CameraStreamWidget(
                    streamUrl: api.mjpegStreamUrl,
                    isLive: true,
                  )
                : const Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(Icons.link_off, size: 64, color: Colors.grey),
                        SizedBox(height: 16),
                        Text('Not connected to a trap'),
                      ],
                    ),
                  ),
          ),

          // ── Session Controls & Info ─────────────────────────────────────
          Container(
            padding: const EdgeInsets.all(16),
            color: Theme.of(context).colorScheme.surfaceContainerHighest,
            child: Column(
              children: [
                // Session status
                if (trapProvider.activeSession != null)
                  InkWell(
                    onTap: () {
                      Navigator.of(context).push(
                        MaterialPageRoute(
                          builder: (_) => SessionDetectionsScreen(
                            sessionId: trapProvider.activeSession!.id,
                          ),
                        ),
                      );
                    },
                    child: Container(
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: Colors.green.shade50,
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(color: Colors.green.shade200),
                      ),
                      child: Row(
                        children: [
                          const Icon(Icons.monitor_heart,
                              color: Colors.green, size: 20),
                          const SizedBox(width: 8),
                          Expanded(
                          child: Text(
                            'Session #${trapProvider.activeSession!.id} active — '
                            '${trapProvider.activeSession!.detectionCount} insects',
                            style: const TextStyle(
                              fontSize: 14,
                              fontWeight: FontWeight.w500,
                              color: Colors.green,
                            ),
                          ),
                          ),
                          const Icon(Icons.chevron_right,
                              color: Colors.green, size: 20),
                        ],
                      ),
                    ),
                  )
                else
                  Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: Colors.grey.shade100,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: const Row(
                      children: [
                        Icon(Icons.pause_circle, color: Colors.grey, size: 20),
                        SizedBox(width: 8),
                        Text(
                          'No active session',
                          style: TextStyle(fontSize: 14, color: Colors.grey),
                        ),
                      ],
                    ),
                  ),
                const SizedBox(height: 12),

                // Start / Stop buttons
                Row(
                  children: [
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: trapProvider.hasActiveSession
                            ? null
                            : () => trapProvider.startSession(),
                        icon: const Icon(Icons.play_arrow),
                        label: const Text('Start Session'),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: Colors.green,
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(vertical: 14),
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: trapProvider.hasActiveSession
                            ? () => trapProvider.stopSession()
                            : null,
                        icon: const Icon(Icons.stop),
                        label: const Text('Stop Session'),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: Colors.red,
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(vertical: 14),
                        ),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
