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

// actors/HttpSseActor.cpp
#include "http_sse_actor.h"
#include "mjpeg-bridge/mjpeg_bridge_actor.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace ct {

// ============================================================================
// HttpRoute Implementation
// ============================================================================

HttpRoute::HttpRoute(const std::string& method,
                     const std::string& pattern,
                     const std::string& handler)
    : method(method)
    , path_pattern(pattern)
    , handler_actor(handler) {

    std::string regex_pattern = pattern;
    std::regex param_regex("\\{([^}]+)\\}");
    std::smatch match;
    std::string::const_iterator searchStart(pattern.cbegin());

    while (std::regex_search(searchStart, pattern.cend(), match, param_regex)) {
        param_names.push_back(match[1]);
        searchStart = match[0].second;
    }

    regex_pattern = std::regex_replace(regex_pattern, param_regex, "([^/]+)");
    pattern_regex = std::regex("^" + regex_pattern + "$");
}

bool HttpRoute::matches(const std::string& method,
                        const std::string& path,
                        std::unordered_map<std::string, std::string>& params) const {
    if (this->method != method) return false;

    std::smatch matches;
    if (!std::regex_match(path, matches, pattern_regex)) return false;

    params.clear();
    for (size_t i = 0; i < param_names.size() && i + 1 < matches.size(); i++) {
        params[param_names[i]] = matches[i + 1];
    }

    return true;
}

// ============================================================================
// HttpSseActor Implementation
// ============================================================================

HttpSseActor::HttpSseActor(int port) : port_(port) {}

HttpSseActor::~HttpSseActor() {
    if (running_) onStop();
}

void HttpSseActor::onStart() {
    running_ = true;

    // Build options array with stable strings
    std::string port_str = std::to_string(port_);
    const char* options[] = {
        "listening_ports", port_str.c_str(),
        "num_threads", "12",
        "enable_keep_alive", "yes",
        "keep_alive_timeout_ms", "30000",
        nullptr
    };

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = &requestHandler;

    ctx_ = mg_start(&callbacks, this, options);
    if (!ctx_) {
        running_ = false;
        return;
    }

    // Register WebSocket handler at /ws
    // This replaces the legacy /events (SSE) and /stream.mjpg /stream_hires.mjpg (MJPEG)
    // endpoints with a single unified WebSocket endpoint:
    //   - Binary frames → JPEG images
    //   - Text frames   → JSON events (status, detections)
    mg_set_websocket_handler(
        ctx_,
        "/ws",
        HttpSseActor::wsConnectHandler,
        HttpSseActor::wsReadyHandler,
        HttpSseActor::wsDataHandler,
        HttpSseActor::wsCloseHandler,
        this
    );
}

void HttpSseActor::onStop() {
    running_ = false;

    // Clear WebSocket clients
    {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        ws_clients_.clear();
    }

    if (ctx_) {
        mg_stop(ctx_);
        ctx_ = nullptr;
    }

    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_.clear();
}

// ============================================================================
// Type-erased dispatch
// ============================================================================

void HttpSseActor::onMessageAny(const std::type_info& type, void* msg) {
    if (type == typeid(HttpResponse)) {
        handleHttpResponse(*static_cast<HttpResponse*>(msg));
    } else if (type == typeid(SseEvent)) {
        handleSseEvent(*static_cast<SseEvent*>(msg));
    } else if (type == typeid(RegisterRoutes)) {
        handleRegisterRoutes(*static_cast<RegisterRoutes*>(msg));
    } else if (type == typeid(UnregisterRoutes)) {
        handleUnregisterRoutes(*static_cast<UnregisterRoutes*>(msg));
    } else if (type == typeid(MjpegFrame)) {
        handleMjpegFrame(*static_cast<MjpegFrame*>(msg));
    }
}

// ============================================================================
// Route Registration
// ============================================================================

void HttpSseActor::handleRegisterRoutes(RegisterRoutes& registration) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    addRoutes(registration);
}

void HttpSseActor::handleUnregisterRoutes(UnregisterRoutes& unregistration) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    removeRoutes(unregistration.handler_actor);
}

void HttpSseActor::addRoutes(const RegisterRoutes& registration) {
    for (const auto& route : registration.routes) {
        routes_.push_back(route);
    }
}

void HttpSseActor::removeRoutes(const std::string& handler_actor) {
    routes_.erase(
        std::remove_if(routes_.begin(), routes_.end(),
            [&handler_actor](const HttpRoute& route) {
                return route.handler_actor == handler_actor;
            }),
        routes_.end()
    );
}

bool HttpSseActor::findRoute(const std::string& method, const std::string& path,
                              std::string& handler_actor,
                              std::unordered_map<std::string, std::string>& params) {
    std::lock_guard<std::mutex> lock(routes_mutex_);

    for (const auto& route : routes_) {
        if (route.matches(method, path, params)) {
            handler_actor = route.handler_actor;
            return true;
        }
    }
    return false;
}

// ============================================================================
// CivetWeb Request Handler
// ============================================================================

int HttpSseActor::requestHandler(struct mg_connection* conn) {
    // Retrieve the actor instance from civetweb's user_data
    struct mg_context* ctx = mg_get_context(conn);
    if (!ctx) return 0;
    auto* actor = static_cast<HttpSseActor*>(mg_get_user_data(ctx));
    if (!actor || !actor->running_) return 0;

    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!req_info) return 0;

    const char* uri = req_info->local_uri ? req_info->local_uri : "";

    // Health check endpoint
    if (strcmp(uri, "/health") == 0) {
        return actor->handleHealthCheck(conn);
    }

    // NOTE: /events (SSE) and /stream.mjpg (MJPEG) endpoints are removed.
    // /status now handled by the REST route registered in HttpHandlerActor,
    // which includes the trapId from the session actor.

    // Return 0 for known paths that civetweb handles internally (WebSocket)
    // so the begin_request callback doesn't block the WebSocket upgrade.
    if (strcmp(uri, "/ws") == 0) {
        return 0; // Let civetweb handle the WebSocket upgrade
    }

    // ── Crop image serving ────────────────────────────────────────────────
    // Serve JPEG files from /v1/crops/{date}/{filename} by reading them from
    // the output directory (known at init time). This avoids the need for
    // Ramen messaging for binary responses.
    if (strncmp(uri, "/v1/crops/", 10) == 0) {
        // URI format: /v1/crops/2026-06-14/1781436445038_2.jpg
        const char* crop_path = uri + 10; // skip "/v1/crops/"
        // Sanitize: no ".." allowed
        if (strstr(crop_path, "..") != nullptr) {
            mg_printf(conn, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
            return 1;
        }
        // Build full path. We use a known base directory.
        // The output dir is passed via config at startup.
        std::string full_path = actor->crop_base_dir_ + "/" + crop_path;
        FILE* f = fopen(full_path.c_str(), "rb");
        if (!f) {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(fsize);
        if (fsize > 0) {
            fread(buf.data(), 1, fsize, f);
        }
        fclose(f);

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %ld\r\n"
            "Cache-Control: public, max-age=86400\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n", fsize);
        if (fsize > 0) {
            mg_write(conn, buf.data(), fsize);
        }
        return 1;
    }

    // Try REST routes (includes /status, /v1/*)
    return actor->handleRestRequest(conn);
}

// ============================================================================
// REST Request Handler
// ============================================================================

int HttpSseActor::handleRestRequest(struct mg_connection* conn) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);

    std::string method = req_info->request_method ? req_info->request_method : "";
    std::string path = req_info->local_uri ? req_info->local_uri : "";

    // Find matching route
    std::string handler_actor;
    std::unordered_map<std::string, std::string> path_params;

    if (!findRoute(method, path, handler_actor, path_params)) {
        mg_printf(conn,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            R"({"error":"Not found"})");
        return 1; // MG_REQUEST_PROCESSED
    }

    uint64_t request_id = next_request_id_++;

    // Read POST body
    std::string body;
    if (req_info->content_length > 0) {
        body.resize(req_info->content_length);
        mg_read(conn, &body[0], req_info->content_length);
    }

    // Store connection for response
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_[request_id] = conn;
    }

    // Build and forward request to handler
    HttpRequest actor_req;
    actor_req.method = method;
    actor_req.path = path;
    actor_req.body = body;
    actor_req.request_id = request_id;
    actor_req.params = std::move(path_params);

    if (req_info->query_string) {
        actor_req.query_params = parseQueryParams(req_info->query_string);
    }

    ramen::send(handler_actor, std::move(actor_req));

    return 1; // MG_REQUEST_PROCESSED
}

// ============================================================================
// Health Check Handler
// ============================================================================

int HttpSseActor::handleHealthCheck(struct mg_connection* conn) {
    std::string response = R"({
        "status": "running",
        "ws_clients": )" + std::to_string(ws_clients_.size()) + R"(
    })";

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        response.size(), response.c_str());

    return 1; // MG_REQUEST_PROCESSED
}

// ============================================================================
// Response Handling
// ============================================================================

void HttpSseActor::sendResponse(uint64_t request_id, int status_code, const std::string& body) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_requests_.find(request_id);
    if (it == pending_requests_.end()) return;

    struct mg_connection* conn = it->second;
    mg_printf(conn,
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status_code, body.size(), body.c_str());

    pending_requests_.erase(it);
}

void HttpSseActor::sendErrorResponse(uint64_t request_id, int status_code, const std::string& message) {
    std::string body = R"({"error":")" + message + R"("})";
    sendResponse(request_id, status_code, body);
}

void HttpSseActor::handleHttpResponse(HttpResponse& response) {
    sendResponse(response.request_id, response.status_code, response.body);
}

// ============================================================================
// WebSocket Implementation
// ============================================================================
//
// The /ws WebSocket endpoint replaces both /events (SSE) and /stream.mjpg
// (MJPEG). It sends:
//
//   Binary frames: JPEG image data (from MjpegFrame messages)
//   Text frames:   JSON event payloads (from SseEvent messages), e.g.:
//                  {"event":"session_started","sessionId":1,"startedAt":...}
//
// When a new client connects, it immediately receives the latest cached
// JPEG frame (if available) as a binary message.
// ============================================================================

int HttpSseActor::wsConnectHandler(const struct mg_connection* conn, void* cbdata) {
    // Allow all WebSocket connection requests
    (void)conn;
    (void)cbdata;
    return 0; // 0 = proceed with handshake
}

void HttpSseActor::wsReadyHandler(struct mg_connection* conn, void* cbdata) {
    // WebSocket handshake completed. Add client to our list.
    auto* actor = static_cast<HttpSseActor*>(cbdata);
    if (!actor || !actor->running_) return;

    {
        std::lock_guard<std::mutex> lock(actor->ws_mutex_);
        actor->ws_clients_.push_back(conn);
    }

    // Send a welcome text frame to keep the connection alive
    std::string welcome = R"({"event":"connected","data":{}})";
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT,
                       welcome.c_str(), welcome.size());

    // Send the latest cached frame immediately (if available)
    MjpegFrame cached;
    bool have_cached = false;
    {
        std::lock_guard<std::mutex> lock(actor->frame_mutex_);
        if (actor->latest_frame_.has_value()) {
            cached = actor->latest_frame_.value();
            have_cached = true;
        }
    }

    if (have_cached && !cached.data.empty()) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY,
                           reinterpret_cast<const char*>(cached.data.data()),
                           cached.data.size());
    }

    // Signal the MJPEG bridge that a client connected, so it starts
    // encoding frames and sending MjpegFrame messages.
    auto* registry = ramen::ActorRegistry::instance();
    auto* bridge = registry ? registry->get("mjpeg_bridge") : nullptr;
    if (bridge) {
        auto* mjpeg_bridge = static_cast<ct::MjpegBridgeActor*>(bridge);
        mjpeg_bridge->client_connected();
    }
}

int HttpSseActor::wsDataHandler(struct mg_connection* conn,
                                int bits,
                                char* data,
                                size_t data_len,
                                void* cbdata) {
    auto* actor = static_cast<HttpSseActor*>(cbdata);
    if (!actor || !actor->running_) {
        return 0; // Close connection
    }

    // Handle WebSocket control frames
    int opcode = bits & 0x0f;

    if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) {
        return 0; // Close this connection
    }

    if (opcode == MG_WEBSOCKET_OPCODE_PING) {
        // Respond with Pong
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_PONG, data, data_len);
        return 1; // Keep connection open
    }

    if (opcode == MG_WEBSOCKET_OPCODE_PONG) {
        return 1; // Ignore pong, keep connection open
    }

    // ── Handle TEXT frames: try JSON-RPC first, fall back to event handling ──
    if (opcode == MG_WEBSOCKET_OPCODE_TEXT && data_len > 0) {
        std::string text(data, data_len);

        // Try to parse as JSON
        try {
            auto json = nlohmann::json::parse(text);

            // If it has "id" and "method", it's a JSON-RPC request → route via callback
            if (json.contains("id") && json.contains("method")) {
                if (actor->ws_api_callback_) {
                    actor->ws_api_callback_(text, conn);
                } else {
                    // Callback not set — send error response
                    nlohmann::json error_resp = {
                        {"id", json["id"]},
                        {"error", {{"code", 503}, {"message", "API handler not available"}}}
                    };
                    std::string error_str = error_resp.dump();
                    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT,
                                       error_str.c_str(), error_str.size());
                }
                return 1;
            }
            // Otherwise it might be a push event — ignore for now
        } catch (const std::exception&) {
            // Not valid JSON — ignore the frame
            (void)text;
        }
    }

    // BINARY frames from client are currently ignored
    (void)data;
    (void)data_len;

    return 1; // Keep connection open
}

void HttpSseActor::wsCloseHandler(const struct mg_connection* conn, void* cbdata) {
    // WebSocket connection closed. Remove client from our list.
    auto* actor = static_cast<HttpSseActor*>(cbdata);
    if (!actor) return;

    std::lock_guard<std::mutex> lock(actor->ws_mutex_);
    auto& clients = actor->ws_clients_;
    // Find by raw pointer identity
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
            [conn](struct mg_connection* c) { return c == conn; }),
        clients.end()
    );
}

// ============================================================================
// WebSocket Send to Specific Connection
// ============================================================================

void HttpSseActor::sendWsTextTo(struct mg_connection* conn, const std::string& message) {
    if (!conn) return;
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT,
                       message.c_str(), message.size());
}

// ============================================================================
// WebSocket Broadcast
// ============================================================================

void HttpSseActor::broadcastWsText(const std::string& message) {
    std::lock_guard<std::mutex> lock(ws_mutex_);

    // Iterate in reverse so erasing doesn't invalidate remaining indices
    for (auto it = ws_clients_.rbegin(); it != ws_clients_.rend(); ) {
        struct mg_connection* conn = *it;
        if (!conn) {
            it = decltype(it)(ws_clients_.erase(std::next(it).base()));
            continue;
        }

        int ret = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT,
                                     message.c_str(), message.size());
        if (ret <= 0) {
            // Write failed — connection is dead, remove it
            // Convert reverse iterator to forward
            auto forward = std::next(it).base();
            it = decltype(it)(ws_clients_.erase(forward));
        } else {
            ++it;
        }
    }
}

void HttpSseActor::broadcastWsBinary(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(ws_mutex_);

    for (auto it = ws_clients_.rbegin(); it != ws_clients_.rend(); ) {
        struct mg_connection* conn = *it;
        if (!conn) {
            it = decltype(it)(ws_clients_.erase(std::next(it).base()));
            continue;
        }

        int ret = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY,
                                     reinterpret_cast<const char*>(data.data()),
                                     data.size());
        if (ret <= 0) {
            auto forward = std::next(it).base();
            it = decltype(it)(ws_clients_.erase(forward));
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Legacy SSE Event Handler — now sends via WebSocket text frames
// ============================================================================

void HttpSseActor::handleSseEvent(SseEvent& event) {
    // Build a compact JSON message: {"event":"...","data":{...}}
    // The event.data is already JSON from the upstream actor.
    std::string ws_message = R"({"event":")" + event.event_type +
                             R"(","data":)" + event.data + "}";

    broadcastWsText(ws_message);
}

// ============================================================================
// Legacy MJPEG Frame Handler — now sends via WebSocket binary frames
// ============================================================================

void HttpSseActor::handleMjpegFrame(MjpegFrame& frame) {
    // Cache the latest frame for new WebSocket clients
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_ = frame;
    }

    // Broadcast to all WebSocket clients as binary frame
    broadcastWsBinary(frame.data);
}

// ============================================================================
// Utility Helpers
// ============================================================================

std::unordered_map<std::string, std::string> HttpSseActor::parseQueryParams(const std::string& query) {
    std::unordered_map<std::string, std::string> params;
    if (query.empty()) return params;

    std::istringstream ss(query);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            params[pair.substr(0, eq)] = pair.substr(eq + 1);
        } else {
            params[pair] = "";
        }
    }
    return params;
}

uint64_t HttpSseActor::getTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace ct