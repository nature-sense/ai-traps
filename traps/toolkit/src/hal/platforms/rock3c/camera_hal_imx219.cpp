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

#include "camera_hal_imx219.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

// V4L2 headers for direct ISP frame capture
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>

// dma-heap for buffer allocation
#include <linux/dma-heap.h>

namespace ct {

// ─── dma-heap helpers ─────────────────────────────────────────────────────────

int CameraHalImx219::alloc_dma_buf(uint32_t size) {
    const char* heap_dev = "/dev/dma_heap/system";
    int heap_fd = open(heap_dev, O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) {
        std::cerr << "[CameraHalImx219] cannot open " << heap_dev << ": "
                  << std::strerror(errno) << "\n";
        return -1;
    }

    struct dma_heap_allocation_data data;
    std::memset(&data, 0, sizeof(data));
    data.len       = size;
    data.fd_flags  = O_RDWR | O_CLOEXEC;
    data.heap_flags = 0;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
        std::cerr << "[CameraHalImx219] DMA_HEAP_IOCTL_ALLOC failed: "
                  << std::strerror(errno) << "\n";
        close(heap_fd);
        return -1;
    }

    close(heap_fd);
    return static_cast<int>(data.fd);
}

void* CameraHalImx219::map_dma_buf(int fd, uint32_t size) {
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        std::cerr << "[CameraHalImx219] mmap dma-buf failed: "
                  << std::strerror(errno) << "\n";
        return nullptr;
    }
    return addr;
}

void CameraHalImx219::unmap_dma_buf(void* addr, uint32_t size, int fd) {
    if (addr && addr != MAP_FAILED) {
        munmap(addr, size);
    }
    if (fd >= 0) {
        close(fd);
    }
}

// ─── librga/im2d NV12 scaling ─────────────────────────────────────────────────

bool CameraHalImx219::scale_nv12(const FrameBuffer& src, FrameBuffer& dst,
                                 int dst_w, int dst_h) {
    if (!src.data || src.size == 0) {
        return false;
    }

    // Wrap source as an rga_buffer_t using virtual address
    rga_buffer_t src_buf = wrapbuffer_virtualaddr_t(
        src.data, static_cast<int>(src.width), static_cast<int>(src.height),
        static_cast<int>(src.width), static_cast<int>(src.height),
        RK_FORMAT_YCbCr_420_SP);

    // Wrap destination as an rga_buffer_t using virtual address
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr_t(
        dst.data, dst_w, dst_h,
        dst_w, dst_h,
        RK_FORMAT_YCbCr_420_SP);

    // Perform hardware-accelerated resize
    IM_STATUS ret = imresize(src_buf, dst_buf);
    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "[CameraHalImx219] imresize failed: " << ret
                  << " (" << dst_w << "x" << dst_h << ")\n";
        return false;
    }

    dst.width  = static_cast<uint32_t>(dst_w);
    dst.height = static_cast<uint32_t>(dst_h);
    dst.stride = static_cast<uint32_t>(dst_w);  // NV12: stride == width
    dst.size   = static_cast<uint32_t>(dst_w * dst_h * 3 / 2);

    return true;
}

// ─── init_aiq ────────────────────────────────────────────────────────────────

static XCamReturn aiq_err_cb(rk_aiq_err_msg_t* msg) {
    if (msg->err_code == XCAM_RETURN_BYPASS) {
        std::cerr << "[CameraHalImx219] AIQ fatal error, should quit\n";
    }
    return XCAM_RETURN_NO_ERROR;
}

static XCamReturn aiq_sof_cb(rk_aiq_metas_t* meta) {
    static int cnt = 0;
    if (++cnt <= 2)
        std::cout << "[CameraHalImx219] AIQ SOF frame_id=" << meta->frame_id << "\n";
    return XCAM_RETURN_NO_ERROR;
}

bool CameraHalImx219::init_aiq(const std::string& iq_dir) {
    // Must set HDR_MODE before AIQ init (required by Rockchip AIQ)
    setenv("HDR_MODE", "0", 1);

    rk_aiq_static_info_t static_info{};
    XCamReturn xret = rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0, &static_info);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalImx219] enumStaticMetasByPhyId failed: " << xret << "\n";
        return false;
    }
    const char* sns_name = static_info.sensor_info.sensor_name;
    std::cout << "[CameraHalImx219] AIQ sensor: " << sns_name << "\n";

    rk_aiq_uapi2_sysctl_preInit_devBufCnt(sns_name, "rkraw_rx", 2);

    // Try scene preinit, but don't fail if it's not supported
    std::cerr << "[CameraHalImx219] Setting scene: normal" << std::endl;
    int scene_ret = rk_aiq_uapi2_sysctl_preInit_scene(sns_name, "normal", "");
    if (scene_ret < 0) {
        std::cerr << "[CameraHalImx219] rk_aiq_uapi2_sysctl_preInit_scene failed: "
                  << scene_ret << " (continuing anyway)" << std::endl;
    }

    std::cerr << "[CameraHalImx219] rk_aiq_uapi2_sysctl_init sns_name=" << sns_name << " iq_dir=" << iq_dir << std::endl;
    aiq_ctx_ = rk_aiq_uapi2_sysctl_init(sns_name, iq_dir.c_str(), aiq_err_cb, aiq_sof_cb);
    if (!aiq_ctx_) {
        std::cerr << "[CameraHalImx219] rk_aiq_uapi2_sysctl_init failed\n";
        return false;
    }

    xret = rk_aiq_uapi2_sysctl_prepare(aiq_ctx_, 0, 0, RK_AIQ_WORKING_MODE_NORMAL);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalImx219] rk_aiq_uapi2_sysctl_prepare failed: " << xret << "\n";
        return false;
    }
    std::cout << "[CameraHalImx219] rk_aiq_uapi2_sysctl_prepare succeeded\n";

    xret = rk_aiq_uapi2_sysctl_start(aiq_ctx_);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalImx219] rk_aiq_uapi2_sysctl_start failed: " << xret << "\n";
        return false;
    }

    std::cout << "[CameraHalImx219] RK_AIQ started\n";

    // Wait for ISP to converge before capturing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    rk_aiq_uapi2_setAeLock(aiq_ctx_, true);
    std::cout << "[CameraHalImx219] AE locked\n";
    rk_aiq_uapi2_lockAWB(aiq_ctx_);
    std::cout << "[CameraHalImx219] AWB locked\n";

    return true;
}

// ─── init_v4l2 ───────────────────────────────────────────────────────────────
// Opens /dev/video0 (ISP mainpath on Armbian for RK3566) and sets up DMABUF-
// based zero-copy capture.  The ISP must already be streaming (started by AIQ)
// before we open the video node, otherwise VIDIOC_S_FMT will fail.

bool CameraHalImx219::init_v4l2() {
    const char* dev_name = "/dev/video0";

    v4l2_fd_ = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (v4l2_fd_ < 0) {
        std::cerr << "[CameraHalImx219] cannot open " << dev_name << ": "
                  << std::strerror(errno) << "\n";
        return false;
    }
    std::cout << "[CameraHalImx219] opened " << dev_name << " (fd=" << v4l2_fd_ << ")\n";

    // ── Query capabilities ───────────────────────────────────────────────────
    struct v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (ioctl(v4l2_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_QUERYCAP failed: " << std::strerror(errno) << "\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return false;
    }
    std::cout << "[CameraHalImx219] driver=" << cap.driver
              << " card=" << cap.card
              << " bus_info=" << cap.bus_info << "\n";

    // ── Set format: NV12 at full resolution ──────────────────────────────────
    struct ::v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width     = static_cast<__u32>(cfg_.camera.full_w);
    fmt.fmt.pix_mp.height    = static_cast<__u32>(cfg_.camera.full_h);
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field     = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_S_FMT failed: " << std::strerror(errno) << "\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return false;
    }
    std::cout << "[CameraHalImx219] S_FMT: " << fmt.fmt.pix_mp.width << "x"
              << fmt.fmt.pix_mp.height << " stride="
              << fmt.fmt.pix_mp.plane_fmt[0].bytesperline << "\n";

    const __u32 frame_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

    // ── Pre-allocate dma-buf buffers for V4L2 DMABUF path ────────────────────
    constexpr unsigned int NUM_BUFS = 4;
    v4l2_buf_pool_.reserve(NUM_BUFS);
    for (unsigned int i = 0; i < NUM_BUFS; ++i) {
        int dma_fd = alloc_dma_buf(frame_size);
        if (dma_fd < 0) {
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }

        void* vaddr = map_dma_buf(dma_fd, frame_size);
        if (!vaddr) {
            close(dma_fd);
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }

        V4l2BufSlot slot;
        slot.index  = i;
        slot.mapped = vaddr;
        slot.length = frame_size;
        slot.queued = false;
        slot.dma_fd = dma_fd;
        v4l2_buf_pool_.push_back(slot);

        std::cout << "[CameraHalImx219] buf[" << i << "]: dma_fd=" << dma_fd
                  << " vaddr=" << vaddr << " size=" << frame_size << "\n";
    }

    // ── Request DMABUF buffers from V4L2 ─────────────────────────────────────
    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count   = NUM_BUFS;
    req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory  = V4L2_MEMORY_DMABUF;

    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_REQBUFS (DMABUF) failed: "
                  << std::strerror(errno) << "\n";
        // Fallback: try MMAP instead
        std::cout << "[CameraHalImx219] DMABUF not supported, falling back to MMAP\n";
        return init_v4l2_mmap(fmt, frame_size);
    }
    std::cout << "[CameraHalImx219] REQBUFS (DMABUF): count=" << req.count << "\n";

    // ── Queue all buffers with their DMABUF fds ──────────────────────────────
    for (auto& slot : v4l2_buf_pool_) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        struct v4l2_buffer qbuf;
        std::memset(&qbuf, 0, sizeof(qbuf));
        std::memset(planes, 0, sizeof(planes));
        qbuf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory      = V4L2_MEMORY_DMABUF;
        qbuf.index       = slot.index;
        qbuf.m.planes    = planes;
        qbuf.length      = 1;
        planes[0].m.fd   = slot.dma_fd;
        planes[0].length = slot.length;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalImx219] VIDIOC_QBUF[" << slot.index
                      << "] failed: " << std::strerror(errno) << "\n";
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }
        slot.queued = true;
    }

    // ── Start streaming ──────────────────────────────────────────────────────
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &buf_type) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_STREAMON failed: "
                  << std::strerror(errno) << "\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return false;
    }

    use_dmabuf_ = true;
    std::cout << "[CameraHalImx219] V4L2 DMABUF streaming started\n";
    return true;
}

// ─── init_v4l2_mmap (fallback) ───────────────────────────────────────────────

bool CameraHalImx219::init_v4l2_mmap(const struct v4l2_format& fmt, __u32 frame_size) {
    (void)fmt;

    // Request MMAP buffers
    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count   = 4;
    req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory  = V4L2_MEMORY_MMAP;

    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_REQBUFS (MMAP) failed: "
                  << std::strerror(errno) << "\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return false;
    }
    std::cout << "[CameraHalImx219] REQBUFS (MMAP): count=" << req.count << "\n";

    v4l2_buf_pool_.reserve(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        struct v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;

        if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[CameraHalImx219] VIDIOC_QUERYBUF[" << i
                      << "] failed: " << std::strerror(errno) << "\n";
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }

        void* mapped = mmap(nullptr, planes[0].length,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            v4l2_fd_, planes[0].m.mem_offset);
        if (mapped == MAP_FAILED) {
            std::cerr << "[CameraHalImx219] mmap[" << i << "] failed: "
                      << std::strerror(errno) << "\n";
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }

        V4l2BufSlot slot;
        slot.index  = i;
        slot.mapped = mapped;
        slot.length = planes[0].length;
        slot.queued = false;
        slot.dma_fd = -1;
        v4l2_buf_pool_.push_back(slot);
    }

    // Queue all MMAP buffers
    for (auto& slot : v4l2_buf_pool_) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        struct v4l2_buffer qbuf;
        std::memset(&qbuf, 0, sizeof(qbuf));
        std::memset(planes, 0, sizeof(planes));
        qbuf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory   = V4L2_MEMORY_MMAP;
        qbuf.index    = slot.index;
        qbuf.m.planes = planes;
        qbuf.length   = 1;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalImx219] VIDIOC_QBUF[" << slot.index
                      << "] failed: " << std::strerror(errno) << "\n";
            close(v4l2_fd_);
            v4l2_fd_ = -1;
            return false;
        }
        slot.queued = true;
    }

    // Start streaming
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &buf_type) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_STREAMON failed: "
                  << std::strerror(errno) << "\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return false;
    }

    use_dmabuf_ = false;
    std::cout << "[CameraHalImx219] V4L2 MMAP streaming started\n";
    return true;
}

// ─── init_scaler_buffers ─────────────────────────────────────────────────────
// Pre-allocate dma-buf backed buffers for the three scaled output streams.

bool CameraHalImx219::init_scaler_buffers() {
    auto alloc_scaler = [&](ScalerBuf& sb, int w, int h) -> bool {
        sb.size = static_cast<uint32_t>(w * h * 3 / 2);  // NV12 size
        sb.fd   = alloc_dma_buf(sb.size);
        if (sb.fd < 0) return false;
        sb.addr = map_dma_buf(sb.fd, sb.size);
        if (!sb.addr) {
            close(sb.fd);
            sb.fd = -1;
            return false;
        }
        return true;
    };

    if (!alloc_scaler(scaler_full_,   cfg_.camera.full_w,  cfg_.camera.full_h))  return false;
    if (!alloc_scaler(scaler_medium_, cfg_.camera.med_w,   cfg_.camera.med_h))   return false;
    if (!alloc_scaler(scaler_lores_,  cfg_.camera.lores_w, cfg_.camera.lores_h)) return false;

    std::cout << "[CameraHalImx219] scaler buffers allocated: "
              << cfg_.camera.full_w << "x" << cfg_.camera.full_h << " + "
              << cfg_.camera.med_w  << "x" << cfg_.camera.med_h  << " + "
              << cfg_.camera.lores_w << "x" << cfg_.camera.lores_h << "\n";
    return true;
}

// ─── init ────────────────────────────────────────────────────────────────────

bool CameraHalImx219::init(const PipelineConfig& cfg) {
    cfg_ = cfg;

    if (!init_aiq(cfg_.camera.iq_dir)) {
        std::cerr << "[CameraHalImx219] AIQ init failed\n";
        return false;
    }

    if (!init_v4l2()) {
        std::cerr << "[CameraHalImx219] V4L2 init failed\n";
        return false;
    }

    if (!init_scaler_buffers()) {
        std::cerr << "[CameraHalImx219] scaler buffer init failed\n";
        return false;
    }

    initialised_ = true;
    std::cout << "[CameraHalImx219] ready\n";
    return true;
}

// ─── acquire_frames ──────────────────────────────────────────────────────────

bool CameraHalImx219::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                     FrameBuffer& lores) {
    acquire_frames_impl();

    full   = frame_full_;
    medium = frame_medium_;
    lores  = frame_lores_;

    // Return true if at least the lores frame was acquired (needed for inference)
    return (frame_lores_.data != nullptr);
}

void CameraHalImx219::acquire_frames_impl() {
    // Reset frame buffers
    frame_full_   = FrameBuffer{};
    frame_medium_ = FrameBuffer{};
    frame_lores_  = FrameBuffer{};

    // ── 1. Dequeue a V4L2 buffer from the ISP mainpath ───────────────────────
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));
    std::memset(planes, 0, sizeof(planes));
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = use_dmabuf_ ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length   = 1;

    struct pollfd pfd = { v4l2_fd_, POLLIN, 0 };
    int poll_ret = poll(&pfd, 1, 2000);
    if (poll_ret <= 0) {
        if (poll_ret == 0) {
            std::cerr << "[CameraHalImx219] V4L2 poll timeout\n";
        } else {
            std::cerr << "[CameraHalImx219] V4L2 poll error: " << std::strerror(errno) << "\n";
        }
        return;
    }

    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "[CameraHalImx219] VIDIOC_DQBUF failed: " << std::strerror(errno) << "\n";
        return;
    }

    auto it = std::find_if(v4l2_buf_pool_.begin(), v4l2_buf_pool_.end(),
                           [&](const V4l2BufSlot& s) { return s.index == buf.index; });
    if (it == v4l2_buf_pool_.end()) {
        std::cerr << "[CameraHalImx219] unknown buffer index " << buf.index << "\n";
        ioctl(v4l2_fd_, VIDIOC_QBUF, &buf);
        return;
    }

    it->queued = false;

    // Build a FrameBuffer for the raw V4L2 capture
    FrameBuffer raw_frame;
    raw_frame.data        = it->mapped;
    raw_frame.width       = static_cast<uint32_t>(cfg_.camera.full_w);
    raw_frame.height      = static_cast<uint32_t>(cfg_.camera.full_h);
    raw_frame.stride      = static_cast<uint32_t>(cfg_.camera.full_w);  // NV12: stride == width
    raw_frame.size        = it->length;
    raw_frame.timestamp_ms = static_cast<int64_t>(buf.timestamp.tv_sec * 1000LL
                                                   + buf.timestamp.tv_usec / 1000);
    raw_frame.dma_fd      = it->dma_fd;

    // ── 2. Scale the raw frame to three resolutions using librga ──────────────
    //    full   → scaler_full_   (for JPEG crop extraction)
    //    medium → scaler_medium_ (for MJPEG preview stream)
    //    lores  → scaler_lores_  (for YOLO inference)

    // Full-res: just reference the raw buffer directly (no scaling needed)
    frame_full_ = raw_frame;

    // Medium-res: scale from raw to scaler_medium_
    frame_medium_.data   = scaler_medium_.addr;
    frame_medium_.dma_fd = scaler_medium_.fd;
    if (!scale_nv12(raw_frame, frame_medium_, cfg_.camera.med_w, cfg_.camera.med_h)) {
        frame_medium_ = FrameBuffer{};
    }
    frame_medium_.timestamp_ms = raw_frame.timestamp_ms;

    // Lores: scale from raw to scaler_lores_
    frame_lores_.data   = scaler_lores_.addr;
    frame_lores_.dma_fd = scaler_lores_.fd;
    if (!scale_nv12(raw_frame, frame_lores_, cfg_.camera.lores_w, cfg_.camera.lores_h)) {
        frame_lores_ = FrameBuffer{};
    }
    frame_lores_.timestamp_ms = raw_frame.timestamp_ms;

    // ── 3. Re-queue the V4L2 buffer ──────────────────────────────────────────
    if (use_dmabuf_) {
        planes[0].m.fd   = it->dma_fd;
        planes[0].length = it->length;
    }
    ioctl(v4l2_fd_, VIDIOC_QBUF, &buf);
    it->queued = true;
}

// ─── release_frames ──────────────────────────────────────────────────────────

void CameraHalImx219::release_frames() {
    release_frames_impl();
}

void CameraHalImx219::release_frames_impl() {
    // Scaler output buffers are persistent (reused each frame), so nothing
    // to release here. The raw V4L2 buffer was already re-queued in
    // acquire_frames_impl().
    frame_full_   = FrameBuffer{};
    frame_medium_ = FrameBuffer{};
    frame_lores_  = FrameBuffer{};
}

// ─── shutdown ────────────────────────────────────────────────────────────────

void CameraHalImx219::shutdown() {
    if (!initialised_) return;

    if (v4l2_fd_ >= 0) {
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &buf_type);
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        std::cout << "[CameraHalImx219] V4L2 closed\n";
    }

    // Release V4L2 buffer pool
    for (auto& slot : v4l2_buf_pool_) {
        if (use_dmabuf_) {
            unmap_dma_buf(slot.mapped, slot.length, slot.dma_fd);
        } else {
            if (slot.mapped) {
                munmap(slot.mapped, slot.length);
                slot.mapped = nullptr;
            }
        }
    }
    v4l2_buf_pool_.clear();

    // Release scaler output buffers
    auto free_scaler = [](ScalerBuf& sb) {
        unmap_dma_buf(sb.addr, sb.size, sb.fd);
        sb.addr = nullptr;
        sb.fd   = -1;
        sb.size = 0;
    };
    free_scaler(scaler_full_);
    free_scaler(scaler_medium_);
    free_scaler(scaler_lores_);

    // Stop AIQ
    if (aiq_ctx_) {
        rk_aiq_uapi2_sysctl_stop(aiq_ctx_, false);
        rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
        aiq_ctx_ = nullptr;
    }

    initialised_ = false;
    std::cout << "[CameraHalImx219] shutdown complete\n";
}

} // namespace ct
