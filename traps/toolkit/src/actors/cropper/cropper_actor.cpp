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

#include "cropper_actor.hpp"

#include <algorithm>
#include <iostream>
#include <cstring>

extern "C" {
#include <jpeglib.h>
}

namespace ct {

// ─── NV12 ROI extraction ──────────────────────────────────────────────────────
// Extracts a rectangular region (x, y, w, h) from an NV12 frame and returns
// it as a contiguous NV12 buffer. The caller owns the returned buffer.
static std::vector<uint8_t> extract_nv12_roi(const void* frame_data,
                                              int frame_stride,
                                              int frame_w, int frame_h,
                                              int x, int y, int w, int h) {
    if (!frame_data || w <= 0 || h <= 0) return {};

    const uint8_t* src = static_cast<const uint8_t*>(frame_data);
    std::vector<uint8_t> roi(static_cast<size_t>(w * h * 3 / 2));

    // Copy Y plane (w × h bytes, row-by-row with stride)
    uint8_t* dst_y = roi.data();
    for (int row = 0; row < h; ++row) {
        const int src_row = y + row;
        if (src_row >= frame_h) break;
        std::memcpy(dst_y + row * w,
                    src + src_row * frame_stride + x,
                    static_cast<size_t>(std::min(w, frame_w - x)));
    }

    // Copy UV plane (w × h/2 bytes, interleaved U/V, subsampled 2x)
    // NV12: UV plane starts at frame_stride * frame_h, each row is frame_stride bytes
    // UV is subsampled 2x horizontally and vertically
    const uint8_t* src_uv = src + frame_stride * frame_h;
    uint8_t* dst_uv = roi.data() + w * h;
    for (int row = 0; row < h / 2; ++row) {
        const int src_row = (y / 2) + row;
        if (src_row >= frame_h / 2) break;
        std::memcpy(dst_uv + row * w,
                    src_uv + src_row * frame_stride + (x & ~1),
                    static_cast<size_t>(std::min(w, frame_w - (x & ~1))));
    }

    return roi;
}

// ─── NV12 → JPEG encoding (libjpeg) ──────────────────────────────────────────
// Converts NV12 to JPEG using the standard libjpeg API.
// NV12 → RGB → JPEG (YCbCr 4:2:0).
static std::vector<uint8_t> nv12_to_jpeg(const uint8_t* nv12_data,
                                          int width, int height) {
    if (!nv12_data || width <= 0 || height <= 0) return {};

    const int y_size = width * height;
    const uint8_t* y_plane  = nv12_data;
    const uint8_t* uv_plane = nv12_data + y_size;

    // ── Convert NV12 → RGB ──────────────────────────────────────────────────
    // Allocate RGB buffer (3 bytes per pixel)
    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int yi = y * width + x;
            const int ui = (y / 2) * width + (x / 2) * 2;  // U at even index
            const int vi = ui + 1;                           // V at odd index

            const int Y  = static_cast<int>(y_plane[yi]);
            const int U  = static_cast<int>(uv_plane[ui]);
            const int V  = static_cast<int>(uv_plane[vi]);

            // BT.601 full-range
            int R = Y + ((1402 * (V - 128)) >> 10);
            int G = Y - ((344 * (U - 128) + 714 * (V - 128)) >> 10);
            int B = Y + ((1772 * (U - 128)) >> 10);

            if (R < 0) R = 0; if (R > 255) R = 255;
            if (G < 0) G = 0; if (G > 255) G = 255;
            if (B < 0) B = 0; if (B > 255) B = 255;

            const int rgb_idx = yi * 3;
            rgb[rgb_idx + 0] = static_cast<uint8_t>(R);
            rgb[rgb_idx + 1] = static_cast<uint8_t>(G);
            rgb[rgb_idx + 2] = static_cast<uint8_t>(B);
        }
    }

    // ── Encode RGB → JPEG ───────────────────────────────────────────────────
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // Write to memory buffer
    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;
    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);

    cinfo.image_width      = static_cast<JDIMENSION>(width);
    cinfo.image_height     = static_cast<JDIMENSION>(height);
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 85, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    // Write rows
    JSAMPROW row_pointer[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb[cinfo.next_scanline * width * 3];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    std::vector<uint8_t> result(jpeg_buf, jpeg_buf + jpeg_size);
    free(jpeg_buf);
    return result;
}

JpegCrop CropperActor::encode_crop(const TrackedObject& obj) {
    JpegCrop crop;
    crop.track_id     = obj.track_id;
    crop.class_id     = obj.detection.class_id;
    crop.confidence   = obj.detection.confidence;
    crop.timestamp_ms = pending_frame_.timestamp_ms;

    if (pending_frame_.data == nullptr) {
        std::cerr << "[CropperActor] no frame available for crop\n";
        return crop;
    }

    // Clamp bbox + padding to frame bounds
    const int frame_w = static_cast<int>(pending_frame_.width);
    const int frame_h = static_cast<int>(pending_frame_.height);
    const int stride  = static_cast<int>(pending_frame_.stride);

    const int x = std::max(0, obj.full_x - padding_px);
    const int y = std::max(0, obj.full_y - padding_px);
    const int w = std::min(frame_w - x, obj.full_w + 2 * padding_px);
    const int h = std::min(frame_h - y, obj.full_h + 2 * padding_px);

    if (w <= 0 || h <= 0) {
        std::cerr << "[CropperActor] invalid crop dimensions: "
                  << x << "," << y << " " << w << "x" << h << "\n";
        return crop;
    }

    // Extract NV12 ROI from the full-res frame
    auto roi_nv12 = extract_nv12_roi(pending_frame_.data, stride,
                                      frame_w, frame_h,
                                      x, y, w, h);
    if (roi_nv12.empty()) {
        std::cerr << "[CropperActor] failed to extract ROI\n";
        return crop;
    }

    // Encode ROI as JPEG
    crop.data = nv12_to_jpeg(roi_nv12.data(), w, h);

    if (crop.data.empty()) {
        std::cerr << "[CropperActor] JPEG encoding failed for track "
                  << obj.track_id << "\n";
    }

    return crop;
}

} // namespace ct

