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

import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/system_metrics.dart';
import '../providers/trap_provider.dart';

/// Real-time system metrics dashboard for the camera trap.
///
/// Shows CPU, memory, storage, NPU, pipeline, network, and power metrics
/// in a card-based layout. Auto-refreshes every 5 seconds while mounted.
class MetricsDashboard extends StatefulWidget {
  const MetricsDashboard({super.key});

  @override
  State<MetricsDashboard> createState() => _MetricsDashboardState();
}

class _MetricsDashboardState extends State<MetricsDashboard> {
  Timer? _refreshTimer;

  @override
  void initState() {
    super.initState();
    // Refresh metrics every 5 seconds
    _refreshTimer = Timer.periodic(const Duration(seconds: 5), (_) {
      if (mounted) {
        context.read<TrapProvider>().fetchSystemMetrics();
      }
    });
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final trapProvider = context.watch<TrapProvider>();
    final metrics = trapProvider.systemMetrics;

    if (!trapProvider.isConnected) {
      return const Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.link_off, size: 48, color: Colors.grey),
            SizedBox(height: 12),
            Text('Connect to a trap to view metrics'),
          ],
        ),
      );
    }

    if (metrics == null) {
      return const Center(child: CircularProgressIndicator());
    }

    return RefreshIndicator(
      onRefresh: () => trapProvider.fetchSystemMetrics(),
      child: ListView(
        padding: const EdgeInsets.all(12),
        children: [
          // ── CPU Card ──────────────────────────────────────────────────
          if (metrics.cpu != null) ..._buildCpuCard(metrics.cpu!),

          // ── Memory Card ───────────────────────────────────────────────
          if (metrics.memory != null) ..._buildMemoryCard(metrics.memory!),

          // ── Pipeline Card ─────────────────────────────────────────────
          if (metrics.pipeline != null)
            ..._buildPipelineCard(metrics.pipeline!),

          // ── Storage Card ──────────────────────────────────────────────
          if (metrics.storage != null) ..._buildStorageCard(metrics.storage!),

          // ── NPU / Network / Power row ─────────────────────────────────
          if (metrics.npu != null ||
              metrics.network != null ||
              metrics.power != null)
            _buildMetricRow(metrics),

          // ── Timestamp ─────────────────────────────────────────────────
          const SizedBox(height: 8),
          Center(
            child: Text(
              'Updated: ${_formatTimestamp(metrics.timestampMs)}',
              style: Theme.of(
                context,
              ).textTheme.bodySmall?.copyWith(color: Colors.grey),
            ),
          ),
        ],
      ),
    );
  }

  // ── CPU Card ──────────────────────────────────────────────────────────────

  List<Widget> _buildCpuCard(CpuMetrics cpu) {
    return [
      _MetricCard(
        title: 'CPU',
        icon: Icons.memory,
        children: [
          _MetricRow(
            'Temperature',
            '${cpu.tempCelsius?.toStringAsFixed(1) ?? "—"}°C',
            color: _tempColor(cpu.tempCelsius),
          ),
          _MetricRow(
            'Load (1m / 5m / 15m)',
            '${cpu.loadAvg1m?.toStringAsFixed(2) ?? "—"} / ${cpu.loadAvg5m?.toStringAsFixed(2) ?? "—"} / ${cpu.loadAvg15m?.toStringAsFixed(2) ?? "—"}',
          ),
          _MetricRow(
            'Frequency',
            cpu.freqMhz != null ? '${cpu.freqMhz} MHz' : '—',
          ),
          if (cpu.usagePercent != null)
            _MetricRow(
              'Usage',
              '${cpu.usagePercent!.toStringAsFixed(1)}%',
              color: cpu.usagePercent! > 80 ? Colors.red : null,
            ),
        ],
      ),
      const SizedBox(height: 8),
    ];
  }

  // ── Memory Card ───────────────────────────────────────────────────────────

  List<Widget> _buildMemoryCard(MemoryMetrics mem) {
    final usedPercent = mem.totalKb != null && mem.totalKb! > 0
        ? (mem.usedKb ?? 0) / mem.totalKb! * 100
        : null;
    return [
      _MetricCard(
        title: 'Memory',
        icon: Icons.storage,
        children: [
          if (mem.totalKb != null && mem.usedKb != null)
            _MetricRow(
              'Used',
              '${_formatBytes(mem.usedKb! * 1024)} / ${_formatBytes(mem.totalKb! * 1024)}',
              color: usedPercent != null && usedPercent > 80
                  ? Colors.red
                  : null,
            ),
          if (mem.availableKb != null)
            _MetricRow('Available', _formatBytes(mem.availableKb! * 1024)),
          if (mem.swapTotalKb != null && mem.swapTotalKb! > 0)
            _MetricRow(
              'Swap',
              '${_formatBytes(mem.swapUsedKb! * 1024)} / ${_formatBytes(mem.swapTotalKb! * 1024)}',
            ),
        ],
      ),
      const SizedBox(height: 8),
    ];
  }

  // ── Pipeline Card ─────────────────────────────────────────────────────────

  List<Widget> _buildPipelineCard(PipelineMetrics pm) {
    return [
      _MetricCard(
        title: 'Pipeline',
        icon: Icons.videocam,
        children: [
          _MetricRow('FPS', pm.fps?.toStringAsFixed(1) ?? '—'),
          _MetricRow(
            'Inference',
            pm.inferenceTimeUs != null
                ? '${pm.inferenceTimeUs!.toStringAsFixed(0)} µs'
                : '—',
          ),
          _MetricRow(
            'Tracking',
            pm.trackingTimeUs != null
                ? '${pm.trackingTimeUs!.toStringAsFixed(0)} µs'
                : '—',
          ),
          _MetricRow('Frames Processed', '${pm.framesProcessed ?? 0}'),
        ],
      ),
      const SizedBox(height: 8),
    ];
  }

  // ── Storage Card ──────────────────────────────────────────────────────────

  List<Widget> _buildStorageCard(StorageMetrics storage) {
    return [
      _MetricCard(
        title: 'Storage',
        icon: Icons.folder,
        children: [
          if (storage.root != null)
            _MetricRow(
              'Root',
              '${_formatBytes(storage.root!.usedBytes ?? 0)} / ${_formatBytes(storage.root!.totalBytes ?? 0)}',
            ),
          if (storage.captures != null)
            _MetricRow(
              'Captures',
              '${_formatBytes(storage.captures!.usedBytes ?? 0)} / ${_formatBytes(storage.captures!.totalBytes ?? 0)}',
            ),
        ],
      ),
      const SizedBox(height: 8),
    ];
  }

  // ── Combined small metrics row ────────────────────────────────────────────

  Widget _buildMetricRow(SystemMetrics metrics) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Additional',
              style: TextStyle(fontWeight: FontWeight.w600, fontSize: 14),
            ),
            const SizedBox(height: 8),
            if (metrics.npu != null)
              _MetricRow(
                'NPU Temp',
                '${metrics.npu!.tempCelsius?.toStringAsFixed(1) ?? "—"}°C',
                color: _tempColor(metrics.npu!.tempCelsius),
              ),
            if (metrics.network != null)
              _MetricRow(
                'Network RX / TX',
                '${_formatBytes(metrics.network!.rxBytes ?? 0)} / ${_formatBytes(metrics.network!.txBytes ?? 0)}',
              ),
            if (metrics.power != null)
              _MetricRow(
                'Power',
                '${metrics.power!.voltageV?.toStringAsFixed(2) ?? "—"}V @ ${metrics.power!.currentA?.toStringAsFixed(2) ?? "—"}A',
              ),
          ],
        ),
      ),
    );
  }

  // ── Helpers ───────────────────────────────────────────────────────────────

  Color? _tempColor(double? celsius) {
    if (celsius == null) return null;
    if (celsius > 80) return Colors.red;
    if (celsius > 60) return Colors.orange;
    return null;
  }

  String _formatTimestamp(int ms) {
    final dt = DateTime.fromMillisecondsSinceEpoch(ms);
    return '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}:${dt.second.toString().padLeft(2, '0')}';
  }

  String _formatBytes(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }
}

// ─── Reusable metric card widget ──────────────────────────────────────────────

class _MetricCard extends StatelessWidget {
  final String title;
  final IconData icon;
  final List<Widget> children;

  const _MetricCard({
    required this.title,
    required this.icon,
    required this.children,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  icon,
                  size: 18,
                  color: Theme.of(context).colorScheme.primary,
                ),
                const SizedBox(width: 8),
                Text(
                  title,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 14,
                  ),
                ),
              ],
            ),
            const Divider(),
            ...children,
          ],
        ),
      ),
    );
  }
}

// ─── Single metric row ────────────────────────────────────────────────────────

Widget _MetricRow(String label, String value, {Color? color}) {
  return Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: const TextStyle(fontSize: 13, color: Colors.grey)),
        Text(
          value,
          style: TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w500,
            color: color,
          ),
        ),
      ],
    ),
  );
}
