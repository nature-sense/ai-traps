#pragma once

#include "base-http-handler/base_http_handler.h"
#include "session_actor.hpp"
#include "event-publisher/event_publisher_actor.hpp"
#include "../types.hpp"
#include <string>
#include <cstdint>

namespace ct {

// ============================================================================
// HttpHandlerActor — REST API handler for the Camera Trap
// ============================================================================
//
// Implements the full REST interface defined in openapi.yaml.
// Communicates with SessionActor via its Pullable/Pushable ports.
//
// ============================================================================

class HttpHandlerActor : public BaseHttpHandler {
public:
    explicit HttpHandlerActor(const std::string& name = "http_handler");

    // ── BaseHttpHandler interface ──────────────────────────────────────────
    void registerRoutes() override;
    void handleRequest(HttpRequest& request) override;
    std::string getHandlerName() const override;

    // ── SessionActor binding ───────────────────────────────────────────────
    /// Set the SessionActor instance that this handler will communicate with.
    /// Must be called before the handler receives requests.
    void setSessionActor(SessionActor* actor) { session_actor_ = actor; }

    // ── Event publisher binding ────────────────────────────────────────────
    /// Set the EventPublisherActor for publishing REST action events.
    /// When set, key REST operations (provision, session start/stop) will
    /// publish events via the event publisher.
    void setEventPublisher(EventPublisherActor* publisher) { event_publisher_ = publisher; }

private:
    // ── Route handlers (one per OpenAPI endpoint) ──────────────────────────

    // POST /v1/provision
    void handleProvisionTrap(HttpRequest& request);

    // GET /v1/traps/{trapId}
    void handleGetTrapStatus(HttpRequest& request);

    // POST /v1/traps/{trapId}/sessions
    void handleStartSession(HttpRequest& request);

    // GET /v1/traps/{trapId}/sessions
    void handleListSessions(HttpRequest& request);

    // GET /v1/traps/{trapId}/sessions/active
    void handleGetActiveSession(HttpRequest& request);

    // GET /v1/traps/{trapId}/sessions/{sessionId}
    void handleGetSession(HttpRequest& request);

    // PUT /v1/traps/{trapId}/sessions/{sessionId}/stop
    void handleStopSession(HttpRequest& request);

    // GET /v1/traps/{trapId}/sessions/{sessionId}/detections
    void handleListDetections(HttpRequest& request);

    // GET /v1/traps/{trapId}/detections/{detectionId}
    void handleGetDetection(HttpRequest& request);

    // GET /v1/crops/{date}/{filename}  (stub)
    void handleGetCropImage(HttpRequest& request);

    // GET /v1/traps/{trapId}/system  (stub)
    void handleGetSystemMetrics(HttpRequest& request);

    // GET /status
    void handleGetStatus(HttpRequest& request);

    // ── Helpers ────────────────────────────────────────────────────────────

    /// Get the SessionActor* from the actor registry.
    /// Returns nullptr if not registered.
    SessionActor* getSessionActor() const;

    /// Build a SessionInfo JSON response body.
    nlohmann::json sessionToJson(const SessionInfo& info) const;

    /// Build a DetectionInfo JSON response body.
    nlohmann::json detectionToJson(const DetectionInfo& info) const;

    /// Pointer to the SessionActor (set via setSessionActor).
    SessionActor* session_actor_ = nullptr;

    /// Pointer to the EventPublisherActor (set via setEventPublisher).
    EventPublisherActor* event_publisher_ = nullptr;
};

} // namespace ct
