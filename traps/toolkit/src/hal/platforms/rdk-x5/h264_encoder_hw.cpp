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

#include "h264_encoder_hw.hpp"

#include <iostream>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>

namespace ct {

// ─── SP Codec API function pointer typedefs ──────────────────────────────────
typedef void* (*SPInitEncoderModuleFn)();
typedef void  (*SPReleaseEncoderModuleFn)(void* obj);
typedef int32_t (*SPStartEncodeFn)(void* obj, int32_t chn, int32_t type,
    int32_t width, int32_t height, int32_t bits);
typedef int32_t (*SPStopEncodeFn)(void* obj);
typedef int32_t (*SPEncoderSetFrameFn)(void* obj, char* buf, int32_t size);
typedef int32_t (*SPEncoderGetStreamFn)(void* obj, char* buf);

#define SP_ENCODER_H264 1

// ─── Global SP codec API state (loaded once) ─────────────────────────────────
static struct SpCodecApi {
    void* handle = nullptr;
    SPInitEncoderModuleFn     sp_init_encoder_module   = nullptr;
    SPReleaseEncoderModuleFn  sp_release_encoder_module = nullptr;
    SPStartEncodeFn           sp_start_encode          = nullptr;
    SPStopEncodeFn            sp_stop_encode           = nullptr;
    SPEncoderSetFrameFn       sp_encoder_set_frame     = nullptr;
    SPEncoderGetStreamFn      sp_encoder_get_stream    = nullptr;
    bool loaded = false;
} s_codec;

static bool load_sp_codec() {
    if (s_codec.loaded) return true;

    // libspcdev.so also provides the codec API (same library as camera)
    s_codec.handle = dlopen("libspcdev.so", RTLD_NOW | RTLD_GLOBAL);
    if (!s_codec.handle) {
        std::cerr << "[SpH264Encoder] Failed to load libspcdev.so: "
                  << dlerror() << "\n";
        return false;
    }

    auto load = [](void* h, const char* name) -> void* {
        void* sym = dlsym(h, name);
        if (!sym)
            std::cerr << "[SpH264Encoder] Symbol not found: " << name << "\n";
        return sym;
    };

    s_codec.sp_init_encoder_module    = reinterpret_cast<SPInitEncoderModuleFn>(load(s_codec.handle, "sp_init_encoder_module"));
    s_codec.sp_release_encoder_module = reinterpret_cast<SPReleaseEncoderModuleFn>(load(s_codec.handle, "sp_release_encoder_module"));
    s_codec.sp_start_encode           = reinterpret_cast<SPStartEncodeFn>(load(s_codec.handle, "sp_start_encode"));
    s_codec.sp_stop_encode            = reinterpret_cast<SPStopEncodeFn>(load(s_codec.handle, "sp_stop_encode"));
    s_codec.sp_encoder_set_frame      = reinterpret_cast<SPEncoderSetFrameFn>(load(s_codec.handle, "sp_encoder_set_frame"));
    s_codec.sp_encoder_get_stream     = reinterpret_cast<SPEncoderGetStreamFn>(load(s_codec.handle, "sp_encoder_get_stream"));

    if (!s_codec.sp_init_encoder_module || !s_codec.sp_release_encoder_module ||
        !s_codec.sp_start_encode || !s_codec.sp_stop_encode ||
        !s_codec.sp_encoder_set_frame || !s_codec.sp_encoder_get_stream) {
        dlclose(s_codec.handle);
        s_codec.handle = nullptr;
        return false;
    }

    s_codec.loaded = true;
    std::cout << "[SpH264Encoder] SP codec API loaded from libspcdev.so\n";
    return true;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
SpH264Encoder::SpH264Encoder() = default;

SpH264Encoder::~SpH264Encoder() noexcept {
    deinit();
}

// ─── init ─────────────────────────────────────────────────────────────────────
int SpH264Encoder::init(int width, int height, int qp) {
    if (initialised_) {
        std::cerr << "[SpH264Encoder] Already initialised\n";
        return 0;
    }

    // Load SP codec API at runtime
    if (!load_sp_codec()) {
        std::cerr << "[SpH264Encoder] SP codec API loading failed\n";
        return -1;
    }

    width_  = width;
    height_ = height;

    std::cout << "[SpH264Encoder] Initialising HW H.264 encoder: "
              << width << "x" << height << " qp=" << qp << "\n";

    // Initialise encoder module
    module_ = s_codec.sp_init_encoder_module();
    if (!module_) {
        std::cerr << "[SpH264Encoder] sp_init_encoder_module() failed\n";
        return -1;
    }

    // Convert QP to bitrate: rough estimate for good quality
    // QP 26 ≈ ~4 Mbps for 1080p30; scale by resolution
    int32_t bitrate = 4000000;
    if (width * height > 0) {
        bitrate = static_cast<int32_t>(4000000 * (static_cast<double>(width * height) / (1920.0 * 1080.0)));
    }

    // Start hardware encoder
    // chn=0, type=SP_ENCODER_H264, width, height, bitrate
    int32_t ret = s_codec.sp_start_encode(
        module_,
        0,                      // chn
        SP_ENCODER_H264,        // type
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        bitrate);

    if (ret != 0) {
        std::cerr << "[SpH264Encoder] sp_start_encode() failed: " << ret << "\n";
        s_codec.sp_release_encoder_module(module_);
        module_ = nullptr;
        return -1;
    }

    initialised_ = true;
    std::cout << "[SpH264Encoder] HW encoder started (bitrate=" << bitrate << ")\n";
    return 0;
}

// ─── encode ───────────────────────────────────────────────────────────────────
std::vector<uint8_t> SpH264Encoder::encode(int dma_fd, uint32_t size) {
    if (!initialised_ || !module_ || dma_fd < 0) {
        return {};
    }

    uint32_t frame_size = static_cast<uint32_t>(size > 0 ? size : width_ * height_ * 3 / 2);

    // mmap the dmabuf for the SP encoder API (which expects a CPU pointer)
    // TODO: if SP API supports dmabuf import, switch to zero-copy
    void* mapped = mmap(nullptr, frame_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << "[SpH264Encoder] mmap dmabuf failed\n";
        return {};
    }

    // Push NV12 frame into encoder
    int32_t ret = s_codec.sp_encoder_set_frame(
        module_,
        static_cast<char*>(mapped),
        static_cast<int32_t>(frame_size));

    munmap(mapped, frame_size);

    if (ret != 0) {
        std::cerr << "[SpH264Encoder] sp_encoder_set_frame() failed: " << ret << "\n";
        return {};
    }

    // Allocate stream buffer (128KB should be enough for keyframe)
    std::vector<uint8_t> stream;
    stream.resize(256 * 1024, 0);

    // Pull encoded H.264 stream
    ret = s_codec.sp_encoder_get_stream(
        module_,
        reinterpret_cast<char*>(stream.data()));

    if (ret > 0) {
        // ret is the encoded stream size
        stream.resize(static_cast<size_t>(ret));
        return stream;
    }

    if (ret != 0) {
        std::cerr << "[SpH264Encoder] sp_encoder_get_stream() failed: " << ret << "\n";
    }

    return {};
}

// ─── deinit ───────────────────────────────────────────────────────────────────
void SpH264Encoder::deinit() {
    if (!initialised_) return;

    std::cout << "[SpH264Encoder] Shutting down HW encoder\n";

    if (module_) {
        s_codec.sp_stop_encode(module_);
        s_codec.sp_release_encoder_module(module_);
        module_ = nullptr;
    }

    initialised_ = false;
}

} // namespace ct