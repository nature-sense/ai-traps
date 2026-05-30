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
#include "civetweb.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>

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
    cleanup_running_ = true;

    // Start cleanup thread for stale MJPEG clients
    cleanup_thread_ = std::thread([this]() {
        while (cleanup_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            cleanupStaleMjpegClients();
        }
    });

    // Build options array with stable strings
    std::string port_str = std::to_string(port_);
    const char* options[] = {
        "listening_ports", port_str.c_str(),
        "num_threads", "4",
        "enable_keep_alive", "yes",
        "keep_alive_timeout_ms", "30000",
        nullptr
    };

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = &requestHandler;

    ctx_ = mg_start(&callbacks, this, options);
    if (!ctx_) {
        // Handle startup error
        cleanup_running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
        return;
    }
}

void HttpSseActor::onStop() {
    running_ = false;
    cleanup_running_ = false;

    if (ctx_) {
        mg_stop(ctx_);
        ctx_ = nullptr;
    }

    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
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

    // Route to appropriate handler based on URL
    if (strcmp(uri, "/events") == 0) {
        return actor->handleSseConnection(conn);
    }
    else if (strcmp(uri, "/stream.mjpg") == 0 || strcmp(uri, "/stream_hires.mjpg") == 0) {
        return actor->handleMjpegStream(conn);
    }
    else if (strcmp(uri, "/health") == 0 || strcmp(uri, "/status") == 0) {
        return actor->handleHealthCheck(conn);
    }
    else {
        return actor->handleRestRequest(conn);
    }
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
        "sse_clients": )" + std::to_string(sse_clients_.size()) + R"(,
        "mjpeg_clients": )" + std::to_string(mjpeg_clients_.size()) + R"(
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
// SSE Implementation
// ============================================================================

int HttpSseActor::handleSseConnection(struct mg_connection* conn) {
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Cache-Control: no-cache\r\n"
        "Content-Type: text/event-stream\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n");

    {
        std::lock_guard<std::mutex> lock(sse_mutex_);
        sse_clients_.push_back(conn);
    }

    // Send welcome event
    std::string welcome = "event: connected\ndata: {\"status\":\"ok\",\"timestamp\":" +
                          std::to_string(getTimestampMs()) + "}\n\n";
    mg_send_chunk(conn, welcome.c_str(), welcome.size());
    mg_send_chunk(conn, "", 0);

    return 1; // MG_REQUEST_PROCESSED
}

void HttpSseActor::handleSseEvent(SseEvent& event) {
    std::string sse_data = "event: " + event.event_type + "\n";
    sse_data += "data: " + event.data + "\n";
    if (!event.id.empty()) sse_data += "id: " + event.id + "\n";
    sse_data += "\n";

    std::lock_guard<std::mutex> lock(sse_mutex_);
    for (auto* conn : sse_clients_) {
        mg_send_chunk(conn, sse_data.c_str(), sse_data.size());
        mg_send_chunk(conn, "", 0);
    }
}

void HttpSseActor::broadcastSse(const SseEvent& event) {
    // Make a copy since handleSseEvent takes a non-const ref
    SseEvent copy = event;
    handleSseEvent(copy);
}

// ============================================================================
// MJPEG Implementation
// ============================================================================

int HttpSseActor::handleMjpegStream(struct mg_connection* conn) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);

    // Parse quality from query string (e.g., /stream.mjpg?quality=75)
    int quality = 75;
    bool is_hires = (strcmp(req_info->local_uri, "/stream_hires.mjpg") == 0);

    if (is_hires) {
        quality = 95;
    }

    if (req_info->query_string) {
        auto params = parseQueryParams(req_info->query_string);
        auto it = params.find("quality");
        if (it != params.end()) {
            quality = std::stoi(it->second);
            quality = std::clamp(quality, 1, 100);
        }
    }

    // Register this client
    uint64_t client_id = registerMjpegClient(conn, quality);

    // Send MJPEG headers
    std::string boundary = "boundary_" + std::to_string(client_id);

    mg_printf(conn,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        boundary.c_str());

    // Send the latest frame immediately if available
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (latest_frame_.has_value()) {
            MjpegFrame frame = latest_frame_.value();
            std::string part = formatMjpegPart(frame, boundary);
            writeToConnection(conn, part);
        }
    }

    // Update client activity
    {
        std::lock_guard<std::mutex> lock(mjpeg_mutex_);
        auto it = mjpeg_clients_.find(client_id);
        if (it != mjpeg_clients_.end()) {
            it->second.last_activity = std::chrono::steady_clock::now();
        }
    }

    return 1; // MG_REQUEST_PROCESSED
}

void HttpSseActor::handleMjpegFrame(MjpegFrame& frame) {
    // Cache latest frame
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_ = frame;
    }

    // Broadcast to all connected MJPEG clients
    broadcastMjpegFrame(frame);
}

void HttpSseActor::broadcastMjpegFrame(const MjpegFrame& frame) {
    std::lock_guard<std::mutex> lock(mjpeg_mutex_);

    std::vector<uint64_t> stale_clients;

    for (auto& [id, client] : mjpeg_clients_) {
        // Check if connection is still valid
        if (!client.conn) {
            stale_clients.push_back(id);
            continue;
        }

        // Update last activity
        client.last_activity = std::chrono::steady_clock::now();

        // Format and send frame
        std::string part = formatMjpegPart(frame, client.boundary);
        if (!writeToConnection(client.conn, part)) {
            stale_clients.push_back(id);
        }
    }

    // Clean up stale clients
    for (uint64_t id : stale_clients) {
        unregisterMjpegClient(id);
    }
}

std::string HttpSseActor::formatMjpegPart(const MjpegFrame& frame, const std::string& boundary) {
    std::string result;
    result.reserve(256 + frame.data.size());

    result += "--" + boundary + "\r\n";
    result += "Content-Type: image/jpeg\r\n";
    result += "Content-Length: " + std::to_string(frame.data.size()) + "\r\n";
    result += "X-Timestamp: " + std::to_string(frame.timestamp) + "\r\n";
    result += "\r\n";

    // Append JPEG binary data
    result.append(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
    result += "\r\n";

    return result;
}

uint64_t HttpSseActor::registerMjpegClient(struct mg_connection* conn, int quality) {
    uint64_t client_id = next_client_id_++;

    MjpegClient client;
    client.id = client_id;
    client.conn = conn;
    client.boundary = "boundary_" + std::to_string(client_id);
    client.quality = quality;
    client.last_activity = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(mjpeg_mutex_);
        mjpeg_clients_[client_id] = std::move(client);
    }

    return client_id;
}

void HttpSseActor::unregisterMjpegClient(uint64_t client_id) {
    std::lock_guard<std::mutex> lock(mjpeg_mutex_);
    mjpeg_clients_.erase(client_id);
}

void HttpSseActor::cleanupStaleMjpegClients() {
    std::lock_guard<std::mutex> lock(mjpeg_mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<uint64_t> stale_clients;

    for (const auto& [id, client] : mjpeg_clients_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - client.last_activity);
        if (elapsed.count() > STALE_TIMEOUT_SECONDS) {
            stale_clients.push_back(id);
        }
    }

    for (uint64_t id : stale_clients) {
        mjpeg_clients_.erase(id);
    }
}

// ============================================================================
// Connection Helpers
// ============================================================================

bool HttpSseActor::writeToConnection(struct mg_connection* conn, const std::string& data) {
    if (!conn) return false;
    int result = mg_write(conn, data.c_str(), data.size());
    return result == static_cast<int>(data.size());
}

bool HttpSseActor::writeToConnection(struct mg_connection* conn, const std::vector<uint8_t>& data) {
    if (!conn || data.empty()) return false;
    int result = mg_write(conn, data.data(), data.size());
    return result == static_cast<int>(data.size());
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

std::string HttpSseActor::generateEventId() {
    static std::atomic<uint64_t> counter{0};
    return std::to_string(getTimestampMs()) + "-" + std::to_string(counter++);
}

uint64_t HttpSseActor::getTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace ct
