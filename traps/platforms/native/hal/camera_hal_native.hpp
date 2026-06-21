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

#include "hal/api/camera_hal.hpp"
#include "hal/api/frame_buffer.hpp"
#include <cstdint>

namespace ct {

// ─── CameraHalNative ─────────────────────────────────────────────────────────
// Stub camera HAL for host (macOS/Linux) development.
// Returns synthetic frames for testing the pipeline without real hardware.
struct CameraHalNative : ICameraHAL {
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

    // Config
    int width_  = 640;
    int height_ = 480;
    int fps_    = 30;
};

} // namespace ct
