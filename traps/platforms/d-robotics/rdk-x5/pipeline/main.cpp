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

// ─── detection/platforms/rdk-x5/main.cpp ──────────────────────────────────────
// Main entry point for the detection-only trap on RDK X5 (D-Robotics X5 SoC).
// Creates an RdkX5DetectionPipeline, initialises it, and runs the main loop.
//
// Usage:
//   ai-trap-detection [--config /etc/ai-trap/config.yaml]
//
// If --config is not provided, built-in defaults are used.

#include "rdk_x5_detection_pipeline.hpp"
#include "hal/api/types.hpp"
#include "hal/api/config_loader.hpp"

#include <iostream>
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace {

ct::BaseDetectionPipeline* g_pipeline = nullptr;

void signal_handler(int sig) {
    std::cout << "\n[main] signal " << sig << " received, shutting down...\n";
    if (g_pipeline) {
        g_pipeline->stop();
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "AI Trap — Detection (RDK X5)\n";

    // ── Parse CLI arguments ───────────────────────────────────────────────────
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // ── Load configuration ────────────────────────────────────────────────────
    ct::PipelineConfig cfg;
    if (!config_path.empty()) {
        std::cout << "[main] loading config from " << config_path << "\n";
        try {
            cfg = ct::loadConfig(config_path);
        } catch (const std::exception& e) {
            std::cerr << "[main] failed to load config: " << e.what() << "\n";
            return 1;
        }
    } else {
        std::cout << "[main] using built-in default configuration\n";
        cfg.camera.model               = "rdkx5";
        cfg.camera.full_w              = 1920;
        cfg.camera.full_h              = 1080;
        cfg.camera.med_w               = 640;
        cfg.camera.med_h               = 480;
        cfg.camera.lores_w             = 320;
        cfg.camera.lores_h             = 320;
        cfg.camera.fps                 = 15;
        cfg.inference.model_path       = "/usr/share/ai-trap/model.bin";
        cfg.storage.output_dir         = "/var/lib/ai-trap/detections";
        cfg.storage.db_path            = "/var/lib/ai-trap/trap.db";
        cfg.inference.confidence_threshold = 0.5f;
        cfg.decision.trigger_confidence = 0.6f;
        cfg.decision.cooldown_ms       = 3000;
        cfg.cropper.padding_px         = 10;
        cfg.cropper.min_confidence     = 0.5f;
    }

    // ── Install signal handlers ───────────────────────────────────────────────
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Create and run pipeline ───────────────────────────────────────────────
    ct::RdkX5DetectionPipeline pipeline;
    g_pipeline = &pipeline;

    if (!pipeline.init(cfg)) {
        std::cerr << "[main] pipeline init failed\n";
        return 1;
    }

    std::cout << "[main] pipeline initialised, entering run loop\n";
    pipeline.run_loop();

    std::cout << "[main] pipeline stopped, exiting\n";
    return 0;
}