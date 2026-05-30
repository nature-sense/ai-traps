#include "camera_hal_native.hpp"
#include <iostream>
#include <cstring>

namespace ct {

bool CameraHalNative::init(const PipelineConfig& cfg) {
    width_  = cfg.camera.full_w;
    height_ = cfg.camera.full_h;
    fps_    = cfg.camera.fps;
    std::cout << "[CameraHalNative] init " << width_ << "x" << height_
              << " @" << fps_ << " fps (stub)\n";
    return true;
}

bool CameraHalNative::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                     FrameBuffer& lores) {
    // Stub: return empty frames
    full.data   = nullptr;
    full.width  = width_;
    full.height = height_;
    full.stride = width_;

    medium.data   = nullptr;
    medium.width  = width_ / 2;
    medium.height = height_ / 2;
    medium.stride = width_ / 2;

    lores.data    = nullptr;
    lores.width   = 320;
    lores.height  = 320;
    lores.stride  = 320;

    return true;
}

void CameraHalNative::release_frames() {
    // No-op in stub
}

void CameraHalNative::shutdown() {
    std::cout << "[CameraHalNative] shutdown (stub)\n";
}

} // namespace ct
