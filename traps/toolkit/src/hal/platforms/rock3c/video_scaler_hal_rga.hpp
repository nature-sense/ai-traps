#pragma once

#include "hal/api/video_scaler.hpp"

#include <rga/RgaApi.h>
#include <rga/rga.h>

namespace ct {

// ─── VideoScalerRGA ───────────────────────────────────────────────────────────
// IVideoScaler implementation for Rockchip platforms using the RGA hardware.
// Supports zero-copy scaling and format conversion using DMA-BUF.
class VideoScalerRGA : public IVideoScaler {
public:
    VideoScalerRGA();
    ~VideoScalerRGA() override;

    // ── IVideoScaler interface ────────────────────────────────────────────────
    bool scale_nv12(int src_fd, int src_w, int src_h,
                    int dst_fd, int dst_w, int dst_h) override;

    bool scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                           int dst_fd, int dst_w, int dst_h) override;

private:
    bool blit(int src_fd, int src_w, int src_h, int src_fmt,
              int dst_fd, int dst_w, int dst_h, int dst_fmt);

    // RockRga instance (from librga)
    // Note: librga usually provides a global instance or a static class.
};

} // namespace ct
