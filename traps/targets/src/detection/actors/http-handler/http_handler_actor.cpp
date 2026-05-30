// ─── http_handler_actor.cpp ───────────────────────────────────────────────────
// REST API handler for the Camera Trap.
// Implements the full REST interface defined in openapi.yaml.
// Communicates with SessionActor via its Pullable/Pushable ports.

#include "http_handler_actor.hpp"
#include <ramen.hpp>
#include <iostream>
#include <chrono>

namespace ct {

// ============================================================================
// Constructor
// ============================================================================

HttpHandlerActor::HttpHandlerActor(const std::string& name)
    : BaseHttpHandler(name) {}

// ============================================================================
// BaseHttpHandler Interface
// ============================================================================

void HttpHandlerActor::registerRoutes() {
    // ── Provisioning ──────────────────────────────────────────────────────
    registerRoute("POST", "/v1/provision");

    // ── Trap Status ───────────────────────────────────────────────────────
    registerRoute("GET", "/v1/traps/{trapId}");

    // ── Sessions ──────────────────────────────────────────────────────────
    registerRoute("POST", "/v1/traps/{trapId}/sessions");
    registerRoute("GET",  "/v1/traps/{trapId}/sessions");
    registerRoute("GET",  "/v1/traps/{trapId}/sessions/active");
    registerRoute("GET",  "/v1/traps/{trapId}/sessions/{sessionId}");
    registerRoute("PUT",  "/v1/traps/{trapId}/sessions/{sessionId}/stop");

    // ── Detections ────────────────────────────────────────────────────────
    registerRoute("GET",  "/v1/traps/{trapId}/sessions/{sessionId}/detections");
    registerRoute("GET",  "/v1/traps/{trapId}/detections/{detectionId}");

    // ── Crop Images (stub) ────────────────────────────────────────────────
    registerRoute("GET",  "/v1/crops/{date}/{filename}");

    // ── System Metrics (stub) ─────────────────────────────────────────────
    registerRoute("GET",  "/v1/traps/{trapId}/system");

    // ── Server Status ─────────────────────────────────────────────────────
    registerRoute("GET",  "/status");

    std::cout << "[HttpHandlerActor] registered " << 12 << " routes\n";
}

void HttpHandlerActor::handleRequest(HttpRequest& request) {
    // Dispatch based on method + path
    const auto& method = request.method;
    const auto& path   = request.path;

    // ── Provisioning ──────────────────────────────────────────────────────
    if (method == "POST" && path == "/v1/provision") {
        return handleProvisionTrap(request);
    }

    // ── Trap Status ───────────────────────────────────────────────────────
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}")) {
        return handleGetTrapStatus(request);
    }

    // ── Sessions ──────────────────────────────────────────────────────────
    if (method == "POST" && matchesPath(request, "/v1/traps/{trapId}/sessions")) {
        return handleStartSession(request);
    }
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/sessions/active")) {
        return handleGetActiveSession(request);
    }
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/sessions")) {
        return handleListSessions(request);
    }
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/sessions/{sessionId}")) {
        return handleGetSession(request);
    }
    if (method == "PUT" && matchesPath(request, "/v1/traps/{trapId}/sessions/{sessionId}/stop")) {
        return handleStopSession(request);
    }

    // ── Detections ────────────────────────────────────────────────────────
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/sessions/{sessionId}/detections")) {
        return handleListDetections(request);
    }
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/detections/{detectionId}")) {
        return handleGetDetection(request);
    }

    // ── Crop Images (stub) ────────────────────────────────────────────────
    if (method == "GET" && matchesPath(request, "/v1/crops/{date}/{filename}")) {
        return handleGetCropImage(request);
    }

    // ── System Metrics (stub) ─────────────────────────────────────────────
    if (method == "GET" && matchesPath(request, "/v1/traps/{trapId}/system")) {
        return handleGetSystemMetrics(request);
    }

    // ── Server Status ─────────────────────────────────────────────────────
    if (method == "GET" && path == "/status") {
        return handleGetStatus(request);
    }

    // Fallback: 404
    sendErrorResponse(request.request_id, 404, "Not found: " + method + " " + path);
}

std::string HttpHandlerActor::getHandlerName() const {
    return "http_handler";
}

// ============================================================================
// Route Handlers
// ============================================================================

// ── POST /v1/provision ───────────────────────────────────────────────────────

void HttpHandlerActor::handleProvisionTrap(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    auto body = parseJsonBody(request);
    if (body.is_null() || !body.contains("trapId") || !body["trapId"].is_string()) {
        sendErrorResponse(request.request_id, 400, "Missing or invalid trapId");
        return;
    }

    std::string trap_id = body["trapId"];

    if (session->is_provisioned()) {
        sendErrorResponse(request.request_id, 409, "Already provisioned");
        return;
    }

    session->in_provision(trap_id);

    // Publish provision event
    if (event_publisher_) {
        nlohmann::json evt = {
            {"trapId", trap_id},
            {"status", "provisioned"}
        };
        event_publisher_->in_event(Event{
            "trap_provisioned", evt.dump()
        });
    }

    nlohmann::json resp = {
        {"trapId", trap_id},
        {"status", "provisioned"}
    };
    sendJsonResponse(request.request_id, 201, resp);
}

// ── GET /v1/traps/{trapId} ───────────────────────────────────────────────────

void HttpHandlerActor::handleGetTrapStatus(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    std::string trap_id = getTrapId(request);

    // Check if trap is provisioned
    std::string stored_id;
    session->out_trap_id(stored_id);

    if (stored_id.empty() || stored_id != trap_id) {
        sendErrorResponse(request.request_id, 404, "Trap not found");
        return;
    }

    // Get active session info
    SessionInfo active;
    session->out_active_session(active);

    nlohmann::json resp = {
        {"trapId", trap_id},
        {"status", "online"}
    };

    if (active.id >= 0) {
        resp["activeSession"] = sessionToJson(active);
    } else {
        resp["activeSession"] = nlohmann::json{{"active", false}};
    }

    sendJsonResponse(request.request_id, 200, resp);
}

// ── POST /v1/traps/{trapId}/sessions ─────────────────────────────────────────

void HttpHandlerActor::handleStartSession(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    std::string trap_id = getTrapId(request);

    // Verify trap exists
    std::string stored_id;
    session->out_trap_id(stored_id);
    if (stored_id.empty() || stored_id != trap_id) {
        sendErrorResponse(request.request_id, 404, "Trap not found");
        return;
    }

    // Start session
    session->in_start_session(int64_t(0));

    // Get the newly created session info
    SessionInfo info = session->active_session();
    if (info.id < 0) {
        sendErrorResponse(request.request_id, 500, "Failed to start session");
        return;
    }

    // Publish session started event
    if (event_publisher_) {
        nlohmann::json evt = {
            {"trapId", trap_id},
            {"sessionId", info.id},
            {"startedAt", info.started_at}
        };
        event_publisher_->in_event(Event{
            "session_started", evt.dump()
        });
    }

    sendJsonResponse(request.request_id, 201, sessionToJson(info));
}

// ── GET /v1/traps/{trapId}/sessions ──────────────────────────────────────────

void HttpHandlerActor::handleListSessions(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    int limit  = 50;
    int offset = 0;

    auto limit_str = getQueryParam(request, "limit");
    if (!limit_str.empty()) {
        try { limit = std::stoi(limit_str); } catch (...) {}
    }
    auto offset_str = getQueryParam(request, "offset");
    if (!offset_str.empty()) {
        try { offset = std::stoi(offset_str); } catch (...) {}
    }

    // Use the list_sessions method directly with limit/offset
    auto sessions = session->list_sessions(limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : sessions) {
        arr.push_back(sessionToJson(s));
    }

    nlohmann::json resp = {{"sessions", arr}};
    sendJsonResponse(request.request_id, 200, resp);
}

// ── GET /v1/traps/{trapId}/sessions/active ───────────────────────────────────

void HttpHandlerActor::handleGetActiveSession(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    SessionInfo info;
    session->out_active_session(info);

    if (info.id >= 0) {
        sendJsonResponse(request.request_id, 200, sessionToJson(info));
    } else {
        sendJsonResponse(request.request_id, 200, nlohmann::json{{"active", false}});
    }
}

// ── GET /v1/traps/{trapId}/sessions/{sessionId} ──────────────────────────────

void HttpHandlerActor::handleGetSession(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    int64_t session_id = static_cast<int64_t>(getSessionId(request));

    session->session_query_id_ = session_id;
    SessionInfo info;
    session->out_get_session(info);

    if (info.id < 0) {
        sendErrorResponse(request.request_id, 404, "Session not found");
        return;
    }

    sendJsonResponse(request.request_id, 200, sessionToJson(info));
}

// ── PUT /v1/traps/{trapId}/sessions/{sessionId}/stop ─────────────────────────

void HttpHandlerActor::handleStopSession(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    int64_t session_id = static_cast<int64_t>(getSessionId(request));

    if (!session->stop_session(session_id)) {
        sendErrorResponse(request.request_id, 400, "Session not active");
        return;
    }

    // Return the stopped session info
    session->session_query_id_ = session_id;
    SessionInfo info;
    session->out_get_session(info);

    // Publish session stopped event
    if (event_publisher_) {
        nlohmann::json evt = {
            {"sessionId", info.id},
            {"startedAt", info.started_at},
            {"stoppedAt", info.stopped_at},
            {"detectionCount", info.detection_count}
        };
        event_publisher_->in_event(Event{
            "session_stopped", evt.dump()
        });
    }

    sendJsonResponse(request.request_id, 200, sessionToJson(info));
}

// ── GET /v1/traps/{trapId}/sessions/{sessionId}/detections ───────────────────

void HttpHandlerActor::handleListDetections(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    int64_t session_id = static_cast<int64_t>(getSessionId(request));

    int limit  = 50;
    int offset = 0;

    auto limit_str = getQueryParam(request, "limit");
    if (!limit_str.empty()) {
        try { limit = std::stoi(limit_str); } catch (...) {}
    }
    auto offset_str = getQueryParam(request, "offset");
    if (!offset_str.empty()) {
        try { offset = std::stoi(offset_str); } catch (...) {}
    }

    auto detections = session->list_detections(session_id, limit, offset);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : detections) {
        arr.push_back(detectionToJson(d));
    }

    nlohmann::json resp = {{"detections", arr}};
    sendJsonResponse(request.request_id, 200, resp);
}

// ── GET /v1/traps/{trapId}/detections/{detectionId} ──────────────────────────

void HttpHandlerActor::handleGetDetection(HttpRequest& request) {
    auto* session = getSessionActor();
    if (!session) {
        sendErrorResponse(request.request_id, 500, "SessionActor not available");
        return;
    }

    int64_t detection_id = 0;
    auto id_str = getPathParam(request, "detectionId");
    if (!id_str.empty()) {
        try { detection_id = std::stoll(id_str); } catch (...) {}
    }

    session->detection_query_id_ = detection_id;
    DetectionInfo det;
    session->out_get_detection(det);

    if (det.id < 0) {
        sendErrorResponse(request.request_id, 404, "Detection not found");
        return;
    }

    sendJsonResponse(request.request_id, 200, detectionToJson(det));
}

// ── GET /v1/crops/{date}/{filename} (stub) ────────────────────────────────────

void HttpHandlerActor::handleGetCropImage(HttpRequest& request) {
    sendErrorResponse(request.request_id, 501, "Crop image serving not implemented yet");
}

// ── GET /v1/traps/{trapId}/system (stub) ─────────────────────────────────────

void HttpHandlerActor::handleGetSystemMetrics(HttpRequest& request) {
    sendErrorResponse(request.request_id, 501, "System metrics not implemented yet");
}

// ── GET /status ──────────────────────────────────────────────────────────────

void HttpHandlerActor::handleGetStatus(HttpRequest& request) {
    nlohmann::json resp = {
        {"status", "running"},
        {"port", 8080},
        {"clients_served", 0},
        {"frames_served", 0},
        {"sse_clients", 0}
    };
    sendJsonResponse(request.request_id, 200, resp);
}

// ============================================================================
// Helpers
// ============================================================================

SessionActor* HttpHandlerActor::getSessionActor() const {
    if (!session_actor_) {
        std::cerr << "[HttpHandlerActor] session_actor_ not set. "
                  << "Call setSessionActor() before handling requests.\n";
    }
    return session_actor_;
}

nlohmann::json HttpHandlerActor::sessionToJson(const SessionInfo& info) const {
    nlohmann::json j;
    j["id"]              = info.id;
    j["startedAt"]       = info.started_at;
    j["detectionCount"]  = info.detection_count;

    if (info.active) {
        j["active"]    = true;
        j["sessionId"] = info.id;
        j["startedAt"] = info.started_at;
        j["detectionCount"] = info.detection_count;
    }

    if (info.stopped_at > 0) {
        j["stoppedAt"] = info.stopped_at;
    } else {
        j["stoppedAt"] = nullptr;
    }

    return j;
}

nlohmann::json HttpHandlerActor::detectionToJson(const DetectionInfo& info) const {
    nlohmann::json j;
    j["id"]         = info.id;
    j["timestamp"]  = info.timestamp;
    j["trackId"]    = info.track_id;
    j["classId"]    = info.class_id;
    j["confidence"] = info.confidence;
    j["sessionId"]  = info.session_id;

    // Build image URL from path
    if (!info.image_path.empty()) {
        // Convert absolute/relative path to URL
        // e.g., /data/captures/2026-05-12/1715500000_3.jpg -> /v1/crops/2026-05-12/1715500000_3.jpg
        std::string url = info.image_path;
        // Extract date and filename from path
        auto pos = url.find_last_of('/');
        if (pos != std::string::npos) {
            std::string filename = url.substr(pos + 1);
            std::string rest = url.substr(0, pos);
            auto pos2 = rest.find_last_of('/');
            if (pos2 != std::string::npos) {
                std::string date = rest.substr(pos2 + 1);
                j["imageUrl"] = "/v1/crops/" + date + "/" + filename;
            } else {
                j["imageUrl"] = info.image_path;
            }
        } else {
            j["imageUrl"] = info.image_path;
        }
    } else {
        j["imageUrl"] = nullptr;
    }

    return j;
}

} // namespace ct
