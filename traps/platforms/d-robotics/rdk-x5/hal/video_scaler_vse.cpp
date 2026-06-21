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

#include "video_scaler_vse.hpp"

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace ct {

// ─── Constructor / Destructor ─────────────────────────────────────────────────
VideoScalerVSE::VideoScalerVSE() = default;

VideoScalerVSE::~VideoScalerVSE() {
    if (vse_fd_ >= 0) {
        close(vse_fd_);
        vse_fd_ = -1;
    }
}

// ─── scale_nv12 ───────────────────────────────────────────────────────────────
bool VideoScalerVSE::scale_nv12(int src_fd, int src_w, int src_h,
                                int dst_fd, int dst_w, int dst_h) {
    // TODO: Implement VSE-based NV12 scaling.
    //
    // On the RDK X5, the VSE is accessed via V4L2 M2M (Memory-to-Memory)
    // device nodes. The typical workflow is:
    //
    //   1. Open the VSE M2M device (e.g., /dev/videoX)
    //   2. Set the output format (source: src_w x src_h, NV12)
    //   3. Set the capture format (destination: dst_w x dst_h, NV12)
    //   4. Queue the source and destination DMA-BUFs
    //   5. Call VIDIOC_STREAMON on both queues
    //   6. Call VIDIOC_DQBUF to wait for completion
    //
    //   Reference:
    //     v4l2-ctl -d /dev/video4 --set-fmt-video=width=640,height=480,pixelformat=NV12
    //
    // NOTE: Preparatory stub — not yet implemented.
    //       The VSE is typically configured once at camera init and runs
    //       continuously in hardware. This standalone scaler is only needed
    //       for ad-hoc scaling operations outside the main pipeline.
    //
    (void)src_fd;
    (void)src_w;
    (void)src_h;
    (void)dst_fd;
    (void)dst_w;
    (void)dst_h;

    std::cerr << "[VideoScalerVSE] scale_nv12: NOT IMPLEMENTED (preparatory stub)\n";
    std::cerr << "[VideoScalerVSE] VSE scaling is handled by CameraHalRdkX5 "
              << "via the VIN→ISP→VSE pipeline.\n";
    return false;
}

// ─── scale_nv12_to_bgr ────────────────────────────────────────────────────────
bool VideoScalerVSE::scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                                       int dst_fd, int dst_w, int dst_h) {
    // TODO: Implement VSE-based NV12→BGR conversion + resize.
    //
    // The VSE supports hardware color space conversion, so this could
    // potentially be done in hardware. However, the BPU inference HAL
    // may accept NV12 directly, making this conversion unnecessary.
    //
    // NOTE: Preparatory stub — not yet implemented.
    (void)src_fd;
    (void)src_w;
    (void)src_h;
    (void)dst_fd;
    (void)dst_w;
    (void)dst_h;

    std::cerr << "[VideoScalerVSE] scale_nv12_to_bgr: NOT IMPLEMENTED "
              << "(preparatory stub)\n";
    return false;
}

} // namespace ct
