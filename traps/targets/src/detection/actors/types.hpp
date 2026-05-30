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

#include <cstdint>
#include <string>
#include <vector>

namespace ct {

// ─── SessionInfo ──────────────────────────────────────────────────────────────
// Domain object representing a monitoring session.
struct SessionInfo {
    int64_t id              = -1;
    int64_t started_at      = 0;
    int64_t stopped_at      = 0;   // 0 = not stopped (still active)
    int     detection_count = 0;
    bool    active          = false;
};

// ─── DetectionInfo ────────────────────────────────────────────────────────────
// Domain object representing a single classification/detection result.
struct DetectionInfo {
    int64_t id          = -1;
    int64_t timestamp   = 0;
    int     track_id    = 0;
    int     class_id    = 0;
    double  confidence  = 0.0;
    std::string image_path;
    int64_t session_id  = -1;
};

} // namespace ct
