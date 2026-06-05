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

#include "rock3c_detection_pipeline.hpp"

#include "camera/camera_hal_actor.hpp"
#include "hal/platforms/rock3c/camera_hal_imx219.hpp"
#include "hal/platforms/rock3c/camera_hal_imx415.hpp"
#include "hal/platforms/rock3c/inference_hal_rknn.hpp"

#include <iostream>
#include <memory>
#include <cstdlib>

namespace ct {

// ─── Destructor ───────────────────────────────────────────────────────────────

Rock3cDetectionPipeline::~Rock3cDetectionPipeline() {
    shutdown();
}

// ─── Init ─────────────────────────────────────────────────────────────────────

bool Rock3cDetectionPipeline::init(const PipelineConfig& cfg) {
    // 1. Initialise the base pipeline (camera, inference, actors, wiring)
    if (!BaseDetectionPipeline::init(cfg)) {
        return false;
    }

    // 2. Determine trap identity for BLE advertising
    //    Use the hostname if available, otherwise fall back to a default.
    const char* hostname = std::getenv("HOSTNAME");
    if (hostname && hostname[0] != '\0') {
        trap_id_ = hostname;
    } else {
        trap_id_ = "rock3c-trap";
    }
    std::cout << "[Rock3cDetectionPipeline] trap_id=" << trap_id_ << "\n";

    // 3. Create and start the BLE WiFi provisioning actor
    wifi_provisioning_ = std::make_unique<WifiProvisioningActor>();
    if (!wifi_provisioning_->init(trap_id_)) {
        std::cerr << "[Rock3cDetectionPipeline] WifiProvisioningActor init failed\n";
        // Non-fatal: the pipeline can still run without BLE
        std::cerr << "[Rock3cDetectionPipeline] continuing without BLE advertising\n";
        wifi_provisioning_.reset();
    } else {
        std::cout << "[Rock3cDetectionPipeline] BLE advertising started (trap_id="
                  << trap_id_ << ")\n";
    }

    return true;
}

// ─── Shutdown ─────────────────────────────────────────────────────────────────

void Rock3cDetectionPipeline::shutdown() {
    std::cout << "[Rock3cDetectionPipeline] shutdown\n";

    // Stop the BLE provisioning actor first (before the base pipeline shuts
    // down the HTTP server and other actors).
    if (wifi_provisioning_) {
        wifi_provisioning_->shutdown();
        wifi_provisioning_.reset();
        std::cout << "[Rock3cDetectionPipeline] BLE advertising stopped\n";
    }

    // Shut down the base pipeline
    BaseDetectionPipeline::shutdown();
}

// ─── Factory methods ──────────────────────────────────────────────────────────

std::unique_ptr<CameraActor> Rock3cDetectionPipeline::createCamera() {
    // Select camera HAL based on the model name from config.
    // Supported models: "imx219" (picamera2), "imx415" (Radxa Camera 4K).
    const std::string& model = cfg_.camera.model;
    std::cout << "[Rock3cDetectionPipeline] creating camera HAL for model=\""
              << model << "\"\n";

    std::unique_ptr<ICameraHAL> hal;
    if (model == "imx219") {
        hal = std::make_unique<CameraHalImx219>();
    } else {
        // Default to IMX415 (also handles "imx415" and unknown models gracefully)
        hal = std::make_unique<CameraHalImx415>();
    }

    auto actor = std::make_unique<CameraHalActor>(std::move(hal));
    return actor;
}

std::unique_ptr<IInferenceHAL> Rock3cDetectionPipeline::createInferenceHAL() {
    return std::make_unique<InferenceHalRKNN>();
}

} // namespace ct
