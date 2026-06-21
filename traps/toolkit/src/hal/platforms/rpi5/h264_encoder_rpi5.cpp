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

#include "h264_encoder_rpi5.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

namespace ct {

// ─── V4L2 M2M helper ──────────────────────────────────────────────────────────
// Simple RAII helper for V4L2 buffer management
struct V4l2Buffer {
    int      fd     = -1;    // dmabuf fd for import
    void*    data   = nullptr;  // mmap'd pointer
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t index  = 0;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

Rpi5H264Encoder::Rpi5H264Encoder() = default;

Rpi5H264Encoder::~Rpi5H264Encoder() noexcept {
    deinit();
}

// ─── init ─────────────────────────────────────────────────────────────────────

int Rpi5H264Encoder::init(int width, int height, int qp) {
    deinit();

    if (width <= 0 || height <= 0) return -1;
    width_ = width;
    height_ = height;

    // Open V4L2 M2M device
    m2m_fd_ = open("/dev/video11", O_RDWR, 0);
    if (m2m_fd_ < 0) {
        std::cerr << "[H264Rpi5] Failed to open /dev/video11: " << strerror(errno) << "\n";
        return -1;
    }

    // Query capabilities
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(m2m_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_QUERYCAP failed\n";
        close(m2m_fd_);
        m2m_fd_ = -1;
        return -1;
    }

    std::cout << "[H264Rpi5] Device: " << cap.card << "\n";

    // ── Set output format (NV12 input) ─────────────────────────────────────────
    struct v4l2_format fmt_out;
    memset(&fmt_out, 0, sizeof(fmt_out));
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.width       = width;
    fmt_out.fmt.pix_mp.height      = height;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt_out.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt_out.fmt.pix_mp.num_planes  = 1;
    fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline = (width + 15) & ~15;
    fmt_out.fmt.pix_mp.plane_fmt[0].sizeimage    = width * height * 3 / 2;

    if (ioctl(m2m_fd_, VIDIOC_S_FMT, &fmt_out) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_S_FMT(OUTPUT) failed\n";
        deinit();
        return -1;
    }

    // ── Set capture format (H.264 output) ───────────────────────────────────────
    struct v4l2_format fmt_cap;
    memset(&fmt_cap, 0, sizeof(fmt_cap));
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.width       = width;
    fmt_cap.fmt.pix_mp.height      = height;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt_cap.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt_cap.fmt.pix_mp.num_planes  = 1;
    fmt_cap.fmt.pix_mp.plane_fmt[0].sizeimage = 512 * 1024;  // 512KB should suffice

    if (ioctl(m2m_fd_, VIDIOC_S_FMT, &fmt_cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_S_FMT(CAPTURE) failed\n";
        deinit();
        return -1;
    }

    // ── Set framerate (informational for the encoder) ───────────────────────────
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 30;
    ioctl(m2m_fd_, VIDIOC_S_PARM, &parm);

    // ── Set bitrate via V4L2 control ───────────────────────────────────────────
    // QP is not directly available on most V4L2 M2M H.264 encoders.
    // Instead we set the video bitrate. QP 26 ≈ 4 Mbps for 1080p30.
    int32_t bitrate = 4000000;
    if (width * height > 0) {
        bitrate = static_cast<int32_t>(4000000 *
            (static_cast<double>(width * height) / (1920.0 * 1080.0)));
    }

    struct v4l2_ext_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id    = V4L2_CID_MPEG_VIDEO_BITRATE;
    ctrl.value = bitrate;

    struct v4l2_ext_controls ctrls;
    memset(&ctrls, 0, sizeof(ctrls));
    ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    if (ioctl(m2m_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        // Non-fatal — many V4L2 H.264 encoders use default bitrate
        std::cerr << "[H264Rpi5] VIDIOC_S_EXT_CTRLS bitrate failed: "
                  << strerror(errno) << "\n";
    }

    // ── Request buffers (OUTPUT: dmabuf import, CAPTURE: mmap) ─────────────────
    struct v4l2_requestbuffers req_out;
    memset(&req_out, 0, sizeof(req_out));
    req_out.count  = 4;
    req_out.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req_out.memory = V4L2_MEMORY_DMABUF;  // Import dmabuf fds

    if (ioctl(m2m_fd_, VIDIOC_REQBUFS, &req_out) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_REQBUFS(OUTPUT, DMABUF) failed\n";
        deinit();
        return -1;
    }

    struct v4l2_requestbuffers req_cap;
    memset(&req_cap, 0, sizeof(req_cap));
    req_cap.count  = 4;
    req_cap.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_cap.memory = V4L2_MEMORY_MMAP;  // CPU-readable output

    if (ioctl(m2m_fd_, VIDIOC_REQBUFS, &req_cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_REQBUFS(CAPTURE, MMAP) failed\n";
        deinit();
        return -1;
    }

    // ── QBUF all capture buffers ─────────────────────────────────────────────
    for (uint32_t i = 0; i < req_cap.count; ++i) {
        struct v4l2_plane plane;
        memset(&plane, 0, sizeof(plane));
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.length   = 1;
        buf.m.planes = &plane;
        buf.m.planes[0].length = 512 * 1024;

        if (ioctl(m2m_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[H264Rpi5] VIDIOC_QUERYBUF(CAPTURE) failed\n";
            deinit();
            return -1;
        }

        // mmap the capture buffer
        void* mapped = mmap(nullptr, buf.m.planes[0].length,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            m2m_fd_, buf.m.planes[0].m.mem_offset);
        if (mapped == MAP_FAILED) {
            std::cerr << "[H264Rpi5] mmap capture buffer failed\n";
            deinit();
            return -1;
        }

        if (ioctl(m2m_fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[H264Rpi5] VIDIOC_QBUF(CAPTURE) failed\n";
            deinit();
            return -1;
        }
    }

    // ── Stream on ──────────────────────────────────────────────────────────────
    enum v4l2_buf_type type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(m2m_fd_, VIDIOC_STREAMON, &type_out) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_STREAMON(OUTPUT) failed\n";
        deinit();
        return -1;
    }

    enum v4l2_buf_type type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(m2m_fd_, VIDIOC_STREAMON, &type_cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_STREAMON(CAPTURE) failed\n";
        deinit();
        return -1;
    }

    initialised_ = true;
    std::cout << "[H264Rpi5] V4L2 M2M H.264 encoder ready: "
              << width << "x" << height << " @ " << bitrate << "bps\n";
    return 0;
}

// ─── encode ───────────────────────────────────────────────────────────────────

std::vector<uint8_t> Rpi5H264Encoder::encode(int dma_fd, uint32_t size) {
    if (!initialised_ || m2m_fd_ < 0 || dma_fd < 0) return {};

    uint32_t frame_size = size > 0 ? size
                          : static_cast<uint32_t>(width_ * height_ * 3 / 2);

    // ── QBUF the input dmabuf ──────────────────────────────────────────────────
    struct v4l2_plane plane_out;
    memset(&plane_out, 0, sizeof(plane_out));
    plane_out.m.fd      = dma_fd;       // dmabuf import fd
    plane_out.length    = frame_size;
    plane_out.bytesused = frame_size;

    struct v4l2_buffer buf_out;
    memset(&buf_out, 0, sizeof(buf_out));
    buf_out.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf_out.memory      = V4L2_MEMORY_DMABUF;
    buf_out.index       = 0;
    buf_out.length      = 1;
    buf_out.m.planes    = &plane_out;

    if (ioctl(m2m_fd_, VIDIOC_QBUF, &buf_out) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_QBUF(OUTPUT, dmabuf) failed: "
                  << strerror(errno) << "\n";
        return {};
    }

    // ── DQBUF the encoded H.264 output ─────────────────────────────────────────
    struct v4l2_plane plane_cap;
    memset(&plane_cap, 0, sizeof(plane_cap));
    struct v4l2_buffer buf_cap;
    memset(&buf_cap, 0, sizeof(buf_cap));
    buf_cap.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf_cap.memory   = V4L2_MEMORY_MMAP;
    buf_cap.length   = 1;
    buf_cap.m.planes = &plane_cap;

    if (ioctl(m2m_fd_, VIDIOC_DQBUF, &buf_cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_DQBUF(CAPTURE) failed: "
                  << strerror(errno) << "\n";
        return {};
    }

    // ── mmap the captured output buffer ────────────────────────────────────────
    // The capture buffers were mmap'd during init. We need to locate the
    // correct mapping. For this simple implementation, we mmap each frame.
    size_t out_len = plane_cap.bytesused;

    void* mapped = mmap(nullptr, out_len, PROT_READ, MAP_SHARED,
                        m2m_fd_, plane_cap.m.mem_offset);
    if (mapped == MAP_FAILED) {
        std::cerr << "[H264Rpi5] mmap capture failed\n";
        return {};
    }

    std::vector<uint8_t> result;
    if (out_len > 0 && mapped) {
        result.assign(static_cast<const uint8_t*>(mapped),
                      static_cast<const uint8_t*>(mapped) + out_len);
    }
    munmap(mapped, out_len);

    // ── Re-QBUF the capture buffer ─────────────────────────────────────────
    plane_cap.m.fd      = -1;
    plane_cap.length    = 512 * 1024;
    plane_cap.bytesused = 0;

    if (ioctl(m2m_fd_, VIDIOC_QBUF, &buf_cap) < 0) {
        std::cerr << "[H264Rpi5] VIDIOC_QBUF(CAPTURE) re-queue failed\n";
    }

    return result;
}

// ─── deinit ───────────────────────────────────────────────────────────────────

void Rpi5H264Encoder::deinit() {
    if (!initialised_) return;

    std::cout << "[H264Rpi5] shutting down\n";

    if (m2m_fd_ >= 0) {
        enum v4l2_buf_type type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        enum v4l2_buf_type type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(m2m_fd_, VIDIOC_STREAMOFF, &type_out);
        ioctl(m2m_fd_, VIDIOC_STREAMOFF, &type_cap);
        close(m2m_fd_);
        m2m_fd_ = -1;
    }

    width_ = 0;
    height_ = 0;
    initialised_ = false;
}

} // namespace ct