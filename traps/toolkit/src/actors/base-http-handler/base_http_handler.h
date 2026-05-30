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

// handlers/BaseHttpHandler.hpp
#pragma once

#include <ramen.hpp>
#include "http_sse_actor.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <regex>
#include <vector>
#include <nlohmann/json.hpp>

namespace ct {

// ============================================================================
// BaseHttpHandler - Abstract Base Class for All HTTP Handlers
// ============================================================================
//
// This class provides:
// - Automatic route registration with the HttpSseActor
// - Helper methods for sending responses and SSE events
// - Path parameter extraction
// - JSON serialization helpers
//
// Concrete handlers must implement:
// - registerRoutes(): Define the endpoints this handler serves
// - handleRequest(): Process incoming HTTP requests
//
// ============================================================================

class BaseHttpHandler : public ramen::Actor {
public:
    explicit BaseHttpHandler(const std::string& name = "handler");

    // ------------------------------------------------------------------------
    // RAMEN Actor Lifecycle
    // ------------------------------------------------------------------------

    void onStart() override;
    void onStop() override;

    // Type-erased dispatch (receives HttpRequest from HttpSseActor)
    void onMessageAny(const std::type_info& type, void* msg) override;

    // ------------------------------------------------------------------------
    // Abstract Methods (Must be implemented by concrete handlers)
    // ------------------------------------------------------------------------

    /**
     * Register all routes for this handler.
     * Called automatically during onStart().
     *
     * Example:
     *   registerRoute("GET", "/api/trap/status");
     *   registerRoute("POST", "/api/trap/trigger");
     *   registerRoute("GET", "/api/trap/events/{eventId}");
     */
    virtual void registerRoutes() = 0;

    /**
     * Handle a matched HTTP request.
     * The request already has path parameters extracted into request.params.
     */
    virtual void handleRequest(HttpRequest& request) = 0;

    // ------------------------------------------------------------------------
    // Optional Overrides
    // ------------------------------------------------------------------------

    /**
     * Return a unique name for this handler type.
     * Used for route registration and logging.
     * Default implementation returns the actor's name.
     */
    virtual std::string getHandlerName() const;

    // ------------------------------------------------------------------------
    // Protected Helper Methods
    // ------------------------------------------------------------------------

    /**
     * Register a single route with the HttpSseActor.
     * @param method HTTP method (GET, POST, PUT, DELETE)
     * @param path_pattern URL pattern with optional {param} placeholders
     */
    void registerRoute(const std::string& method, const std::string& path_pattern);

    /**
     * Register multiple routes at once.
     * @param routes Vector of {method, path_pattern} pairs
     */
    void registerRoutes(const std::vector<std::pair<std::string, std::string>>& routes);

    /**
     * Send an HTTP response back to the client.
     * @param request_id ID from the original HttpRequest
     * @param status_code HTTP status code (200, 404, 500, etc.)
     * @param body Response body (usually JSON)
     */
    void sendResponse(uint64_t request_id, int status_code, const std::string& body);

    /**
     * Send a JSON HTTP response.
     * @param request_id ID from the original HttpRequest
     * @param status_code HTTP status code
     * @param json_response nlohmann::json object (will be serialized)
     */
    void sendJsonResponse(uint64_t request_id, int status_code, const nlohmann::json& json_response);

    /**
     * Send an error response.
     * @param request_id ID from the original HttpRequest
     * @param status_code HTTP status code (400, 404, 500)
     * @param error_message Human-readable error description
     */
    void sendErrorResponse(uint64_t request_id, int status_code, const std::string& error_message);

    /**
     * Broadcast an SSE event to all connected clients.
     * @param event_type Event name (e.g., "detection", "trap_triggered")
     * @param data JSON string payload
     */
    void broadcastEvent(const std::string& event_type, const std::string& data);

    /**
     * Broadcast an SSE event with JSON payload.
     * @param event_type Event name
     * @param json_data nlohmann::json object
     */
    void broadcastJsonEvent(const std::string& event_type, const nlohmann::json& json_data);

    /**
     * Extract a path parameter from the request.
     * @param request The HttpRequest containing params map
     * @param name Parameter name (e.g., "trapId", "sessionId")
     * @return Parameter value, or empty string if not found
     */
    std::string getPathParam(const HttpRequest& request, const std::string& name);

    /**
     * Extract a query parameter from the request.
     * @param request The HttpRequest containing query_params map
     * @param name Parameter name
     * @param default_value Value to return if parameter not found
     * @return Parameter value or default
     */
    std::string getQueryParam(const HttpRequest& request, const std::string& name,
                              const std::string& default_value = "");

    /**
     * Parse JSON body from request.
     * @param request The HttpRequest containing body string
     * @return nlohmann::json object (empty if parsing fails)
     */
    nlohmann::json parseJsonBody(const HttpRequest& request);

    /**
     * Check if the request has a valid JSON body.
     */
    bool hasValidJsonBody(const HttpRequest& request);

    // ------------------------------------------------------------------------
    // Route Matching Helpers
    // ------------------------------------------------------------------------

    /**
     * Check if a request matches a specific path pattern.
     * Useful for internal routing within a handler.
     */
    bool matchesPath(const HttpRequest& request, const std::string& pattern);

    /**
     * Extract session ID from request (common pattern).
     * Assumes path pattern contains {sessionId} parameter.
     */
    uint64_t getSessionId(const HttpRequest& request);

    /**
     * Extract trap ID from request (common pattern).
     * Assumes path pattern contains {trapId} parameter.
     */
    std::string getTrapId(const HttpRequest& request);

private:
    // Handler name (used for route registration and logging)
    std::string name_;

    // Registered routes for this handler (for optional introspection)
    std::vector<HttpRoute> registered_routes_;
    bool routes_registered_{false};

    // Helper to send registration message to HttpSseActor
    void sendRegistrationToRouter();
};

} // namespace ct
