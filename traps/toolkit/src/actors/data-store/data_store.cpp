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

#include "data_store.hpp"
#include <cstdio>   // rename
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <set>

namespace ct {

// ============================================================================
// Lifecycle
// ============================================================================

bool DataStore::init(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_path_ = db_path;

    std::cout << "[DataStore] init: " << db_path_ << "\n";

    // Try to load existing state from disk
    if (load_from_disk()) {
        std::cout << "[DataStore] loaded " << sessions_.size()
                  << " sessions, " << detections_.size()
                  << " detections from disk\n";
    } else {
        std::cout << "[DataStore] fresh start (no persisted state)\n";
    }

    return true;
}

void DataStore::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[DataStore] shutdown, saving state...\n";
    save_to_disk();
}

// ============================================================================
// Trap Identity
// ============================================================================

bool DataStore::is_provisioned() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return provisioned_;
}

bool DataStore::provision(const std::string& trap_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (provisioned_) {
        std::cerr << "[DataStore] already provisioned with id=" << trap_id_ << "\n";
        return false;
    }
    trap_id_ = trap_id;
    provisioned_ = true;
    std::cout << "[DataStore] provisioned trap_id=" << trap_id_ << "\n";
    save_to_disk();
    return true;
}

std::string DataStore::get_trap_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trap_id_;
}

// ============================================================================
// Session Operations
// ============================================================================

int64_t DataStore::create_session(int64_t started_at) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Auto-close any existing active session
    int64_t old_active = active_session_id_.load();
    if (old_active >= 0) {
        auto it = sessions_.find(old_active);
        if (it != sessions_.end() && it->second.stopped_at == 0) {
            it->second.stopped_at = started_at;  // close at new session start time
            std::cout << "[DataStore] auto-closed session " << old_active << "\n";
        }
    }

    // Create new session
    SessionRecord rec;
    rec.id = next_session_id_++;
    rec.started_at = started_at;
    rec.stopped_at = 0;
    rec.detection_count = 0;

    sessions_[rec.id] = rec;
    active_session_id_.store(rec.id);

    std::cout << "[DataStore] session " << rec.id << " started\n";

    // Enforce max_sessions (default 0 = unlimited)
    enforce_max_sessions();

    save_to_disk();
    return rec.id;
}

bool DataStore::stop_session(int64_t session_id, int64_t stopped_at) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (active_session_id_.load() != session_id) {
        std::cerr << "[DataStore] session " << session_id
                  << " is not active (active=" << active_session_id_.load() << ")\n";
        return false;
    }

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "[DataStore] session " << session_id << " not found\n";
        return false;
    }

    // Count detections for this session
    int detection_count = 0;
    for (const auto& [id, det] : detections_) {
        if (det.session_id == session_id) {
            detection_count++;
        }
    }

    it->second.stopped_at = stopped_at;
    it->second.detection_count = detection_count;

    active_session_id_.store(-1);

    std::cout << "[DataStore] session " << session_id << " stopped ("
              << detection_count << " detections)\n";

    save_to_disk();
    return true;
}

int64_t DataStore::active_session_id() const {
    return active_session_id_.load();
}

// ============================================================================
// Queries
// ============================================================================

std::vector<DataStore::SessionRecord> DataStore::list_sessions(int limit, int offset) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Collect sessions in reverse order (newest first)
    std::vector<SessionRecord> result;
    result.reserve(std::min(static_cast<size_t>(limit), sessions_.size()));

    for (auto it = sessions_.rbegin(); it != sessions_.rend(); ++it) {
        if (offset > 0) {
            offset--;
            continue;
        }
        if (limit <= 0) break;
        result.push_back(it->second);
        limit--;
    }

    return result;
}

DataStore::SessionRecord DataStore::get_session(int64_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        SessionRecord empty;
        empty.id = -1;
        return empty;
    }
    return it->second;
}

std::vector<DataStore::DetectionRecord> DataStore::list_detections(
    int64_t session_id, int limit, int offset) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<DetectionRecord> result;
    result.reserve(std::min(static_cast<size_t>(limit), detections_.size()));

    // Iterate in reverse (newest first) and filter by session_id
    for (auto it = detections_.rbegin(); it != detections_.rend(); ++it) {
        if (it->second.session_id != session_id) continue;
        if (offset > 0) {
            offset--;
            continue;
        }
        if (limit <= 0) break;
        result.push_back(it->second);
        limit--;
    }

    return result;
}

DataStore::DetectionRecord DataStore::get_detection(int64_t detection_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = detections_.find(detection_id);
    if (it == detections_.end()) {
        DetectionRecord empty;
        empty.id = -1;
        return empty;
    }
    return it->second;
}

// ============================================================================
// Detection Insertion
// ============================================================================

int64_t DataStore::insert_detection(const DetectionRecord& det) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool is_new = true;

    // Check if we already have the best crop for this (session_id, track_id).
    // If we do, only replace it if the new detection has higher confidence.
    // This ensures one crop per track — the one with the highest confidence.
    if (det.session_id >= 0) {
        for (auto& [existing_id, existing] : detections_) {
            if (existing.session_id == det.session_id && existing.track_id == det.track_id) {
                is_new = false;
                if (det.confidence > existing.confidence) {
                    // Update the existing record with better data
                    existing.timestamp = det.timestamp;
                    existing.class_id = det.class_id;
                    existing.confidence = det.confidence;
                    existing.image_path = det.image_path;
                    std::cout << "[DataStore] updated detection id=" << existing.id
                              << " (better confidence " << det.confidence
                              << " > " << existing.confidence << ")\n";
                    // detection_count doesn't change since this is not a new track
                    save_to_disk();
                    return existing.id;
                } else {
                    // Existing detection is already better — skip
                    std::cout << "[DataStore] skipped detection — existing id=" << existing.id
                              << " has confidence " << existing.confidence
                              << " >= " << det.confidence << "\n";
                    return existing.id;
                }
            }
        }
    }

    // No existing detection for this (session_id, track_id) — insert new one
    DetectionRecord rec = det;
    rec.id = next_detection_id_++;

    detections_[rec.id] = rec;

    std::cout << "[DataStore] inserted detection id=" << rec.id
              << " session=" << rec.session_id
              << " track=" << rec.track_id
              << " class=" << rec.class_id << "\n";

    // ── Update session detection count in real time ────────────────────────
    // Count distinct track_ids for the session so the count reflects unique
    // insects, not total frames. Update the session record immediately so
    // get_status / list_sessions returns the live count.
    if (is_new && det.session_id >= 0) {
        // Count distinct track IDs for this session
        std::set<int> distinct_tracks;
        for (const auto& [id, d] : detections_) {
            if (d.session_id == det.session_id) {
                distinct_tracks.insert(d.track_id);
            }
        }
        int track_count = static_cast<int>(distinct_tracks.size());

        // Update the session record
        auto session_it = sessions_.find(det.session_id);
        if (session_it != sessions_.end()) {
            session_it->second.detection_count = track_count;
            std::cout << "[DataStore] session " << det.session_id
                      << " detection_count updated to " << track_count << "\n";
        }
    }

    save_to_disk();
    return rec.id;
}

// ============================================================================
// Persistence — Atomic JSON save/load
// ============================================================================

bool DataStore::save_to_disk() {
    if (db_path_.empty()) return false;

    // Build JSON object
    nlohmann::json j;

    // Trap identity
    j["trap_id"] = trap_id_;
    j["provisioned"] = provisioned_;

    // Auto-increment counters
    j["next_session_id"] = next_session_id_;
    j["next_detection_id"] = next_detection_id_;

    // Sessions (sorted by ID)
    nlohmann::json sessions_arr = nlohmann::json::array();
    for (const auto& [id, rec] : sessions_) {
        sessions_arr.push_back(rec);
    }
    j["sessions"] = sessions_arr;

    // Detections (sorted by ID)
    nlohmann::json detections_arr = nlohmann::json::array();
    for (const auto& [id, rec] : detections_) {
        detections_arr.push_back(rec);
    }
    j["detections"] = detections_arr;

    // Write to a temp file first, then atomically rename
    std::string tmp_path = db_path_ + ".tmp";
    std::ofstream f(tmp_path, std::ios::binary);
    if (!f) {
        std::cerr << "[DataStore] failed to open " << tmp_path
                  << " for writing: " << std::strerror(errno) << "\n";
        return false;
    }
    f << j.dump(2);  // pretty-print with 2-space indent
    f.close();
    if (f.fail()) {
        std::cerr << "[DataStore] write error to " << tmp_path << "\n";
        std::remove(tmp_path.c_str());
        return false;
    }

    // Atomic rename (atomic on POSIX if source and dest are on same filesystem)
    if (std::rename(tmp_path.c_str(), db_path_.c_str()) != 0) {
        std::cerr << "[DataStore] rename failed: " << std::strerror(errno) << "\n";
        std::remove(tmp_path.c_str());
        return false;
    }

    return true;
}

bool DataStore::load_from_disk() {
    if (db_path_.empty()) return false;

    std::ifstream f(db_path_);
    if (!f) {
        // File doesn't exist yet — this is normal for first boot
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[DataStore] parse error loading " << db_path_
                  << ": " << e.what() << "\n";
        return false;
    }

    // Load trap identity
    if (j.contains("trap_id")) j.at("trap_id").get_to(trap_id_);
    if (j.contains("provisioned")) j.at("provisioned").get_to(provisioned_);

    // Load auto-increment counters
    if (j.contains("next_session_id")) j.at("next_session_id").get_to(next_session_id_);
    if (j.contains("next_detection_id")) j.at("next_detection_id").get_to(next_detection_id_);

    // Load sessions
    sessions_.clear();
    if (j.contains("sessions") && j["sessions"].is_array()) {
        for (const auto& item : j["sessions"]) {
            SessionRecord rec = item.get<SessionRecord>();
            sessions_[rec.id] = rec;
        }
    }

    // Load detections
    detections_.clear();
    if (j.contains("detections") && j["detections"].is_array()) {
        for (const auto& item : j["detections"]) {
            DetectionRecord rec = item.get<DetectionRecord>();
            detections_[rec.id] = rec;
        }
    }

    // Determine if any session is still active (stopped_at == 0 for non-archived)
    // On restart, all sessions are considered stopped (no active inference)
    active_session_id_.store(-1);

    return true;
}

// ============================================================================
// Private Helpers
// ============================================================================

void DataStore::enforce_max_sessions() {
    // Default: no limit (0 = unlimited)
    (void)0;  // Placeholder — max_sessions enforcement will be added if needed.
    // The SessionActor already handles max_sessions via its own parameter.
    // DataStore stores everything; SessionActor limits creation.
}

} // namespace ct