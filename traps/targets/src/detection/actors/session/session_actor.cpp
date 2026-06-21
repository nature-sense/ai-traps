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

#include "session_actor.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <thread>

namespace ct {

// POSIX recursive mkdir
static bool make_dirs(const std::string& path) {
    for (std::size_t pos = 1; pos <= path.size(); ++pos) {
        if (pos == path.size() || path[pos] == '/') {
            std::string sub = path.substr(0, pos);
            if (::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "[SessionActor] mkdir " << sub << ": "
                          << std::strerror(errno) << "\n";
                return false;
            }
        }
    }
    return true;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool SessionActor::init(const std::string& output_dir, const std::string& db_path,
                        const std::string& trap_id, int max_sessions) {
    output_dir_ = output_dir;
    max_sessions_ = max_sessions;
    std::cout << "[SessionActor] init\n"
              << "  output_dir: " << output_dir << "\n"
              << "  db_path:    " << db_path    << "\n"
              << "  max_sessions: " << max_sessions_
              << (max_sessions_ == 0 ? " (unlimited)" : "") << "\n";

    if (!make_dirs(output_dir_)) return false;

    // Initialise the DataStore (in-memory store with JSON persistence)
    if (!data_store_.init(db_path)) {
        std::cerr << "[SessionActor] DataStore::init failed\n";
        return false;
    }

    // If a trap_id was provided via config, pre-provision it
    if (!trap_id.empty()) {
        trap_id_ = trap_id;
        // If DataStore was loaded from disk and already has a trap_id, use that
        // instead of overriding it.
        std::string stored_id = data_store_.get_trap_id();
        if (!stored_id.empty()) {
            trap_id_ = stored_id;
            std::cout << "[SessionActor] using stored trap_id=" << trap_id_
                      << " (from disk)\n";
        } else {
            data_store_.provision(trap_id);
            std::cout << "[SessionActor] pre-provisioned trap_id=" << trap_id_
                      << " (from config)\n";
        }
    } else {
        // Load any existing trap_id from DataStore
        std::string stored_id = data_store_.get_trap_id();
        if (!stored_id.empty()) {
            trap_id_ = stored_id;
            std::cout << "[SessionActor] loaded trap_id=" << trap_id_
                      << " from disk\n";
        }
    }

    std::cout << "[SessionActor] ready\n";
    return true;
}

void SessionActor::shutdown() {
    data_store_.shutdown();
    std::cout << "[SessionActor] shutdown\n";
}

// ── Session operations ────────────────────────────────────────────────────────

int64_t SessionActor::start_session() {
    std::lock_guard<std::mutex> lock(session_mutex_);

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Delegate to DataStore (handles auto-close, ID generation, persistence)
    int64_t session_id = data_store_.create_session(now_ms);
    if (session_id < 0) {
        return -1;
    }

    active_session_id_.store(session_id);
    inference_enabled_.store(true);

    std::cout << "[SessionActor] session " << session_id << " started\n";

    return session_id;
}

bool SessionActor::stop_session(int64_t session_id) {
    std::lock_guard<std::mutex> lock(session_mutex_);

    if (active_session_id_.load() != session_id) {
        std::cerr << "[SessionActor] session " << session_id
                  << " is not active (active=" << active_session_id_.load() << ")\n";
        return false;
    }

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Delegate to DataStore (handles stop, detection count, persistence)
    if (!data_store_.stop_session(session_id, now_ms)) {
        return false;
    }

    active_session_id_.store(-1);
    inference_enabled_.store(false);

    std::cout << "[SessionActor] session " << session_id << " stopped\n";

    return true;
}

int64_t SessionActor::active_session_id() const {
    return active_session_id_.load();
}

SessionInfo SessionActor::active_session() const {
    std::lock_guard<std::mutex> lock(session_mutex_);

    int64_t sid = active_session_id_.load();
    if (sid < 0) {
        return SessionInfo{};
    }

    auto rec = data_store_.get_session(sid);
    if (rec.id < 0) {
        return SessionInfo{};
    }

    SessionInfo info;
    info.id              = rec.id;
    info.started_at      = rec.started_at;
    info.stopped_at      = rec.stopped_at;
    info.detection_count = rec.detection_count;
    info.active          = (rec.id == active_session_id_.load());
    return info;
}

// ── Queries ───────────────────────────────────────────────────────────────────

std::vector<SessionInfo> SessionActor::list_sessions(int limit, int offset) const {
    auto records = data_store_.list_sessions(limit, offset);

    std::vector<SessionInfo> sessions;
    sessions.reserve(records.size());

    for (const auto& rec : records) {
        SessionInfo info;
        info.id              = rec.id;
        info.started_at      = rec.started_at;
        info.stopped_at      = rec.stopped_at;
        info.detection_count = rec.detection_count;
        info.active          = (rec.id == active_session_id_.load());
        sessions.push_back(std::move(info));
    }

    return sessions;
}

SessionInfo SessionActor::get_session(int64_t session_id) const {
    auto rec = data_store_.get_session(session_id);
    if (rec.id < 0) {
        return SessionInfo{};
    }

    SessionInfo info;
    info.id              = rec.id;
    info.started_at      = rec.started_at;
    info.stopped_at      = rec.stopped_at;
    info.detection_count = rec.detection_count;
    info.active          = (rec.id == active_session_id_.load());
    return info;
}

std::vector<DetectionInfo> SessionActor::list_detections(int64_t session_id, int limit, int offset) const {
    auto records = data_store_.list_detections(session_id, limit, offset);

    std::vector<DetectionInfo> detections;
    detections.reserve(records.size());

    for (const auto& rec : records) {
        DetectionInfo det;
        det.id         = rec.id;
        det.timestamp  = rec.timestamp;
        det.track_id   = rec.track_id;
        det.class_id   = rec.class_id;
        det.confidence = rec.confidence;
        det.image_path = rec.image_path;
        det.session_id = rec.session_id;
        detections.push_back(std::move(det));
    }

    return detections;
}

DetectionInfo SessionActor::get_detection(int64_t detection_id) const {
    auto rec = data_store_.get_detection(detection_id);
    if (rec.id < 0) {
        DetectionInfo empty;
        empty.id = -1;
        return empty;
    }

    DetectionInfo det;
    det.id         = rec.id;
    det.timestamp  = rec.timestamp;
    det.track_id   = rec.track_id;
    det.class_id   = rec.class_id;
    det.confidence = rec.confidence;
    det.image_path = rec.image_path;
    det.session_id = rec.session_id;
    return det;
}

// ── Trap identity ─────────────────────────────────────────────────────────────

bool SessionActor::is_provisioned() const {
    return !trap_id_.empty();
}

bool SessionActor::provision(const std::string& trap_id) {
    if (!trap_id_.empty()) {
        std::cerr << "[SessionActor] already provisioned with id=" << trap_id_ << "\n";
        return false;
    }

    if (!data_store_.provision(trap_id)) {
        return false;
    }

    trap_id_ = trap_id;
    std::cout << "[SessionActor] provisioned trap_id=" << trap_id << "\n";
    return true;
}

std::string SessionActor::trap_id() const {
    return trap_id_;
}

// ── Classification storage ────────────────────────────────────────────────────

std::string SessionActor::save_jpeg(const JpegCrop& crop) {
    if (crop.data.empty()) return {};

    // Build path: {output_dir}/YYYY-MM-DD/{timestamp_ms}_{track_id}.jpg
    const std::time_t t = crop.timestamp_ms / 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream date_dir;
    date_dir << output_dir_ << "/" << std::put_time(&tm, "%Y-%m-%d");
    if (!make_dirs(date_dir.str())) return {};

    std::ostringstream path;
    path << date_dir.str() << "/" << crop.timestamp_ms << "_" << crop.track_id << ".jpg";

    std::ofstream f(path.str(), std::ios::binary);
    if (!f) {
        std::cerr << "[SessionActor] failed to open: " << path.str() << "\n";
        return {};
    }
    f.write(reinterpret_cast<const char*>(crop.data.data()),
            static_cast<std::streamsize>(crop.data.size()));
    return path.str();
}

int64_t SessionActor::insert_classification(const JpegCrop& crop, const std::string& path) {
    int64_t session_id = active_session_id_.load();

    DataStore::DetectionRecord rec;
    rec.session_id = session_id;
    rec.timestamp  = crop.timestamp_ms;
    rec.track_id   = crop.track_id;
    rec.class_id   = crop.class_id;
    rec.confidence = static_cast<double>(crop.confidence);
    rec.image_path = path;

    int64_t classification_id = data_store_.insert_detection(rec);

    if (classification_id >= 0) {
        std::cout << "[SessionActor] saved classification id=" << classification_id
                  << " track=" << crop.track_id
                  << " class=" << crop.class_id
                  << " conf=" << crop.confidence << "\n";
    }

    return classification_id;
}

} // namespace ct