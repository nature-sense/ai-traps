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
#include <memory>

#ifdef HAVE_MPP
extern "C" {
// MPP headers (under rockchip/ subdirectory)
#include <rockchip/rk_type.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_venc_cfg.h>
#include <rockchip/rk_venc_rc.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
}
#endif

namespace ct {

// ─── HardwareJpegEncoder ──────────────────────────────────────────────────────
// Wraps Rockchip MPP VENC hardware JPEG encoder for NV12→JPEG conversion.
// Uses hardware VPU for encoding, reducing CPU load significantly.
//
// Usage:
//   HardwareJpegEncoder enc;
//   if (enc.init(640, 480, 85)) {
//       auto jpeg = enc.encode(frame.data, frame.width, frame.height, frame.stride);
//   }
//   enc.shutdown();
//
// Thread safety: Not thread-safe. Create one per thread or protect with mutex.
class HardwareJpegEncoder {
public:
    HardwareJpegEncoder();
    ~HardwareJpegEncoder();
    
    // Initialize encoder with given dimensions and quality (1-100)
    bool init(int width, int height, int quality = 85);
    
    // Encode NV12 frame to JPEG
    std::vector<uint8_t> encode(const void* nv12_data, int width, int height, int stride);
    
    // Clean up resources
    void shutdown();
    
    // Check if encoder is initialized
    bool is_initialized() const { return initialized_; }
    
private:
    bool create_mpp_encoder();
    bool configure_encoder(int width, int height, int quality);
    void cleanup();
    
#ifdef HAVE_MPP
    MppCtx          mpp_ctx_ = nullptr;
    MppApi*         mpp_api_ = nullptr;
    MppEncCfg       mpp_cfg_ = nullptr;
    MppBufferGroup  buf_group_ = nullptr;
#endif
    
    int width_ = 0;
    int height_ = 0;
    int quality_ = 85;
    bool initialized_ = false;
    
    // Statistics
    uint64_t frames_encoded_ = 0;
    uint64_t total_encode_time_us_ = 0;
};

} // namespace ct