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
#include <jpeglib.h>

namespace ct {

// ─── NV12 ROI extraction ──────────────────────────────────────────────────────
static std::vector<uint8_t> extract_nv12_roi(const void* frame_data,
                                              int frame_stride,
                                              int frame_w, int frame_h,
                                              int x, int y, int w, int h) {
    if (!frame_data || w <= 0 || h <= 0) return {};

    const uint8_t* src = static_cast<const uint8_t*>(frame_data);
    std::vector<uint8_t> roi(static_cast<size_t>(w * h * 3 / 2));

    uint8_t* dst_y = roi.data();
    for (int row = 0; row < h; ++row) {
        const int src_row = y + row;
        if (src_row >= frame_h) break;
        std::memcpy(dst_y + row * w,
                    src + src_row * frame_stride + x,
                    static_cast<size_t>(std::min(w, frame_w - x)));
    }

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

// ─── NV12 → JPEG (libjpeg, CPU) ──────────────────────────────────────────────
static std::vector<uint8_t> nv12_to_jpeg(const uint8_t* nv12, int w, int h,
                                          int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned long jpeg_size = 0;
    unsigned char* jpeg_buf = nullptr;
    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);

    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    const uint8_t* y_plane = nv12;
    const uint8_t* uv_plane = nv12 + w * h;

    std::vector<uint8_t> row_buf(static_cast<size_t>(w) * 3);
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            row_buf[col * 3 + 0] = y_plane[row * w + col];
            int uv_row = row / 2;
            int uv_col = (col / 2) * 2;
            row_buf[col * 3 + 1] = uv_plane[uv_row * w + uv_col];
            row_buf[col * 3 + 2] = uv_plane[uv_row * w + uv_col + 1];
        }
        JSAMPROW row_ptr = row_buf.data();
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_compress(&cinfo);

    std::vector<uint8_t> result;
    if (jpeg_buf && jpeg_size > 0)
        result.assign(jpeg_buf, jpeg_buf + jpeg_size);

    jpeg_destroy_compress(&cinfo);
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
        std::cerr << "[CropperActor] no frame\n";
        return crop;
    }

    const int frame_w = static_cast<int>(pending_frame_.width);
    const int frame_h = static_cast<int>(pending_frame_.height);
    const int stride  = static_cast<int>(pending_frame_.stride);

    const int x = std::max(0, obj.full_x - padding_px);
    const int y = std::max(0, obj.full_y - padding_px);
    const int w = std::min(frame_w - x, obj.full_w + 2 * padding_px);
    const int h = std::min(frame_h - y, obj.full_h + 2 * padding_px);

    if (w <= 0 || h <= 0) {
        std::cerr << "[CropperActor] invalid crop\n";
        return crop;
    }

    // Extract NV12 ROI
    auto roi = extract_nv12_roi(pending_frame_.data, stride,
                                 frame_w, frame_h, x, y, w, h);
    if (roi.empty()) {
        std::cerr << "[CropperActor] ROI extraction failed\n";
        return crop;
    }

    // JPEG encode (CPU via libjpeg)
    crop.data = nv12_to_jpeg(roi.data(), w, h, 85);
    if (crop.data.empty()) {
        std::cerr << "[CropperActor] JPEG encode failed for track "
                  << obj.track_id << "\n";
    }
    return crop;
}

} // namespace ct