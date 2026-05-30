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
