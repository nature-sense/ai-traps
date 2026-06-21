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

// actors/HttpSseActor.hpp
#pragma once

#include <ramen.hpp>
#include "civetweb.h"
#include <regex>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <memory>
#include <optional>
#include <cstdint>
#include <functional>
#include <thread>
#include <chrono>

namespace ct {

// ============================================================================
// Message Types
// ============================================================================

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> query_params;
    uint64_t request_id;
};

struct HttpResponse {
    uint64_t request_id;
    int status_code;
    std::string body;
    std::string content_type = "application/json";
};

// SseEvent and MjpegFrame are kept for backward compatibility with other
// actors that send these messages. They are dispatched to WebSocket
// connections instead of SSE/MJPEG HTTP endpoints.
struct SseEvent {
    std::string event_type;
    std::string data;
    std::string id;
};

struct MjpegFrame {
    std::vector<uint8_t> data;
    uint64_t timestamp;
    int width;
    int height;
    int quality;
};

// ============================================================================
// Route Definition
// ============================================================================

struct HttpRoute {
    std::string method;
    std::string path_pattern;
    std::string handler_actor;
    std::regex pattern_regex;
    std::vector<std::string> param_names;
    
    HttpRoute(const std::string& method, 
              const std::string& pattern, 
              const std::string& handler);
    
    bool matches(const std::string& method, 
                 const std::string& path,
                 std::unordered_map<std::string, std::string>& params) const;
};

// ============================================================================
// Registration Messages
// ============================================================================

struct RegisterRoutes {
    std::string handler_actor;
    std::vector<HttpRoute> routes;
};

struct UnregisterRoutes {
    std::string handler_actor;
};

// ============================================================================
// HttpSseActor - Combined HTTP Server + Router + Unified WebSocket
// ============================================================================
//
// Replaces legacy SSE and MJPEG HTTP endpoints with a single WebSocket
// endpoint at /ws:
//   - Binary frames → JPEG images
//   - Text frames   → JSON events (status, detections)
//
// Legacy HTTP endpoints are removed but the SSE/MJPEG message types are
// kept so other actors can still send SseEvent and MjpegFrame messages.
// These are now dispatched to all connected WebSocket clients.
// ============================================================================

class HttpSseActor : public ramen::Actor {
public:
    explicit HttpSseActor(int port = 8080);
    ~HttpSseActor() override;

    void onStart() override;
    void onStop() override;
    
    // Message handlers (dispatched via onMessageAny)
    void handleHttpResponse(HttpResponse& response);
    void handleSseEvent(SseEvent& event);
    void handleRegisterRoutes(RegisterRoutes& registration);
    void handleUnregisterRoutes(UnregisterRoutes& unregistration);
    void handleMjpegFrame(MjpegFrame& frame);

    // Type-erased dispatch
    void onMessageAny(const std::type_info& type, void* msg) override;

    // Public configuration: base directory for crop image serving at /v1/crops/
    std::string crop_base_dir_;

    // Fast-path pointer to the SessionActor (optional). When set, the
    // requestHandler can respond to trivial queries (status, active session)
    // synchronously without routing through the Ramen message queue, which
    // would add ~135ms minimum latency.
    class SessionActor* session_actor_ = nullptr;

    // Set the SessionActor pointer for fast-path queries.
    void setSessionActor(class SessionActor* actor) { session_actor_ = actor; }

    // Set a callback for JSON-RPC over WebSocket routing.
    // The callback receives the JSON text, the mg_connection pointer, and should
    // send responses directly via mg_websocket_write or sendWsTextTo().
    // This avoids the toolkit layer depending on targets-specific WsApiHandler.
    // Send a text frame to a specific WebSocket connection.
    // Used by WsApiHandler (in the targets layer) to reply to JSON-RPC requests.
    void sendWsTextTo(struct mg_connection* conn, const std::string& message);

    // Callback for JSON-RPC over WebSocket routing.
    using WsApiCallback = std::function<void(const std::string& json_text, struct mg_connection* conn)>;
    void setWsApiCallback(WsApiCallback cb) { ws_api_callback_ = std::move(cb); }

private:
    // CivetWeb callbacks — HTTP
    static int requestHandler(struct mg_connection* conn);
    
    // Request type handlers (HTTP)
    int handleRestRequest(struct mg_connection* conn);
    int handleHealthCheck(struct mg_connection* conn);
    
    // CivetWeb callbacks — WebSocket
    static int wsConnectHandler(const struct mg_connection* conn, void* cbdata);
    static void wsReadyHandler(struct mg_connection* conn, void* cbdata);
    static int wsDataHandler(struct mg_connection* conn,
                             int bits,
                             char* data,
                             size_t data_len,
                             void* cbdata);
    static void wsCloseHandler(const struct mg_connection* conn, void* cbdata);
    
    // WebSocket client management
    struct WsClient {
        struct mg_connection* conn;
        std::chrono::steady_clock::time_point connected_at;
    };
    void broadcastWsText(const std::string& message);
    void broadcastWsBinary(const std::vector<uint8_t>& data);

    std::mutex ws_mutex_;
    // Using a vector since we track by raw pointer (conn identity)
    std::vector<struct mg_connection*> ws_clients_;
    
    // Route management
    bool findRoute(const std::string& method, const std::string& path,
                   std::string& handler_actor,
                   std::unordered_map<std::string, std::string>& params);
    void addRoutes(const RegisterRoutes& registration);
    void removeRoutes(const std::string& handler_actor);
    
    // Response handling
    void sendResponse(uint64_t request_id, int status_code, const std::string& body);
    void sendErrorResponse(uint64_t request_id, int status_code, const std::string& message);
    
    // Helpers
    std::unordered_map<std::string, std::string> parseQueryParams(const std::string& query);
    uint64_t getTimestampMs();

    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // Server state
    int port_;
    struct mg_context* ctx_{nullptr};
    std::atomic<bool> running_{false};
    
    // Route table
    std::vector<HttpRoute> routes_;
    std::mutex routes_mutex_;
    
    // Pending REST requests (request_id -> connection)
    std::unordered_map<uint64_t, struct mg_connection*> pending_requests_;
    std::mutex pending_mutex_;
    std::atomic<uint64_t> next_request_id_{1};
    
    // Legacy SSE/MJPEG members removed. Instead, WebSocket clients handle
    // both event streaming (text frames) and image streaming (binary frames).

    // Latest frame cache (for new WebSocket clients on connect)
    std::optional<MjpegFrame> latest_frame_;
    std::mutex frame_mutex_;

    // JSON-RPC callback set by the pipeline layer (avoids toolkit→targets dependency)
    WsApiCallback ws_api_callback_;
};

} // namespace ct