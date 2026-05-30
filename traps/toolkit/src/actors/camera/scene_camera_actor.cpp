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

#include "scene_camera_actor.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <regex>
#include <map>

// PNG decoder
#include <png.h>

namespace ct {

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ─── PNG → RGBA decoder ──────────────────────────────────────────────────────
// Decodes a PNG file from memory to RGBA pixel data.
// Returns true on success, with out_rgba, out_w, out_h populated.
static bool decode_png_to_rgba(const std::vector<uint8_t>& png_data,
                                std::vector<uint8_t>& out_rgba,
                                int& out_w, int& out_h) {
    // Check PNG signature
    if (png_sig_cmp(png_data.data(), 0, 8) != 0) {
        std::cerr << "[SceneCameraActor] not a valid PNG file\n";
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                  nullptr, nullptr, nullptr);
    if (!png_ptr) return false;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return false;
    }

    // Set up PNG read from memory
    struct ReadCtx { const uint8_t* data; size_t size; size_t pos; };
    ReadCtx ctx{png_data.data(), png_data.size(), 0};

    png_set_read_fn(png_ptr, &ctx, [](png_structp p, png_bytep out, png_size_t len) {
        auto* c = static_cast<ReadCtx*>(png_get_io_ptr(p));
        size_t avail = std::min(len, c->size - c->pos);
        std::memcpy(out, c->data + c->pos, avail);
        c->pos += avail;
    });

    png_read_info(png_ptr, info_ptr);

    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = -1;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth,
                 &color_type, nullptr, nullptr, nullptr);

    // Convert to 8-bit RGBA
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    out_w = static_cast<int>(width);
    out_h = static_cast<int>(height);
    out_rgba.resize(static_cast<size_t>(width) * height * 4);

    // Read rows
    std::vector<png_bytep> row_ptrs(height);
    for (png_uint_32 y = 0; y < height; ++y)
        row_ptrs[y] = out_rgba.data() + y * width * 4;

    png_read_image(png_ptr, row_ptrs.data());
    png_read_end(png_ptr, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return true;
}

// ─── RGBA → NV12 conversion ──────────────────────────────────────────────────
// Converts RGBA pixel data to NV12 format (Y plane + interleaved UV plane).
static void rgba_to_nv12(const uint8_t* rgba, int w, int h,
                          std::vector<uint8_t>& out_nv12) {
    const int y_size = w * h;
    const int uv_size = w * (h / 2);
    out_nv12.resize(y_size + uv_size);

    uint8_t* y_plane = out_nv12.data();
    uint8_t* uv_plane = out_nv12.data() + y_size;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* pixel = rgba + (y * w + x) * 4;
            uint8_t r = pixel[0];
            uint8_t g = pixel[1];
            uint8_t b = pixel[2];

            // BT.601 Y
            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y_plane[y * w + x] = static_cast<uint8_t>(std::clamp(Y, 16, 235));

            // UV at half resolution (subsampled 2x2)
            if (y % 2 == 0 && x % 2 == 0) {
                // Average 2x2 block for U/V
                int cb_sum = 0, cr_sum = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int yy = y + dy;
                        int xx = x + dx;
                        if (yy >= h || xx >= w) continue;
                        const uint8_t* p = rgba + (yy * w + xx) * 4;
                        // BT.601 Cb/Cr
                        cb_sum += ((-38 * p[0] - 74 * p[1] + 112 * p[2] + 128) >> 8) + 128;
                        cr_sum += ((112 * p[0] - 94 * p[1] - 18 * p[2] + 128) >> 8) + 128;
                    }
                }
                int uv_idx = (y / 2) * w + x;
                uv_plane[uv_idx]     = static_cast<uint8_t>(std::clamp((cb_sum + 2) / 4, 16, 240));
                uv_plane[uv_idx + 1] = static_cast<uint8_t>(std::clamp((cr_sum + 2) / 4, 16, 240));
            }
        }
    }
}

// ─── NV12 letterbox scaling ──────────────────────────────────────────────────
static void nv12_letterbox(const uint8_t* src, int src_w, int src_h,
                            uint8_t* dst, int dst_w, int dst_h) {
    const float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
    const float dst_aspect = static_cast<float>(dst_w) / static_cast<float>(dst_h);

    int scaled_w, scaled_h;
    int pad_top = 0, pad_left = 0;

    if (src_aspect > dst_aspect) {
        scaled_w = dst_w;
        scaled_h = static_cast<int>(dst_w / src_aspect);
        pad_top = (dst_h - scaled_h) / 2;
    } else {
        scaled_h = dst_h;
        scaled_w = static_cast<int>(dst_h * src_aspect);
        pad_left = (dst_w - scaled_w) / 2;
    }

    scaled_w &= ~1;
    scaled_h &= ~1;
    pad_top &= ~1;
    pad_left &= ~1;

    const int dst_y_size = dst_w * dst_h;
    const int dst_uv_size = dst_w * (dst_h / 2);

    std::memset(dst, 16, dst_y_size);
    std::memset(dst + dst_y_size, 128, dst_uv_size);

    const int src_y_size = src_w * src_h;

    for (int dy = 0; dy < scaled_h; ++dy) {
        int sy = dy * src_h / scaled_h;
        const uint8_t* src_row = src + sy * src_w;
        uint8_t* dst_row = dst + (pad_top + dy) * dst_w + pad_left;
        for (int dx = 0; dx < scaled_w; ++dx) {
            int sx = dx * src_w / scaled_w;
            dst_row[dx] = src_row[sx];
        }
    }

    const int src_uv_h = src_h / 2;
    const int dst_uv_h = dst_h / 2;
    const int scaled_uv_h = scaled_h / 2;

    for (int dy = 0; dy < scaled_uv_h; ++dy) {
        int sy = dy * src_uv_h / scaled_uv_h;
        const uint8_t* src_row = src + src_y_size + sy * src_w;
        uint8_t* dst_row = dst + dst_y_size + (pad_top / 2 + dy) * dst_w + pad_left;
        for (int dx = 0; dx < scaled_w; dx += 2) {
            int sx = dx * src_w / scaled_w;
            dst_row[dx + 0] = src_row[sx + 0];
            dst_row[dx + 1] = src_row[sx + 1];
        }
    }
}

// ─── NV12 nearest-neighbour scaling ──────────────────────────────────────────
static void nv12_scale_nearest(const uint8_t* src, int src_w, int src_h,
                                uint8_t* dst, int dst_w, int dst_h) {
    const int src_y_size = src_w * src_h;
    const int dst_y_size = dst_w * dst_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = dy * src_h / dst_h;
        const uint8_t* src_row = src + sy * src_w;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = dx * src_w / dst_w;
            dst[dy * dst_w + dx] = src_row[sx];
        }
    }

    const int src_uv_stride = src_w;
    const int src_uv_h = src_h / 2;
    const int dst_uv_stride = dst_w;
    const int dst_uv_h = dst_h / 2;

    for (int dy = 0; dy < dst_uv_h; ++dy) {
        int sy = dy * src_uv_h / dst_uv_h;
        const uint8_t* src_row = src + src_y_size + sy * src_uv_stride;
        uint8_t* dst_row = dst + dst_y_size + dy * dst_uv_stride;
        for (int dx = 0; dx < dst_w; dx += 2) {
            int sx = dx * src_w / dst_w;
            dst_row[dx + 0] = src_row[sx + 0];
            dst_row[dx + 1] = src_row[sx + 1];
        }
    }
}

// ─── load_frame_file ─────────────────────────────────────────────────────────
// Decode a single PNG file to NV12 and scale to all three resolutions.
bool SceneCameraActor::load_frame_file(const std::filesystem::path& path,
                                        SceneFrame& out) {
    // Read PNG file
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "[SceneCameraActor] cannot open: " << path << "\n";
        return false;
    }
    auto file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> png_data(file_size);
    f.read(reinterpret_cast<char*>(png_data.data()), file_size);
    if (!f) {
        std::cerr << "[SceneCameraActor] failed to read: " << path << "\n";
        return false;
    }

    // Decode PNG → RGBA
    int decoded_w = 0, decoded_h = 0;
    std::vector<uint8_t> rgba;
    if (!decode_png_to_rgba(png_data, rgba, decoded_w, decoded_h)) {
        std::cerr << "[SceneCameraActor] failed to decode PNG: " << path << "\n";
        return false;
    }

    out.full_w = decoded_w;
    out.full_h = decoded_h;

    // Convert RGBA → NV12 at native resolution
    std::vector<uint8_t> nv12_native;
    rgba_to_nv12(rgba.data(), decoded_w, decoded_h, nv12_native);

    // Scale to full resolution (maintaining aspect ratio via letterbox)
    const int full_size = cfg_.camera.full_w * cfg_.camera.full_h * 3 / 2;
    out.nv12_full.resize(full_size);
    nv12_letterbox(nv12_native.data(), decoded_w, decoded_h,
                   out.nv12_full.data(), cfg_.camera.full_w, cfg_.camera.full_h);

    // Scale to medium (nearest-neighbour, no letterbox needed for display)
    const int medium_size = cfg_.camera.med_w * cfg_.camera.med_h * 3 / 2;
    out.nv12_medium.resize(medium_size);
    nv12_scale_nearest(out.nv12_full.data(), cfg_.camera.full_w, cfg_.camera.full_h,
                       out.nv12_medium.data(), cfg_.camera.med_w, cfg_.camera.med_h);

    // Scale to lores with letterbox (maintain aspect ratio for YOLO)
    const int lores_size = cfg_.camera.lores_w * cfg_.camera.lores_h * 3 / 2;
    out.nv12_lores.resize(lores_size);
    nv12_letterbox(out.nv12_full.data(), cfg_.camera.full_w, cfg_.camera.full_h,
                   out.nv12_lores.data(), cfg_.camera.lores_w, cfg_.camera.lores_h);

    return true;
}

// ─── load_scene ──────────────────────────────────────────────────────────────
// Scan the scene directory for frame_*.png files, sort by frame number,
// and pre-decode all frames.
bool SceneCameraActor::load_scene(const std::string& scene_dir) {
    namespace fs = std::filesystem;

    fs::path dir(scene_dir);
    if (!fs::is_directory(dir)) {
        std::cerr << "[SceneCameraActor] scene directory not found: "
                  << scene_dir << "\n";
        return false;
    }

    // Regex to match frame_NNNN.png
    std::regex frame_re(R"(frame_(\d+)\.(?:png)$)", std::regex::icase);

    // Collect matching files with their frame numbers
    std::map<int, fs::path> sorted_frames;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, frame_re)) {
            int frame_num = std::stoi(match[1].str());
            sorted_frames[frame_num] = entry.path();
        }
    }

    if (sorted_frames.empty()) {
        std::cerr << "[SceneCameraActor] no frame_*.png files found in: "
                  << scene_dir << "\n";
        return false;
    }

    std::cout << "[SceneCameraActor] found " << sorted_frames.size()
              << " frames in " << scene_dir << "\n";

    // Pre-decode all frames
    frames_.reserve(sorted_frames.size());
    for (const auto& [num, path] : sorted_frames) {
        SceneFrame frame;
        if (!load_frame_file(path, frame)) {
            std::cerr << "[SceneCameraActor] failed to load frame "
                      << path.filename() << " — aborting\n";
            return false;
        }
        frames_.push_back(std::move(frame));
    }

    std::cout << "[SceneCameraActor] pre-decoded " << frames_.size()
              << " frames (" << frames_.front().full_w << "x"
              << frames_.front().full_h << " native)\n";
    return true;
}

// ─── init ────────────────────────────────────────────────────────────────────

bool SceneCameraActor::init(const PipelineConfig& cfg) {
    cfg_ = cfg;

    scene_dir_ = cfg_.camera.scene_dir;
    if (scene_dir_.empty()) {
        std::cerr << "[SceneCameraActor] no scene_dir configured. "
                  << "Set camera.scene_dir in config.toml\n";
        return false;
    }

    // Load and pre-decode all frames
    if (!load_scene(scene_dir_)) {
        return false;
    }

    // Initialise FrameBuffer views for frame 0

    const int64_t ts = now_ms();

    frame_full_ = FrameBuffer{

        frames_[0].nv12_full.data(),
        static_cast<uint32_t>(cfg_.camera.full_w),
        static_cast<uint32_t>(cfg_.camera.full_h),
        static_cast<uint32_t>(cfg_.camera.full_w),
        static_cast<uint32_t>(frames_[0].nv12_full.size()),
        ts
    };

    frame_medium_ = FrameBuffer{

        frames_[0].nv12_medium.data(),
        static_cast<uint32_t>(cfg_.camera.med_w),
        static_cast<uint32_t>(cfg_.camera.med_h),
        static_cast<uint32_t>(cfg_.camera.med_w),
        static_cast<uint32_t>(frames_[0].nv12_medium.size()),
        ts
    };

    frame_lores_ = FrameBuffer{

        frames_[0].nv12_lores.data(),
        static_cast<uint32_t>(cfg_.camera.lores_w),
        static_cast<uint32_t>(cfg_.camera.lores_h),
        static_cast<uint32_t>(cfg_.camera.lores_w),
        static_cast<uint32_t>(frames_[0].nv12_lores.size()),
        ts
    };

    initialised_ = true;
    std::cout << "[SceneCameraActor] ready\n"
              << "  scene:  " << scene_dir_ << "\n"
              << "  frames: " << frames_.size() << "\n"
              << "  full:   " << cfg_.camera.full_w << "x" << cfg_.camera.full_h << "\n"
              << "  medium: " << cfg_.camera.med_w << "x" << cfg_.camera.med_h << "\n"
              << "  lores:  " << cfg_.camera.lores_w << "x" << cfg_.camera.lores_h << "\n"
              << "  fps:    " << cfg_.camera.fps << "\n";
    return true;
}

// ─── tick ─────────────────────────────────────────────────────────────────────

void SceneCameraActor::tick() {
    if (!initialised_ || frames_.empty()) return;

    const int64_t ts = now_ms();
    ++frame_count_;

    // Point FrameBuffer views at the current frame's buffers

    frame_full_.data       = frames_[current_frame_].nv12_full.data();
    frame_full_.size       = static_cast<uint32_t>(frames_[current_frame_].nv12_full.size());
    frame_full_.timestamp_ms = ts;

    frame_medium_.data       = frames_[current_frame_].nv12_medium.data();
    frame_medium_.size       = static_cast<uint32_t>(frames_[current_frame_].nv12_medium.size());
    frame_medium_.timestamp_ms = ts;

    frame_lores_.data       = frames_[current_frame_].nv12_lores.data();
    frame_lores_.size       = static_cast<uint32_t>(frames_[current_frame_].nv12_lores.size());
    frame_lores_.timestamp_ms = ts;

    // Push into pipeline
    out_frame_full(frame_full_);
    out_frame_medium(frame_medium_);
    out_frame_lores(frame_lores_);

    // Advance to next frame, looping back to 0 when the sequence ends
    ++current_frame_;
    if (current_frame_ >= frames_.size()) {
        current_frame_ = 0;
        std::cout << "[SceneCameraActor] loop complete (" << frames_.size()
                  << " frames) — restarting from frame 0\n";
        // Notify the pipeline to reset tracker state so track IDs don't
        // accumulate across scene loops (insects jump to different positions).
        if (on_loop) {
            on_loop();
        }
    }

}

// ─── shutdown ────────────────────────────────────────────────────────────────

void SceneCameraActor::shutdown() {
    if (!initialised_) return;

    frames_.clear();
    current_frame_ = 0;

    initialised_ = false;
    std::cout << "[SceneCameraActor] shutdown complete ("
              << frame_count_ << " frames served)\n";
}

} // namespace ct
