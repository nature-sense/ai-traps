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
