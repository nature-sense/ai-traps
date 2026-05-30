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

#include "software_jpeg_encoder.hpp"
#include <iostream>
#include <cstring>
#include <jpeglib.h>

namespace ct {

bool SoftwareJpegEncoder::init(int width, int height, int quality) {
    if (initialized_) {
        std::cerr << "[SoftwareJpegEncoder] Already initialized\n";
        return false;
    }

    if (width <= 0 || height <= 0) {
        std::cerr << "[SoftwareJpegEncoder] Invalid dimensions: "
                  << width << "x" << height << "\n";
        return false;
    }

    if (quality < 1 || quality > 100) {
        std::cerr << "[SoftwareJpegEncoder] Invalid quality: " << quality
                  << " (must be 1-100)\n";
        return false;
    }

    width_ = width;
    height_ = height;
    quality_ = quality;
    initialized_ = true;

    std::cout << "[SoftwareJpegEncoder] Initialized for "
              << width << "x" << height << " JPEG, quality=" << quality << "\n";
    return true;
}

// ─── NV12 → RGB conversion ──────────────────────────────────────────────────
// NV12: Y plane (stride × height) followed by interleaved UV plane (stride × height/2)
// UV plane: Cb (U) at even indices, Cr (V) at odd indices
// Each UV pair covers a 2×2 block of Y pixels (4:2:0 subsampling)
//
// BT.601 full-range conversion:
//   R = Y + 1.402 * (Cr - 128)
//   G = Y - 0.344 * (Cb - 128) - 0.714 * (Cr - 128)
//   B = Y + 1.772 * (Cb - 128)

static inline int clamp_uint8(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

static void nv12_to_rgb_row(const uint8_t* y_plane, const uint8_t* uv_plane,
                             int stride, int width, int y,
                             uint8_t* rgb_row) {
    int uv_y = y / 2;
    const uint8_t* uv_row = uv_plane + uv_y * stride;

    for (int x = 0; x < width; x++) {
        int y_val = y_plane[y * stride + x];

        int uv_x = (x / 2) * 2;  // Align to UV pair boundary
        int cb = static_cast<int>(uv_row[uv_x]) - 128;      // Cb (U)
        int cr = static_cast<int>(uv_row[uv_x + 1]) - 128;  // Cr (V)

        // BT.601 full-range with proper rounding
        int r = y_val + ((1402 * cr + 500) / 1000);
        int g = y_val - ((344 * cb + 500) / 1000) - ((714 * cr + 500) / 1000);
        int b = y_val + ((1772 * cb + 500) / 1000);

        int idx = x * 3;
        rgb_row[idx + 0] = static_cast<uint8_t>(clamp_uint8(r));
        rgb_row[idx + 1] = static_cast<uint8_t>(clamp_uint8(g));
        rgb_row[idx + 2] = static_cast<uint8_t>(clamp_uint8(b));
    }
}

std::vector<uint8_t> SoftwareJpegEncoder::encode(const void* nv12_data, int width, int height, int stride) {
    if (!initialized_) {
        std::cerr << "[SoftwareJpegEncoder] Not initialized\n";
        return {};
    }

    if (!nv12_data || width <= 0 || height <= 0) {
        std::cerr << "[SoftwareJpegEncoder] Invalid frame data\n";
        return {};
    }

    // NV12: Y plane is stride × height, UV plane is stride × height/2
    const uint8_t* y_plane = static_cast<const uint8_t*>(nv12_data);
    const uint8_t* uv_plane = y_plane + stride * height;

    // Set up JPEG compression
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // Output to memory buffer
    unsigned char* jpeg_buffer = nullptr;
    unsigned long jpeg_size = 0;
    jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality_, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    // Convert NV12 → RGB row by row and write
    std::vector<uint8_t> rgb_row(width * 3);

    for (int y = 0; y < height; y++) {
        nv12_to_rgb_row(y_plane, uv_plane, stride, width, y, rgb_row.data());

        JSAMPROW row_ptr = rgb_row.data();
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_compress(&cinfo);

    // Copy result
    std::vector<uint8_t> result;
    if (jpeg_buffer && jpeg_size > 0) {
        result.assign(jpeg_buffer, jpeg_buffer + jpeg_size);
    }

    // Cleanup
    free(jpeg_buffer);
    jpeg_destroy_compress(&cinfo);

    if (result.empty()) {
        std::cerr << "[SoftwareJpegEncoder] Empty JPEG output\n";
    }

    return result;
}

} // namespace ct
