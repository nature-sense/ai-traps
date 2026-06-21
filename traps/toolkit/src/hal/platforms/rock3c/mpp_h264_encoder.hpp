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

#include <vector>
#include <cstdint>

#include "hal/api/h264_encoder.hpp"

namespace ct {

// ─── MppH264Encoder ───────────────────────────────────────────────────────────
// Hardware-accelerated NV12 → H.264 encoding via MPP (VEPU on RK3566).
// Generates standalone IDR frames — each encode() call produces a complete
// H.264 keyframe that can be decoded independently.
class MppH264Encoder : public H264Encoder {
public:
    MppH264Encoder();
    ~MppH264Encoder() noexcept override;

    MppH264Encoder(const MppH264Encoder&) = delete;
    MppH264Encoder& operator=(const MppH264Encoder&) = delete;

    int init(int w, int h, int qp = 26) override;

    std::vector<uint8_t> encode(int dma_fd, uint32_t size) override;

    void deinit();

private:
    void* mpp_ctx_     = nullptr;  // MppCtx
    void* mpp_api_     = nullptr;  // MppApi* (cast from mpp_create)
    void* mpp_frame_   = nullptr;  // MppFrame
    void* buf_group_   = nullptr;  // MppBufferGroup
    int   width_       = 0;
    int   height_      = 0;
};

} // namespace ct