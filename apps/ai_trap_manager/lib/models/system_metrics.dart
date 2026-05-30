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

/// Real-time system health and pipeline performance metrics from the trap.
///
/// Corresponds to the SystemMetrics schema in the OpenAPI spec.
class SystemMetrics {
  final int timestampMs;
  final int uptimeSeconds;
  final CpuMetrics? cpu;
  final MemoryMetrics? memory;
  final StorageMetrics? storage;
  final NpuMetrics? npu;
  final NetworkMetrics? network;
  final PipelineMetrics? pipeline;
  final PowerMetrics? power;

  SystemMetrics({
    required this.timestampMs,
    required this.uptimeSeconds,
    this.cpu,
    this.memory,
    this.storage,
    this.npu,
    this.network,
    this.pipeline,
    this.power,
  });

  factory SystemMetrics.fromJson(Map<String, dynamic> json) {
    return SystemMetrics(
      timestampMs: json['timestamp_ms'] as int? ?? 0,
      uptimeSeconds: json['uptime_seconds'] as int? ?? 0,
      cpu: json['cpu'] != null
          ? CpuMetrics.fromJson(json['cpu'] as Map<String, dynamic>)
          : null,
      memory: json['memory'] != null
          ? MemoryMetrics.fromJson(json['memory'] as Map<String, dynamic>)
          : null,
      storage: json['storage'] != null
          ? StorageMetrics.fromJson(json['storage'] as Map<String, dynamic>)
          : null,
      npu: json['npu'] != null
          ? NpuMetrics.fromJson(json['npu'] as Map<String, dynamic>)
          : null,
      network: json['network'] != null
          ? NetworkMetrics.fromJson(json['network'] as Map<String, dynamic>)
          : null,
      pipeline: json['pipeline'] != null
          ? PipelineMetrics.fromJson(json['pipeline'] as Map<String, dynamic>)
          : null,
      power: json['power'] != null
          ? PowerMetrics.fromJson(json['power'] as Map<String, dynamic>)
          : null,
    );
  }
}

class CpuMetrics {
  final double? tempCelsius;
  final double? usagePercent;
  final int? freqMhz;
  final double? loadAvg1m;
  final double? loadAvg5m;
  final double? loadAvg15m;

  CpuMetrics({
    this.tempCelsius,
    this.usagePercent,
    this.freqMhz,
    this.loadAvg1m,
    this.loadAvg5m,
    this.loadAvg15m,
  });

  factory CpuMetrics.fromJson(Map<String, dynamic> json) => CpuMetrics(
        tempCelsius: (json['temp_celsius'] as num?)?.toDouble(),
        usagePercent: (json['usage_percent'] as num?)?.toDouble(),
        freqMhz: json['freq_mhz'] as int?,
        loadAvg1m: (json['load_avg_1m'] as num?)?.toDouble(),
        loadAvg5m: (json['load_avg_5m'] as num?)?.toDouble(),
        loadAvg15m: (json['load_avg_15m'] as num?)?.toDouble(),
      );
}

class MemoryMetrics {
  final int? totalKb;
  final int? availableKb;
  final int? freeKb;
  final int? usedKb;
  final int? swapTotalKb;
  final int? swapUsedKb;

  MemoryMetrics({
    this.totalKb,
    this.availableKb,
    this.freeKb,
    this.usedKb,
    this.swapTotalKb,
    this.swapUsedKb,
  });

  factory MemoryMetrics.fromJson(Map<String, dynamic> json) => MemoryMetrics(
        totalKb: json['total_kb'] as int?,
        availableKb: json['available_kb'] as int?,
        freeKb: json['free_kb'] as int?,
        usedKb: json['used_kb'] as int?,
        swapTotalKb: json['swap_total_kb'] as int?,
        swapUsedKb: json['swap_used_kb'] as int?,
      );
}

class StorageMetrics {
  final StorageInfo? root;
  final StorageInfo? captures;

  StorageMetrics({this.root, this.captures});

  factory StorageMetrics.fromJson(Map<String, dynamic> json) => StorageMetrics(
        root: json['root'] != null
            ? StorageInfo.fromJson(json['root'] as Map<String, dynamic>)
            : null,
        captures: json['captures'] != null
            ? StorageInfo.fromJson(json['captures'] as Map<String, dynamic>)
            : null,
      );
}

class StorageInfo {
  final int? totalBytes;
  final int? usedBytes;

  StorageInfo({this.totalBytes, this.usedBytes});

  factory StorageInfo.fromJson(Map<String, dynamic> json) => StorageInfo(
        totalBytes: json['total_bytes'] as int?,
        usedBytes: json['used_bytes'] as int?,
      );
}

class NpuMetrics {
  final double? tempCelsius;

  NpuMetrics({this.tempCelsius});

  factory NpuMetrics.fromJson(Map<String, dynamic> json) => NpuMetrics(
        tempCelsius: (json['temp_celsius'] as num?)?.toDouble(),
      );
}

class NetworkMetrics {
  final int? rxBytes;
  final int? txBytes;

  NetworkMetrics({this.rxBytes, this.txBytes});

  factory NetworkMetrics.fromJson(Map<String, dynamic> json) =>
      NetworkMetrics(
        rxBytes: json['rx_bytes'] as int?,
        txBytes: json['tx_bytes'] as int?,
      );
}

class PipelineMetrics {
  final double? fps;
  final double? inferenceTimeUs;
  final double? trackingTimeUs;
  final double? tickTimeUs;
  final int? framesProcessed;

  PipelineMetrics({
    this.fps,
    this.inferenceTimeUs,
    this.trackingTimeUs,
    this.tickTimeUs,
    this.framesProcessed,
  });

  factory PipelineMetrics.fromJson(Map<String, dynamic> json) =>
      PipelineMetrics(
        fps: (json['fps'] as num?)?.toDouble(),
        inferenceTimeUs: (json['inference_time_us'] as num?)?.toDouble(),
        trackingTimeUs: (json['tracking_time_us'] as num?)?.toDouble(),
        tickTimeUs: (json['tick_time_us'] as num?)?.toDouble(),
        framesProcessed: json['frames_processed'] as int?,
      );
}

class PowerMetrics {
  final double? voltageV;
  final double? currentA;

  PowerMetrics({this.voltageV, this.currentA});

  factory PowerMetrics.fromJson(Map<String, dynamic> json) => PowerMetrics(
        voltageV: (json['voltage_v'] as num?)?.toDouble(),
        currentA: (json['current_a'] as num?)?.toDouble(),
      );
}
