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
#include <dlfcn.h>

// D-Robotics SP (Smart Platform) camera API function pointers.
// Loaded at runtime via dlopen from libspcdev.so (at /usr/lib/ on board).
// This avoids compile-time link dependency on the RDK X5 SDK libs.
//
// SP API from sp_vio.h:
//   sp_init_vio_module() / sp_release_vio_module()
//   sp_open_camera_v3()  / sp_open_vps()
//   sp_vio_get_frame()   / sp_vio_get_yuv()
//   sp_vio_close()

// NV12 frame size: Y plane (w*h) + UV plane (w*h/2)
#define NV12_FRAME_SIZE(w, h)  ((w) * (h) * 3 / 2)

namespace ct {

// ─── SP API function pointer typedefs ────────────────────────────────────────
typedef void* (*SPInitVioModuleFn)();
typedef void  (*SPReleaseVioModuleFn)(void* obj);
typedef int32_t (*SPOpenCameraV3Fn)(void* obj, int32_t pipe, int32_t video_idx,
    int32_t chn_num, void* params, int32_t* in_w, int32_t* in_h,
    int32_t* crop_x, int32_t* crop_y, int32_t* crop_w, int32_t* crop_h,
    int32_t* rotate);
typedef int32_t (*SPOpenVpsFn)(void* obj, int32_t pipe, int32_t chn_num,
    int32_t proc_mode, int32_t src_w, int32_t src_h,
    int32_t* dst_w, int32_t* dst_h,
    int32_t* crop_x, int32_t* crop_y, int32_t* crop_w, int32_t* crop_h,
    int32_t* rotate);
typedef int32_t (*SPVioGetFrameFn)(void* obj, char* buf, int32_t w, int32_t h,
    int32_t timeout);
typedef int32_t (*SPVioGetYuvFn)(void* obj, char* buf, int32_t w, int32_t h,
    int32_t timeout);
typedef int32_t (*SPVioCloseFn)(void* obj);

// NV12 frame size macro (same as sp_vio.h FRAME_BUFFER_SIZE)
#define SP_HOST_AUTO_DETECT (-1)
#define SP_VPS_SCALE 1

// ─── Global SP API state (loaded once) ───────────────────────────────────────
static struct SpApi {
    void* handle = nullptr;
    SPInitVioModuleFn     sp_init_vio_module  = nullptr;
    SPReleaseVioModuleFn  sp_release_vio_module = nullptr;
    SPOpenCameraV3Fn      sp_open_camera_v3   = nullptr;
    SPOpenVpsFn           sp_open_vps         = nullptr;
    SPVioGetFrameFn       sp_vio_get_frame    = nullptr;
    SPVioGetYuvFn         sp_vio_get_yuv      = nullptr;
    SPVioCloseFn          sp_vio_close        = nullptr;
    bool loaded = false;
} s_sp;

static bool load_sp_api() {
    if (s_sp.loaded) return true;

    s_sp.handle = dlopen("libspcdev.so", RTLD_NOW | RTLD_GLOBAL);
    if (!s_sp.handle) {
        std::cerr << "[CameraHalRdkX5] Failed to load libspcdev.so: "
                  << dlerror() << "\n";
        return false;
    }

    auto load_sym = [](void* h, const char* name) -> void* {
        void* sym = dlsym(h, name);
        if (!sym) {
            std::cerr << "[CameraHalRdkX5] Symbol not found: " << name
                      << " (" << dlerror() << ")\n";
        }
        return sym;
    };

    s_sp.sp_init_vio_module   = reinterpret_cast<SPInitVioModuleFn>(load_sym(s_sp.handle, "sp_init_vio_module"));
    s_sp.sp_release_vio_module = reinterpret_cast<SPReleaseVioModuleFn>(load_sym(s_sp.handle, "sp_release_vio_module"));
    s_sp.sp_open_camera_v3    = reinterpret_cast<SPOpenCameraV3Fn>(load_sym(s_sp.handle, "sp_open_camera_v3"));
    s_sp.sp_open_vps          = reinterpret_cast<SPOpenVpsFn>(load_sym(s_sp.handle, "sp_open_vps"));
    s_sp.sp_vio_get_frame     = reinterpret_cast<SPVioGetFrameFn>(load_sym(s_sp.handle, "sp_vio_get_frame"));
    s_sp.sp_vio_get_yuv       = reinterpret_cast<SPVioGetYuvFn>(load_sym(s_sp.handle, "sp_vio_get_yuv"));
    s_sp.sp_vio_close         = reinterpret_cast<SPVioCloseFn>(load_sym(s_sp.handle, "sp_vio_close"));

    if (!s_sp.sp_init_vio_module || !s_sp.sp_release_vio_module ||
        !s_sp.sp_open_camera_v3 || !s_sp.sp_open_vps ||
        !s_sp.sp_vio_get_frame || !s_sp.sp_vio_get_yuv ||
        !s_sp.sp_vio_close) {
        dlclose(s_sp.handle);
        s_sp.handle = nullptr;
        return false;
    }

    s_sp.loaded = true;
    std::cout << "[CameraHalRdkX5] SP API loaded from libspcdev.so\n";
    return true;
}

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

    // Load SP API at runtime
    if (!load_sp_api()) {
        std::cerr << "[CameraHalRdkX5] SP API loading failed\n";
        return false;
    }

    cfg_ = cfg;

    std::cout << "[CameraHalRdkX5] Initialising RDK X5 camera pipeline\n";
    std::cout << "[CameraHalRdkX5]   Full:   " << cfg.camera.full_w << "x" << cfg.camera.full_h << "\n";
    std::cout << "[CameraHalRdkX5]   Medium: " << cfg.camera.med_w << "x" << cfg.camera.med_h << "\n";
    std::cout << "[CameraHalRdkX5]   Lores:  " << cfg.camera.lores_w << "x" << cfg.camera.lores_h << "\n";
    std::cout << "[CameraHalRdkX5]   FPS:    " << cfg.camera.fps << "\n";

    // 1. Initialise the VIO module (inits VIN→ISP pipeline)
    if (!init_camera_pipeline()) {
        std::cerr << "[CameraHalRdkX5] Camera pipeline initialisation failed\n";
        return false;
    }

    // 2. Initialise VPS channels for multi-stream scaling
    if (!init_vps_channels()) {
        std::cerr << "[CameraHalRdkX5] VPS channel initialisation failed\n";
        shutdown();
        return false;
    }

    initialised_ = true;
    std::cout << "[CameraHalRdkX5] Initialised successfully\n";
    return true;
}

// ─── init_camera_pipeline ─────────────────────────────────────────────────────
bool CameraHalRdkX5::init_camera_pipeline() {
    // 1. Initialise the VIO module
    vio_module_ = s_sp.sp_init_vio_module();
    if (!vio_module_) {
        std::cerr << "[CameraHalRdkX5] sp_init_vio_module() failed\n";
        return false;
    }
    std::cout << "[CameraHalRdkX5] VIO module initialised\n";

    // 2. Configure sensor parameters
    //    On RDK X5, the sensor is auto-detected by the hobot_isi_sensor kernel
    //    module. We provide the desired resolution and FPS.
    //
    // sp_sensors_parameters struct (from sp_vio.h):
    //   { int32_t raw_height; int32_t raw_width; int32_t fps; }
    struct {
        int32_t raw_height;
        int32_t raw_width;
        int32_t fps;
    } sensor_params{};
    sensor_params.raw_height = static_cast<int32_t>(cfg_.camera.full_h);
    sensor_params.raw_width  = static_cast<int32_t>(cfg_.camera.full_w);
    sensor_params.fps        = static_cast<int32_t>(cfg_.camera.fps);

    // 3. Open the camera with V3 API
    //    pipe_id = 0, video_index = SP_HOST_AUTO_DETECT (-1)
    //    chn_num = 1 (single input channel from VIN)
    int32_t input_width  = 0;
    int32_t input_height = 0;
    int32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0, rotate = 0;

    int32_t ret = s_sp.sp_open_camera_v3(
        vio_module_,
        0,                          // pipe_id
        SP_HOST_AUTO_DETECT,        // video_index (auto-detect sensor)
        1,                          // chn_num
        &sensor_params,
        &input_width,
        &input_height,
        &crop_x, &crop_y, &crop_w, &crop_h,
        &rotate);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_open_camera_v3() failed: " << ret << "\n";
        s_sp.sp_release_vio_module(vio_module_);
        vio_module_ = nullptr;
        return false;
    }

    std::cout << "[CameraHalRdkX5] Camera opened: sensor "
              << input_width << "x" << input_height
              << " @" << cfg_.camera.fps << " fps\n";

    // Update full-res to the actual sensor resolution
    if (input_width > 0 && input_height > 0) {
        cfg_.camera.full_w = static_cast<int>(input_width);
        cfg_.camera.full_h = static_cast<int>(input_height);
    }

    // Allocate the full-res VPS output buffer
    if (!alloc_vps_buffer(vps_full_, cfg_.camera.full_w, cfg_.camera.full_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate full-res VPS buffer\n";
        s_sp.sp_vio_close(vio_module_);
        s_sp.sp_release_vio_module(vio_module_);
        vio_module_ = nullptr;
        return false;
    }

    return true;
}

// ─── init_vps_channels ────────────────────────────────────────────────────────
bool CameraHalRdkX5::init_vps_channels() {
    // ── Medium-res VPS channel ──────────────────────────────────────────────
    int32_t med_w = static_cast<int32_t>(cfg_.camera.med_w);
    int32_t med_h = static_cast<int32_t>(cfg_.camera.med_h);
    int32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0, rotate = 0;

    int32_t ret = s_sp.sp_open_vps(
        vio_module_,
        0,                          // pipe_id
        1,                          // chn_num
        SP_VPS_SCALE,               // proc_mode
        cfg_.camera.full_w,
        cfg_.camera.full_h,
        &med_w, &med_h,
        &crop_x, &crop_y, &crop_w, &crop_h,
        &rotate);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_open_vps(medium) failed: " << ret << "\n";
        return false;
    }

    cfg_.camera.med_w = static_cast<int>(med_w);
    cfg_.camera.med_h = static_cast<int>(med_h);

    if (!alloc_vps_buffer(vps_medium_, med_w, med_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate medium VPS buffer\n";
        return false;
    }

    // ── Lores VPS channel ──────────────────────────────────────────────────
    int32_t lo_w = static_cast<int32_t>(cfg_.camera.lores_w);
    int32_t lo_h = static_cast<int32_t>(cfg_.camera.lores_h);

    ret = s_sp.sp_open_vps(
        vio_module_,
        0,                          // pipe_id
        2,                          // chn_num (second VPS channel)
        SP_VPS_SCALE,
        cfg_.camera.full_w,
        cfg_.camera.full_h,
        &lo_w, &lo_h,
        &crop_x, &crop_y, &crop_w, &crop_h,
        &rotate);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_open_vps(lores) failed: " << ret << "\n";
        return false;
    }

    cfg_.camera.lores_w = static_cast<int>(lo_w);
    cfg_.camera.lores_h = static_cast<int>(lo_h);

    if (!alloc_vps_buffer(vps_lores_, lo_w, lo_h)) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate lores VPS buffer\n";
        return false;
    }

    std::cout << "[CameraHalRdkX5] VPS channels initialised:\n";
    std::cout << "[CameraHalRdkX5]   Full:   " << vps_full_.width << "x" << vps_full_.height << "\n";
    std::cout << "[CameraHalRdkX5]   Medium: " << vps_medium_.width << "x" << vps_medium_.height << "\n";
    std::cout << "[CameraHalRdkX5]   Lores:  " << vps_lores_.width << "x" << vps_lores_.height << "\n";

    return true;
}

// ─── alloc_vps_buffer / free_vps_buffer ───────────────────────────────────────
bool CameraHalRdkX5::alloc_vps_buffer(VpsBuffer& buf, int width, int height) {
    uint32_t frame_size = NV12_FRAME_SIZE(width, height);

    buf.data = std::aligned_alloc(4096, frame_size);
    if (!buf.data) {
        std::cerr << "[CameraHalRdkX5] Failed to allocate " << frame_size << " bytes\n";
        return false;
    }

    buf.width  = static_cast<uint32_t>(width);
    buf.height = static_cast<uint32_t>(height);
    buf.size   = frame_size;
    std::memset(buf.data, 0, frame_size);
    return true;
}

void CameraHalRdkX5::free_vps_buffer(VpsBuffer& buf) {
    if (buf.data) {
        std::free(buf.data);
        buf.data = nullptr;
    }
    buf.width = buf.height = buf.size = 0;
}

// ─── acquire_frames ───────────────────────────────────────────────────────────
bool CameraHalRdkX5::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                    FrameBuffer& lores) {
    if (!initialised_) return false;

    // Get full-res frame
    int32_t ret = s_sp.sp_vio_get_frame(
        vio_module_,
        static_cast<char*>(vps_full_.data),
        static_cast<int32_t>(vps_full_.width),
        static_cast<int32_t>(vps_full_.height),
        1000);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_vio_get_frame(full) failed: " << ret << "\n";
        return false;
    }

    // Get medium-res frame from VPS
    ret = s_sp.sp_vio_get_yuv(
        vio_module_,
        static_cast<char*>(vps_medium_.data),
        static_cast<int32_t>(vps_medium_.width),
        static_cast<int32_t>(vps_medium_.height),
        1000);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_vio_get_yuv(medium) failed: " << ret << "\n";
        return false;
    }

    // Get lores frame from VPS
    ret = s_sp.sp_vio_get_yuv(
        vio_module_,
        static_cast<char*>(vps_lores_.data),
        static_cast<int32_t>(vps_lores_.width),
        static_cast<int32_t>(vps_lores_.height),
        1000);

    if (ret != 0) {
        std::cerr << "[CameraHalRdkX5] sp_vio_get_yuv(lores) failed: " << ret << "\n";
        return false;
    }

    // Populate FrameBuffer structs
    frame_full_.data         = vps_full_.data;
    frame_full_.width        = vps_full_.width;
    frame_full_.height       = vps_full_.height;
    frame_full_.stride       = vps_full_.width;
    frame_full_.size         = vps_full_.size;
    frame_full_.timestamp_ms = 0;
    frame_full_.dma_fd       = -1;

    frame_medium_.data         = vps_medium_.data;
    frame_medium_.width        = vps_medium_.width;
    frame_medium_.height       = vps_medium_.height;
    frame_medium_.stride       = vps_medium_.width;
    frame_medium_.size         = vps_medium_.size;
    frame_medium_.timestamp_ms = 0;
    frame_medium_.dma_fd       = -1;

    frame_lores_.data         = vps_lores_.data;
    frame_lores_.width        = vps_lores_.width;
    frame_lores_.height       = vps_lores_.height;
    frame_lores_.stride       = vps_lores_.width;
    frame_lores_.size         = vps_lores_.size;
    frame_lores_.timestamp_ms = 0;
    frame_lores_.dma_fd       = -1;

    full   = frame_full_;
    medium = frame_medium_;
    lores  = frame_lores_;

    return true;
}

// ─── release_frames ───────────────────────────────────────────────────────────
void CameraHalRdkX5::release_frames() {
    // Buffers are persistent; SP pipeline handles re-queue internally.
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void CameraHalRdkX5::shutdown() {
    if (!initialised_) return;

    std::cout << "[CameraHalRdkX5] Shutting down\n";

    if (vio_module_) {
        s_sp.sp_vio_close(vio_module_);
        s_sp.sp_release_vio_module(vio_module_);
        vio_module_ = nullptr;
    }

    free_vps_buffer(vps_full_);
    free_vps_buffer(vps_medium_);
    free_vps_buffer(vps_lores_);

    initialised_ = false;
    std::cout << "[CameraHalRdkX5] Shutdown complete\n";
}

} // namespace ct