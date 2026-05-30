import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/// A widget that displays an MJPEG stream from the camera trap.
///
/// Uses a raw HTTP client to read the multipart/x-mixed-replace stream,
/// parses JPEG frames from the boundary-delimited chunks, and displays
/// each frame using [Image.memory].
class CameraStreamWidget extends StatefulWidget {
  final String streamUrl;
  final bool isLive;

  const CameraStreamWidget({
    super.key,
    required this.streamUrl,
    this.isLive = true,
  });

  @override
  State<CameraStreamWidget> createState() => _CameraStreamWidgetState();
}

class _CameraStreamWidgetState extends State<CameraStreamWidget> {
  http.Client? _client;
  Uint8List? _latestFrame;
  bool _loading = true;
  String? _error;
  int _frameCount = 0;
  bool _disposed = false;
  Timer? _reconnectTimer;

  static const String _mjpegBoundary = '--jpgboundary';

  @override
  void initState() {
    super.initState();
    _startStream();
  }

  @override
  void didUpdateWidget(CameraStreamWidget oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.streamUrl != widget.streamUrl) {
      _stopStream();
      _startStream();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _reconnectTimer?.cancel();
    _stopStream();
    super.dispose();
  }

  void _stopStream() {
    _client?.close();
    _client = null;
  }

  void _scheduleReconnect() {
    if (_disposed) return;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      if (!_disposed && mounted) {
        debugPrint('[CameraStreamWidget] Reconnecting...');
        setState(() {
          _loading = true;
          _error = null;
        });
        _startStream();
      }
    });
  }

  Future<void> _startStream() async {
    debugPrint('[CameraStreamWidget] Starting MJPEG stream: ${widget.streamUrl}');

    _client = http.Client();
    try {
      final request = http.Request('GET', Uri.parse(widget.streamUrl));
      final response = await _client!.send(request);

      debugPrint('[CameraStreamWidget] Response status: ${response.statusCode}');
      debugPrint('[CameraStreamWidget] Response headers: ${response.headers}');

      if (response.statusCode != 200) {
        throw Exception('HTTP ${response.statusCode}: ${response.reasonPhrase}');
      }

      // Detect boundary from Content-Type header
      String boundary = _mjpegBoundary;
      final contentType = response.headers['content-type'] ?? '';
      debugPrint('[CameraStreamWidget] Content-Type: $contentType');
      if (contentType.contains('boundary=')) {
        final boundaryMatch = RegExp(r'boundary=(.+?)(?:;|$)').firstMatch(contentType);
        if (boundaryMatch != null) {
          boundary = '--${boundaryMatch.group(1)!}';
          debugPrint('[CameraStreamWidget] Using boundary from header: "$boundary"');
        }
      } else {
        debugPrint('[CameraStreamWidget] No boundary in Content-Type, using default: "$boundary"');
      }

      // Read the stream in chunks and parse MJPEG frames
      final byteStream = response.stream;
      final completer = Completer<void>();
      final chunks = <int>[];
      bool inFrame = false;
      int boundaryIndex = 0;
      int totalBytesReceived = 0;
      final stopwatch = Stopwatch()..start();

      byteStream.listen(
        (data) {
          totalBytesReceived += data.length;

          for (final byte in data) {
            chunks.add(byte);

            // Check for boundary marker
            final expectedByte = boundary.codeUnitAt(boundaryIndex);
            if (byte == expectedByte) {
              boundaryIndex++;
              if (boundaryIndex == boundary.length) {
                // Found a complete boundary - everything before it is a JPEG frame
                if (inFrame && chunks.length > boundary.length) {
                  // Extract the JPEG data (everything before the boundary)
                  final frameLen = chunks.length - boundary.length;
                  if (frameLen > 100) {
                    // Skip the HTTP headers before the JPEG data (find SOI marker 0xFFD8)
                    final jpegData = Uint8List.fromList(chunks.sublist(0, frameLen));
                    int jpegStart = 0;
                    for (int i = 0; i < jpegData.length - 1; i++) {
                      if (jpegData[i] == 0xFF && jpegData[i + 1] == 0xD8) {
                        jpegStart = i;
                        break;
                      }
                    }
                    if (jpegStart < jpegData.length - 2) {
                      final frame = jpegData.sublist(jpegStart);
                      if (mounted) {
                        setState(() {
                          _latestFrame = frame;
                          _loading = false;
                          _frameCount++;
                        });
                      }
                      if (_frameCount % 30 == 0) {
                        debugPrint(
                          '[CameraStreamWidget] Frame #$_frameCount decoded '
                          '(${frame.length} bytes, '
                          '${totalBytesReceived ~/ 1024}KB total, '
                          '${stopwatch.elapsed.inSeconds}s elapsed)',
                        );
                      }
                    }
                  }
                }
                chunks.clear();
                boundaryIndex = 0;
                inFrame = true;
              }
            } else {
              boundaryIndex = 0;
            }
          }

          // Log every 500KB received if no frames decoded yet
          if (_frameCount == 0 && totalBytesReceived % (500 * 1024) < data.length) {
            debugPrint(
              '[CameraStreamWidget] Received ${totalBytesReceived ~/ 1024}KB, '
              'no frames decoded yet. Buffer size: ${chunks.length} bytes',
            );
            // Dump first 200 bytes as hex to see what we're getting
            if (totalBytesReceived < 5000) {
              final preview = chunks.take(200).map((b) => b.toRadixString(16).padLeft(2, '0')).join(' ');
              debugPrint('[CameraStreamWidget] First bytes hex: $preview');
              // Also try to show as ASCII
              final ascii = chunks.take(200).map((b) => b >= 32 && b < 127 ? String.fromCharCode(b) : '.').join();
              debugPrint('[CameraStreamWidget] First bytes ASCII: $ascii');
            }
          }
        },
        onDone: () {
          debugPrint('[CameraStreamWidget] Stream ended. Total bytes: $totalBytesReceived, Frames: $_frameCount');
          if (!completer.isCompleted) completer.complete();
          // Auto-reconnect when the stream ends (e.g. after navigating away)
          _scheduleReconnect();
        },
        onError: (e) {
          debugPrint('[CameraStreamWidget] Stream error: $e');
          if (mounted) {
            setState(() {
              _error = e.toString();
              _loading = false;
            });
          }
          if (!completer.isCompleted) completer.completeError(e);
          // Auto-reconnect on error
          _scheduleReconnect();
        },
        cancelOnError: false,
      );

      await completer.future;
    } catch (e) {
      debugPrint('[CameraStreamWidget] Connection failed: $e');
      if (mounted) {
        setState(() {
          _error = e.toString();
          _loading = false;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.videocam_off, size: 64, color: Colors.red),
            const SizedBox(height: 16),
            Text(
              'Stream unavailable',
              style: Theme.of(context).textTheme.bodyLarge,
            ),
            const SizedBox(height: 8),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 24),
              child: Text(
                _error!,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Colors.grey,
                    ),
                textAlign: TextAlign.center,
                maxLines: 3,
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ),
      );
    }

    if (_loading) {
      return Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const CircularProgressIndicator(),
            const SizedBox(height: 16),
            Text(
              'Connecting to stream...',
              style: Theme.of(context).textTheme.bodyMedium,
            ),
          ],
        ),
      );
    }

    if (_latestFrame != null) {
      return Image.memory(
        _latestFrame!,
        fit: BoxFit.contain,
        width: double.infinity,
        height: double.infinity,
        gaplessPlayback: true,
      );
    }

    return const Center(child: Text('No frames received'));
  }
}
