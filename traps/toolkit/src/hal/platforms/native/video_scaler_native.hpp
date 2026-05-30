#pragma once

#include "hal/api/video_scaler.hpp"

namespace ct {

// ─── VideoScalerNative ───────────────────────────────────────────────────────
// Stub video scaler for host (macOS/Linux) development.
// Performs a software memcpy (no actual scaling) for testing the pipeline.
struct VideoScalerNative : IVideoScaler {
    bool scale_nv12(int src_fd, int src_w, int src_h,
                    int dst_fd, int dst_w, int dst_h) override;
    bool scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                           int dst_fd, int dst_w, int dst_h) override;
};

} // namespace ct
