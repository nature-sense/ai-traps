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

#include <cstdint>

namespace ct {

// ─── IVideoScaler ─────────────────────────────────────────────────────────────
// Abstract interface for hardware-accelerated video scaling and format conversion.
// Each platform provides a concrete implementation (VPSS, RGA, software, etc.).
struct IVideoScaler {
    virtual ~IVideoScaler() = default;

    // Scale NV12 frame from src to dst size. Both buffers must be pre-allocated.
    // src_fd/dst_fd: DMA-BUF file descriptors (or -1 for system memory).
    virtual bool scale_nv12(int src_fd, int src_w, int src_h,
                            int dst_fd, int dst_w, int dst_h) = 0;

    // Scale NV12 frame to BGR888 (for NPU inference input).
    virtual bool scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                                   int dst_fd, int dst_w, int dst_h) = 0;
};

} // namespace ct
