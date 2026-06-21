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

#include "pipeline/base_detection_pipeline.hpp"
#include "wifi-provisioning/wifi_provisioning_actor.hpp"

#include <memory>
#include <string>

namespace ct {

// ─── Rock3cDetectionPipeline ──────────────────────────────────────────────────
// Platform-specific pipeline for ROCK 3C (RK3566).
// Uses CameraHalImx219 (IMX219 sensor via V4L2 + AIQ) and
// InferenceHalRKNN (RKNN NPU).
//
// Also starts the WifiProvisioningActor for BLE advertising and WiFi
// provisioning via the GATT service.
class Rock3cDetectionPipeline : public BaseDetectionPipeline {
public:
    ~Rock3cDetectionPipeline() override;

    // Override init to start the BLE provisioning actor after the base
    // pipeline has been initialised.
    bool init(const PipelineConfig& cfg) override;

    // Override shutdown to stop the BLE provisioning actor before the
    // base pipeline shuts down.
    void shutdown() override;

protected:
    std::unique_ptr<CameraActor> createCamera() override;
    std::unique_ptr<IInferenceHAL> createInferenceHAL() override;

private:
    // BLE WiFi provisioning actor — advertises trap identity and exposes
    // a GATT service for remote WiFi configuration.
    std::unique_ptr<WifiProvisioningActor> wifi_provisioning_;

    // Trap identity string used for BLE advertising.
    std::string trap_id_;
};

} // namespace ct
