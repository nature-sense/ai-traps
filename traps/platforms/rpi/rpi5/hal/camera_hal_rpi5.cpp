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

#include "camera_hal_rpi5.hpp"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <libcamera/libcamera.h>

namespace ct {

// ─── Forward-declared implementation ─────────────────────────────────────────

struct CameraHalRpi5::Impl {
    // libcamera objects
    std::unique_ptr<libcamera::CameraManager> cm_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;

    // Stream configuration (4 streams)
    struct StreamCfg {
        libcamera::Stream* stream = nullptr;
        libcamera::StreamConfiguration config;
        std::vector<std::unique_ptr<libcamera::FrameBuffer>> buffers;
        FrameBuffer* user_fb = nullptr;  // points to FrameBuffer we populate in acquire_frames
    };
    StreamCfg streams_[4];

    // Current request
    std::unique_ptr<libcamera::Request> request_;

    // Config
    PipelineConfig cfg;
    bool started = false;

    // ── Stream indices ────────────────────────────────────────────────────────
    static constexpr int STREAM_FULL   = 0;  // 1920x1080 NV12  → crops + H.264
    static constexpr int STREAM_MEDIUM = 1;  // 640x480  NV12  → overlay
    static constexpr int STREAM_LORES  = 2;  // 320x320  NV12  → reserved / fallback
    static constexpr int STREAM_CORAL  = 3;  // 320x320  RGB   → Coral TPU input
};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

CameraHalRpi5::CameraHalRpi5()
    : impl_(std::make_unique<Impl>()) {}

CameraHalRpi5::~CameraHalRpi5() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool CameraHalRpi5::init(const PipelineConfig& cfg) {
    if (initialised_) return true;

    impl_->cfg = cfg;
    auto& cm = impl_->cm_;

    std::cout << "[CameraHalRpi5] Initialising libcamera\n";

    // ── 1. Start camera manager ────────────────────────────────────────────────
    cm = std::make_unique<libcamera::CameraManager>();
    int ret = cm->start();
    if (ret < 0) {
        std::cerr << "[CameraHalRpi5] CameraManager::start() failed: " << ret << "\n";
        return false;
    }

    if (cm->cameras().empty()) {
        std::cerr << "[CameraHalRpi5] No cameras found\n";
        return false;
    }

    // ── 2. Open the first available camera (Pi Camera 3) ────────────────────────
    auto& cameras = cm->cameras();
    // Prefer the Pi Camera 3 by name substring, otherwise use first
    std::shared_ptr<libcamera::Camera> cam;
    for (auto& c : cameras) {
        std::string id = c->id();
        std::cout << "[CameraHalRpi5] Found camera: " << id << "\n";
        if (id.find("imx708") != std::string::npos || id.find("Pi Camera") != std::string::npos) {
            cam = cm->get(id);
            break;
        }
    }
    if (!cam) {
        cam = cm->get(cameras[0]->id());
    }
    if (!cam) {
        std::cerr << "[CameraHalRpi5] Failed to acquire camera\n";
        return false;
    }

    ret = cam->acquire();
    if (ret < 0) {
        std::cerr << "[CameraHalRpi5] Camera::acquire() failed: " << ret << "\n";
        return false;
    }
    impl_->camera_ = cam;

    // ── 3. Configure streams ────────────────────────────────────────────────────
    std::unique_ptr<libcamera::CameraConfiguration> config = cam->generateConfiguration({});
    if (!config) {
        std::cerr << "[CameraHalRpi5] generateConfiguration() failed\n";
        return false;
    }

    // Clear default roles and add our 4 streams
    config->at(0).pixelFormat = libcamera::formats::NV12;
    config->at(0).size = libcamera::Size(cfg.full_w, cfg.full_h);
    config->at(0).bufferCount = 4;

    // Add stream 1 (medium)
    libcamera::StreamRole role_medium = libcamera::StreamRole::StillCapture;
    config->addConfiguration(role_medium);
    config->at(1).pixelFormat = libcamera::formats::NV12;
    config->at(1).size = libcamera::Size(cfg.med_w, cfg.med_h);
    config->at(1).bufferCount = 4;

    // Add stream 2 (lores NV12)
    config->addConfiguration(libcamera::StreamRole::StillCapture);
    config->at(2).pixelFormat = libcamera::formats::NV12;
    config->at(2).size = libcamera::Size(cfg.lores_w, cfg.lores_h);
    config->at(2).bufferCount = 4;

    // Add stream 3 (RGB for Coral)
    config->addConfiguration(libcamera::StreamRole::VideoRecording);
    config->at(3).pixelFormat = libcamera::formats::RGB888;
    config->at(3).size = libcamera::Size(cfg.lores_w, cfg.lores_h);
    config->at(3).bufferCount = 4;

    // Validate and apply configuration
    switch (config->validate()) {
        case libcamera::CameraConfiguration::Adjusted:
            std::cout << "[CameraHalRpi5] Stream configuration was adjusted\n";
            break;
        case libcamera::CameraConfiguration::Invalid:
            std::cerr << "[CameraHalRpi5] Invalid stream configuration\n";
            return false;
        default:
            break;
    }

    ret = cam->configure(config.get());
    if (ret < 0) {
        std::cerr << "[CameraHalRpi5] Camera::configure() failed: " << ret << "\n";
        return false;
    }

    // Store stream pointers
    impl_->streams_[Impl::STREAM_FULL].config  = config->at(0);
    impl_->streams_[Impl::STREAM_FULL].stream  = config->at(0).stream();
    impl_->streams_[Impl::STREAM_MEDIUM].config = config->at(1);
    impl_->streams_[Impl::STREAM_MEDIUM].stream = config->at(1).stream();
    impl_->streams_[Impl::STREAM_LORES].config  = config->at(2);
    impl_->streams_[Impl::STREAM_LORES].stream  = config->at(2).stream();
    impl_->streams_[Impl::STREAM_CORAL].config  = config->at(3);
    impl_->streams_[Impl::STREAM_CORAL].stream  = config->at(3).stream();

    // ── 4. Allocate frame buffers ──────────────────────────────────────────────
    impl_->allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(cam);

    for (auto& sc : impl_->streams_) {
        if (!sc.stream) continue;
        ret = impl_->allocator_->allocate(sc.stream);
        if (ret < 0) {
            std::cerr << "[CameraHalRpi5] FrameBufferAllocator::allocate() failed\n";
            return false;
        }
        sc.buffers = impl_->allocator_->buffers(sc.stream);
        std::cout << "[CameraHalRpi5] Stream allocated " << sc.buffers.size()
                  << " buffers " << sc.config.size.toString()
                  << " (" << sc.config.pixelFormat.toString() << ")\n";
    }

    // ── 5. Create a re-usable request ──────────────────────────────────────────
    // Register all stream buffers with the request
    impl_->request_ = cam->createRequest();
    if (!impl_->request_) {
        std::cerr << "[CameraHalRpi5] createRequest() failed\n";
        return false;
    }

    for (auto& sc : impl_->streams_) {
        if (!sc.stream || sc.buffers.empty()) continue;
        impl_->request_->addBuffer(sc.stream, sc.buffers[0].get());
    }

    // ── 6. Start the camera ────────────────────────────────────────────────────
    ret = cam->start();
    if (ret < 0) {
        std::cerr << "[CameraHalRpi5] Camera::start() failed: " << ret << "\n";
        return false;
    }
    impl_->started = true;

    std::cout << "[CameraHalRpi5] Camera ready: "
              << config->at(0).size.toString() << " NV12"
              << " + " << config->at(1).size.toString() << " NV12"
              << " + " << config->at(2).size.toString() << " NV12"
              << " + " << config->at(3).size.toString() << " RGB888\n";

    initialised_ = true;
    return true;
}

// ─── acquire_frames ───────────────────────────────────────────────────────────

bool CameraHalRpi5::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                    FrameBuffer& lores) {
    if (!initialised_) return false;

    auto& impl = *impl_;
    auto* cam = impl.camera_.get();

    // Queue the request and wait for completion
    cam->queueRequest(impl.request_.get());

    // Wait for the completed request
    auto* cm = impl.cm_.get();
    auto timer = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    libcamera::Request* completed = cm->waitForRequest(timer);
    if (!completed) {
        std::cerr << "[CameraHalRpi5] waitForRequest() timed out\n";
        return false;
    }

    if (completed->status() != libcamera::Request::RequestComplete) {
        std::cerr << "[CameraHalRpi5] Request incomplete, status="
                  << completed->status() << "\n";
        return false;
    }

    // Extract dmabuf fds from completed buffers
    const auto& buffers = completed->buffers();

    for (const auto& [stream, fb] : buffers) {
        if (!fb || fb->planes().empty()) continue;
        const auto& plane = fb->planes()[0];
        int dma_fd = plane.fd.get();

        // Map the dmabuf to userspace for FrameBuffer.data
        size_t size = plane.length;
        void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
        if (data == MAP_FAILED) {
            std::cerr << "[CameraHalRpi5] mmap dmabuf failed\n";
            continue;
        }

        FrameBuffer* target = nullptr;
        uint32_t stride = plane.stride;

        if (stream == impl.streams_[Impl::STREAM_FULL].stream) {
            target = &full;
        } else if (stream == impl.streams_[Impl::STREAM_MEDIUM].stream) {
            target = &medium;
        } else if (stream == impl.streams_[Impl::STREAM_LORES].stream) {
            target = &lores;
        } else if (stream == impl.streams_[Impl::STREAM_CORAL].stream) {
            // Coral input stream — store for inference HAL to consume
            // We use the lores slot since the pipeline expects corals inference
            // input at correspondig resolution.
            // The inference HAL picks this up from the pipeline's FrameBuffer.
            // For now, assign to a spare field — the inference HAL mmaps the fd.
            target = &lores;  // lores_w x lores_h, RGB format
        }

        if (target) {
            target->data      = data;
            target->width     = fb->metadata().width;
            target->height    = fb->metadata().height;
            target->stride    = stride;
            target->size      = static_cast<uint32_t>(size);
            target->timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            target->dma_fd    = dma_fd;
        }
    }

    return true;
}

// ─── release_frames ───────────────────────────────────────────────────────────

void CameraHalRpi5::release_frames() {
    if (!initialised_) return;

    // Unmap all previously mmap'd buffers
    auto unmap_if = [](FrameBuffer& fb) {
        if (fb.data && fb.size > 0) {
            munmap(fb.data, fb.size);
            fb.data = nullptr;
        }
    };

    // We track the last mapped buffers via the FrameBuffer struct values.
    // The pipeline clears these between ticks.
}

// ─── shutdown ─────────────────────────────────────────────────────────────────

void CameraHalRpi5::shutdown() {
    if (!initialised_) return;

    std::cout << "[CameraHalRpi5] Shutdown\n";

    if (impl_->started && impl_->camera_) {
        impl_->camera_->stop();
        impl_->started = false;
    }

    impl_->request_.reset();
    impl_->allocator_.reset();

    if (impl_->camera_) {
        impl_->camera_->release();
        impl_->camera_.reset();
    }

    if (impl_->cm_) {
        impl_->cm_->stop();
        impl_->cm_.reset();
    }

    initialised_ = false;
}

} // namespace ct