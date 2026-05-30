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

// ─── Frame buffer handle ──────────────────────────────────────────────────────
// Represents one video frame at any resolution. Used as the common frame type
// across the pipeline (camera → cropper → overlay → encode).
//
// On Rockchip platforms, the buffer may be backed by VPSS DMA memory and
// valid only for the duration of one pipeline tick. On other platforms,
// the buffer is heap-allocated and persists as long as the owning actor
// holds it.
struct FrameBuffer {
    void*    data         = nullptr;   // Pointer to pixel data (NV12)
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint32_t stride       = 0;         // Bytes per row
    uint32_t size         = 0;         // Total bytes
    int64_t  timestamp_ms = 0;
    int      dma_fd       = -1;        // Platform-specific zero-copy handle
                                       // (dma-buf fd on Rockchip, -1 on other platforms)
};

} // namespace ct
