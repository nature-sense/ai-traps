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
