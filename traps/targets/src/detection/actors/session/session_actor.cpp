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

#include <sqlite3.h>

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

    // Open database via BaseStorage
    if (!BaseStorage::init(db_path)) {
        std::cerr << "[SessionActor] BaseStorage::init failed\n";
        return false;
    }

    // Create tables and prepare statements
    if (!init_db()) {
        std::cerr << "[SessionActor] init_db failed\n";
        return false;
    }

    // If a trap_id was provided via config, pre-provision it
    if (!trap_id.empty()) {
        trap_id_ = trap_id;
        std::cout << "[SessionActor] pre-provisioned trap_id=" << trap_id_
                  << " (from config)\n";
    }

    // Enforce max_sessions limit on startup
    enforce_max_sessions();

    std::cout << "[SessionActor] ready\n";
    return true;
}

void SessionActor::shutdown() {
    if (insert_stmt_) {
        sqlite3_finalize(insert_stmt_);
        insert_stmt_ = nullptr;
    }

    BaseStorage::shutdown();

    std::cout << "[SessionActor] shutdown\n";
}

// ── Database initialisation ───────────────────────────────────────────────────

bool SessionActor::init_db() {
    // Create the sessions table
    if (!exec_sql(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            started_at      INTEGER NOT NULL,
            stopped_at      INTEGER,
            detection_count INTEGER DEFAULT 0
        );
    )")) {
        return false;
    }

    // Create trap_info table
    if (!exec_sql(R"(
        CREATE TABLE IF NOT EXISTS trap_info (
            id              TEXT PRIMARY KEY,
            name            TEXT,
            provisioned_at  INTEGER NOT NULL
        );
    )")) {
        // Non-fatal
    }

    // Create the classifications table
    if (!exec_sql(R"(
        CREATE TABLE IF NOT EXISTS classifications (
            id                  INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp           INTEGER NOT NULL,
            track_id            INTEGER NOT NULL,
            class_id            INTEGER NOT NULL,
            confidence          REAL NOT NULL,
            detection_class_id  INTEGER NOT NULL DEFAULT 0,
            detection_confidence REAL NOT NULL DEFAULT 0.0,
            image_path          TEXT NOT NULL,
            session_id          INTEGER REFERENCES sessions(id)
        );
    )")) {
        return false;
    }

    // Create indexes
    exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_classifications_timestamp "
        "ON classifications(timestamp);");
    exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_classifications_session "
        "ON classifications(session_id);");

    // Prepare the INSERT statement for reuse
    const char* insert_sql = R"(
        INSERT INTO classifications
            (timestamp, track_id, class_id, confidence,
             detection_class_id, detection_confidence, image_path, session_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (!prepare_stmt(insert_sql, &insert_stmt_)) {
        return false;
    }

    // Load trap_id if provisioned
    sqlite3_stmt* trap_stmt = nullptr;
    int trap_rc = sqlite3_prepare_v2(db(),
        "SELECT id FROM trap_info LIMIT 1;", -1, &trap_stmt, nullptr);
    if (trap_rc == SQLITE_OK && trap_stmt) {
        if (sqlite3_step(trap_stmt) == SQLITE_ROW) {
            trap_id_ = reinterpret_cast<const char*>(sqlite3_column_text(trap_stmt, 0));
        }
        sqlite3_finalize(trap_stmt);
    }

    std::cout << "[SessionActor] SQLite ready (all tables created)\n";
    return true;
}

// ── Session operations ────────────────────────────────────────────────────────

int64_t SessionActor::start_session() {
    std::lock_guard<std::mutex> lock(session_mutex_);

    // Auto-close any existing active session
    if (active_session_id_ >= 0) {
        auto_close_active_session();
    }

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Insert new session
    sqlite3_stmt* stmt = nullptr;
    const char* insert = "INSERT INTO sessions (started_at) VALUES (?);";
    int rc = sqlite3_prepare_v2(db(), insert, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[SessionActor] prepare INSERT failed: "
                  << sqlite3_errmsg(db()) << "\n";
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, now_ms);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "[SessionActor] INSERT failed: "
                  << sqlite3_errmsg(db()) << "\n";
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t session_id = sqlite3_last_insert_rowid(db());
    sqlite3_finalize(stmt);

    active_session_id_.store(session_id);
    inference_enabled_.store(true);

    std::cout << "[SessionActor] session " << session_id << " started\n";

    // TODO: Publish SessionStartedEvent via Ramen event bus when available

    // Enforce max_sessions limit
    enforce_max_sessions();

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

    // Count classifications for this session
    int detection_count = 0;
    sqlite3_stmt* count_stmt = nullptr;
    const char* count_sql = "SELECT COUNT(*) FROM classifications WHERE session_id = ?;";
    if (sqlite3_prepare_v2(db(), count_sql, -1, &count_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(count_stmt, 1, session_id);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            detection_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    // Update session
    sqlite3_stmt* stmt = nullptr;
    const char* update = "UPDATE sessions SET stopped_at = ?, detection_count = ? WHERE id = ?;";
    int rc = sqlite3_prepare_v2(db(), update, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, now_ms);
        sqlite3_bind_int(stmt, 2, detection_count);
        sqlite3_bind_int64(stmt, 3, session_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    active_session_id_.store(-1);
    inference_enabled_.store(false);

    std::cout << "[SessionActor] session " << session_id << " stopped ("
              << detection_count << " classifications)\n";

    // TODO: Publish SessionStoppedEvent via Ramen event bus when available

    return true;
}

int64_t SessionActor::active_session_id() const {
    return active_session_id_.load();
}

SessionInfo SessionActor::active_session() const {
    int64_t sid = active_session_id_.load();
    if (sid < 0) {
        return SessionInfo{};
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, started_at, stopped_at, detection_count "
                      "FROM sessions WHERE id = ?;";

    SessionInfo info;
    if (sqlite3_prepare_v2(db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, sid);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info.id              = sqlite3_column_int64(stmt, 0);
            info.started_at      = sqlite3_column_int64(stmt, 1);
            info.stopped_at      = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                                   ? sqlite3_column_int64(stmt, 2) : 0;
            info.detection_count = sqlite3_column_int(stmt, 3);
            info.active          = true;
        }
        sqlite3_finalize(stmt);
    }

    return info;
}

void SessionActor::auto_close_active_session() {
    int64_t sid = active_session_id_.load();
    if (sid < 0) return;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Count classifications for this session
    int detection_count = 0;
    sqlite3_stmt* count_stmt = nullptr;
    const char* count_sql = "SELECT COUNT(*) FROM classifications WHERE session_id = ?;";
    if (sqlite3_prepare_v2(db(), count_sql, -1, &count_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(count_stmt, 1, sid);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            detection_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    sqlite3_stmt* stmt = nullptr;
    const char* update = "UPDATE sessions SET stopped_at = ?, detection_count = ? WHERE id = ? AND stopped_at IS NULL;";
    if (sqlite3_prepare_v2(db(), update, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, now_ms);
        sqlite3_bind_int(stmt, 2, detection_count);
        sqlite3_bind_int64(stmt, 3, sid);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    active_session_id_.store(-1);
    inference_enabled_.store(false);

    std::cout << "[SessionActor] auto-closed session " << sid
              << " (" << detection_count << " classifications)\n";

    // TODO: Publish SessionStoppedEvent via Ramen event bus when available
}

void SessionActor::enforce_max_sessions() {
    if (max_sessions_ <= 0) return;

    // Count total sessions
    int total = 0;
    sqlite3_stmt* count_stmt = nullptr;
    const char* count_sql = "SELECT COUNT(*) FROM sessions;";
    if (sqlite3_prepare_v2(db(), count_sql, -1, &count_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            total = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    int to_delete = total - max_sessions_;
    if (to_delete <= 0) return;

    std::cout << "[SessionActor] enforcing max_sessions=" << max_sessions_
              << " (total=" << total << ", deleting " << to_delete << " oldest)\n";

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = "SELECT id, started_at, detection_count FROM sessions "
                             "ORDER BY started_at ASC LIMIT ?;";
    if (sqlite3_prepare_v2(db(), select_sql, -1, &select_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(select_stmt, 1, to_delete);

        while (sqlite3_step(select_stmt) == SQLITE_ROW) {
            int64_t sid = sqlite3_column_int64(select_stmt, 0);
            int64_t started_at = sqlite3_column_int64(select_stmt, 1);
            int detection_count = sqlite3_column_int(select_stmt, 2);

            // Delete associated classifications first
            sqlite3_stmt* del_det = nullptr;
            const char* del_det_sql = "DELETE FROM classifications WHERE session_id = ?;";
            if (sqlite3_prepare_v2(db(), del_det_sql, -1, &del_det, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(del_det, 1, sid);
                sqlite3_step(del_det);
                sqlite3_finalize(del_det);
            }

            // Delete the session
            sqlite3_stmt* del_ses = nullptr;
            const char* del_ses_sql = "DELETE FROM sessions WHERE id = ?;";
            if (sqlite3_prepare_v2(db(), del_ses_sql, -1, &del_ses, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(del_ses, 1, sid);
                sqlite3_step(del_ses);
                sqlite3_finalize(del_ses);
            }

            std::cout << "[SessionActor] deleted session " << sid
                      << " (started=" << started_at
                      << ", classifications=" << detection_count << ")\n";

            // TODO: Publish SessionDeletedEvent via Ramen event bus when available
        }
        sqlite3_finalize(select_stmt);
    }
}

// ── Queries ───────────────────────────────────────────────────────────────────

std::vector<SessionInfo> SessionActor::list_sessions(int limit, int offset) const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, started_at, stopped_at, detection_count "
                      "FROM sessions ORDER BY started_at DESC LIMIT ? OFFSET ?;";

    std::vector<SessionInfo> sessions;
    if (sqlite3_prepare_v2(db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SessionInfo info;
            info.id              = sqlite3_column_int64(stmt, 0);
            info.started_at      = sqlite3_column_int64(stmt, 1);
            info.stopped_at      = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                                   ? sqlite3_column_int64(stmt, 2) : 0;
            info.detection_count = sqlite3_column_int(stmt, 3);
            info.active          = (info.id == active_session_id_.load());
            sessions.push_back(std::move(info));
        }
        sqlite3_finalize(stmt);
    }

    return sessions;
}

SessionInfo SessionActor::get_session(int64_t session_id) const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, started_at, stopped_at, detection_count "
                      "FROM sessions WHERE id = ?;";

    SessionInfo info;
    if (sqlite3_prepare_v2(db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, session_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info.id              = sqlite3_column_int64(stmt, 0);
            info.started_at      = sqlite3_column_int64(stmt, 1);
            info.stopped_at      = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                                   ? sqlite3_column_int64(stmt, 2) : 0;
            info.detection_count = sqlite3_column_int(stmt, 3);
            info.active          = (info.id == active_session_id_.load());
        }
        sqlite3_finalize(stmt);
    }

    return info;
}

std::vector<DetectionInfo> SessionActor::list_detections(int64_t session_id, int limit, int offset) const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, timestamp, track_id, class_id, confidence, image_path, session_id "
                      "FROM classifications WHERE session_id = ? "
                      "ORDER BY timestamp DESC LIMIT ? OFFSET ?;";

    std::vector<DetectionInfo> detections;
    if (sqlite3_prepare_v2(db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, session_id);
        sqlite3_bind_int(stmt, 2, limit);
        sqlite3_bind_int(stmt, 3, offset);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DetectionInfo det;
            det.id         = sqlite3_column_int64(stmt, 0);
            det.timestamp  = sqlite3_column_int64(stmt, 1);
            det.track_id   = sqlite3_column_int(stmt, 2);
            det.class_id   = sqlite3_column_int(stmt, 3);
            det.confidence = sqlite3_column_double(stmt, 4);
            det.session_id = sqlite3_column_int64(stmt, 6);

            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (path) det.image_path = path;

            detections.push_back(std::move(det));
        }
        sqlite3_finalize(stmt);
    }

    return detections;
}

DetectionInfo SessionActor::get_detection(int64_t detection_id) const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, timestamp, track_id, class_id, confidence, image_path, session_id "
                      "FROM classifications WHERE id = ?;";

    DetectionInfo det;
    if (sqlite3_prepare_v2(db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, detection_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            det.id         = sqlite3_column_int64(stmt, 0);
            det.timestamp  = sqlite3_column_int64(stmt, 1);
            det.track_id   = sqlite3_column_int(stmt, 2);
            det.class_id   = sqlite3_column_int(stmt, 3);
            det.confidence = sqlite3_column_double(stmt, 4);
            det.session_id = sqlite3_column_int64(stmt, 6);

            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (path) det.image_path = path;
        }
        sqlite3_finalize(stmt);
    }

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

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* stmt = nullptr;
    const char* insert = "INSERT INTO trap_info (id, name, provisioned_at) VALUES (?, ?, ?);";
    int rc = sqlite3_prepare_v2(db(), insert, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[SessionActor] provision prepare failed: "
                  << sqlite3_errmsg(db()) << "\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, trap_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trap_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now_ms);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "[SessionActor] provision INSERT failed: "
                  << sqlite3_errmsg(db()) << "\n";
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

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
    if (!insert_stmt_) return -1;

    int64_t session_id = active_session_id_.load();

    sqlite3_bind_int64(insert_stmt_, 1, crop.timestamp_ms);
    sqlite3_bind_int(insert_stmt_, 2, crop.track_id);
    sqlite3_bind_int(insert_stmt_, 3, crop.class_id);
    sqlite3_bind_double(insert_stmt_, 4, static_cast<double>(crop.confidence));
    sqlite3_bind_int(insert_stmt_, 5, 0);      // detection_class_id placeholder
    sqlite3_bind_double(insert_stmt_, 6, 0.0); // detection_confidence placeholder
    sqlite3_bind_text(insert_stmt_, 7, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert_stmt_, 8, session_id);

    int rc = sqlite3_step(insert_stmt_);
    if (rc != SQLITE_DONE) {
        std::cerr << "[SessionActor] INSERT failed: " << sqlite3_errmsg(db()) << "\n";
        sqlite3_reset(insert_stmt_);
        return -1;
    }

    int64_t classification_id = sqlite3_last_insert_rowid(db());
    sqlite3_reset(insert_stmt_);

    std::cout << "[SessionActor] inserted classification id=" << classification_id
              << " track=" << crop.track_id
              << " class=" << crop.class_id
              << " conf=" << crop.confidence << "\n";

    return classification_id;
}

} // namespace ct
