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
#include "base-storage/base_storage.hpp"
#include "../types.hpp"
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>

namespace ct {

// ─── SessionActor ─────────────────────────────────────────────────────────────
// Combined session lifecycle manager + classification storage actor.
//
// Manages monitoring sessions (start/stop/auto-close), inference gating,
// trap identity provisioning, and persists classification crops to disk/SQLite.
//
// Exposes Ramen pull/push ports for the REST router to query and modify
// session state and query the database.
//
// Registered in the ActorRegistry as "session_actor" so that the HTTP handler
// can send messages to it via the registry.
//
// Thread safety:
//   - start_session() / stop_session() / active_session_id() are thread-safe.
//   - is_inference_enabled() is lock-free (atomic flag).
//   - The pipeline thread calls is_inference_enabled() on every tick.
struct SessionActor : public ramen::Actor, public BaseStorage {
    // ── Events (stubbed — will be replaced by Ramen event bus) ────────────────
    // Fired after each classification is saved.
    using OnSaved = std::function<void(const JpegCrop&, const std::string& path, int64_t classification_id)>;

    // ── Ramen ports for REST interface ────────────────────────────────────────
    // Pull handlers — the REST router pulls these to get domain objects.
    ramen::Pullable<std::vector<SessionInfo>>   out_list_sessions  = [this](std::vector<SessionInfo>& v) { v = list_sessions(); };
    ramen::Pullable<SessionInfo>                out_get_session    = [this](SessionInfo& s) { s = get_session(session_query_id_); };
    ramen::Pullable<std::vector<DetectionInfo>> out_list_detections = [this](std::vector<DetectionInfo>& v) { v = list_detections(session_query_id_); };
    ramen::Pullable<DetectionInfo>              out_get_detection  = [this](DetectionInfo& d) { d = get_detection(detection_query_id_); };
    ramen::Pullable<SessionInfo>                out_active_session = [this](SessionInfo& s) { s = active_session(); };
    ramen::Pullable<std::string>                out_trap_id        = [this](std::string& id) { id = trap_id(); };

    // Push handlers — the REST router pushes commands to these.
    ramen::Pushable<int64_t> in_start_session = [this](const int64_t&) { start_session(); };
    ramen::Pushable<int64_t> in_stop_session  = [this](const int64_t& session_id) { stop_session(session_id); };
    ramen::Pushable<std::string> in_provision = [this](const std::string& trap_id) { provision(trap_id); };

    // Query parameter setters — the REST router sets these before pulling.
    int64_t session_query_id_   = -1;
    int64_t detection_query_id_ = -1;

    // ── Classification input ──────────────────────────────────────────────────
    ramen::Pushable<std::vector<JpegCrop>> in_crops =
        [this](const std::vector<JpegCrop>& crops) {
            for (const auto& crop : crops) {
                auto path = save_jpeg(crop);
                if (!path.empty()) {
                    int64_t classification_id = insert_classification(crop, path);
                    if (on_saved_) on_saved_(crop, path, classification_id);
                }
            }
        };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Initialise storage: open SQLite database and create all tables.
    // Calls BaseStorage::init() internally.
    // max_sessions: maximum number of sessions allowed (0 = unlimited).
    using BaseStorage::init;  // bring base class init into scope
    bool init(const std::string& output_dir, const std::string& db_path,
              const std::string& trap_id = "", int max_sessions = 0);

    // Shutdown: finalize prepared statements, close database.
    void shutdown() override;

    void set_on_saved(OnSaved cb) { on_saved_ = std::move(cb); }

    // Reset storage node state.
    void reset() {
        // No per-track state to clear (classifications are always new INSERTs)
    }

    // ── Session operations ────────────────────────────────────────────────────
    // Start a new session.  If a session is already active, it is auto-closed.
    // Returns the new session_id.
    int64_t start_session();

    // Stop the given session.  Returns false if session_id is not active.
    bool stop_session(int64_t session_id);

    // Get the currently active session, or -1 if none.
    int64_t active_session_id() const;

    // Get active session info as a domain object.
    SessionInfo active_session() const;

    // ── Inference gating ──────────────────────────────────────────────────────
    // Called by Pipeline::run_loop() on every tick — lock-free.
    bool is_inference_enabled() const { return inference_enabled_.load(); }

    // ── Queries ───────────────────────────────────────────────────────────────
    // List all sessions.
    std::vector<SessionInfo> list_sessions(int limit = 50, int offset = 0) const;

    // Get a single session. Returns empty SessionInfo with id=-1 if not found.
    SessionInfo get_session(int64_t session_id) const;

    // Get detections for a session.
    std::vector<DetectionInfo> list_detections(int64_t session_id, int limit = 50, int offset = 0) const;

    // Get a single detection. Returns empty DetectionInfo with id=-1 if not found.
    DetectionInfo get_detection(int64_t detection_id) const;

    // ── Trap identity ─────────────────────────────────────────────────────────
    bool is_provisioned() const;
    bool provision(const std::string& trap_id);
    std::string trap_id() const;

private:
    // ── Database helpers ──────────────────────────────────────────────────────
    bool init_db();
    void auto_close_active_session();
    void enforce_max_sessions();

    // ── Classification storage ────────────────────────────────────────────────
    std::string save_jpeg(const JpegCrop& crop);
    int64_t insert_classification(const JpegCrop& crop, const std::string& path);

    std::string output_dir_;
    OnSaved     on_saved_;

    // Prepared statements
    sqlite3_stmt* insert_stmt_ = nullptr;  // prepared INSERT INTO classifications

    // ── Session state ─────────────────────────────────────────────────────────
    mutable std::mutex session_mutex_;
    std::atomic<int64_t> active_session_id_{-1};
    std::atomic<bool>    inference_enabled_{false};

    // Trap identity
    std::string trap_id_;

    // Max sessions limit (0 = unlimited)
    int max_sessions_ = 0;
};

} // namespace ct
