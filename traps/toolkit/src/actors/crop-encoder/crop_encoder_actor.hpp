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

#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include "jpeg-encoder/hardware_jpeg_encoder.hpp"
#include "jpeg-encoder/software_jpeg_encoder.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace ct {

// ─── CropEncoderActor ────────────────────────────────────────────────────
// Receives ClassifiedTrack results from ClassifierActor, crops the NV12 ROI
// from the full-res frame, encodes as JPEG, and emits JpegCrop structs.
//
// This replaces the CropperActor's JPEG encoding step in the classification
// pipeline. It does NOT do best-shot tracking — that's handled by BestShotKeeper.
//
// Wiring:
//   classifier.out_classified >> crop_encoder.in_classified
//   crop_encoder.out_crops    >> classified_storage.in_crops
//
struct CropEncoderActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    ramen::Pushable<ClassifiedTrack> in_classified =
        [this](const ClassifiedTrack& ct) {
            encode(ct);
        };

    // ── Outputs ───────────────────────────────────────────────────────────────
    ramen::Pusher<JpegCrop> out_crops{};

    // ── Config ────────────────────────────────────────────────────────────────
    int crop_padding_px = 16;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool init(bool use_hardware_encoder = false) {
        if (use_hardware_encoder) {
            hw_encoder_ = std::make_unique<HardwareJpegEncoder>();
            if (hw_encoder_->init()) {
                std::cout << "[CropEncoderActor] using hardware JPEG encoder\n";
                return true;
            }
            std::cerr << "[CropEncoderActor] hardware encoder failed, falling back to software\n";
            hw_encoder_.reset();
        }
        sw_encoder_ = std::make_unique<SoftwareJpegEncoder>();
        std::cout << "[CropEncoderActor] using software JPEG encoder\n";
        return true;
    }

    // PipelineConfig-based init.
    bool init(const PipelineConfig& cfg) {
        (void)cfg;
        return init(false);  // default to software encoder
    }

private:
    void encode(const ClassifiedTrack& ct) {
        const auto& frame = ct.frame;
        if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
            return;
        }

        // Compute padded crop region
        int frame_w = static_cast<int>(frame.width);
        int frame_h = static_cast<int>(frame.height);

        int crop_x = std::max(0, ct.bbox_x - crop_padding_px);
        int crop_y = std::max(0, ct.bbox_y - crop_padding_px);
        int crop_w = std::min(ct.bbox_w + 2 * crop_padding_px, frame_w - crop_x);
        int crop_h = std::min(ct.bbox_h + 2 * crop_padding_px, frame_h - crop_y);

        if (crop_w <= 0 || crop_h <= 0) return;

        // Extract NV12 crop from the full frame
        std::vector<uint8_t> nv12_crop(crop_w * crop_h * 3 / 2);

        // Copy Y plane
        for (int row = 0; row < crop_h; ++row) {
            int src_row = crop_y + row;
            const uint8_t* src = frame.data.data() + src_row * frame_w + crop_x;
            uint8_t* dst = nv12_crop.data() + row * crop_w;
            std::memcpy(dst, src, crop_w);
        }

        // Copy UV plane (NV12: interleaved U/V at half resolution)
        int uv_src_offset = frame_w * frame_h;
        int uv_dst_offset = crop_w * crop_h;
        for (int row = 0; row < crop_h / 2; ++row) {
            int src_row = crop_y / 2 + row;
            const uint8_t* src = frame.data.data() + uv_src_offset + src_row * frame_w + (crop_x / 2) * 2;
            uint8_t* dst = nv12_crop.data() + uv_dst_offset + row * crop_w;
            std::memcpy(dst, src, crop_w);
        }

        // Encode to JPEG
        std::vector<uint8_t> jpeg_data;
        if (hw_encoder_) {
            jpeg_data = hw_encoder_->encode(nv12_crop, crop_w, crop_h);
        } else if (sw_encoder_) {
            jpeg_data = sw_encoder_->encode(nv12_crop, crop_w, crop_h);
        }

        if (jpeg_data.empty()) {
            std::cerr << "[CropEncoderActor] JPEG encoding failed for track "
                      << ct.track_id << "\n";
            return;
        }

        // Build JpegCrop output
        JpegCrop crop;
        crop.track_id = ct.track_id;
        crop.class_id = ct.classification.class_id;
        crop.confidence = ct.classification.confidence;
        crop.timestamp_ms = ct.classification.timestamp_ms;
        crop.data = std::move(jpeg_data);
        crop.is_update = false;  // Classifications are always new (no update cycle)

        out_crops.push(crop);
    }

    std::unique_ptr<HardwareJpegEncoder> hw_encoder_;
    std::unique_ptr<SoftwareJpegEncoder> sw_encoder_;
};

} // namespace ct
