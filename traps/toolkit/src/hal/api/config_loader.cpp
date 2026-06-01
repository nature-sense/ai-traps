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

#include "config_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ct {

// ─── Helper: read a scalar value with a default ───────────────────────────────
// Returns the value of `key` from `node` if present, otherwise `default_val`.

template<typename T>
static T get_or(const YAML::Node& node, const std::string& key, const T& default_val) {
    if (!node || !node[key]) {
        return default_val;
    }
    try {
        return node[key].as<T>();
    } catch (const YAML::Exception& e) {
        std::cerr << "[config_loader] warning: failed to parse '" << key
                  << "': " << e.what() << " (using default)\n";
        return default_val;
    }
}

// ─── loadConfig ───────────────────────────────────────────────────────────────

PipelineConfig loadConfig(const std::string& path) {
    PipelineConfig cfg;

    try {
        YAML::Node root = YAML::LoadFile(path);

        // ── Camera ────────────────────────────────────────────────────────────
        if (root["camera"]) {
            const auto& n = root["camera"];
            cfg.camera.model     = get_or(n, "model",     cfg.camera.model);
            cfg.camera.full_w    = get_or(n, "full_w",    cfg.camera.full_w);
            cfg.camera.full_h    = get_or(n, "full_h",    cfg.camera.full_h);
            cfg.camera.med_w     = get_or(n, "med_w",     cfg.camera.med_w);
            cfg.camera.med_h     = get_or(n, "med_h",     cfg.camera.med_h);
            cfg.camera.lores_w   = get_or(n, "lores_w",   cfg.camera.lores_w);
            cfg.camera.lores_h   = get_or(n, "lores_h",   cfg.camera.lores_h);
            cfg.camera.fps       = get_or(n, "fps",       cfg.camera.fps);
            cfg.camera.device    = get_or(n, "device",    cfg.camera.device);
            cfg.camera.scene_dir = get_or(n, "scene_dir", cfg.camera.scene_dir);
            cfg.camera.iq_dir    = get_or(n, "iq_dir",    cfg.camera.iq_dir);
        }

        // ── Inference ─────────────────────────────────────────────────────────
        if (root["inference"]) {
            const auto& n = root["inference"];
            cfg.inference.backend              = get_or(n, "backend",               cfg.inference.backend);
            cfg.inference.model_path           = get_or(n, "model_path",            cfg.inference.model_path);
            cfg.inference.confidence_threshold = get_or(n, "confidence_threshold",  cfg.inference.confidence_threshold);
        }

        // ── Storage ───────────────────────────────────────────────────────────
        if (root["storage"]) {
            const auto& n = root["storage"];
            cfg.storage.output_dir = get_or(n, "output_dir", cfg.storage.output_dir);
            cfg.storage.db_path    = get_or(n, "db_path",    cfg.storage.db_path);
        }

        // ── Cropper ───────────────────────────────────────────────────────────
        if (root["cropper"]) {
            const auto& n = root["cropper"];
            cfg.cropper.padding_px    = get_or(n, "padding_px",    cfg.cropper.padding_px);
            cfg.cropper.min_confidence = get_or(n, "min_confidence", cfg.cropper.min_confidence);
        }

        // ── Classifier ────────────────────────────────────────────────────────
        if (root["classifier"]) {
            const auto& n = root["classifier"];
            cfg.classifier.model_path           = get_or(n, "model_path",            cfg.classifier.model_path);
            cfg.classifier.confidence_threshold  = get_or(n, "confidence_threshold",  cfg.classifier.confidence_threshold);
            cfg.classifier.input_width           = get_or(n, "input_width",           cfg.classifier.input_width);
            cfg.classifier.input_height          = get_or(n, "input_height",          cfg.classifier.input_height);
        }

        // ── Actuator ──────────────────────────────────────────────────────────
        if (root["actuator"]) {
            const auto& n = root["actuator"];
            cfg.actuator.type        = get_or(n, "type",         cfg.actuator.type);
            cfg.actuator.gpio        = get_or(n, "gpio",         cfg.actuator.gpio);
            cfg.actuator.duration_ms = get_or(n, "duration_ms",  cfg.actuator.duration_ms);
        }

        // ── Motion Sensor ─────────────────────────────────────────────────────
        if (root["motion_sensor"]) {
            const auto& n = root["motion_sensor"];
            cfg.motion_sensor.gpio = get_or(n, "gpio", cfg.motion_sensor.gpio);
        }

        // ── Decision ──────────────────────────────────────────────────────────
        if (root["decision"]) {
            const auto& n = root["decision"];
            cfg.decision.trigger_confidence = get_or(n, "trigger_confidence", cfg.decision.trigger_confidence);
            cfg.decision.cooldown_ms        = get_or(n, "cooldown_ms",        cfg.decision.cooldown_ms);
        }

    } catch (const YAML::BadFile& e) {
        throw std::runtime_error("Cannot open config file '" + path + "': " + e.what());
    } catch (const YAML::ParserException& e) {
        throw std::runtime_error("YAML parse error in '" + path + "': " + e.what());
    }

    return cfg;
}

// ─── saveConfig ───────────────────────────────────────────────────────────────

void saveConfig(const std::string& path, const PipelineConfig& cfg) {
    YAML::Emitter out;
    out << YAML::BeginMap;

    // ── Camera ────────────────────────────────────────────────────────────────
    out << YAML::Key << "camera" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "model"      << YAML::Value << cfg.camera.model;
    out << YAML::Key << "full_w"     << YAML::Value << cfg.camera.full_w;
    out << YAML::Key << "full_h"     << YAML::Value << cfg.camera.full_h;
    out << YAML::Key << "med_w"      << YAML::Value << cfg.camera.med_w;
    out << YAML::Key << "med_h"      << YAML::Value << cfg.camera.med_h;
    out << YAML::Key << "lores_w"    << YAML::Value << cfg.camera.lores_w;
    out << YAML::Key << "lores_h"    << YAML::Value << cfg.camera.lores_h;
    out << YAML::Key << "fps"        << YAML::Value << cfg.camera.fps;
    out << YAML::Key << "device"     << YAML::Value << cfg.camera.device;
    out << YAML::Key << "scene_dir"  << YAML::Value << cfg.camera.scene_dir;
    out << YAML::Key << "iq_dir"     << YAML::Value << cfg.camera.iq_dir;
    out << YAML::EndMap;

    // ── Inference ─────────────────────────────────────────────────────────────
    out << YAML::Key << "inference" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "backend"               << YAML::Value << cfg.inference.backend;
    out << YAML::Key << "model_path"            << YAML::Value << cfg.inference.model_path;
    out << YAML::Key << "confidence_threshold"  << YAML::Value << cfg.inference.confidence_threshold;
    out << YAML::EndMap;

    // ── Storage ───────────────────────────────────────────────────────────────
    out << YAML::Key << "storage" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "output_dir" << YAML::Value << cfg.storage.output_dir;
    out << YAML::Key << "db_path"    << YAML::Value << cfg.storage.db_path;
    out << YAML::EndMap;

    // ── Cropper ───────────────────────────────────────────────────────────────
    out << YAML::Key << "cropper" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "padding_px"     << YAML::Value << cfg.cropper.padding_px;
    out << YAML::Key << "min_confidence" << YAML::Value << cfg.cropper.min_confidence;
    out << YAML::EndMap;

    // ── Classifier ────────────────────────────────────────────────────────────
    out << YAML::Key << "classifier" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "model_path"           << YAML::Value << cfg.classifier.model_path;
    out << YAML::Key << "confidence_threshold" << YAML::Value << cfg.classifier.confidence_threshold;
    out << YAML::Key << "input_width"          << YAML::Value << cfg.classifier.input_width;
    out << YAML::Key << "input_height"         << YAML::Value << cfg.classifier.input_height;
    out << YAML::EndMap;

    // ── Actuator ──────────────────────────────────────────────────────────────
    out << YAML::Key << "actuator" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "type"        << YAML::Value << cfg.actuator.type;
    out << YAML::Key << "gpio"        << YAML::Value << cfg.actuator.gpio;
    out << YAML::Key << "duration_ms" << YAML::Value << cfg.actuator.duration_ms;
    out << YAML::EndMap;

    // ── Motion Sensor ─────────────────────────────────────────────────────────
    out << YAML::Key << "motion_sensor" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "gpio" << YAML::Value << cfg.motion_sensor.gpio;
    out << YAML::EndMap;

    // ── Decision ──────────────────────────────────────────────────────────────
    out << YAML::Key << "decision" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "trigger_confidence" << YAML::Value << cfg.decision.trigger_confidence;
    out << YAML::Key << "cooldown_ms"        << YAML::Value << cfg.decision.cooldown_ms;
    out << YAML::EndMap;

    out << YAML::EndMap;

    // Write to file
    std::ofstream fout(path);
    if (!fout.is_open()) {
        throw std::runtime_error("Cannot write config file '" + path + "'");
    }
    fout << out.c_str();
    fout.close();
}

} // namespace ct
