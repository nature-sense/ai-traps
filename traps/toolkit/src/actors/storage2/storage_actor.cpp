#include "storage_actor.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

// SQLite3 header
#include <sqlite3.h>

namespace ct {

// POSIX recursive mkdir (std::filesystem not available on uclibc/GCC8 without -lstdc++fs)
static bool make_dirs(const std::string& path) {
    for (std::size_t pos = 1; pos <= path.size(); ++pos) {
        if (pos == path.size() || path[pos] == '/') {
            std::string sub = path.substr(0, pos);
            if (::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "[StorageActor] mkdir " << sub << ": " << std::strerror(errno) << "\n";
                return false;
            }
        }
    }
    return true;
}

bool StorageActor::init(const std::string& output_dir, const std::string& db_path) {
    output_dir_ = output_dir;
    std::cout << "[StorageActor] init\n"
              << "  output_dir: " << output_dir << "\n"
              << "  db_path:    " << db_path    << "\n";

    if (!make_dirs(output_dir_)) return false;

    // Open database via BaseStorage
    if (!BaseStorage::init(db_path)) {
        std::cerr << "[StorageActor] BaseStorage::init failed\n";
        return false;
    }

    // Create tables and prepare statements
    if (!init_db()) {
        std::cerr << "[StorageActor] init_db failed\n";
        return false;
    }

    std::cout << "[StorageActor] ready\n";
    return true;
}

void StorageActor::shutdown() {
    if (insert_stmt_) {
        sqlite3_finalize(insert_stmt_);
        insert_stmt_ = nullptr;
    }
    if (update_stmt_) {
        sqlite3_finalize(update_stmt_);
        update_stmt_ = nullptr;
    }

    // Close database via BaseStorage
    BaseStorage::shutdown();

    std::cout << "[StorageActor] shutdown\n";
}

std::string StorageActor::save_jpeg(const JpegCrop& crop) {
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
        std::cerr << "[StorageActor] failed to open: " << path.str() << "\n";
        return {};
    }
    f.write(reinterpret_cast<const char*>(crop.data.data()),
            static_cast<std::streamsize>(crop.data.size()));
    return path.str();
}

// Overload: reuse an existing filename (for updates — overwrites the old file).
// The existing_filename is relative to output_dir_ (e.g. "2026-05-20/12345_1.jpg").
std::string StorageActor::save_jpeg(const JpegCrop& crop, const std::string& existing_filename) {
    if (crop.data.empty()) return {};

    // Build the full path from the existing filename
    std::string full_path = output_dir_ + "/" + existing_filename;

    // Ensure the directory exists (it should already, but be safe)
    std::string::size_type slash = full_path.rfind('/');
    if (slash != std::string::npos) {
        std::string dir = full_path.substr(0, slash);
        if (!make_dirs(dir)) return {};
    }

    std::ofstream f(full_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << "[StorageActor] failed to open for overwrite: " << full_path << "\n";
        return {};
    }
    f.write(reinterpret_cast<const char*>(crop.data.data()),
            static_cast<std::streamsize>(crop.data.size()));
    return full_path;
}

int64_t StorageActor::insert_metadata(const JpegCrop& crop, const std::string& path) {
    if (!insert_stmt_) return -1;

    // Get current session_id (if any)
    int64_t session_id = session_id_fn_ ? session_id_fn_() : -1;

    // Bind values to the prepared INSERT statement
    sqlite3_bind_int64(insert_stmt_, 1, crop.timestamp_ms);
    sqlite3_bind_int(insert_stmt_, 2, crop.track_id);
    sqlite3_bind_int(insert_stmt_, 3, crop.class_id);
    sqlite3_bind_double(insert_stmt_, 4, static_cast<double>(crop.confidence));
    // bbox_x, bbox_y, bbox_w, bbox_h are not stored in JpegCrop directly;
    // they were used during cropping. Store 0 as placeholders.
    sqlite3_bind_int(insert_stmt_, 5, 0);   // bbox_x
    sqlite3_bind_int(insert_stmt_, 6, 0);   // bbox_y
    sqlite3_bind_int(insert_stmt_, 7, 0);   // bbox_w
    sqlite3_bind_int(insert_stmt_, 8, 0);   // bbox_h
    sqlite3_bind_text(insert_stmt_, 9, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert_stmt_, 10, session_id);

    int rc = sqlite3_step(insert_stmt_);
    if (rc != SQLITE_DONE) {
        std::cerr << "[StorageActor] INSERT failed: " << sqlite3_errmsg(db()) << "\n";
        sqlite3_reset(insert_stmt_);
        return -1;
    }

    int64_t detection_id = sqlite3_last_insert_rowid(db());
    sqlite3_reset(insert_stmt_);

    std::cout << "[StorageActor] inserted detection id=" << detection_id
              << " track=" << crop.track_id
              << " conf=" << crop.confidence << "\n";

    return detection_id;
}

void StorageActor::update_metadata(const JpegCrop& crop, const std::string& path, int64_t detection_id) {
    if (!update_stmt_) return;

    // Get current session_id (may have changed since insert)
    int64_t session_id = session_id_fn_ ? session_id_fn_() : -1;

    // Bind values to the prepared UPDATE statement
    sqlite3_bind_int64(update_stmt_, 1, crop.timestamp_ms);
    sqlite3_bind_double(update_stmt_, 2, static_cast<double>(crop.confidence));
    sqlite3_bind_text(update_stmt_, 3, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(update_stmt_, 4, session_id);
    sqlite3_bind_int64(update_stmt_, 5, detection_id);

    int rc = sqlite3_step(update_stmt_);
    if (rc != SQLITE_DONE) {
        std::cerr << "[StorageActor] UPDATE failed: " << sqlite3_errmsg(db()) << "\n";
    }

    sqlite3_reset(update_stmt_);

    std::cout << "[StorageActor] updated detection id=" << detection_id
              << " track=" << crop.track_id
              << " conf=" << crop.confidence << "\n";
}

bool StorageActor::init_db() {
    // Create the detections table if it doesn't exist
    const char* create_table = R"(
        CREATE TABLE IF NOT EXISTS detections (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   INTEGER NOT NULL,       -- milliseconds since epoch
            track_id    INTEGER NOT NULL,
            class_id    INTEGER NOT NULL,
            confidence  REAL NOT NULL,
            bbox_x      INTEGER NOT NULL DEFAULT 0,
            bbox_y      INTEGER NOT NULL DEFAULT 0,
            bbox_w      INTEGER NOT NULL DEFAULT 0,
            bbox_h      INTEGER NOT NULL DEFAULT 0,
            image_path  TEXT NOT NULL,
            session_id  INTEGER REFERENCES sessions(id)
        );
    )";

    if (!exec_sql(create_table)) {
        return false;
    }

    // Create index on timestamp for efficient queries
    if (!exec_sql(
            "CREATE INDEX IF NOT EXISTS idx_detections_timestamp "
            "ON detections(timestamp);")) {
        // Non-fatal
    }

    // Prepare the INSERT statement for reuse
    const char* insert_sql = R"(
        INSERT INTO detections
            (timestamp, track_id, class_id, confidence,
             bbox_x, bbox_y, bbox_w, bbox_h, image_path, session_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (!prepare_stmt(insert_sql, &insert_stmt_)) {
        return false;
    }

    // Prepare the UPDATE statement for reuse (update confidence, timestamp, image_path, session_id)
    const char* update_sql = R"(
        UPDATE detections
        SET timestamp = ?, confidence = ?, image_path = ?, session_id = ?
        WHERE id = ?
    )";

    if (!prepare_stmt(update_sql, &update_stmt_)) {
        // Non-fatal — updates will be skipped
        update_stmt_ = nullptr;
    }

    std::cout << "[StorageActor] SQLite ready (WAL mode, prepared INSERT + UPDATE)\n";
    return true;
}

} // namespace ct
