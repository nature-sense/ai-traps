// actors/event-publisher/event_publisher_actor.hpp
#pragma once

#include <ramen.hpp>
#include "http_sse_actor.h"
#include <string>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace ct {

// ============================================================================
// Event — Generic Event Structure
// ============================================================================
//
// A generic event that can be published by any actor. The event type and data
// are backend-agnostic — the EventPublisherActor dispatches to whatever
// backends are configured (SSE, MQTT, BLE, etc.).
//
// Fields:
//   event_type   — Human-readable event name (e.g., "detection", "session_started")
//   data         — Payload (typically JSON string)
//   timestamp_ms — Epoch milliseconds. 0 = auto-fill with current time.
//
// ============================================================================

struct Event {
    std::string event_type;
    std::string data;
    int64_t     timestamp_ms = 0;
};

// ============================================================================
// EventPublisherActor — Generic Event Publisher
// ============================================================================
//
// Provides a standard entry point for actors that want to push events.
// In the current implementation, events are forwarded to the HttpSseActor
// for SSE broadcasting. Future backends (MQTT, WebSocket, BLE, file logging)
// can be added without changing the actors that publish events.
//
// Actors can push events in two ways:
//   1. Via Ramen port wiring:  publisher->in_event.push(event)
//   2. Via ActorRegistry:      ramen::send("event_publisher", event)
//
// ============================================================================

class EventPublisherActor : public ramen::Actor {
public:
    EventPublisherActor();

    // ------------------------------------------------------------------------
    // RAMEN Actor Lifecycle
    // ------------------------------------------------------------------------

    void onStart() override;
    void onStop() override;

    // Type-erased dispatch (receives Event from ActorRegistry)
    void onMessageAny(const std::type_info& type, void* msg) override;

    // ------------------------------------------------------------------------
    // Input Ports
    // ------------------------------------------------------------------------

    /// Pushable input port — any actor can push events via port wiring.
    /// Example:  publisher->in_event.push(myEvent);
    ramen::Pushable<Event> in_event{
        [this](const Event& event) { publishEvent(event); }
    };

    // ------------------------------------------------------------------------
    // Backend Configuration
    // ------------------------------------------------------------------------

    /// Set the SSE backend. Events will be broadcast as SSE events to all
    /// connected SSE clients via the HttpSseActor.
    void setSseActor(HttpSseActor* sse_actor);

    // Future backends can be added similarly:
    // void setMqttBackend(MqttClient* client);
    // void setBleBackend(BleAdapter* adapter);
    // void setFileLogger(const std::string& path);

    // ------------------------------------------------------------------------
    // Optional Overrides
    // ------------------------------------------------------------------------

    /// Return the registry name for this publisher.
    virtual std::string getPublisherName() const;

private:
    // Backend references
    HttpSseActor* sse_actor_{nullptr};

    // Thread safety
    std::mutex mutex_;

    // Registry state
    bool registered_{false};

    // Internal dispatch
    void publishEvent(const Event& event);

    /// Get current time in epoch milliseconds.
    static int64_t nowMs();
};

} // namespace ct
