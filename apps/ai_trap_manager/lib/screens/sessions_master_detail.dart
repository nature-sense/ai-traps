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
import '../models/session.dart';
import '../models/detection.dart';
import 'session_detections_screen.dart';

/// Tablet master-detail view for sessions.
///
/// - **Master** (left 40%): scrollable list of sessions
/// - **Detail** (right 60%): detection grid for the selected session
///
/// On phone, this screen is never used — the phone layout pushes
/// [SessionDetectionsScreen] as a full-screen route instead.
class SessionsMasterDetail extends StatefulWidget {
  const SessionsMasterDetail({super.key});

  @override
  State<SessionsMasterDetail> createState() => _SessionsMasterDetailState();
}

class _SessionsMasterDetailState extends State<SessionsMasterDetail> {
  Session? _selectedSession;

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final sessions = trapProvider.sessions;

    return LayoutBuilder(
      builder: (context, constraints) {
        final masterWidth = (constraints.maxWidth * 0.4).clamp(200.0, 400.0);

        return Row(
          children: [
            // ── Master: Session List ──────────────────────────────────────
            SizedBox(
              width: masterWidth,
              child: sessions.isEmpty
                  ? const Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(Icons.history, size: 48, color: Colors.grey),
                          SizedBox(height: 12),
                          Text(
                            'No sessions yet',
                            style: TextStyle(fontSize: 16, color: Colors.grey),
                          ),
                        ],
                      ),
                    )
                  : ListView.separated(
                      padding: const EdgeInsets.all(8),
                      itemCount: sessions.length,
                      separatorBuilder: (_, _) => const Divider(height: 1),
                      itemBuilder: (context, index) {
                        final session = sessions[index];
                        final isSelected = _selectedSession?.id == session.id;
                        return _SessionMasterTile(
                          session: session,
                          isSelected: isSelected,
                          onTap: () {
                            setState(() => _selectedSession = session);
                            trapProvider.listDetections(session.id);
                          },
                        );
                      },
                    ),
            ),

            // ── Divider ───────────────────────────────────────────────────
            const VerticalDivider(width: 1),

            // ── Detail: Detection Grid ────────────────────────────────────
            Expanded(
              child: _selectedSession == null
                  ? const Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(Icons.touch_app, size: 48, color: Colors.grey),
                          SizedBox(height: 12),
                          Text(
                            'Select a session to view detections',
                            style: TextStyle(fontSize: 16, color: Colors.grey),
                          ),
                        ],
                      ),
                    )
                  : _DetectionGrid(sessionId: _selectedSession!.id),
            ),
          ],
        );
      },
    );
  }
}

/// A single session tile in the master list, with a selected highlight.
class _SessionMasterTile extends StatelessWidget {
  final Session session;
  final bool isSelected;
  final VoidCallback onTap;

  const _SessionMasterTile({
    required this.session,
    required this.isSelected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final color = isSelected ? Colors.green.shade50 : Colors.transparent;

    return Material(
      color: color,
      child: InkWell(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          child: Row(
            children: [
              CircleAvatar(
                radius: 18,
                backgroundColor: Colors.green.shade100,
                child: Text(
                  '#${session.id}',
                  style: TextStyle(
                    fontWeight: FontWeight.bold,
                    fontSize: 12,
                    color: Colors.green.shade800,
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Session #${session.id}',
                      style: const TextStyle(fontWeight: FontWeight.w500),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      _formatTimestamp(session.startedAt),
                      style: const TextStyle(fontSize: 11, color: Colors.grey),
                    ),
                  ],
                ),
              ),
              Column(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Text(
                    '${session.detectionCount}',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.bold,
                      color: Colors.green.shade700,
                    ),
                  ),
                  const Text(
                    'detections',
                    style: TextStyle(fontSize: 10, color: Colors.grey),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  String _formatTimestamp(int unixMs) {
    final dt = DateTime.fromMillisecondsSinceEpoch(unixMs);
    return '${dt.year}-${_pad(dt.month)}-${_pad(dt.day)} '
        '${_pad(dt.hour)}:${_pad(dt.minute)}';
  }

  String _pad(int value) => value.toString().padLeft(2, '0');
}

/// Detection grid for the detail pane, reusing the same card style as
/// [SessionDetectionsScreen].
class _DetectionGrid extends StatelessWidget {
  final int sessionId;

  const _DetectionGrid({required this.sessionId});

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final detections = trapProvider.detections;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // Header
        Container(
          width: double.infinity,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.surfaceContainerHighest,
          ),
          child: Text(
            'Session #$sessionId — ${detections.length} detections',
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
          ),
        ),
        // Grid
        Expanded(
          child: detections.isEmpty
              ? const Center(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(
                        Icons.image_not_supported,
                        size: 48,
                        color: Colors.grey,
                      ),
                      SizedBox(height: 12),
                      Text(
                        'No detections in this session',
                        style: TextStyle(fontSize: 16, color: Colors.grey),
                      ),
                    ],
                  ),
                )
              : GridView.builder(
                  padding: const EdgeInsets.all(8),
                  gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: 3,
                    crossAxisSpacing: 8,
                    mainAxisSpacing: 8,
                    childAspectRatio: 0.85,
                  ),
                  itemCount: detections.length,
                  itemBuilder: (context, index) {
                    final detection = detections[index];
                    final host = trapProvider.trapIp;
                    String _imageUrl(String url) {
                      if (url.startsWith('http://') ||
                          url.startsWith('https://'))
                        return url;
                      if (url.startsWith('/')) return 'http://$host:8080$url';
                      return url;
                    }

                    final imageUrl = detection.imageUrl;
                    return _EmbeddedDetectionCard(
                      key: ValueKey(detection.id),
                      detection: detection,
                      imageUrl: imageUrl.isNotEmpty
                          ? _imageUrl(imageUrl)
                          : null,
                    );
                  },
                ),
        ),
      ],
    );
  }

  String _buildImageUrl(dynamic api, Detection detection) {
    final imageUrl = detection.imageUrl;
    if (imageUrl.startsWith('http')) return imageUrl;
    if (imageUrl.startsWith('/v1/crops/')) {
      return '${api.baseUrl}$imageUrl';
    }
    return '${api.baseUrl}/v1/crops/$imageUrl';
  }
}

/// Detection card for the embedded grid, identical in style to
/// [_DetectionCard] in [SessionDetectionsScreen].
class _EmbeddedDetectionCard extends StatelessWidget {
  final Detection detection;
  final String? imageUrl;

  const _EmbeddedDetectionCard({
    super.key,
    required this.detection,
    this.imageUrl,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 300),
              child: imageUrl != null
                  ? Image.network(
                      imageUrl!,
                      key: ValueKey(imageUrl),
                      fit: BoxFit.cover,
                      width: double.infinity,
                      errorBuilder: (context, error, stackTrace) => Container(
                        color: Colors.grey.shade200,
                        child: const Center(
                          child: Icon(
                            Icons.broken_image,
                            size: 40,
                            color: Colors.grey,
                          ),
                        ),
                      ),
                      loadingBuilder: (context, child, loadingProgress) {
                        if (loadingProgress == null) return child;
                        return Container(
                          color: Colors.grey.shade100,
                          child: const Center(
                            child: CircularProgressIndicator(),
                          ),
                        );
                      },
                    )
                  : Container(
                      key: const ValueKey('no-image'),
                      color: Colors.grey.shade200,
                      child: const Center(
                        child: Icon(Icons.image, size: 40, color: Colors.grey),
                      ),
                    ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Conf: ${(detection.confidence * 100).toStringAsFixed(1)}%',
                  style: const TextStyle(
                    fontWeight: FontWeight.w500,
                    fontSize: 13,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  _formatTimestamp(detection.timestamp),
                  style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  String _formatTimestamp(int unixMs) {
    final dt = DateTime.fromMillisecondsSinceEpoch(unixMs);
    return '${dt.hour.toString().padLeft(2, '0')}:'
        '${dt.minute.toString().padLeft(2, '0')}:'
        '${dt.second.toString().padLeft(2, '0')}';
  }
}
