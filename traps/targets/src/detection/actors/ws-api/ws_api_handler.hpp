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

// Forward declaration for CivetWeb's mg_connection (included at file level
// before any namespace declarations in the .cpp to avoid type mangling issues).
struct mg_connection;

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <cstdint>

namespace ct {

// Forward declarations
class SessionActor;
class HttpSseActor;
class BaseDetectionPipeline;

// ============================================================================
// WsApiHandler — JSON RPC over WebSocket
// ============================================================================
//
// Handles JSON text frames received on the /ws WebSocket endpoint.
// Clients send requests in this format:
//
//   {
//     "id": 1,
//     "method": "list_sessions",
//     "params": {"limit": 50, "offset": 0}
//   }
//
// The handler dispatches to the appropriate method and sends back:
//
//   Success: {"id": 1, "result": {...}}
//   Error:   {"id": 1, "error": {"code": 400, "message": "..."}}
//
// Method list (replaces former REST endpoints):
//   - provision        (params: {trapId})
//   - get_status       (params: {} — returns trapId, activeSession, server status)
//   - start_session    (params: {})
//   - stop_session     (params: {session_id})
//   - list_sessions    (params: {limit, offset})
//   - get_session      (params: {session_id})
//   - get_active_session (params: {})
//   - list_detections  (params: {session_id, limit, offset})
//   - get_detection    (params: {detection_id})
//   - get_system_metrics (params: {})
//
// Note: The HttpSseActor must be set before any API calls are processed.
// ============================================================================

class WsApiHandler {
public:
    WsApiHandler();

    // ── Bindings ───────────────────────────────────────────────────────────
    // Set the SessionActor for querying/controlling session state.
    void setSessionActor(SessionActor* actor) { session_actor_ = actor; }

    // Set the HttpSseActor for replying on the WebSocket connection.
    void setHttpSseActor(HttpSseActor* actor) { http_sse_actor_ = actor; }

    // Set the BaseDetectionPipeline for reading pipeline metrics.
    void setPipeline(BaseDetectionPipeline* pipeline) { pipeline_ = pipeline; }

    // ── Main entry point ───────────────────────────────────────────────────
    // Handle a JSON RPC request received on a WebSocket text frame.
    // Sends the response directly as a WebSocket text frame via HttpSseActor.
    // json_request: the parsed JSON object from the client.
    // ws_conn: the CivetWeb mg_connection for replying.
    void handleRequest(const nlohmann::json& json_request,
                       struct mg_connection* ws_conn);

private:
    // ── Method handlers ────────────────────────────────────────────────────
    void handleProvision(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleGetStatus(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleStartSession(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleStopSession(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleListSessions(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleGetSession(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleGetActiveSession(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleListDetections(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleGetDetection(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);
    void handleGetSystemMetrics(const nlohmann::json& params, int64_t id, struct mg_connection* ws_conn);

    // ── Helpers ────────────────────────────────────────────────────────────
    // Send a JSON-RPC success response via WebSocket
    void sendResult(int64_t id, const nlohmann::json& result, struct mg_connection* ws_conn);

    // Send a JSON-RPC error response via WebSocket
    void sendError(int64_t id, int code, const std::string& message, struct mg_connection* ws_conn);

    // Get a string parameter from params JSON, with error handling
    std::string getStringParam(const nlohmann::json& params, const std::string& key,
                               const std::string& default_val = "") const;

    // Get an int64 parameter from params JSON
    int64_t getInt64Param(const nlohmann::json& params, const std::string& key,
                          int64_t default_val = 0) const;

    // Get an int parameter from params JSON
    int getIntParam(const nlohmann::json& params, const std::string& key,
                    int default_val = 0) const;

    // ── Pointers (non-owning) ──────────────────────────────────────────────
    SessionActor*           session_actor_  = nullptr;
    HttpSseActor*           http_sse_actor_ = nullptr;
    BaseDetectionPipeline*  pipeline_       = nullptr;
};

} // namespace ct