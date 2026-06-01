/*
 * Copyright 2026 Nature Sense
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "types.hpp"

#include <string>

namespace ct {

// ─── Config file loader ───────────────────────────────────────────────────────
// Reads a YAML configuration file and populates a PipelineConfig struct.
//
// The YAML structure mirrors the C++ struct hierarchy:
//
//   camera:
//     model: "imx415"
//     full_w: 1920
//     full_h: 1080
//     ...
//   inference:
//     backend: "rknn"
//     model_path: "/usr/share/ai-trap/models/yolo11n.rknn"
//     ...
//   storage:
//     output_dir: "/var/lib/ai-trap/detections"
//     ...
//   cropper:
//     padding_px: 10
//     ...
//   classifier:
//     model_path: ""
//     ...
//   actuator:
//     type: "none"
//     ...
//   motion_sensor:
//     gpio: -1
//     ...
//   decision:
//     trigger_confidence: 0.6
//     cooldown_ms: 3000
//
// Fields not present in the YAML file retain their default values from the
// PipelineConfig struct definition.

// Load configuration from a YAML file.
// Returns the populated PipelineConfig. Missing fields use defaults.
// Throws std::runtime_error if the file cannot be read or parsed.
PipelineConfig loadConfig(const std::string& path);

// Save configuration to a YAML file.
// Writes all fields of the PipelineConfig to the specified path.
// Throws std::runtime_error if the file cannot be written.
void saveConfig(const std::string& path, const PipelineConfig& cfg);

} // namespace ct
