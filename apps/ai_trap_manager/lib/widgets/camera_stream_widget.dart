// Copyright 2026 Nature Sense
// SPDX-License-Identifier: Apache-2.0

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import '../services/h264_decoder.dart';

/// Widget that displays a live camera stream from the trap's WebSocket.
class CameraStreamWidget extends StatefulWidget {
  final String wsUrl;
  final bool isLive;
  const CameraStreamWidget({
    super.key,
    required this.wsUrl,
    this.isLive = true,
  });
  @override
  State<CameraStreamWidget> createState() => _CameraStreamWidgetState();
}

class _CameraStreamWidgetState extends State<CameraStreamWidget> {
  WebSocket? _ws;
  StreamSubscription? _sub;
  Uint8List? _latestFrame;
  bool _loading = true;
  String? _error;
  bool _disposed = false;
  Timer? _reconnectTimer;

  @override
  void initState() {
    super.initState();
    _startStream();
  }

  @override
  void didUpdateWidget(CameraStreamWidget old) {
    super.didUpdateWidget(old);
    if (old.wsUrl != widget.wsUrl) {
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
    _sub?.cancel();
    _sub = null;
    _ws?.close();
    _ws = null;
  }

  void _scheduleReconnect() {
    if (_disposed) return;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      if (!_disposed && mounted) {
        setState(() {
          _loading = true;
          _error = null;
        });
        _startStream();
      }
    });
  }

  Future<void> _startStream() async {
    try {
      final uri = Uri.parse(widget.wsUrl);
      _ws = await WebSocket.connect(
        uri.toString(),
      ).timeout(const Duration(seconds: 10));

      if (_disposed) {
        _ws?.close();
        _ws = null;
        return;
      }

      if (mounted) setState(() => _loading = false);

      _sub = _ws!.listen(
        (message) async {
          if (message is List<int>) {
            final frame = Uint8List.fromList(message);
            if (frame.length < 50) return;

            if (Platform.isMacOS) {
              final rgba = await H264Decoder.decode(frame, 640, 480);
              if (rgba != null && mounted) {
                setState(() {
                  _latestFrame = rgba;
                  _loading = false;
                });
              }
            }
          }
        },
        onDone: () => _scheduleReconnect(),
        onError: (e) {
          if (mounted) setState(() => _error = e.toString());
          _scheduleReconnect();
        },
      );
    } catch (e) {
      if (mounted) {
        setState(() {
          _error = e.toString();
          _loading = false;
        });
      }
      _scheduleReconnect();
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
          ],
        ),
      );
    }
    if (_loading) return const Center(child: CircularProgressIndicator());

    if (_latestFrame != null && Platform.isMacOS) {
      return RgbaImage(data: _latestFrame!, width: 640, height: 480);
    }

    return const Center(child: Text('No frames'));
  }
}

/// Displays raw RGBA pixel data as a [RawImage].
class RgbaImage extends StatefulWidget {
  final Uint8List data;
  final int width;
  final int height;

  const RgbaImage({
    required this.data,
    required this.width,
    required this.height,
  });

  @override
  State<RgbaImage> createState() => _RgbaImageState();
}

class _RgbaImageState extends State<RgbaImage> {
  ui.Image? _image;
  bool _loading = true;
  String? _error;

  @override
  void initState() {
    super.initState();
    _doDecode();
  }

  @override
  void didUpdateWidget(RgbaImage old) {
    super.didUpdateWidget(old);
    if (old.data != widget.data) _doDecode();
  }

  void _doDecode() {
    _loading = true;
    _error = null;
    ui.decodeImageFromPixels(
      widget.data,
      widget.width,
      widget.height,
      ui.PixelFormat.rgba8888,
      (ui.Image result) {
        if (mounted) {
          setState(() {
            _image = result;
            _loading = false;
          });
        }
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) return const Center(child: CircularProgressIndicator());
    if (_error != null) return Center(child: Text(_error!));
    return RawImage(image: _image, fit: BoxFit.contain);
  }
}
