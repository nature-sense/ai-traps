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

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <atomic>

namespace ct {

// ─── DataStore ─────────────────────────────────────────────────────────────────
// Thread-safe in-memory data store for sessions and detections.
//
// Replaces SQLite-based BaseStorage with a simple std::map-backed store.
// Persists state to a JSON file on every mutation and periodically via
// a background timer.
//
// Thread safety:
//   - All public methods acquire a mutex (internal locking).
//   - saveToDisk() and loadFromDisk() are also guarded by the same mutex.
//
// Persistence strategy:
//   - saveToDisk() called after every state mutation (CRUD methods).
//   - A periodic 30s timer also calls saveToDisk() as a safety net.
//   - loadFromDisk() called once at init() time.
//   - Writes to a temp file, then atomically renames to target path.
//
// Data volume: at most a few hundred records (sessions + detections).
// JSON snapshot is typically < 200KB.
// ============================================================================
struct DataStore {
    // ── Record types ───────────────────────────────────────────────────────
    struct SessionRecord {
        int64_t id              = -1;
        int64_t started_at      = 0;
        int64_t stopped_at      = 0;  // 0 = not stopped (still active)
        int     detection_count = 0;
    };

    struct DetectionRecord {
        int64_t id          = -1;
        int64_t session_id  = -1;
        int64_t timestamp   = 0;
        int     track_id    = 0;
        int     class_id    = 0;
        double  confidence  = 0.0;
        std::string image_path;
    };

    // ── Lifecycle ──────────────────────────────────────────────────────────
    // Initialise the data store with a path for the JSON snapshot file.
    // Automatically loads state from disk if the file exists.
    // max_sessions: maximum number of sessions allowed (0 = unlimited).
    bool init(const std::string& db_path);

    // Shutdown: optionally save one final snapshot (safe to skip).
    void shutdown();

    // ── Trap identity ──────────────────────────────────────────────────────
    bool is_provisioned() const;
    bool provision(const std::string& trap_id);
    std::string get_trap_id() const;

    // ── Session operations (all thread-safe) ───────────────────────────────
    // Start a new session. Auto-closes any active session.
    // Returns the new session_id, or -1 on failure.
    int64_t create_session(int64_t started_at);

    // Stop the given session. Returns false if session_id is not active.
    bool stop_session(int64_t session_id, int64_t stopped_at);

    // Get active session ID. Returns -1 if no session is active.
    int64_t active_session_id() const;

    // ── Queries ────────────────────────────────────────────────────────────
    std::vector<SessionRecord> list_sessions(int limit = 50, int offset = 0) const;
    SessionRecord get_session(int64_t session_id) const;

    std::vector<DetectionRecord> list_detections(int64_t session_id, int limit = 50, int offset = 0) const;
    DetectionRecord get_detection(int64_t detection_id) const;

    // ── Detection insertion ────────────────────────────────────────────────
    int64_t insert_detection(const DetectionRecord& det);

    // ── Persistence ────────────────────────────────────────────────────────
    // Save current state to JSON file (thread-safe). Called on every mutation
    // and periodically. Returns true on success.
    bool save_to_disk();

    // Load state from JSON file. Called once at init(). Returns false if
    // the file doesn't exist or is corrupt.
    bool load_from_disk();

private:
    // Enforce max_sessions by deleting the oldest sessions + their detections.
    void enforce_max_sessions();

    // ── Internal (guarded by mutex_) ───────────────────────────────────────
    mutable std::mutex mutex_;

    // Session storage: session_id → SessionRecord
    std::map<int64_t, SessionRecord> sessions_;

    // Detection storage: detection_id → DetectionRecord
    std::map<int64_t, DetectionRecord> detections_;

    // Auto-increment counters
    int64_t next_session_id_  = 1;
    int64_t next_detection_id_ = 1;

    // Trap identity
    std::string trap_id_;
    bool provisioned_ = false;

    // Active session ID (no lock needed for atomic, but mutations are gated by mutex)
    std::atomic<int64_t> active_session_id_{-1};

    // Persistence
    std::string db_path_;
};

// ── Helper functions for JSON serialization ─────────────────────────────────
inline void to_json(nlohmann::json& j, const DataStore::SessionRecord& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"started_at", s.started_at},
        {"stopped_at", s.stopped_at},
        {"detection_count", s.detection_count}
    };
}

inline void from_json(const nlohmann::json& j, DataStore::SessionRecord& s) {
    j.at("id").get_to(s.id);
    j.at("started_at").get_to(s.started_at);
    j.at("stopped_at").get_to(s.stopped_at);
    j.at("detection_count").get_to(s.detection_count);
}

inline void to_json(nlohmann::json& j, const DataStore::DetectionRecord& d) {
    j = nlohmann::json{
        {"id", d.id},
        {"session_id", d.session_id},
        {"timestamp", d.timestamp},
        {"track_id", d.track_id},
        {"class_id", d.class_id},
        {"confidence", d.confidence},
        {"image_path", d.image_path}
    };
}

inline void from_json(const nlohmann::json& j, DataStore::DetectionRecord& d) {
    j.at("id").get_to(d.id);
    j.at("session_id").get_to(d.session_id);
    j.at("timestamp").get_to(d.timestamp);
    j.at("track_id").get_to(d.track_id);
    j.at("class_id").get_to(d.class_id);
    j.at("confidence").get_to(d.confidence);
    j.at("image_path").get_to(d.image_path);
}

} // namespace ct