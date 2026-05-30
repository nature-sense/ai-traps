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
#include "hal/api/classifier_hal.hpp"
#include <memory>
#include <vector>
#include <cstdint>
#include <iostream>

namespace ct {

// ─── ClassifierActor ──────────────────────────────────────────────────────────
// Receives BestShot structs from BestShotKeeper and runs the classifier model
// on the best detection crop.
//
// Processing steps:
//   1. Extract NV12 ROI from the full-res frame using the bbox
//   2. Convert NV12 ROI → RGB
//   3. Resize to classifier input size (configurable, default 224×224)
//   4. Run IClassifierHAL::classify() on the resized RGB buffer
//   5. Emit the ClassifiedTrack result
//
// Wiring:
//   best_shot_keeper.out_best >> classifier.in_best_shot
//   classifier.out_classified >> crop_encoder.in_classified
//
struct ClassifierActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    ramen::Pushable<BestShot> in_best_shot =
        [this](const BestShot& shot) {
            classify(shot);
        };

    // ── Outputs ───────────────────────────────────────────────────────────────
    ramen::Pusher<ClassifiedTrack> out_classified{};

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Takes ownership of the IClassifierHAL instance.
    bool init(std::unique_ptr<IClassifierHAL> hal,
              const std::string& model_path,
              float confidence_threshold,
              int input_w = 224,
              int input_h = 224)
    {
        hal_ = std::move(hal);
        if (!hal_) {
            std::cerr << "[ClassifierActor] no HAL provided\n";
            return false;
        }
        if (!hal_->init(model_path, confidence_threshold)) {
            std::cerr << "[ClassifierActor] HAL init failed\n";
            return false;
        }
        input_width_ = input_w;
        input_height_ = input_h;
        std::cout << "[ClassifierActor] ready (model=" << model_path
                  << ", input=" << input_w << "x" << input_h << ")\n";
        return true;
    }

    // PipelineConfig-based init (no HAL — subclass must set hal_ before calling).
    // This is used by ClassifiedPipeline::init() which wires up the actor
    // after the platform-specific subclass has created the HAL.
    bool init(const PipelineConfig& cfg) {
        input_width_ = cfg.classifier_input_width;
        input_height_ = cfg.classifier_input_height;
        std::cout << "[ClassifierActor] configured from PipelineConfig (input="
                  << input_width_ << "x" << input_height_ << ")\n";
        return true;
    }

    void shutdown() {
        if (hal_) hal_->shutdown();
    }

    // ── Query ─────────────────────────────────────────────────────────────────
    int64_t last_classification_us() const {
        return hal_ ? hal_->last_classification_us() : 0;
    }

    IClassifierHAL& hal() { return *hal_; }

private:
    void classify(const BestShot& shot) {
        if (!hal_) return;

        // Clamp bbox to frame dimensions
        int frame_w = static_cast<int>(shot.frame.width);
        int frame_h = static_cast<int>(shot.frame.height);
        int x = std::max(0, shot.bbox_x);
        int y = std::max(0, shot.bbox_y);
        int w = std::min(shot.bbox_w, frame_w - x);
        int h = std::min(shot.bbox_h, frame_h - y);

        if (w <= 0 || h <= 0) {
            std::cerr << "[ClassifierActor] invalid bbox for track " << shot.track_id << "\n";
            return;
        }

        // Allocate RGB buffer for the resized classifier input
        int rgb_size = input_width_ * input_height_ * 3;
        std::vector<uint8_t> rgb_buf(rgb_size);

        // Convert NV12 ROI → RGB and resize to classifier input size
        // This is a simple bilinear resize + NV12-to-RGB conversion.
        // On Rockchip platforms, this could be accelerated with RGA.
        if (!nv12_to_rgb_resize(shot.frame.data.data(),
                                frame_w, frame_h,
                                x, y, w, h,
                                rgb_buf.data(),
                                input_width_, input_height_))
        {
            std::cerr << "[ClassifierActor] NV12→RGB conversion failed for track "
                      << shot.track_id << "\n";
            return;
        }

        // Run classifier
        Classification result = hal_->classify(rgb_buf.data(),
                                                input_width_,
                                                input_height_,
                                                shot.timestamp_ms);

        // Build output
        ClassifiedTrack ct;
        ct.track_id = shot.track_id;
        ct.classification = result;
        ct.detection_confidence = shot.confidence;
        ct.detection_class_id = shot.class_id;
        ct.bbox_x = shot.bbox_x;
        ct.bbox_y = shot.bbox_y;
        ct.bbox_w = shot.bbox_w;
        ct.bbox_h = shot.bbox_h;
        ct.frame = shot.frame;

        out_classified.push(ct);
    }

    // Convert NV12 ROI to RGB with resize.
    // Extracts the region [x, y, w, h] from an NV12 frame of size (frame_w, frame_h),
    // converts to RGB, and resizes to (out_w, out_h).
    static bool nv12_to_rgb_resize(const uint8_t* nv12,
                                    int frame_w, int frame_h,
                                    int x, int y, int w, int h,
                                    uint8_t* rgb_out,
                                    int out_w, int out_h)
    {
        if (!nv12 || !rgb_out || w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) {
            return false;
        }

        // Simple bilinear resize from NV12 ROI to RGB output.
        // For each output pixel, map back to source coordinates in the ROI,
        // sample the NV12 data, and convert YUV to RGB.
        const int uv_offset = frame_w * frame_h;

        for (int oy = 0; oy < out_h; ++oy) {
            for (int ox = 0; ox < out_w; ++ox) {
                // Map to source ROI coordinates (floating point)
                float sx_f = x + (static_cast<float>(ox) / out_w) * w;
                float sy_f = y + (static_cast<float>(oy) / out_h) * h;

                // Clamp to ROI bounds
                sx_f = std::max(static_cast<float>(x), std::min(sx_f, static_cast<float>(x + w - 1)));
                sy_f = std::max(static_cast<float>(y), std::min(sy_f, static_cast<float>(y + h - 1)));

                int sx = static_cast<int>(sx_f);
                int sy = static_cast<int>(sy_f);

                // Sample Y
                int y_val = nv12[sy * frame_w + sx];

                // Sample UV (NV12: interleaved U/V at half resolution)
                int uv_x = sx / 2;
                int uv_y = sy / 2;
                int uv_idx = uv_offset + uv_y * frame_w + uv_x * 2;
                int u_val = nv12[uv_idx] - 128;
                int v_val = nv12[uv_idx + 1] - 128;

                // YUV to RGB (BT.601)
                int r = y_val + (v_val * 1436) / 1024;
                int g = y_val - (u_val * 352) / 1024 - (v_val * 731) / 1024;
                int b = y_val + (u_val * 1814) / 1024;

                // Clamp to [0, 255]
                r = std::max(0, std::min(255, r));
                g = std::max(0, std::min(255, g));
                b = std::max(0, std::min(255, b));

                int out_idx = (oy * out_w + ox) * 3;
                rgb_out[out_idx + 0] = static_cast<uint8_t>(r);
                rgb_out[out_idx + 1] = static_cast<uint8_t>(g);
                rgb_out[out_idx + 2] = static_cast<uint8_t>(b);
            }
        }

        return true;
    }

    std::unique_ptr<IClassifierHAL> hal_;
    int input_width_ = 224;
    int input_height_ = 224;
};

} // namespace ct
