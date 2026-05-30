#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include "base-storage/base_storage.hpp"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declaration of sqlite3_stmt
struct sqlite3_stmt;

namespace ct {

// ─── StorageActor ──────────────────────────────────────────────────────────────
// Receives JPEG crops from CropperNode, writes them to disk, and records
// metadata in SQLite.  Notifies an optional callback (used by EventBus / SSE)
// for each saved crop.
//
// Detection lifecycle:
//   - First crop for a track → INSERT new row, save JPEG, emit on_saved event
//   - Update crop for a track → UPDATE existing row, replace JPEG file, emit on_saved event
//
// SQLite is opened in WAL mode so the REST server thread can issue reads
// concurrently without blocking writes here.
//
// File layout:
//   {output_dir}/YYYY-MM-DD/{timestamp_ms}_{track_id}.jpg
struct StorageActor : public BaseStorage {
    // Fired after each crop is saved; caller wires this to EventBus::publish()
    // detection_id is the SQLite rowid (set after INSERT, or the existing rowid for UPDATE).
    using OnSaved = std::function<void(const JpegCrop&, const std::string& path, int64_t detection_id)>;

    // ── Input ─────────────────────────────────────────────────────────────────
    ramen::Pushable<std::vector<JpegCrop>> in_crops =
        [this](const std::vector<JpegCrop>& crops) {
            for (const auto& crop : crops) {
                if (crop.is_update) {
                    // Update existing detection — replace image + metadata
                    auto it = track_detection_map_.find(crop.track_id);
                    if (it != track_detection_map_.end()) {
                        // Reuse the existing filename so we overwrite the old file
                        auto path = save_jpeg(crop, it->second.filename);
                        if (!path.empty()) {
                            update_metadata(crop, path, it->second.detection_id);
                            if (on_saved_) on_saved_(crop, path, it->second.detection_id);
                        }
                    }
                } else {
                    // New detection — insert new row
                    auto path = save_jpeg(crop);
                    if (!path.empty()) {
                        int64_t detection_id = insert_metadata(crop, path);
                        if (detection_id >= 0) {
                            track_detection_map_[crop.track_id] = {detection_id, path};
                        }
                        if (on_saved_) on_saved_(crop, path, detection_id);
                    }
                }
            }
        };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Initialise storage: open SQLite database and create tables.
    // Calls BaseStorage::init() internally.
    bool init(const std::string& output_dir, const std::string& db_path);

    // Shutdown: finalize prepared statements, close database.
    // Calls BaseStorage::shutdown() internally.
    void shutdown() override;

    void set_on_saved(OnSaved cb) { on_saved_ = std::move(cb); }

    // Reset storage node state — clears the track→detection mapping so that
    // crops from a new scene loop are treated as fresh INSERTs rather than
    // updates to old detection rows. Called on scene loop and session start.
    void reset() {
        track_detection_map_.clear();
    }

    // ── Session binding ───────────────────────────────────────────────────────
    // Set a callback that returns the current active session_id (or -1 if none).
    // The session_id is stored alongside each detection in SQLite.
    using SessionIdFn = std::function<int64_t()>;
    void set_session_id_fn(SessionIdFn fn) { session_id_fn_ = std::move(fn); }

private:
    // Track file info: detection_id + the filename used on disk.
    // For updates, we reuse the same filename so the old file is overwritten.
    struct TrackFileInfo {
        int64_t     detection_id = -1;
        std::string filename;       // e.g. "2026-05-20/1779290426595_1.jpg"
    };

    std::string save_jpeg(const JpegCrop& crop);
    std::string save_jpeg(const JpegCrop& crop, const std::string& existing_filename);
    int64_t insert_metadata(const JpegCrop& crop, const std::string& path);
    void update_metadata(const JpegCrop& crop, const std::string& path, int64_t detection_id);
    bool init_db();

    std::string output_dir_;
    OnSaved     on_saved_;
    SessionIdFn session_id_fn_;

    // Prepared statements
    sqlite3_stmt* insert_stmt_ = nullptr;  // prepared INSERT INTO detections
    sqlite3_stmt* update_stmt_ = nullptr;  // prepared UPDATE detections

    // Track detection_id + filename per track_id (for updates)
    std::unordered_map<int, TrackFileInfo> track_detection_map_;
};

} // namespace ct
