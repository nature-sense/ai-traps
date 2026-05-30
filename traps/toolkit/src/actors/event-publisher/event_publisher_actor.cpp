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

// actors/event-publisher/event_publisher_actor.cpp
#include "event_publisher_actor.hpp"

#include <iostream>
#include <chrono>

namespace ct {

// ============================================================================
// Constructor
// ============================================================================

EventPublisherActor::EventPublisherActor() {
    // in_event is initialised inline in the header
}

// ============================================================================
// RAMEN Actor Lifecycle
// ============================================================================

void EventPublisherActor::onStart() {
    // Register with ActorRegistry so actors can send events by name
    auto* registry = ramen::ActorRegistry::instance();
    if (registry) {
        registry->registerActor(getPublisherName(), this);
        registered_ = true;
        std::cout << "[EventPublisherActor] registered as \"" << getPublisherName() << "\"\n";
    }
}

void EventPublisherActor::onStop() {
    // Unregister from ActorRegistry
    if (registered_) {
        auto* registry = ramen::ActorRegistry::instance();
        if (registry) {
            registry->unregisterActor(getPublisherName());
        }
        registered_ = false;
        std::cout << "[EventPublisherActor] unregistered\n";
    }
}

void EventPublisherActor::onMessageAny(const std::type_info& type, void* msg) {
    if (type == typeid(Event)) {
        publishEvent(*static_cast<Event*>(msg));
    }
}

// ============================================================================
// Publisher Name
// ============================================================================

std::string EventPublisherActor::getPublisherName() const {
    return "event_publisher";
}

// ============================================================================
// Backend Configuration
// ============================================================================

void EventPublisherActor::setSseActor(HttpSseActor* sse_actor) {
    std::lock_guard<std::mutex> lock(mutex_);
    sse_actor_ = sse_actor;
}

// ============================================================================
// Event Publishing (Internal Dispatch)
// ============================================================================

void EventPublisherActor::publishEvent(const Event& event) {
    // Auto-fill timestamp if not set
    Event evt = event;
    if (evt.timestamp_ms == 0) {
        evt.timestamp_ms = nowMs();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // ── SSE Backend ──────────────────────────────────────────────────────────
    if (sse_actor_) {
        SseEvent sse;
        sse.event_type = evt.event_type;
        sse.data = evt.data;
        sse.id = std::to_string(evt.timestamp_ms);
        sse_actor_->handleSseEvent(sse);
    }

    // Future backends would be dispatched here:
    // if (mqtt_client_) { mqtt_client_->publish(evt.event_type, evt.data); }
    // if (ble_adapter_) { ble_adapter_->advertise(evt); }
    // if (logger_)      { logger_->log(evt); }
}

// ============================================================================
// Helpers
// ============================================================================

int64_t EventPublisherActor::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace ct
