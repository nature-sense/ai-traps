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
import '../providers/trap_provider.dart';
import '../models/system_metrics.dart';

/// Screen displaying real-time system metrics from the camera trap.
///
/// Shows each category (CPU, Memory, Storage, NPU, Pipeline, Network, Power)
/// as a distinct card. On phone (< 600dp) cards are arranged in a linear list.
/// On tablet (≥ 600dp) cards are arranged in a 2-column grid.
class MonitoringScreen extends StatefulWidget {
  const MonitoringScreen({super.key});

  @override
  State<MonitoringScreen> createState() => _MonitoringScreenState();
}

class _MonitoringScreenState extends State<MonitoringScreen> {
  Timer? _refreshTimer;

  @override
  void initState() {
    super.initState();
    _fetchMetrics();
    _refreshTimer = Timer.periodic(
      const Duration(seconds: 5),
      (_) => _fetchMetrics(),
    );
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  Future<void> _fetchMetrics() async {
    if (!mounted) return;
    await context.read<TrapProvider>().fetchSystemMetrics();
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<TrapProvider>(
      builder: (context, provider, _) {
        final metrics = provider.systemMetrics;

        if (metrics == null) {
          return const Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                CircularProgressIndicator(),
                SizedBox(height: 16),
                Text('Fetching system metrics...'),
              ],
            ),
          );
        }

        final isWide = MediaQuery.of(context).size.width >= 600;

        return RefreshIndicator(
          onRefresh: _fetchMetrics,
          child: isWide ? _buildGrid(metrics) : _buildList(metrics),
        );
      },
    );
  }

  // ── Phone: linear list of cards ─────────────────────────────────────────
  Widget _buildList(SystemMetrics metrics) {
    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        _buildSystemCard(metrics),
        if (metrics.cpu != null) _buildCpuCard(metrics.cpu!),
        if (metrics.memory != null) _buildMemoryCard(metrics.memory!),
        if (metrics.storage != null) _buildStorageCard(metrics.storage!),
        if (metrics.npu != null) _buildNpuCard(metrics.npu!),
        if (metrics.pipeline != null) _buildPipelineCard(metrics.pipeline!),
        if (metrics.network != null) _buildNetworkCard(metrics.network!),
        if (metrics.power != null) _buildPowerCard(metrics.power!),
        _buildTimestamp(metrics.timestampMs),
      ],
    );
  }

  // ── Tablet: 2-column grid of cards ──────────────────────────────────────
  Widget _buildGrid(SystemMetrics metrics) {
    final cards = <Widget>[
      _buildSystemCard(metrics),
      if (metrics.cpu != null) _buildCpuCard(metrics.cpu!),
      if (metrics.memory != null) _buildMemoryCard(metrics.memory!),
      if (metrics.storage != null) _buildStorageCard(metrics.storage!),
      if (metrics.npu != null) _buildNpuCard(metrics.npu!),
      if (metrics.pipeline != null) _buildPipelineCard(metrics.pipeline!),
      if (metrics.network != null) _buildNetworkCard(metrics.network!),
      if (metrics.power != null) _buildPowerCard(metrics.power!),
    ];

    return GridView.builder(
      padding: const EdgeInsets.all(12),
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
        maxCrossAxisExtent: 400,
        crossAxisSpacing: 12,
        mainAxisSpacing: 12,
        childAspectRatio: 1.6,
      ),
      itemCount: cards.length + 1, // +1 for timestamp
      itemBuilder: (context, index) {
        if (index < cards.length) return cards[index];
        return _buildTimestamp(metrics.timestampMs);
      },
    );
  }

  // ── Card Builders ───────────────────────────────────────────────────────

  Widget _buildSystemCard(SystemMetrics metrics) {
    return _metricCard(
      icon: Icons.monitor_heart_outlined,
      title: 'System',
      children: [
        _infoRow('Uptime', _formatUptime(metrics.uptimeSeconds)),
      ],
    );
  }

  Widget _buildCpuCard(CpuMetrics cpu) {
    return _metricCard(
      icon: Icons.memory,
      title: 'CPU',
      children: [
        if (cpu.tempCelsius != null)
          _metricRow('Temperature', cpu.tempCelsius!, '°C',
              valueColor: _tempColor(cpu.tempCelsius)),
        if (cpu.usagePercent != null)
          _metricRow('Usage', cpu.usagePercent!, '%'),
        if (cpu.freqMhz != null)
          _infoRow('Frequency', '${cpu.freqMhz} MHz'),
        if (cpu.loadAvg1m != null || cpu.loadAvg5m != null)
          _infoRow(
            'Load',
            '${cpu.loadAvg1m?.toStringAsFixed(2) ?? '?'} / '
            '${cpu.loadAvg5m?.toStringAsFixed(2) ?? '?'} / '
            '${cpu.loadAvg15m?.toStringAsFixed(2) ?? '?'}',
          ),
      ],
    );
  }

  Widget _buildMemoryCard(MemoryMetrics memory) {
    return _metricCard(
      icon: Icons.storage,
      title: 'Memory',
      children: [
        _infoRow('RAM',
            '${_formatKb(memory.usedKb)} / ${_formatKb(memory.totalKb)}'),
        if (memory.availableKb != null)
          _infoRow('Available', _formatKb(memory.availableKb)),
        if (memory.freeKb != null) _infoRow('Free', _formatKb(memory.freeKb)),
        if (memory.swapTotalKb != null && memory.swapTotalKb! > 0)
          _infoRow('Swap',
              '${_formatKb(memory.swapUsedKb)} / ${_formatKb(memory.swapTotalKb)}'),
      ],
    );
  }

  Widget _buildStorageCard(StorageMetrics storage) {
    return _metricCard(
      icon: Icons.disc_full,
      title: 'Storage',
      children: [
        if (storage.root != null) _storageRow('Root', storage.root!),
        if (storage.captures != null)
          _storageRow('Captures', storage.captures!),
      ],
    );
  }

  Widget _buildNpuCard(NpuMetrics npu) {
    return _metricCard(
      icon: Icons.developer_board,
      title: 'NPU',
      children: [
        if (npu.tempCelsius != null)
          _metricRow('Temperature', npu.tempCelsius!, '°C',
              valueColor: _tempColor(npu.tempCelsius)),
      ],
    );
  }

  Widget _buildPipelineCard(PipelineMetrics pipeline) {
    return _metricCard(
      icon: Icons.videocam,
      title: 'Pipeline',
      children: [
        if (pipeline.fps != null)
          _metricRow('FPS', pipeline.fps!, '',
              valueColor: Colors.green),
        if (pipeline.inferenceTimeUs != null)
          _infoRow('Inference',
              '${(pipeline.inferenceTimeUs! / 1000).toStringAsFixed(1)} ms'),
        if (pipeline.trackingTimeUs != null)
          _infoRow('Tracking',
              '${(pipeline.trackingTimeUs! / 1000).toStringAsFixed(1)} ms'),
        if (pipeline.tickTimeUs != null)
          _infoRow('Tick time',
              '${(pipeline.tickTimeUs! / 1000).toStringAsFixed(1)} ms'),
        if (pipeline.framesProcessed != null)
          _infoRow('Frames', '${pipeline.framesProcessed}'),
      ],
    );
  }

  Widget _buildNetworkCard(NetworkMetrics network) {
    return _metricCard(
      icon: Icons.wifi,
      title: 'Network',
      children: [
        _infoRow('RX', _formatBytes(network.rxBytes)),
        _infoRow('TX', _formatBytes(network.txBytes)),
      ],
    );
  }

  Widget _buildPowerCard(PowerMetrics power) {
    return _metricCard(
      icon: Icons.battery_charging_full,
      title: 'Power',
      children: [
        if (power.voltageV != null)
          _infoRow('Voltage', '${power.voltageV!.toStringAsFixed(2)} V'),
        if (power.currentA != null)
          _infoRow('Current', '${power.currentA!.toStringAsFixed(3)} A'),
      ],
    );
  }

  Widget _buildTimestamp(int timestampMs) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Text(
        'Last updated: ${DateTime.fromMillisecondsSinceEpoch(timestampMs)}',
        style: Theme.of(context).textTheme.bodySmall?.copyWith(
              color: Colors.grey,
            ),
        textAlign: TextAlign.center,
      ),
    );
  }

  // ── Card Widget ─────────────────────────────────────────────────────────

  Widget _metricCard({
    required IconData icon,
    required String title,
    required List<Widget> children,
  }) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Header
            Row(
              children: [
                Icon(icon, size: 18, color: Colors.green[700]),
                const SizedBox(width: 8),
                Text(
                  title,
                  style: Theme.of(context).textTheme.titleSmall?.copyWith(
                        fontWeight: FontWeight.bold,
                      ),
                ),
              ],
            ),
            const Divider(height: 16),
            // Content
            ...children,
          ],
        ),
      ),
    );
  }

  // ── Row Helpers ─────────────────────────────────────────────────────────

  Widget _infoRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: Theme.of(context).textTheme.bodySmall),
          Text(
            value,
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  fontWeight: FontWeight.w500,
                  fontFamily: 'monospace',
                ),
          ),
        ],
      ),
    );
  }

  Widget _metricRow(
    String label,
    double value,
    String unit, {
    Color? valueColor,
  }) {
    final display = '${value.toStringAsFixed(1)}$unit';
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: Theme.of(context).textTheme.bodySmall),
          Text(
            display,
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  fontWeight: FontWeight.bold,
                  fontFamily: 'monospace',
                  color: valueColor,
                ),
          ),
        ],
      ),
    );
  }

  Widget _storageRow(String label, StorageInfo info) {
    final used = info.usedBytes ?? 0;
    final total = info.totalBytes ?? 0;
    final hasData = total > 0;
    final percent = hasData ? (used / total) * 100 : 0.0;
    final fraction = hasData ? used / total : 0.0;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: Theme.of(context).textTheme.bodySmall),
          const SizedBox(height: 4),
          ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: LinearProgressIndicator(
              value: fraction,
              backgroundColor: Colors.grey[300],
              valueColor: AlwaysStoppedAnimation<Color>(
                percent > 90 ? Colors.red : Colors.green,
              ),
              minHeight: 6,
            ),
          ),
          const SizedBox(height: 2),
          Text(
            hasData
                ? '${_formatBytes(used)} / ${_formatBytes(total)} (${percent.toStringAsFixed(1)}%)'
                : 'No data',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  fontFamily: 'monospace',
                  color: Colors.grey[600],
                  fontSize: 11,
                ),
          ),
        ],
      ),
    );
  }

  // ── Formatting Helpers ──────────────────────────────────────────────────

  String _formatUptime(int seconds) {
    final days = seconds ~/ 86400;
    final hours = (seconds % 86400) ~/ 3600;
    final minutes = (seconds % 3600) ~/ 60;
    final secs = seconds % 60;
    final parts = <String>[];
    if (days > 0) parts.add('${days}d');
    if (hours > 0) parts.add('${hours}h');
    if (minutes > 0) parts.add('${minutes}m');
    parts.add('${secs}s');
    return parts.join(' ');
  }

  String _formatKb(int? kb) {
    if (kb == null) return '?';
    if (kb >= 1024 * 1024) {
      return '${(kb / (1024 * 1024)).toStringAsFixed(1)} GB';
    }
    if (kb >= 1024) {
      return '${(kb / 1024).toStringAsFixed(1)} MB';
    }
    return '$kb KB';
  }

  String _formatBytes(int? bytes) {
    if (bytes == null) return '?';
    if (bytes >= 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
    }
    if (bytes >= 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    if (bytes >= 1024) {
      return '${(bytes / 1024).toStringAsFixed(1)} KB';
    }
    return '$bytes B';
  }

  Color _tempColor(double? celsius) {
    if (celsius == null) return Colors.grey;
    if (celsius > 80) return Colors.red;
    if (celsius > 65) return Colors.orange;
    return Colors.green;
  }
}
