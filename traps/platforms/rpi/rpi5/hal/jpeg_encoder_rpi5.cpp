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

#include "jpeg_encoder_rpi5.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <linux/dma-heap.h>
#include <poll.h>

namespace ct {

// ─── VideoCore VII JPEG encoder constants ─────────────────────────────────────
// On Pi 5, /dev/video31 is the V4L2 M2M JPEG encoder (via bcm2835-codec).
// The encoder supports:
//   - INPUT:  NV12 (captured frames)
//   - OUTPUT: JPEG
//   - CROP:   V4L2_SEL_TGT_CROP for hardware crop before encode

Rpi5JpegEncoder::Rpi5JpegEncoder()
    : m2m_fd_(-1), initialised_(false) {}

Rpi5JpegEncoder::~Rpi5JpegEncoder() {
    if (initialised_ && m2m_fd_ >= 0) {
        close(m2m_fd_);
    }
}

bool Rpi5JpegEncoder::ensure_initialised() {
    if (initialised_) return true;

    // Open V4L2 M2M JPEG encoder device
    m2m_fd_ = open("/dev/video31", O_RDWR, 0);
    if (m2m_fd_ < 0) {
        std::cerr << "[JPEGRpi5] Failed to open /dev/video31: " << strerror(errno) << "\n";
        return false;
    }

    initialised_ = true;
    std::cout << "[JPEGRpi5] V4L2 M2M JPEG encoder ready\n";
    return true;
}

std::vector<uint8_t> Rpi5JpegEncoder::encode_crop(int src_dma_fd,
                                                    uint32_t src_w,
                                                    uint32_t src_h,
                                                    uint32_t src_stride,
                                                    uint32_t crop_x,
                                                    uint32_t crop_y,
                                                    uint32_t crop_w,
                                                    uint32_t crop_h,
                                                    int quality) {
    if (!ensure_initialised() || m2m_fd_ < 0 || src_dma_fd < 0) {
        return {};
    }

    // ── Set output format (NV12 input) ─────────────────────────────────────────
    struct v4l2_format fmt_out;
    memset(&fmt_out, 0, sizeof(fmt_out));
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.width       = src_w;
    fmt_out.fmt.pix_mp.height      = src_h;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt_out.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt_out.fmt.pix_mp.num_planes  = 1;
    fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline = src_stride;
    fmt_out.fmt.pix_mp.plane_fmt[0].sizeimage    = src_w * src_h * 3 / 2;

    if (ioctl(m2m_fd_, VIDIOC_S_FMT, &fmt_out) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_S_FMT(OUTPUT) failed: " << strerror(errno) << "\n";
        return {};
    }

    // ── Set capture format (JPEG output) ───────────────────────────────────────
    struct v4l2_format fmt_cap;
    memset(&fmt_cap, 0, sizeof(fmt_cap));
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.width       = crop_w;
    fmt_cap.fmt.pix_mp.height      = crop_h;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_JPEG;
    fmt_cap.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt_cap.fmt.pix_mp.num_planes  = 1;
    fmt_cap.fmt.pix_mp.plane_fmt[0].sizeimage = crop_w * crop_h * 3 / 2;  // ample

    if (ioctl(m2m_fd_, VIDIOC_S_FMT, &fmt_cap) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_S_FMT(CAPTURE) failed: " << strerror(errno) << "\n";
        return {};
    }

    // ── Request buffers (zero-copy dmabuf for both planes) ─────────────────────
    struct v4l2_requestbuffers req_out;
    memset(&req_out, 0, sizeof(req_out));
    req_out.count  = 2;
    req_out.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req_out.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(m2m_fd_, VIDIOC_REQBUFS, &req_out) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_REQBUFS(OUTPUT, DMABUF) failed\n";
        return {};
    }

    struct v4l2_requestbuffers req_cap;
    memset(&req_cap, 0, sizeof(req_cap));
    req_cap.count  = 2;
    req_cap.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_cap.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(m2m_fd_, VIDIOC_REQBUFS, &req_cap) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_REQBUFS(CAPTURE, DMABUF) failed\n";
        return {};
    }

    // ── Allocate output dmabuf for JPEG ─────────────────────────────────────────
    size_t jpeg_buf_size = static_cast<size_t>(crop_w * crop_h * 2);
    int out_fd = open("/dev/dma_heap/system", O_RDONLY);
    if (out_fd < 0) {
        std::cerr << "[JPEGRpi5] Failed to open dma_heap: " << strerror(errno) << "\n";
        return {};
    }

    struct dma_heap_allocation_data alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.len      = jpeg_buf_size;
    alloc.fd_flags = O_RDWR | O_CLOEXEC;

    int dma_ret = ioctl(out_fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
    close(out_fd);
    if (dma_ret < 0) {
        std::cerr << "[JPEGRpi5] DMA_HEAP_IOCTL_ALLOC failed: " << strerror(errno) << "\n";
        return {};
    }
    int jpeg_dma_fd = alloc.fd;

    // ── Set crop selection ─────────────────────────────────────────────────────
    struct v4l2_selection sel;
    memset(&sel, 0, sizeof(sel));
    sel.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    sel.target = V4L2_SEL_TGT_CROP;
    sel.flags = V4L2_SEL_FLAG_GE;
    sel.r.left   = static_cast<__s32>(crop_x);
    sel.r.top    = static_cast<__s32>(crop_y);
    sel.r.width  = static_cast<__s32>(crop_w);
    sel.r.height = static_cast<__s32>(crop_h);

    if (ioctl(m2m_fd_, VIDIOC_S_SELECTION, &sel) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_S_SELECTION failed: " << strerror(errno) << "\n";
        close(jpeg_dma_fd);
        return {};
    }

    // ── Stream on ──────────────────────────────────────────────────────────────
    enum v4l2_buf_type type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(m2m_fd_, VIDIOC_STREAMON, &type_out) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_STREAMON(OUTPUT) failed\n";
        close(jpeg_dma_fd);
        return {};
    }
    enum v4l2_buf_type type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(m2m_fd_, VIDIOC_STREAMON, &type_cap) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_STREAMON(CAPTURE) failed\n";
        close(jpeg_dma_fd);
        return {};
    }

    // ── QBUF input (source dmabuf) ─────────────────────────────────────────────
    {
        struct v4l2_plane plane;
        memset(&plane, 0, sizeof(plane));
        plane.m.fd      = src_dma_fd;   // import the source frame
        plane.length    = src_w * src_h * 3 / 2;
        plane.bytesused = src_w * src_h * 3 / 2;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type     = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory   = V4L2_MEMORY_DMABUF;
        buf.index    = 0;
        buf.length   = 1;
        buf.m.planes = &plane;

        if (ioctl(m2m_fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[JPEGRpi5] VIDIOC_QBUF(OUTPUT) failed\n";
            close(jpeg_dma_fd);
            return {};
        }
    }

    // ── QBUF output (JPEG dmabuf) ──────────────────────────────────────────────
    {
        struct v4l2_plane plane;
        memset(&plane, 0, sizeof(plane));
        plane.m.fd      = jpeg_dma_fd;
        plane.length    = jpeg_buf_size;
        plane.bytesused = 0;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_DMABUF;
        buf.index    = 0;
        buf.length   = 1;
        buf.m.planes = &plane;

        if (ioctl(m2m_fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[JPEGRpi5] VIDIOC_QBUF(CAPTURE) failed\n";
            close(jpeg_dma_fd);
            return {};
        }
    }

    // ── Start processing (buffer done implicitly by streamon) ──────────────────

    // ── Poll for completion ─────────────────────────────────────────────────────
    struct pollfd pfd;
    pfd.fd     = m2m_fd_;
    pfd.events = POLLIN;
    int poll_ret = poll(&pfd, 1, 5000);  // 5s timeout
    if (poll_ret <= 0) {
        std::cerr << "[JPEGRpi5] poll timeout or error\n";
        return {};
    }

    // ── DQBUF capture ──────────────────────────────────────────────────────────
    struct v4l2_plane plane_cap;
    memset(&plane_cap, 0, sizeof(plane_cap));
    struct v4l2_buffer buf_cap;
    memset(&buf_cap, 0, sizeof(buf_cap));
    buf_cap.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf_cap.memory   = V4L2_MEMORY_DMABUF;
    buf_cap.length   = 1;
    buf_cap.index    = 0;
    buf_cap.m.planes = &plane_cap;

    if (ioctl(m2m_fd_, VIDIOC_DQBUF, &buf_cap) < 0) {
        std::cerr << "[JPEGRpi5] VIDIOC_DQBUF(CAPTURE) failed\n";
        close(jpeg_dma_fd);
        return {};
    }

    // ── mmap the JPEG output dmabuf ────────────────────────────────────────────
    void* mapped = mmap(nullptr, jpeg_buf_size, PROT_READ, MAP_SHARED,
                        jpeg_dma_fd, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << "[JPEGRpi5] mmap JPEG dmabuf failed\n";
        close(jpeg_dma_fd);
        return {};
    }

    uint32_t jpeg_len = plane_cap.bytesused;
    std::vector<uint8_t> result;
    if (jpeg_len > 0) {
        result.assign(static_cast<const uint8_t*>(mapped),
                      static_cast<const uint8_t*>(mapped) + jpeg_len);
    }
    munmap(mapped, jpeg_buf_size);
    close(jpeg_dma_fd);

    // ── Stream off ─────────────────────────────────────────────────────────────
    ioctl(m2m_fd_, VIDIOC_STREAMOFF, &type_out);
    ioctl(m2m_fd_, VIDIOC_STREAMOFF, &type_cap);

    return result;
}

} // namespace ct