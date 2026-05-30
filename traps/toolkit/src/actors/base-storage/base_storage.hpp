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

#include <string>
#include <iostream>
#include <sqlite3.h>

namespace ct {

// ─── BaseStorage ──────────────────────────────────────────────────────────────
// Header-only base class for storage actors that use SQLite. Manages the opening
// and closing of the database connection. Derived classes implement their own
// table schemas and query logic.
//
// Usage:
//   class MyStorage : public BaseStorage {
//     bool init(const std::string& db_path) override {
//       if (!BaseStorage::init(db_path)) return false;
//       // create my tables, prepare statements...
//       return true;
//     }
//   };
class BaseStorage {
public:
    virtual ~BaseStorage() { shutdown(); }

    // Open (or create) the SQLite database at db_path.
    // Returns true on success. Safe to call multiple times — subsequent calls
    // are no-ops if already open.
    virtual bool init(const std::string& db_path) {
        if (db_) return true;  // already open

        std::cout << "[BaseStorage] opening database: " << db_path << "\n";

        int rc = sqlite3_open_v2(db_path.c_str(), &db_,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                  nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[BaseStorage] sqlite3_open_v2 failed: "
                      << sqlite3_errmsg(db_) << "\n";
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }

        exec_pragma("journal_mode=WAL");
        std::cout << "[BaseStorage] database ready\n";
        return true;
    }

    // Close the database. Safe to call multiple times.
    virtual void shutdown() {
        if (!db_) return;
        std::cout << "[BaseStorage] closing database\n";
        sqlite3_close(db_);
        db_ = nullptr;
    }

    // Access the raw sqlite3 handle for use by derived classes.
    sqlite3* db() const { return db_; }

protected:
    // Execute a PRAGMA statement. Returns true on success.
    bool exec_pragma(const std::string& pragma) {
        if (!db_) return false;
        char* err_msg = nullptr;
        std::string sql = "PRAGMA " + pragma + ";";
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::cerr << "[BaseStorage] PRAGMA " << pragma << " failed: "
                      << (err_msg ? err_msg : "unknown") << "\n";
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    // Execute a CREATE TABLE / CREATE INDEX statement. Returns true on success.
    bool exec_sql(const std::string& sql) {
        if (!db_) return false;
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::cerr << "[BaseStorage] SQL exec failed: "
                      << (err_msg ? err_msg : "unknown") << "\n"
                      << "  SQL: " << sql << "\n";
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    // Prepare a SQL statement. Returns true on success.
    // The prepared statement pointer is written to stmt_out.
    bool prepare_stmt(const std::string& sql, sqlite3_stmt** stmt_out) {
        if (!db_ || !stmt_out) return false;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, stmt_out, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[BaseStorage] sqlite3_prepare_v2 failed: "
                      << sqlite3_errmsg(db_) << "\n"
                      << "  SQL: " << sql << "\n";
            *stmt_out = nullptr;
            return false;
        }
        return true;
    }

    sqlite3* db_ = nullptr;
};

} // namespace ct
