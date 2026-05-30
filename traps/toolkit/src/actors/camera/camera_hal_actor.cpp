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

#include "camera_hal_actor.hpp"
#include <iostream>

namespace ct {

CameraHalActor::CameraHalActor(std::unique_ptr<ICameraHAL> hal)
    : hal_(std::move(hal))
{
}

bool CameraHalActor::init(const PipelineConfig& cfg) {
    cfg_ = cfg;

    if (!hal_->init(cfg_)) {
        std::cerr << "[CameraHalActor] HAL init failed\n";
        return false;
    }

    std::cout << "[CameraHalActor] ready\n"
              << "  full:   " << cfg_.camera.full_w  << "x" << cfg_.camera.full_h  << "\n"
              << "  medium: " << cfg_.camera.med_w   << "x" << cfg_.camera.med_h   << "\n"
              << "  lores:  " << cfg_.camera.lores_w << "x" << cfg_.camera.lores_h << "\n";
    return true;
}

void CameraHalActor::shutdown() {
    hal_->shutdown();
    std::cout << "[CameraHalActor] shutdown complete\n";
}

void CameraHalActor::tick() {
    // Acquire frames from the HAL
    if (!hal_->acquire_frames(frame_full_, frame_medium_, frame_lores_)) {
        return;  // No frame available this tick
    }

    // Push frames to downstream Ramen ports
    if (frame_full_.dma_fd >= 0 || frame_full_.data) {
        out_frame_full(frame_full_);
    }
    if (frame_medium_.dma_fd >= 0 || frame_medium_.data) {
        out_frame_medium(frame_medium_);
    }
    if (frame_lores_.dma_fd >= 0 || frame_lores_.data) {
        out_frame_lores(frame_lores_);
    }


    // Release frames back to the HAL buffer pool
    hal_->release_frames();
}

} // namespace ct
