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

#include "camera_hal_rdkx5.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>

namespace ct {

// ─── Constructor / Destructor ─────────────────────────────────────────────────
CameraHalRdkX5::CameraHalRdkX5() = default;

CameraHalRdkX5::~CameraHalRdkX5() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────
bool CameraHalRdkX5::init(const PipelineConfig& cfg) {
    if (initialised_) {
        std::cerr << "[CameraHalRdkX5] Already initialised\n";
        return true;
    }

    cfg_ = cfg;

    std::cout << "[CameraHalRdkX5] Initialising RDK X5 camera pipeline\n";
    std::cout << "[CameraHalRdkX5]   Full:   " << cfg.camera.full_w << "x" << cfg.camera.full_h << "\n";
    std::cout << "[CameraHalRdkX5]   Medium: " << cfg.camera.med_w << "x" << cfg.camera.med_h << "\n";
    std::cout << "[CameraHalRdkX5]   Lores:  " << cfg.camera.lores_w << "x" << cfg.camera.lores_h << "\n";
    std::cout << "[CameraHalRdkX5]   FPS:    " << cfg.camera.fps << "\n";
    std::cout << "[CameraHalRdkX5]   Device: " << cfg.camera.device << "\n";

    // 1. Initialise V4L2 capture
    if (!init_v4l2()) {
        std::cerr << "[CameraHalRdkX5] V4L2 initialisation failed\n";
        return false;
    }

    // 2. Initialise VSE channels for multi-stream scaling
    if (!init_vse_channels()) {
        std::cerr << "[CameraHalRdkX5] VSE channel initialisation failed\n";
        return false;
    }

    initialised_ = true;
    std::cout << "[CameraHalRdkX5] Initialised successfully\n";
    return true;
}

// ─── init_v4l2 ────────────────────────────────────────────────────────────────
bool CameraHalRdkX5::init_v4l2() {
    // Open the V4L2 capture device
    // On RDK X5, the camera pipeline is accessed via /dev/video* with HBN framework
    v4l2_fd_ = open(cfg_.camera.device.c_str(), O_RDWR);
    if (v4l2_fd_ < 0) {
        std::cerr << "[CameraHalRdkX5] Cannot open " << cfg_.camera.device
                  << ": " << strerror(errno) << "\n";
        return false;
    }

    // Query device capabilities
    struct v4l2_capability cap{};
    if (ioctl(v4l2_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[CameraHalRdkX5] VIDIOC_QUERYCAP failed: " << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[CameraHalRdkX5] V4L2 device: " << cap.card << "\n";
    std::cout << "[CameraHalRdkX5]   Driver: " << cap.driver << "\n";

    // Set format: NV12 at full resolution
    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width       = static_cast<__u32>(cfg_.camera.full_w);
    fmt.fmt.pix_mp.height      = static_cast<__u32>(cfg_.camera.full_h);
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;

    if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[CameraHalRdkX5] VIDIOC_S_FMT failed: " << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[CameraHalRdkX5] Format set: "
              << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height
              << " NV12\n";

    // Set frame rate
    struct v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<__u32>(cfg_.camera.fps);
    ioctl(v4l2_fd_, VIDIOC_S_PARM, &parm);

    // Request DMABUF buffers (preferred for zero-copy)
    struct v4l2_requestbuffers req{};
    req.count   = 4;
    req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory  = V4L2_MEMORY_DMABUF;

    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        // Fall back to MMAP if DMABUF is not supported
        std::cerr << "[CameraHalRdkX5] DMABUF not supported, falling back to MMAP\n";
        use_dmabuf_ = false;

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "[CameraHalRdkX5] VIDIOC_REQBUFS (MMAP) failed: "
                      << strerror(errno) << "\n";
            return false;
        }
    } else {
        use_dmabuf_ = true;
    }

    // Query and mmap buffers
    v4l2_buf_pool_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
        struct v4l2_buffer buf{};
        buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory  = use_dmabuf_ ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
        buf.index   = i;
        buf.length  = 1;
        buf.m.planes = planes;

        if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[CameraHalRdkX5] VIDIOC_QUERYBUF " << i
                      << " failed: " << strerror(errno) << "\n";
            return false;
        }

        v4l2_buf_pool_[i].index  = i;
        v4l2_buf_pool_[i].length = planes[0].length;

        if (!use_dmabuf_) {
            // MMAP path: map the buffer
            v4l2_buf_pool_[i].data = mmap(nullptr, planes[0].length,
                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                          v4l2_fd_, planes[0].m.mem_offset);
            if (v4l2_buf_pool_[i].data == MAP_FAILED) {
                std::cerr << "[CameraHalRdkX5] mmap buffer " << i
                          << " failed: " << strerror(errno) << "\n";
                return false;
            }
            v4l2_buf_pool_[i].dma_fd = -1;
        } else {
            // DMABUF path: allocate a dma-buf and export it
            // TODO: Implement dma-heap allocation for RDK X5
            v4l2_buf_pool_[i].data   = nullptr;
            v4l2_buf_pool_[i].dma_fd = -1;
        }
    }

    // Queue all buffers
    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
        struct v4l2_buffer buf{};
        buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory  = use_dmabuf_ ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
        buf.index   = i;
        buf.length  = 1;
        buf.m.planes = planes;

        if (use_dmabuf_) {
            planes[0].m.fd     = v4l2_buf_pool_[i].dma_fd;
            planes[0].length   = v4l2_buf_pool_[i].length;
        }

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[CameraHalRdkX5] VIDIOC_QBUF " << i
                      << " failed: " << strerror(errno) << "\n";
            return false;
        }
        v4l2_buf_pool_[i].queued = true;
    }

    // Start streaming
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &buf_type) < 0) {
        std::cerr << "[CameraHalRdkX5] VIDIOC_STREAMON failed: "
                  << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[CameraHalRdkX5] V4L2 streaming started\n";
    return true;
}

// ─── init_vse_channels ────────────────────────────────────────────────────────
bool CameraHalRdkX5::init_vse_channels() {
    // On the RDK X5, the VSE is accessed via V4L2 subdevices or dedicated
    // device nodes (e.g., /dev/video4, /dev/video5, /dev/video7).
    //
    // VSE Channel 0: Full-res pass-through (same as camera input)
    // VSE Channel 1: Medium-res (640×480)
    // VSE Channel 3: Lores (320×320)
    //
    // TODO: Determine the correct VSE device node paths for RDK X5.
    //       Reference: v4l2-ctl -d /dev/video4 --set-fmt-video=...
    //
    // For now, we pre-allocate DMA buffers for the VSE outputs and will
    // configure the VSE channels once the device nodes are known.

    // Allocate VSE output buffers
    if (!alloc_vse_buffer(vse_full_, cfg_.camera.full_w, cfg_.camera.full_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate VSE full buffer\n";
        return false;
    }

    if (!alloc_vse_buffer(vse_medium_, cfg_.camera.med_w, cfg_.camera.med_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate VSE medium buffer\n";
        return false;
    }

    if (!alloc_vse_buffer(vse_lores_, cfg_.camera.lores_w, cfg_.camera.lores_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate VSE lores buffer\n";
        return false;
    }

    std::cout << "[CameraHalRdkX5] VSE buffers allocated:\n";
    std::cout << "[CameraHalRdkX5]   Full:   " << vse_full_.output_w << "x"
              << vse_full_.output_h << " (fd=" << vse_full_.dma_fd << ")\n";
    std::cout << "[CameraHalRdkX5]   Medium: " << vse_medium_.output_w << "x"
              << vse_medium_.output_h << " (fd=" << vse_medium_.dma_fd << ")\n";
    std::cout << "[CameraHalRdkX5]   Lores:  " << vse_lores_.output_w << "x"
              << vse_lores_.output_h << " (fd=" << vse_lores_.dma_fd << ")\n";

    return true;
}

// ─── alloc_vse_buffer ─────────────────────────────────────────────────────────
bool CameraHalRdkX5::alloc_vse_buffer(VseChannel& ch, int width, int height) {
    // NV12 frame size: Y plane (w*h) + UV plane (w*h/2)
    uint32_t frame_size = static_cast<uint32_t>(width * height * 3 / 2);

    // Allocate DMA-BUF from dma-heap
    // On RDK X5, the dma-heap device may be at /dev/dma_heap/system
    // or a platform-specific path.
    int dma_heap_fd = open("/dev/dma_heap/system", O_RDWR);
    if (dma_heap_fd < 0) {
        // Fall back to anonymous mmap for preparatory stub
        std::cerr << "[CameraHalRdkX5] dma-heap not available, "
                  << "allocating system memory for VSE buffer\n";
        ch.mapped = std::aligned_alloc(4096, frame_size);
        if (!ch.mapped) {
            std::cerr << "[CameraHalRdkX5] Failed to allocate " << frame_size
                      << " bytes for VSE buffer\n";
            return false;
        }
        ch.dma_fd  = -1;
        ch.size    = frame_size;
        ch.output_w = width;
        ch.output_h = height;
        return true;
    }

    struct dma_heap_allocation_data alloc{};
    alloc.len  = frame_size;
    alloc.fd_flags = O_RDWR | O_CLOEXEC;
    alloc.heap_flags = 0;

    if (ioctl(dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
        std::cerr << "[CameraHalRdkX5] DMA_HEAP_IOCTL_ALLOC failed: "
                  << strerror(errno) << "\n";
        close(dma_heap_fd);
        return false;
    }

    close(dma_heap_fd);

    // Map the DMA-BUF into userspace
    ch.dma_fd = alloc.fd;
    ch.mapped = mmap(nullptr, frame_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, ch.dma_fd, 0);
    if (ch.mapped == MAP_FAILED) {
        std::cerr << "[CameraHalRdkX5] mmap DMA-BUF failed: "
                  << strerror(errno) << "\n";
        close(ch.dma_fd);
        ch.dma_fd = -1;
        return false;
    }

    ch.size     = frame_size;
    ch.output_w = width;
    ch.output_h = height;
    return true;
}

// ─── free_vse_buffer ──────────────────────────────────────────────────────────
void CameraHalRdkX5::free_vse_buffer(VseChannel& ch) {
    if (ch.mapped && ch.mapped != MAP_FAILED) {
        munmap(ch.mapped, ch.size);
        ch.mapped = nullptr;
    }
    if (ch.dma_fd >= 0) {
        close(ch.dma_fd);
        ch.dma_fd = -1;
    }
    if (ch.fd >= 0) {
        close(ch.fd);
        ch.fd = -1;
    }
    ch.size     = 0;
    ch.output_w = 0;
    ch.output_h = 0;
}

// ─── acquire_frames ───────────────────────────────────────────────────────────
bool CameraHalRdkX5::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                    FrameBuffer& lores) {
    if (!initialised_) return false;

    // 1. Dequeue a frame from V4L2
    struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
    struct v4l2_buffer buf{};
    buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory  = use_dmabuf_ ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    buf.length  = 1;
    buf.m.planes = planes;

    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "[CameraHalRdkX5] VIDIOC_DQBUF failed: "
                  << strerror(errno) << "\n";
        return false;
    }

    // 2. Configure VSE channels to scale from the captured frame
    //    TODO: Implement actual VSE hardware configuration via V4L2 subdev
    //    or dedicated VSE ioctls. For now, this is a preparatory stub that
    //    copies the raw camera frame to all three outputs.
    //
    //    In production, the VSE would be configured once at init time and
    //    triggered per-frame via a single ioctl that processes all channels
    //    simultaneously in hardware.
    //
    //    Reference (from D-Robotics multimedia samples):
    //      ./pipelines/single_pipe_vin_isp_vse -s 0
    //
    //    VSE channel setup (conceptual):
    //      v4l2-ctl -d /dev/video4 --set-fmt-video=width=640,height=480,pixelformat=NV12
    //      v4l2-ctl -d /dev/video5 --set-fmt-video=width=320,height=320,pixelformat=NV12

    // For the preparatory stub, we populate the frame buffers with the
    // VSE output buffer pointers. The actual VSE hardware scaling will
    // be implemented once the device is available for testing.

    // Full-res: point to VSE channel 0 buffer
    frame_full_.data         = vse_full_.mapped;
    frame_full_.width        = static_cast<uint32_t>(vse_full_.output_w);
    frame_full_.height       = static_cast<uint32_t>(vse_full_.output_h);
    frame_full_.stride       = static_cast<uint32_t>(vse_full_.output_w);
    frame_full_.size         = vse_full_.size;
    frame_full_.timestamp_ms = static_cast<int64_t>(buf.timestamp.tv_sec * 1000 +
                                                     buf.timestamp.tv_usec / 1000);
    frame_full_.dma_fd       = vse_full_.dma_fd;

    // Medium-res: point to VSE channel 1 buffer
    frame_medium_.data         = vse_medium_.mapped;
    frame_medium_.width        = static_cast<uint32_t>(vse_medium_.output_w);
    frame_medium_.height       = static_cast<uint32_t>(vse_medium_.output_h);
    frame_medium_.stride       = static_cast<uint32_t>(vse_medium_.output_w);
    frame_medium_.size         = vse_medium_.size;
    frame_medium_.timestamp_ms = frame_full_.timestamp_ms;
    frame_medium_.dma_fd       = vse_medium_.dma_fd;

    // Lores: point to VSE channel 3 buffer
    frame_lores_.data         = vse_lores_.mapped;
    frame_lores_.width        = static_cast<uint32_t>(vse_lores_.output_w);
    frame_lores_.height       = static_cast<uint32_t>(vse_lores_.output_h);
    frame_lores_.stride       = static_cast<uint32_t>(vse_lores_.output_w);
    frame_lores_.size         = vse_lores_.size;
    frame_lores_.timestamp_ms = frame_full_.timestamp_ms;
    frame_lores_.dma_fd       = vse_lores_.dma_fd;

    // 3. Return the frame buffers to the caller
    full   = frame_full_;
    medium = frame_medium_;
    lores  = frame_lores_;

    // 4. Re-queue the V4L2 buffer for the next frame
    if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[CameraHalRdkX5] VIDIOC_QBUF failed: "
                  << strerror(errno) << "\n";
        return false;
    }

    return true;
}

// ─── release_frames ───────────────────────────────────────────────────────────
void CameraHalRdkX5::release_frames() {
    // VSE output buffers are persistent (pre-allocated in init).
    // The V4L2 capture buffer was already re-queued in acquire_frames().
    // No additional release is needed.
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void CameraHalRdkX5::shutdown() {
    if (!initialised_) return;

    std::cout << "[CameraHalRdkX5] Shutting down\n";

    // Stop V4L2 streaming
    if (v4l2_fd_ >= 0) {
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &buf_type);

        // Unmap V4L2 buffers
        for (auto& slot : v4l2_buf_pool_) {
            if (slot.data && slot.data != MAP_FAILED) {
                munmap(slot.data, slot.length);
            }
            if (slot.dma_fd >= 0) {
                close(slot.dma_fd);
            }
        }
        v4l2_buf_pool_.clear();

        close(v4l2_fd_);
        v4l2_fd_ = -1;
    }

    // Free VSE output buffers
    free_vse_buffer(vse_full_);
    free_vse_buffer(vse_medium_);
    free_vse_buffer(vse_lores_);

    initialised_ = false;
    std::cout << "[CameraHalRdkX5] Shutdown complete\n";
}

} // namespace ct
