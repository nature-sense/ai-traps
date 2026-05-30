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

#include "camera_actor.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <functional>


namespace ct {

// ─── SceneCameraActor ─────────────────────────────────────────────────────────
// Simulated camera actor that reads a sequence of PNG images from a directory
// and feeds them through the pipeline one frame per tick, advancing through the
// sequence.  This enables deterministic testing of tracking, multiple detections,
// and session lifecycle without a real camera.
//
// Directory format (e.g. test_scenes/moth_crossing/):
//   scene.toml           # Optional: [scene] fps=15, description="..."
//   frame_0000.png       # Frame 0
//   frame_0001.png       # Frame 1
//   ...
//
// Files named frame_*.png are sorted numerically by the extracted frame number.
// When the sequence ends, it loops back to frame 0.
//
// All frames are pre-decoded to NV12 and pre-scaled to full/medium/lores
// during init().  tick() is extremely fast — just advancing an index pointer
// and pushing frames.
//
// Usage in config.toml:
//   [camera]
//   model = "scene"
//   scene_dir = "test_scenes/moth_crossing"
struct SceneCameraActor : CameraActor {
    bool init(const PipelineConfig& cfg) override;
    void shutdown() override;
    void tick() override;

    const FrameBuffer& last_lores() const override { return frame_lores_; }

    const PipelineConfig& cfg() const override { return cfg_; }

    void aiq_pause() override {}   // No AIQ on simulated camera
    void aiq_resume() override {}  // No AIQ on simulated camera

    // Callback invoked when the scene loops back to frame 0.
    // The pipeline wires this to reset the tracker so that track IDs don't
    // accumulate across scene loops (which would cause track ID churn when
    // insects jump to different positions on loop).
    std::function<void()> on_loop;

private:

    // One pre-decoded frame at all three resolutions
    struct SceneFrame {
        std::vector<uint8_t> nv12_full;
        std::vector<uint8_t> nv12_medium;
        std::vector<uint8_t> nv12_lores;
        int full_w = 0;
        int full_h = 0;
    };

    bool load_scene(const std::string& scene_dir);
    bool load_frame_file(const std::filesystem::path& path, SceneFrame& out);

    PipelineConfig cfg_{};

    std::vector<SceneFrame> frames_;
    size_t current_frame_ = 0;

    // FrameBuffer views into the current frame's buffers
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};


    bool initialised_ = false;
    uint64_t frame_count_ = 0;
    std::string scene_dir_;
};

} // namespace ct
