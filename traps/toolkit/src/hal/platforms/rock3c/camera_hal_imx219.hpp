#pragma once

#include "hal/api/camera_hal.hpp"

// RK_AIQ: ISP 3A control
extern "C" {
#include "uAPI2/rk_aiq_user_api2_sysctl.h"
#include "uAPI2/rk_aiq_user_api2_imgproc.h"
#include "uAPI2/rk_aiq_user_api2_ae.h"
}

// librga/im2d for hardware-accelerated image scaling
#include <rga/im2d.h>
#include <rga/im2d_single.h>
#include <rga/im2d_type.h>
#include <rga/rga.h>

#include <vector>
#include <cstdint>
#include <string>

// V4L2 types needed for the init_v4l2_mmap declaration
#include <linux/videodev2.h>

namespace ct {

// ─── CameraHalImx219 ──────────────────────────────────────────────────────────
// ICameraHAL implementation for the Sony IMX219 sensor (8MP, 3280×2464 max).
//
// Uses V4L2 directly on /dev/video0 (ISP mainpath on Armbian for RK3566)
// for frame capture, librga/im2d for hardware-accelerated scaling to
// produce the three resolution streams, and Linux dma-heap for buffer
// allocation.
//
// Resolution streams:
//   full   — cfg.full_w × cfg.full_h  (NV12, for JPEG crop extraction)
//   medium — cfg.med_w  × cfg.med_h   (NV12, for MJPEG preview stream)
//   lores  — cfg.lores_w × cfg.lores_h (NV12, for YOLO inference)
//
// Sensor-specific defaults:
//   - V4L2 device: /dev/video0
//   - Max resolution: 3280×2464 (but typically used at 1920×1080 or 1640×1232)
//   - IQ dir: /usr/share/iqfiles/imx219
//   - AIQ sensor name: "imx219" (auto-detected by AIQ)
struct CameraHalImx219 : ICameraHAL {
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

private:
    bool init_aiq(const std::string& iq_dir);
    bool init_v4l2();
    bool init_v4l2_mmap(const struct ::v4l2_format& fmt, __u32 frame_size);
    bool init_scaler_buffers();

    void acquire_frames_impl();
    void release_frames_impl();

    // Allocate a dma-buf fd of the given size from /dev/dma_heap/system
    static int alloc_dma_buf(uint32_t size);
    // Map a dma-buf fd into userspace
    static void* map_dma_buf(int fd, uint32_t size);
    // Unmap and close a dma-buf
    static void unmap_dma_buf(void* addr, uint32_t size, int fd);

    // Scale one NV12 frame to another using librga/im2d
    bool scale_nv12(const FrameBuffer& src, FrameBuffer& dst,
                    int dst_w, int dst_h);

    PipelineConfig cfg_{};

    // V4L2 capture state (DMABUF preferred, MMAP fallback)
    struct V4l2BufSlot {
        unsigned int index;
        void*        mapped;   // virtual address (mmap'd)
        uint32_t     length;   // buffer length
        bool         queued;   // true if the buffer is queued in V4L2
        int          dma_fd;   // DMABUF fd (-1 if MMAP fallback)
    };
    int v4l2_fd_ = -1;
    std::vector<V4l2BufSlot> v4l2_buf_pool_;
    bool use_dmabuf_ = false;  // true if DMABUF path initialised

    // Scaler output buffers (one per resolution stream)
    struct ScalerBuf {
        void*    addr = nullptr;
        int      fd   = -1;
        uint32_t size = 0;
    };
    ScalerBuf scaler_full_;
    ScalerBuf scaler_medium_;
    ScalerBuf scaler_lores_;

    // Per-tick frame buffers (populated by acquire_frames, released by release_frames)
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};

    bool initialised_ = false;

    rk_aiq_sys_ctx_t* aiq_ctx_ = nullptr;
};

} // namespace ct
