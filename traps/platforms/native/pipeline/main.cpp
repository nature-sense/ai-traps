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

// ─── detection/platforms/native/main.cpp ───────────────────────────────────────
// Main entry point for the detection-only trap on macOS (host platform).
// Creates a NativeDetectionPipeline, initialises it, and runs the main loop.

#include "native_detection_pipeline.hpp"
#include "hal/api/types.hpp"

#include <iostream>
#include <csignal>
#include <cstdlib>

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
    std::cout << "AI Trap — Detection (native/host)\n";

    // ── Parse config from environment / defaults ──────────────────────────────
    ct::PipelineConfig cfg;
    cfg.camera.model               = "native";
    cfg.camera.full_w              = 640;
    cfg.camera.full_h              = 480;
    cfg.camera.med_w               = 640;
    cfg.camera.med_h               = 480;
    cfg.camera.lores_w             = 320;
    cfg.camera.lores_h             = 320;
    cfg.camera.fps                 = 15;
    cfg.inference.model_path       = "models/yolo11n.rknn";  // won't be loaded by stub
    cfg.storage.output_dir         = "/tmp/ai-trap-detections";
    cfg.storage.db_path            = "/tmp/ai-trap-detections/trap.db";
    cfg.inference.confidence_threshold = 0.5f;
    cfg.decision.trigger_confidence = 0.6f;
    cfg.decision.cooldown_ms       = 3000;
    cfg.cropper.padding_px         = 10;
    cfg.cropper.min_confidence     = 0.5f;

    // ── Install signal handlers ───────────────────────────────────────────────
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Create and run pipeline ───────────────────────────────────────────────
    ct::NativeDetectionPipeline pipeline;
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
