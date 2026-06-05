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

#include "camera_hal_a7s.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>

// V4L2 headers
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <linux/videodev2.h>
#include <errno.h>

namespace ct {

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Return a human-readable name for a V4L2 pixel format.
static const char* v4l2_fmt_name(uint32_t fourcc) {
    static char buf[5];
    buf[0] = (fourcc >>  0) & 0xFF;
    buf[1] = (fourcc >>  8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
    return buf;
}

/// Try to open a V4L2 video device. Returns fd or -1.
/// Accepts both single-planar and multiplanar capture devices.
static int try_open_v4l2(const char* device) {
    int fd = ::open(device, O_RDWR, 0);
    if (fd < 0) return -1;

    struct v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        ::close(fd);
        return -1;
    }

    // Must be a video capture device (single or multi-planar)
    if (!(cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
        ::close(fd);
        return -1;
    }

    std::cout << "[CameraHalA7s] Opened " << device
              << ": " << reinterpret_cast<const char*>(cap.card)
              << " (driver: " << reinterpret_cast<const char*>(cap.driver) << ")\n";
    return fd;
}

/// Determine if the device uses multiplanar API based on capabilities.
static bool is_mplane(int fd) {
    struct v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) return false;
    return (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
}

/// Return the appropriate buffer type for capture (single or multi-planar).
static uint32_t capture_buf_type(bool mplane) {
    return mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
CameraHalA7s::CameraHalA7s() = default;

CameraHalA7s::~CameraHalA7s() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────
bool CameraHalA7s::init(const PipelineConfig& cfg) {
    if (initialised_) {
        std::cerr << "[CameraHalA7s] Already initialised\n";
        return true;
    }

    cfg_ = cfg;

    // 1. Open V4L2 device
    if (!init_v4l2()) {
        std::cerr << "[CameraHalA7s] Failed to initialise V4L2\n";
        return false;
    }

    // 2. Pre-allocate scaler output buffers
    uint32_t med_size  = static_cast<uint32_t>(cfg.camera.med_w * cfg.camera.med_h * 3 / 2);
    uint32_t lores_size = static_cast<uint32_t>(cfg.camera.lores_w * cfg.camera.lores_h * 3 / 2);

    scaler_medium_.addr = std::malloc(med_size);
    scaler_medium_.size = med_size;
    scaler_lores_.addr  = std::malloc(lores_size);
    scaler_lores_.size  = lores_size;

    if (!scaler_medium_.addr || !scaler_lores_.addr) {
        std::cerr << "[CameraHalA7s] Failed to allocate scaler buffers\n";
        shutdown();
        return false;
    }

    initialised_ = true;
    std::cout << "[CameraHalA7s] Initialised successfully ("
              << cfg.camera.full_w << "x" << cfg.camera.full_h << " NV12 via V4L2)\n";
    return true;
}

// ─── init_v4l2 ────────────────────────────────────────────────────────────────
bool CameraHalA7s::init_v4l2() {
    // Try the configured device path first, then common alternatives
    const char* device = cfg_.camera.device.c_str();
    v4l2_fd_ = try_open_v4l2(device);
    if (v4l2_fd_ < 0) {
        // Fallback: try /dev/video0 through /dev/video3
        for (int i = 0; i <= 3; ++i) {
            char alt_dev[32];
            snprintf(alt_dev, sizeof(alt_dev), "/dev/video%d", i);
            v4l2_fd_ = try_open_v4l2(alt_dev);
            if (v4l2_fd_ >= 0) break;
        }
    }

    if (v4l2_fd_ < 0) {
        std::cerr << "[CameraHalA7s] No V4L2 capture device found\n";
        return false;
    }

    // Detect if multiplanar API is required
    mplane_ = is_mplane(v4l2_fd_);
    buf_type_ = capture_buf_type(mplane_);

    std::cout << "[CameraHalA7s] Using " << (mplane_ ? "multiplanar" : "single-planar")
              << " V4L2 API\n";

    // 2. Set format: NV12 at configured resolution
    if (mplane_) {
        struct v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width       = static_cast<__u32>(cfg_.camera.full_w);
        fmt.fmt.pix_mp.height      = static_cast<__u32>(cfg_.camera.full_h);
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes  = 1;

        if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "[CameraHalA7s] VIDIOC_S_FMT (MPLANE) failed: "
                      << strerror(errno) << "\n";
            return false;
        }

        std::cout << "[CameraHalA7s] Format set: " << fmt.fmt.pix_mp.width << "x"
                  << fmt.fmt.pix_mp.height << " "
                  << v4l2_fmt_name(fmt.fmt.pix_mp.pixelformat)
                  << " stride=" << fmt.fmt.pix_mp.plane_fmt[0].bytesperline << "\n";

        if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12) {
            std::cerr << "[CameraHalA7s] Driver changed pixel format to "
                      << v4l2_fmt_name(fmt.fmt.pix_mp.pixelformat)
                      << ", expected NV12\n";
            return false;
        }

        // Store the actual bytesperline from the driver
        v4l2_stride_ = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        struct v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = static_cast<__u32>(cfg_.camera.full_w);
        fmt.fmt.pix.height      = static_cast<__u32>(cfg_.camera.full_h);
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "[CameraHalA7s] VIDIOC_S_FMT failed: "
                      << strerror(errno) << "\n";
            return false;
        }

        std::cout << "[CameraHalA7s] Format set: " << fmt.fmt.pix.width << "x"
                  << fmt.fmt.pix.height << " "
                  << v4l2_fmt_name(fmt.fmt.pix.pixelformat)
                  << " stride=" << fmt.fmt.pix.bytesperline << "\n";

        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12) {
            std::cerr << "[CameraHalA7s] Driver changed pixel format to "
                      << v4l2_fmt_name(fmt.fmt.pix.pixelformat)
                      << ", expected NV12\n";
            return false;
        }

        v4l2_stride_ = fmt.fmt.pix.bytesperline;
    }

    // 3. Set framerate
    struct v4l2_streamparm parm{};
    parm.type = buf_type_;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<__u32>(cfg_.camera.fps);
    if (ioctl(v4l2_fd_, VIDIOC_S_PARM, &parm) < 0) {
        std::cerr << "[CameraHalA7s] VIDIOC_S_PARM (fps) failed: " << strerror(errno) << "\n";
        // Non-fatal — driver may not support framerate control
    }

    // 4. Request buffers and MMAP
    uint32_t frame_size = cfg_.camera.full_w * cfg_.camera.full_h * 3 / 2;
    if (!init_v4l2_mmap(frame_size)) {
        return false;
    }

    // 5. Set ISP controls
    if (!set_v4l2_controls()) {
        std::cerr << "[CameraHalA7s] Failed to set V4L2 controls (non-fatal)\n";
        // Continue anyway — controls may not be supported by all drivers
    }

    return true;
}

// ─── init_v4l2_mmap ───────────────────────────────────────────────────────────
bool CameraHalA7s::init_v4l2_mmap(uint32_t frame_size) {
    struct v4l2_requestbuffers req{};
    req.count  = 4;  // Double-buffered + 2 for safety
    req.type   = buf_type_;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[CameraHalA7s] VIDIOC_REQBUFS failed: " << strerror(errno) << "\n";
        return false;
    }

    if (req.count < 2) {
        std::cerr << "[CameraHalA7s] Insufficient buffers (" << req.count << ")\n";
        return false;
    }

    v4l2_buf_pool_.resize(req.count);

    for (unsigned int i = 0; i < req.count; ++i) {
        if (mplane_) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "[CameraHalA7s] VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            v4l2_buf_pool_[i].index  = i;
            v4l2_buf_pool_[i].length = plane.length;
            v4l2_buf_pool_[i].mapped = mmap(nullptr, plane.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            v4l2_fd_, plane.m.mem_offset);
            if (v4l2_buf_pool_[i].mapped == MAP_FAILED) {
                std::cerr << "[CameraHalA7s] mmap[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            std::cout << "[CameraHalA7s] Buffer " << i << ": "
                      << plane.length << " bytes @ " << v4l2_buf_pool_[i].mapped << "\n";
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "[CameraHalA7s] VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            v4l2_buf_pool_[i].index  = i;
            v4l2_buf_pool_[i].length = buf.length;
            v4l2_buf_pool_[i].mapped = mmap(nullptr, buf.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            v4l2_fd_, buf.m.offset);
            if (v4l2_buf_pool_[i].mapped == MAP_FAILED) {
                std::cerr << "[CameraHalA7s] mmap[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            std::cout << "[CameraHalA7s] Buffer " << i << ": "
                      << buf.length << " bytes @ " << v4l2_buf_pool_[i].mapped << "\n";
        }
    }

    // Pre-queue all buffers
    for (auto& slot : v4l2_buf_pool_) {
        if (mplane_) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory  = V4L2_MEMORY_MMAP;
            buf.index   = slot.index;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "[CameraHalA7s] VIDIOC_QBUF[" << slot.index << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = slot.index;

            if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "[CameraHalA7s] VIDIOC_QBUF[" << slot.index << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        }
        slot.queued = true;
    }

    // Start streaming
    int type = static_cast<int>(buf_type_);
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[CameraHalA7s] VIDIOC_STREAMON failed: " << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[CameraHalA7s] Streaming started (" << req.count << " MMAP buffers)\n";
    return true;
}

// ─── set_v4l2_controls ────────────────────────────────────────────────────────
bool CameraHalA7s::set_v4l2_controls() {
    // Set exposure time (in 100µs units for V4L2)
    // V4L2_CID_EXPOSURE_ABSOLUTE is in 100µs increments
    struct v4l2_control ctrl{};

    // Exposure: default 20ms = 200 * 100µs
    ctrl.id    = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = 200;  // 200 × 100µs = 20ms
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalA7s] V4L2_CID_EXPOSURE_ABSOLUTE not supported: "
                  << strerror(errno) << "\n";
    }

    // Auto exposure (enable AE)
    ctrl.id    = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_AUTO;
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalA7s] V4L2_CID_EXPOSURE_AUTO not supported: "
                  << strerror(errno) << "\n";
    }

    // Analogue gain (1.0 = unity, range depends on sensor)
    // V4L2_CID_ANALOGUE_GAIN is in driver-specific units
    ctrl.id    = V4L2_CID_ANALOGUE_GAIN;
    ctrl.value = 10;  // Low gain
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalA7s] V4L2_CID_ANALOGUE_GAIN not supported: "
                  << strerror(errno) << "\n";
    }

    // Auto white balance
    ctrl.id    = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = 1;  // Enable
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalA7s] V4L2_CID_AUTO_WHITE_BALANCE not supported: "
                  << strerror(errno) << "\n";
    }

    return true;
}

// ─── acquire_frames ───────────────────────────────────────────────────────────
bool CameraHalA7s::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                  FrameBuffer& lores) {
    if (!initialised_) return false;

    // 1. Dequeue a frame (blocking, with poll timeout)
    struct v4l2_buffer buf{};
    struct v4l2_plane plane{};
    buf.type    = buf_type_;
    buf.memory  = V4L2_MEMORY_MMAP;
    if (mplane_) {
        buf.m.planes = &plane;
        buf.length   = 1;
    }

    // Use poll() to wait for data with timeout
    struct pollfd pfd;
    pfd.fd     = v4l2_fd_;
    pfd.events = POLLIN;

    int poll_ret = poll(&pfd, 1, 3000);  // 3 second timeout
    if (poll_ret <= 0) {
        if (poll_ret == 0) {
            std::cerr << "[CameraHalA7s] Frame capture timeout\n";
        } else {
            std::cerr << "[CameraHalA7s] poll() error: " << strerror(errno) << "\n";
        }
        return false;
    }

    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "[CameraHalA7s] VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
        return false;
    }

    // Find the buffer slot
    V4l2BufSlot& slot = v4l2_buf_pool_[buf.index];
    slot.queued = false;

    // 2. Get timestamp
    int64_t timestamp_ms = 0;
    if (buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
        timestamp_ms = static_cast<int64_t>(buf.timestamp.tv_sec) * 1000
                     + static_cast<int64_t>(buf.timestamp.tv_usec) / 1000;
    } else {
        timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 3. Populate full frame buffer (points directly to MMAP buffer)
    uint32_t bytesused = mplane_ ? plane.bytesused : buf.bytesused;
    uint32_t buf_length = mplane_ ? plane.length : buf.length;

    frame_full_.data         = static_cast<uint8_t*>(slot.mapped);
    frame_full_.width        = static_cast<uint32_t>(cfg_.camera.full_w);
    frame_full_.height       = static_cast<uint32_t>(cfg_.camera.full_h);
    frame_full_.stride       = v4l2_stride_ > 0 ? v4l2_stride_ : static_cast<uint32_t>(cfg_.camera.full_w);
    frame_full_.size         = bytesused > 0 ? bytesused : buf_length;
    frame_full_.timestamp_ms = timestamp_ms;
    frame_full_.dma_fd       = -1;

    // 4. Scale to medium and lores using CPU bilinear scaler
    const uint8_t* src = static_cast<const uint8_t*>(slot.mapped);

    // Medium
    scale_nv12_cpu(src,
                   static_cast<uint8_t*>(scaler_medium_.addr),
                   cfg_.camera.full_w, cfg_.camera.full_h,
                   cfg_.camera.med_w, cfg_.camera.med_h);

    frame_medium_.data         = static_cast<uint8_t*>(scaler_medium_.addr);
    frame_medium_.width        = static_cast<uint32_t>(cfg_.camera.med_w);
    frame_medium_.height       = static_cast<uint32_t>(cfg_.camera.med_h);
    frame_medium_.stride       = static_cast<uint32_t>(cfg_.camera.med_w);
    frame_medium_.size         = scaler_medium_.size;
    frame_medium_.timestamp_ms = timestamp_ms;
    frame_medium_.dma_fd       = -1;

    // Lores
    scale_nv12_cpu(src,
                   static_cast<uint8_t*>(scaler_lores_.addr),
                   cfg_.camera.full_w, cfg_.camera.full_h,
                   cfg_.camera.lores_w, cfg_.camera.lores_h);

    frame_lores_.data         = static_cast<uint8_t*>(scaler_lores_.addr);
    frame_lores_.width        = static_cast<uint32_t>(cfg_.camera.lores_w);
    frame_lores_.height       = static_cast<uint32_t>(cfg_.camera.lores_h);
    frame_lores_.stride       = static_cast<uint32_t>(cfg_.camera.lores_w);
    frame_lores_.size         = scaler_lores_.size;
    frame_lores_.timestamp_ms = timestamp_ms;
    frame_lores_.dma_fd       = -1;

    // 5. Re-queue the buffer for the next frame
    if (mplane_) {
        struct v4l2_buffer qbuf{};
        struct v4l2_plane qplane{};
        qbuf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory  = V4L2_MEMORY_MMAP;
        qbuf.index   = buf.index;
        qbuf.m.planes = &qplane;
        qbuf.length   = 1;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalA7s] VIDIOC_QBUF (re-queue) failed: "
                      << strerror(errno) << "\n";
            return false;
        }
    } else {
        struct v4l2_buffer qbuf{};
        qbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index  = buf.index;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalA7s] VIDIOC_QBUF (re-queue) failed: "
                      << strerror(errno) << "\n";
            return false;
        }
    }
    slot.queued = true;

    // 6. Assign output references
    full   = frame_full_;
    medium = frame_medium_;
    lores  = frame_lores_;

    return true;
}

// ─── release_frames ───────────────────────────────────────────────────────────
void CameraHalA7s::release_frames() {
    // No-op: V4L2 MMAP buffers are managed by the kernel.
    // The scaler output buffers persist until the next acquire_frames() call.
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void CameraHalA7s::shutdown() {
    if (!initialised_ && v4l2_fd_ < 0) return;

    // Stop streaming
    if (v4l2_fd_ >= 0) {
        int type = static_cast<int>(buf_type_);
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
    }

    // Unmap V4L2 buffers
    for (auto& slot : v4l2_buf_pool_) {
        if (slot.mapped && slot.mapped != MAP_FAILED) {
            munmap(slot.mapped, slot.length);
        }
    }
    v4l2_buf_pool_.clear();

    // Close V4L2 device
    if (v4l2_fd_ >= 0) {
        ::close(v4l2_fd_);
        v4l2_fd_ = -1;
    }

    // Free scaler buffers
    if (scaler_medium_.addr) {
        std::free(scaler_medium_.addr);
        scaler_medium_.addr = nullptr;
        scaler_medium_.size = 0;
    }
    if (scaler_lores_.addr) {
        std::free(scaler_lores_.addr);
        scaler_lores_.addr = nullptr;
        scaler_lores_.size = 0;
    }

    initialised_ = false;
    std::cout << "[CameraHalA7s] Shutdown complete\n";
}

// ─── scale_nv12_cpu (static) ──────────────────────────────────────────────────
// CPU-based bilinear scaler for NV12 frames.
// Scales the Y plane (luma) and UV plane (chroma) separately.
void CameraHalA7s::scale_nv12_cpu(const uint8_t* src, uint8_t* dst,
                                  int src_w, int src_h,
                                  int dst_w, int dst_h) {
    // ── Scale Y plane (luma) ──────────────────────────────────────────────────
    float scale_x = static_cast<float>(src_w) / static_cast<float>(dst_w);
    float scale_y = static_cast<float>(src_h) / static_cast<float>(dst_h);

    for (int dy = 0; dy < dst_h; ++dy) {
        float src_y_f = dy * scale_y;
        int sy0 = static_cast<int>(src_y_f);
        int sy1 = std::min(sy0 + 1, src_h - 1);
        float fy = src_y_f - sy0;

        for (int dx = 0; dx < dst_w; ++dx) {
            float src_x_f = dx * scale_x;
            int sx0 = static_cast<int>(src_x_f);
            int sx1 = std::min(sx0 + 1, src_w - 1);
            float fx = src_x_f - sx0;

            // Bilinear interpolation
            float top = (1.0f - fx) * src[sy0 * src_w + sx0]
                      + fx * src[sy0 * src_w + sx1];
            float bot = (1.0f - fx) * src[sy1 * src_w + sx0]
                      + fx * src[sy1 * src_w + sx1];
            float val = (1.0f - fy) * top + fy * bot;

            dst[dy * dst_w + dx] = static_cast<uint8_t>(val);
        }
    }

    // ── Scale UV plane (chroma) ───────────────────────────────────────────────
    // NV12 chroma is interleaved UV (2 bytes per pixel), half resolution.
    int src_cw = src_w / 2;
    int src_ch = src_h / 2;
    int dst_cw = dst_w / 2;
    int dst_ch = dst_h / 2;

    float scale_cx = static_cast<float>(src_cw) / static_cast<float>(dst_cw);
    float scale_cy = static_cast<float>(src_ch) / static_cast<float>(dst_ch);

    const uint8_t* src_uv = src + src_w * src_h;  // NV12: UV plane starts after Y
    uint8_t* dst_uv = dst + dst_w * dst_h;

    for (int dy = 0; dy < dst_ch; ++dy) {
        float src_y_f = dy * scale_cy;
        int sy0 = static_cast<int>(src_y_f);
        int sy1 = std::min(sy0 + 1, src_ch - 1);
        float fy = src_y_f - sy0;

        for (int dx = 0; dx < dst_cw; ++dx) {
            float src_x_f = dx * scale_cx;
            int sx0 = static_cast<int>(src_x_f);
            int sx1 = std::min(sx0 + 1, src_cw - 1);
            float fx = src_x_f - sx0;

            // Each chroma element is 2 bytes (U, V interleaved)
            for (int c = 0; c < 2; ++c) {
                float top = (1.0f - fx) * src_uv[sy0 * src_cw * 2 + sx0 * 2 + c]
                          + fx * src_uv[sy0 * src_cw * 2 + sx1 * 2 + c];
                float bot = (1.0f - fx) * src_uv[sy1 * src_cw * 2 + sx0 * 2 + c]
                          + fx * src_uv[sy1 * src_cw * 2 + sx1 * 2 + c];
                float val = (1.0f - fy) * top + fy * bot;

                dst_uv[dy * dst_cw * 2 + dx * 2 + c] = static_cast<uint8_t>(val);
            }
        }
    }
}

} // namespace ct
