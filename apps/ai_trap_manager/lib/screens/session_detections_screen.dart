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
import '../models/detection.dart';

/// Displays a responsive grid of detection cards for a given session.
class SessionDetectionsScreen extends StatefulWidget {
  final int sessionId;

  const SessionDetectionsScreen({super.key, required this.sessionId});

  @override
  State<SessionDetectionsScreen> createState() =>
      _SessionDetectionsScreenState();
}

class _SessionDetectionsScreenState extends State<SessionDetectionsScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<TrapProvider>().listDetections(widget.sessionId);
    });
  }

  /// Prepend the trap host to relative image URLs.
  String _imageUrl(String url, String host) {
    if (url.startsWith('http://') || url.startsWith('https://')) return url;
    if (url.startsWith('/')) return 'http://$host:8080$url';
    return url;
  }

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final detections = trapProvider.detections;
    final host = trapProvider.trapIp;

    return Scaffold(
      appBar: AppBar(
        title: Text('Session #${widget.sessionId} Detections'),
        actions: [
          IconButton(
            onPressed: () => trapProvider.listDetections(widget.sessionId),
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: detections.isEmpty
          ? const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.image_not_supported, size: 64, color: Colors.grey),
                  SizedBox(height: 16),
                  Text(
                    'No detections in this session',
                    style: TextStyle(fontSize: 18, color: Colors.grey),
                  ),
                ],
              ),
            )
          : RefreshIndicator(
              onRefresh: () => trapProvider.listDetections(widget.sessionId),
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final crossAxisCount = constraints.maxWidth > 900
                      ? 4
                      : constraints.maxWidth > 600
                      ? 3
                      : 2;

                  return GridView.builder(
                    padding: const EdgeInsets.all(8),
                    gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                      crossAxisCount: crossAxisCount,
                      crossAxisSpacing: 8,
                      mainAxisSpacing: 8,
                      childAspectRatio: 0.85,
                    ),
                    itemCount: detections.length,
                    itemBuilder: (context, index) {
                      final detection = detections[index];
                      return _DetectionCard(
                        key: ValueKey(detection.id),
                        detection: detection,
                        imageUrl: detection.imageUrl.isNotEmpty
                            ? _imageUrl(detection.imageUrl, host)
                            : null,
                      );
                    },
                  );
                },
              ),
            ),
    );
  }
}

class _DetectionCard extends StatelessWidget {
  final Detection detection;
  final String? imageUrl;

  const _DetectionCard({super.key, required this.detection, this.imageUrl});

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
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
                      errorBuilder: (context, error, stackTrace) {
                        return Container(
                          color: Colors.grey.shade200,
                          child: const Center(
                            child: Icon(
                              Icons.broken_image,
                              size: 32,
                              color: Colors.grey,
                            ),
                          ),
                        );
                      },
                      loadingBuilder: (context, child, loadingProgress) {
                        if (loadingProgress == null) return child;
                        return Container(
                          color: Colors.grey.shade100,
                          child: const Center(
                            child: SizedBox(
                              width: 24,
                              height: 24,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            ),
                          ),
                        );
                      },
                    )
                  : Container(
                      key: const ValueKey('no-image'),
                      color: Colors.grey.shade200,
                      child: const Center(
                        child: Icon(Icons.image, size: 32, color: Colors.grey),
                      ),
                    ),
            ),
          ),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            color: Colors.green.shade50,
            child: Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        'Conf: ${(detection.confidence * 100).toStringAsFixed(1)}%',
                        style: TextStyle(
                          fontWeight: FontWeight.w500,
                          fontSize: 12,
                          color: Colors.green.shade800,
                        ),
                      ),
                      Text(
                        _formatTimestamp(detection.timestamp),
                        style: TextStyle(
                          fontSize: 10,
                          color: Colors.grey.shade600,
                        ),
                      ),
                    ],
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 2,
                  ),
                  decoration: BoxDecoration(
                    color: Colors.green.shade100,
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Text(
                    'C${detection.classId}',
                    style: TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.bold,
                      color: Colors.green.shade800,
                    ),
                  ),
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
