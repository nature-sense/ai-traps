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

#include "civetweb.h"
#include "ws_api_handler.hpp"
#include "../session/session_actor.hpp"
#include "../types.hpp"
#include "http-sse/http_sse_actor.h"
#include "data-store/data_store.hpp"
#include "../../pipeline/base_detection_pipeline.hpp"
#include <iostream>
#include <chrono>
#include <sys/statvfs.h>

namespace ct {

// ============================================================================
// Constructor
// ============================================================================

WsApiHandler::WsApiHandler() {
    std::cout << "[WsApiHandler] created\n";
}

// ============================================================================
// Main Entry Point
// ============================================================================

void WsApiHandler::handleRequest(const nlohmann::json& json_request,
                                  struct mg_connection* ws_conn) {
    // Validate required fields
    if (!json_request.contains("id") || !json_request.contains("method")) {
        sendError(0, 400, "Missing required fields: 'id' and 'method'", ws_conn);
        return;
    }

    int64_t id = json_request["id"].is_number() ? json_request["id"].get<int64_t>() : 0;
    std::string method = json_request["method"].get<std::string>();

    nlohmann::json params;
    if (json_request.contains("params") && json_request["params"].is_object()) {
        params = json_request["params"];
    }

    std::cout << "[WsApiHandler] method=" << method << " id=" << id << "\n";

    // Dispatch to method handler
    if (method == "provision") {
        handleProvision(params, id, ws_conn);
    } else if (method == "get_status") {
        handleGetStatus(params, id, ws_conn);
    } else if (method == "start_session") {
        handleStartSession(params, id, ws_conn);
    } else if (method == "stop_session") {
        handleStopSession(params, id, ws_conn);
    } else if (method == "list_sessions") {
        handleListSessions(params, id, ws_conn);
    } else if (method == "get_session") {
        handleGetSession(params, id, ws_conn);
    } else if (method == "get_active_session") {
        handleGetActiveSession(params, id, ws_conn);
    } else if (method == "list_detections") {
        handleListDetections(params, id, ws_conn);
    } else if (method == "get_detection") {
        handleGetDetection(params, id, ws_conn);
    } else if (method == "get_system_metrics") {
        handleGetSystemMetrics(params, id, ws_conn);
    } else {
        sendError(id, 404, "Unknown method: " + method, ws_conn);
    }
}

// ============================================================================
// Method Handlers
// ============================================================================

void WsApiHandler::handleProvision(const nlohmann::json& params, int64_t id,
                                    struct mg_connection* ws_conn) {
    if (!params.contains("trapId") || !params["trapId"].is_string()) {
        sendError(id, 400, "Missing or invalid trapId", ws_conn);
        return;
    }

    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    std::string trap_id = params["trapId"].get<std::string>();

    if (session_actor_->is_provisioned()) {
        sendError(id, 409, "Already provisioned", ws_conn);
        return;
    }

    session_actor_->provision(trap_id);

    nlohmann::json result = {
        {"trapId", trap_id},
        {"status", "provisioned"}
    };
    sendResult(id, result, ws_conn);
}

void WsApiHandler::handleGetStatus(const nlohmann::json& params, int64_t id,
                                    struct mg_connection* ws_conn) {
    (void)params;

    nlohmann::json result = {
        {"status", "running"},
        {"port", 8080}
    };

    if (session_actor_) {
        if (session_actor_->is_provisioned()) {
            result["trapId"] = session_actor_->trap_id();
        }

        auto active = session_actor_->active_session();
        if (active.id >= 0) {
            result["activeSession"] = {
                {"id", active.id},
                {"startedAt", active.started_at},
                {"detectionCount", active.detection_count},
                {"active", true}
            };
        } else {
            result["activeSession"] = nlohmann::json{{"active", false}};
        }
    }

    sendResult(id, result, ws_conn);
}

void WsApiHandler::handleStartSession(const nlohmann::json& params, int64_t id,
                                       struct mg_connection* ws_conn) {
    (void)params;

    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int64_t session_id = session_actor_->start_session();
    if (session_id < 0) {
        sendError(id, 500, "Failed to start session", ws_conn);
        return;
    }

    auto active = session_actor_->active_session();
    nlohmann::json result = {
        {"sessionId", session_id},
        {"startedAt", active.started_at},
        {"active", true}
    };
    sendResult(id, result, ws_conn);
}

void WsApiHandler::handleStopSession(const nlohmann::json& params, int64_t id,
                                      struct mg_connection* ws_conn) {
    if (!params.contains("session_id")) {
        sendError(id, 400, "Missing session_id", ws_conn);
        return;
    }

    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int64_t session_id = getInt64Param(params, "session_id");
    if (!session_actor_->stop_session(session_id)) {
        sendError(id, 400, "Session not active", ws_conn);
        return;
    }

    nlohmann::json result = {
        {"sessionId", session_id},
        {"status", "stopped"}
    };
    sendResult(id, result, ws_conn);
}

void WsApiHandler::handleListSessions(const nlohmann::json& params, int64_t id,
                                       struct mg_connection* ws_conn) {
    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int limit  = getIntParam(params, "limit", 50);
    int offset = getIntParam(params, "offset", 0);

    auto sessions = session_actor_->list_sessions(limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : sessions) {
        nlohmann::json j;
        j["id"]              = s.id;
        j["startedAt"]       = s.started_at;
        j["active"]          = s.active;
        j["detectionCount"]  = s.detection_count;
        if (s.stopped_at > 0) {
            j["stoppedAt"] = s.stopped_at;
        }
        arr.push_back(j);
    }

    sendResult(id, {{"sessions", arr}}, ws_conn);
}

void WsApiHandler::handleGetSession(const nlohmann::json& params, int64_t id,
                                     struct mg_connection* ws_conn) {
    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int64_t session_id = getInt64Param(params, "session_id");
    auto info = session_actor_->get_session(session_id);

    if (info.id < 0) {
        sendError(id, 404, "Session not found", ws_conn);
        return;
    }

    nlohmann::json result;
    result["id"]              = info.id;
    result["startedAt"]       = info.started_at;
    result["detectionCount"]  = info.detection_count;
    result["active"]          = info.active;
    if (info.stopped_at > 0) {
        result["stoppedAt"] = info.stopped_at;
    }

    sendResult(id, result, ws_conn);
}

void WsApiHandler::handleGetActiveSession(const nlohmann::json& params, int64_t id,
                                           struct mg_connection* ws_conn) {
    (void)params;

    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    auto info = session_actor_->active_session();
    if (info.id >= 0) {
        nlohmann::json result;
        result["id"]              = info.id;
        result["startedAt"]       = info.started_at;
        result["detectionCount"]  = info.detection_count;
        result["active"]          = true;
        sendResult(id, result, ws_conn);
    } else {
        sendResult(id, {{"active", false}}, ws_conn);
    }
}

void WsApiHandler::handleListDetections(const nlohmann::json& params, int64_t id,
                                         struct mg_connection* ws_conn) {
    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int64_t session_id = getInt64Param(params, "session_id");
    int limit  = getIntParam(params, "limit", 50);
    int offset = getIntParam(params, "offset", 0);

    auto detections = session_actor_->list_detections(session_id, limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : detections) {
        nlohmann::json j;
        j["id"]         = d.id;
        j["timestamp"]  = d.timestamp;
        j["trackId"]    = d.track_id;
        j["classId"]    = d.class_id;
        j["confidence"] = d.confidence;
        j["sessionId"]  = d.session_id;
        if (!d.image_path.empty()) {
            // Convert path to crop URL
            auto pos = d.image_path.find_last_of('/');
            if (pos != std::string::npos) {
                std::string filename = d.image_path.substr(pos + 1);
                std::string rest = d.image_path.substr(0, pos);
                auto pos2 = rest.find_last_of('/');
                if (pos2 != std::string::npos) {
                    std::string date = rest.substr(pos2 + 1);
                    j["imageUrl"] = "/v1/crops/" + date + "/" + filename;
                } else {
                    j["imageUrl"] = d.image_path;
                }
            } else {
                j["imageUrl"] = d.image_path;
            }
        }
        arr.push_back(j);
    }

    sendResult(id, {{"detections", arr}}, ws_conn);
}

void WsApiHandler::handleGetDetection(const nlohmann::json& params, int64_t id,
                                       struct mg_connection* ws_conn) {
    if (!session_actor_) {
        sendError(id, 500, "SessionActor not available", ws_conn);
        return;
    }

    int64_t detection_id = getInt64Param(params, "detection_id");
    auto det = session_actor_->get_detection(detection_id);

    if (det.id < 0) {
        sendError(id, 404, "Detection not found", ws_conn);
        return;
    }

    nlohmann::json result;
    result["id"]         = det.id;
    result["timestamp"]  = det.timestamp;
    result["trackId"]    = det.track_id;
    result["classId"]    = det.class_id;
    result["confidence"] = det.confidence;
    result["sessionId"]  = det.session_id;

    if (!det.image_path.empty()) {
        auto pos = det.image_path.find_last_of('/');
        if (pos != std::string::npos) {
            std::string filename = det.image_path.substr(pos + 1);
            std::string rest = det.image_path.substr(0, pos);
            auto pos2 = rest.find_last_of('/');
            if (pos2 != std::string::npos) {
                std::string date = rest.substr(pos2 + 1);
                result["imageUrl"] = "/v1/crops/" + date + "/" + filename;
            } else {
                result["imageUrl"] = det.image_path;
            }
        } else {
            result["imageUrl"] = det.image_path;
        }
    }

    sendResult(id, result, ws_conn);
}

// ---------------------------------------------------------------------------
// Helper: read a file and trim whitespace
// ---------------------------------------------------------------------------
static std::string readFile(const std::string& path) {
    std::string result;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[4096];
    while (auto n = std::fread(buf, 1, sizeof(buf) - 1, f)) {
        buf[n] = '\0';
        result += buf;
    }
    std::fclose(f);
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == ' ' || result.back() == '\t'))
        result.pop_back();
    return result;
}

// ---------------------------------------------------------------------------
// Helper: read /proc/stat and compute CPU usage %
// Returns -1 on failure
// ---------------------------------------------------------------------------
static double readCpuUsage(const std::string& prev_idle, const std::string& prev_total) {
    std::string stat = readFile("/proc/stat");
    if (stat.empty()) return -1.0;
    const char* line = stat.c_str();
    // Skip to first line starting with "cpu "
    while (*line && std::strncmp(line, "cpu ", 4) != 0) {
        while (*line && *line != '\n') ++line;
        if (*line == '\n') ++line;
    }
    if (std::strncmp(line, "cpu ", 4) != 0) return -1.0;
    long long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (std::sscanf(line, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
                    &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 4)
        return -1.0;
    long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    long long prev_i = 0, prev_t = 0;
    if (!prev_idle.empty()) prev_i = std::stoll(prev_idle);
    if (!prev_total.empty()) prev_t = std::stoll(prev_total);
    if (prev_t == 0) return -1.0; // First call — no delta yet
    long long delta_idle = idle - prev_i;
    long long delta_total = total - prev_t;
    if (delta_total == 0) return 0.0;
    return 100.0 * (1.0 - (double)delta_idle / (double)delta_total);
}

void WsApiHandler::handleGetSystemMetrics(const nlohmann::json& params, int64_t id,
                                           struct mg_connection* ws_conn) {
    (void)params;

    // ── Timestamp ──────────────────────────────────────────────────────────────
    auto now = std::chrono::system_clock::now();
    auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // ── Uptime ─────────────────────────────────────────────────────────────────
    int64_t uptime_seconds = 0;
    {
        std::string upt = readFile("/proc/uptime");
        if (!upt.empty()) {
            double up;
            if (std::sscanf(upt.c_str(), "%lf", &up) == 1)
                uptime_seconds = static_cast<int64_t>(up);
        }
    }

    nlohmann::json result;
    result["timestamp_ms"] = timestamp_ms;
    result["uptime_seconds"] = uptime_seconds;

    // ── CPU metrics ──────────────────────────────────────────────────────────
    {
        nlohmann::json cpu;
        // Temperature
        std::string temp = readFile("/sys/class/thermal/thermal_zone0/temp");
        if (!temp.empty()) {
            int millideg = 0;
            if (std::sscanf(temp.c_str(), "%d", &millideg) == 1)
                cpu["temp_celsius"] = millideg / 1000.0;
        }

        // Load average
        std::string load = readFile("/proc/loadavg");
        if (!load.empty()) {
            double l1 = 0, l5 = 0, l15 = 0;
            if (std::sscanf(load.c_str(), "%lf %lf %lf", &l1, &l5, &l15) == 3) {
                cpu["load_avg_1m"] = l1;
                cpu["load_avg_5m"] = l5;
                cpu["load_avg_15m"] = l15;
            }
        }

        // CPU frequency (first core)
        std::string freq = readFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (!freq.empty()) {
            int khz = 0;
            if (std::sscanf(freq.c_str(), "%d", &khz) == 1)
                cpu["freq_mhz"] = khz / 1000;
        }

        // CPU usage % — cached across calls for delta
        static std::string prev_idle, prev_total;
        cpu["usage_percent"] = readCpuUsage(prev_idle, prev_total);
        {
            // Read current values for next call's delta
            std::string stat = readFile("/proc/stat");
            if (!stat.empty()) {
                const char* line = stat.c_str();
                while (*line && std::strncmp(line, "cpu ", 4) != 0) {
                    while (*line && *line != '\n') ++line;
                    if (*line == '\n') ++line;
                }
                if (std::strncmp(line, "cpu ", 4) == 0) {
                    long long user, nice, sys, idle, iowait, irq, softirq, steal;
                    if (std::sscanf(line, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
                                    &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
                        long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
                        prev_idle = std::to_string(idle);
                        prev_total = std::to_string(total);
                    }
                }
            }
        }

        if (!cpu.empty()) result["cpu"] = cpu;
    }

    // ── Memory metrics ────────────────────────────────────────────────────────
    {
        std::string mem = readFile("/proc/meminfo");
        if (!mem.empty()) {
            nlohmann::json mem_json;
            auto extract = [&](const std::string& key) -> int64_t {
                auto pos = mem.find(key);
                if (pos == std::string::npos) return 0;
                pos += key.length();
                while (pos < mem.length() && (mem[pos] == ':' || mem[pos] == ' ')) ++pos;
                char buf[64];
                int bi = 0;
                while (pos < mem.length() && std::isdigit(mem[pos]) && bi < 63)
                    buf[bi++] = mem[pos++];
                buf[bi] = '\0';
                return std::atoll(buf);
            };
            int64_t total_kb = extract("MemTotal");
            int64_t available_kb = extract("MemAvailable");
            int64_t free_kb = extract("MemFree");
            int64_t swap_total = extract("SwapTotal");
            int64_t swap_free = extract("SwapFree");
            int64_t swap_used = (swap_total > 0) ? swap_total - swap_free : 0;

            if (total_kb > 0) {
                mem_json["total_kb"] = total_kb;
                mem_json["available_kb"] = available_kb;
                mem_json["free_kb"] = free_kb;
                mem_json["used_kb"] = total_kb - available_kb;
            }
            if (swap_total > 0) {
                mem_json["swap_total_kb"] = swap_total;
                mem_json["swap_used_kb"] = swap_used;
            }
            if (!mem_json.empty()) result["memory"] = mem_json;
        }
    }

    // ── Storage metrics ──────────────────────────────────────────────────────
    {
        nlohmann::json storage;
        auto stat_fs = [](const std::string& path) -> nlohmann::json {
            nlohmann::json j;
            struct statvfs buf;
            if (statvfs(path.c_str(), &buf) == 0) {
                unsigned long long total = (unsigned long long)buf.f_blocks * buf.f_frsize;
                unsigned long long free  = (unsigned long long)buf.f_bfree  * buf.f_frsize;
                j["total_bytes"] = static_cast<int64_t>(total);
                j["used_bytes"]  = static_cast<int64_t>(total - free);
            }
            return j;
        };
        auto root_json = stat_fs("/");
        if (!root_json.empty()) storage["root"] = root_json;

        // Output/detections dir if different from root
        std::string output_dir = "/tmp";
        if (session_actor_) {
            output_dir = session_actor_->output_dir();
        }
        if (output_dir != "/") {
            auto output_json = stat_fs(output_dir);
            if (!output_json.empty()) storage["captures"] = output_json;
        }

        if (!storage.empty()) result["storage"] = storage;
    }

    // ── NPU metrics ──────────────────────────────────────────────────────────
    {
        // Try Rockchip NPU thermal zone
        std::string npu_temp = readFile("/sys/class/thermal/thermal_zone1/temp");
        if (npu_temp.empty())
            npu_temp = readFile("/sys/class/thermal/thermal_zone2/temp");
        if (!npu_temp.empty()) {
            int millideg = 0;
            if (std::sscanf(npu_temp.c_str(), "%d", &millideg) == 1) {
                result["npu"] = nlohmann::json{
                    {"temp_celsius", millideg / 1000.0}
                };
            }
        }
    }

    // ── Pipeline metrics ─────────────────────────────────────────────────────
    {
        if (pipeline_) {
            nlohmann::json pm;
            int64_t frames = pipeline_->metricsFramesProcessed();
            pm["frames_processed"] = frames;

            double inference_us = pipeline_->metricsInferenceTimeUs();
            double tracking_us  = pipeline_->metricsTrackingTimeUs();
            double tick_us      = pipeline_->metricsTickTimeUs();

            if (inference_us > 0) pm["inference_time_us"] = inference_us;
            if (tracking_us > 0)  pm["tracking_time_us"]  = tracking_us;
            if (tick_us > 0)      pm["tick_time_us"]      = tick_us;

            // Compute FPS from frame interval
            double fps = pipeline_->config().camera.fps;
            pm["fps"] = fps;

            if (!pm.empty()) result["pipeline"] = pm;
        }
    }

    // ── Network metrics ──────────────────────────────────────────────────────
    {
        std::string net = readFile("/proc/net/dev");
        if (!net.empty()) {
            // Sum bytes across all non-loopback interfaces
            int64_t rx_total = 0, tx_total = 0;
            auto lines = net;
            size_t pos = 0;
            while ((pos = lines.find('\n', pos)) != std::string::npos) {
                ++pos;
                if (pos >= lines.length()) break;
                size_t end = lines.find('\n', pos);
                std::string line = lines.substr(pos, end - pos);
                // Skip header lines and loopback
                if (line.find("lo:") != std::string::npos || line.find("Inter-|") != std::string::npos
                    || line.find("face") != std::string::npos)
                    continue;
                // Look for interface with colon (e.g., "eth0:", "wlan0:")
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    long long rx = 0, tx = 0;
                    if (std::sscanf(line.c_str() + colon + 1, "%lld %*d %*d %*d %*d %*d %*d %*d %lld",
                                    &rx, &tx) >= 2) {
                        rx_total += rx;
                        tx_total += tx;
                    }
                }
                pos = end;
            }
            if (rx_total > 0 || tx_total > 0) {
                result["network"] = nlohmann::json{
                    {"rx_bytes", rx_total},
                    {"tx_bytes", tx_total}
                };
            }
        }
    }

    // ── Power metrics (if available via sysfs) ────────────────────────────────
    {
        std::string voltage = readFile("/sys/class/power_supply/battery/voltage_now");
        std::string current = readFile("/sys/class/power_supply/battery/current_now");
        if (!voltage.empty() || !current.empty()) {
            nlohmann::json pwr;
            if (!voltage.empty()) {
                int64_t uv = std::atoll(voltage.c_str());
                if (uv > 0) pwr["voltage_v"] = uv / 1'000'000.0;
            }
            if (!current.empty()) {
                int64_t ua = std::atoll(current.c_str());
                if (ua > 0) pwr["current_a"] = ua / 1'000'000.0;
            }
            if (!pwr.empty()) result["power"] = pwr;
        }
    }

    sendResult(id, result, ws_conn);
}

// ============================================================================
// Helpers — Send JSON RPC responses via WebSocket
// ============================================================================

void WsApiHandler::sendResult(int64_t id, const nlohmann::json& result,
                               struct mg_connection* ws_conn) {
    if (!http_sse_actor_ || !ws_conn) return;

    nlohmann::json response = {
        {"id", id},
        {"result", result}
    };

    std::string json_str = response.dump();
    http_sse_actor_->sendWsTextTo(ws_conn, json_str);
}

void WsApiHandler::sendError(int64_t id, int code, const std::string& message,
                              struct mg_connection* ws_conn) {
    if (!http_sse_actor_ || !ws_conn) return;

    nlohmann::json response = {
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };

    std::string json_str = response.dump();
    http_sse_actor_->sendWsTextTo(ws_conn, json_str);
}

// ============================================================================
// Parameter Helpers
// ============================================================================

std::string WsApiHandler::getStringParam(const nlohmann::json& params,
                                          const std::string& key,
                                          const std::string& default_val) const {
    if (params.contains(key) && params[key].is_string()) {
        return params[key].get<std::string>();
    }
    return default_val;
}

int64_t WsApiHandler::getInt64Param(const nlohmann::json& params,
                                     const std::string& key,
                                     int64_t default_val) const {
    if (params.contains(key) && params[key].is_number()) {
        return params[key].get<int64_t>();
    }
    return default_val;
}

int WsApiHandler::getIntParam(const nlohmann::json& params,
                               const std::string& key,
                               int default_val) const {
    if (params.contains(key) && params[key].is_number()) {
        return params[key].get<int>();
    }
    return default_val;
}

} // namespace ct