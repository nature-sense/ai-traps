// ─── detection/platforms/cubie-a7s/main.cpp ────────────────────────────────────
// Main entry point for the detection-only trap on Radxa Cubie A7S (Allwinner A527).
// Creates a CubieA7sDetectionPipeline, initialises it, and runs the main loop.
//
// Camera: Radxa Camera 4K (IMX415) via V4L2
// Inference: Vivante VIPLite NPU (NBG model format)
// Scaling: CPU-based bilinear (no RGA hardware scaler)

#include "cubie_a7s_detection_pipeline.hpp"
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
    std::cout << "AI Trap — Detection (Radxa Cubie A7S)\n";

    // ── Parse config from environment / defaults ──────────────────────────────
    ct::PipelineConfig cfg;
    cfg.camera.model               = "a7s";
    cfg.camera.full_w              = 1920;
    cfg.camera.full_h              = 1080;
    cfg.camera.med_w               = 640;
    cfg.camera.med_h               = 480;
    cfg.camera.lores_w             = 320;
    cfg.camera.lores_h             = 320;
    cfg.camera.fps                 = 15;
    cfg.camera.device              = "/dev/video0";
    cfg.inference.model_path       = "/usr/share/ai-trap/models/yolo11n.nbg";
    cfg.storage.output_dir         = "/var/lib/ai-trap/detections";
    cfg.storage.db_path            = "/var/lib/ai-trap/trap.db";
    cfg.inference.confidence_threshold = 0.5f;
    cfg.decision.trigger_confidence = 0.6f;
    cfg.decision.cooldown_ms       = 3000;
    cfg.cropper.padding_px         = 10;
    cfg.cropper.min_confidence     = 0.5f;

    // ── Install signal handlers ───────────────────────────────────────────────
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Create and run pipeline ───────────────────────────────────────────────
    ct::CubieA7sDetectionPipeline pipeline;
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
