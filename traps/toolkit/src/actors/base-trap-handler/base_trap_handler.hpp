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

// handlers/BaseTrapHandler.hpp
#pragma once

#include <ramen.hpp>
#include "../actors/RouterActor.hpp"
#include "../actors/HttpSseActor.hpp"

namespace insect_trap {

/**
 * Base class for all trap handlers.
 * Provides automatic route registration and helper methods.
 */
class BaseTrapHandler : public ramen::Actor {
public:
    void onStart() override final {
        // Auto-register routes when actor starts
        registerRoutes();
    }

    void onStop() override final {
        // Auto-unregister on shutdown
        unregisterRoutes();
    }

    // Pure virtual - each trap handler implements its own routes
    virtual void registerRoutes() = 0;

    // Optional: override to handle custom cleanup
    virtual void unregisterRoutes() {
        UnregisterRoutes unreg;
        unreg.trap_type = getTrapType();
        send("router", std::move(unreg));
    }

    // Helper: Send HTTP response
    void sendResponse(uint64_t request_id, int status_code, const std::string& body) {
        HttpResponse response;
        response.request_id = request_id;
        response.status_code = status_code;
        response.body = body;
        send("http_server", std::move(response));
    }

    // Helper: Broadcast SSE event
    void broadcastEvent(const std::string& event_type, const std::string& data) {
        SseEvent event;
        event.event_type = getTrapType() + "_" + event_type;
        event.data = data;
        send("http_server", std::move(event));
    }

protected:
    // Helper to register a single route
    void registerRoute(const std::string& method,
                       const std::string& path_pattern) {
        RegisterRoutes reg;
        reg.trap_type = getTrapType();
        reg.handler_actor = getActorName();
        reg.routes.emplace_back(method, path_pattern, getActorName());
        send("router", std::move(reg));
    }

    // Helper to register multiple routes at once
    void registerRoutes(const std::vector<std::pair<std::string, std::string>>& routes) {
        RegisterRoutes reg;
        reg.trap_type = getTrapType();
        reg.handler_actor = getActorName();
        for (const auto& [method, pattern] : routes) {
            reg.routes.emplace_back(method, pattern, getActorName());
        }
        send("router", std::move(reg));
    }

    // Each trap handler must provide its unique type name
    virtual std::string getTrapType() const = 0;

    // Get this actor's name (set when created)
    std::string getActorName() const {
        // This depends on your RAMEN implementation
        // Some actor frameworks provide getName(), others store it
        return actor_name_;
    }

    void setActorName(const std::string& name) { actor_name_ = name; }

private:
    std::string actor_name_;
};

} // namespace insect_trap