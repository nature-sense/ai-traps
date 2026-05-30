// handlers/BaseHttpHandler.cpp
#include "base_http_handler.h"

namespace ct {

// ============================================================================
// Constructor
// ============================================================================

BaseHttpHandler::BaseHttpHandler(const std::string& name)
    : name_(name) {}

// ============================================================================
// RAMEN Actor Lifecycle
// ============================================================================

void BaseHttpHandler::onStart() {
    // Register routes with the HttpSseActor
    registerRoutes();
    routes_registered_ = true;
    sendRegistrationToRouter();
}

void BaseHttpHandler::onStop() {
    // Unregister routes from the HttpSseActor
    if (routes_registered_) {
        UnregisterRoutes msg;
        msg.handler_actor = getHandlerName();
        ramen::send("http_server", std::move(msg));
        routes_registered_ = false;
    }
}

void BaseHttpHandler::onMessageAny(const std::type_info& type, void* msg) {
    if (type == typeid(HttpRequest)) {
        handleRequest(*static_cast<HttpRequest*>(msg));
    }
}

// ============================================================================
// Handler Name
// ============================================================================

std::string BaseHttpHandler::getHandlerName() const {
    return name_;
}

// ============================================================================
// Route Registration
// ============================================================================

void BaseHttpHandler::registerRoute(const std::string& method, const std::string& path_pattern) {
    registered_routes_.emplace_back(method, path_pattern, getHandlerName());
}

void BaseHttpHandler::registerRoutes(const std::vector<std::pair<std::string, std::string>>& routes) {
    for (const auto& [method, pattern] : routes) {
        registerRoute(method, pattern);
    }
}

void BaseHttpHandler::sendRegistrationToRouter() {
    RegisterRoutes msg;
    msg.handler_actor = getHandlerName();
    msg.routes = registered_routes_;
    ramen::send("http_server", std::move(msg));
}

// ============================================================================
// Response Helpers
// ============================================================================

void BaseHttpHandler::sendResponse(uint64_t request_id, int status_code, const std::string& body) {
    HttpResponse response;
    response.request_id = request_id;
    response.status_code = status_code;
    response.body = body;
    ramen::send("http_server", std::move(response));
}

void BaseHttpHandler::sendJsonResponse(uint64_t request_id, int status_code, const nlohmann::json& json_response) {
    sendResponse(request_id, status_code, json_response.dump());
}

void BaseHttpHandler::sendErrorResponse(uint64_t request_id, int status_code, const std::string& error_message) {
    nlohmann::json error_json = {
        {"error", error_message}
    };
    sendJsonResponse(request_id, status_code, error_json);
}

// ============================================================================
// SSE Helpers
// ============================================================================

void BaseHttpHandler::broadcastEvent(const std::string& event_type, const std::string& data) {
    SseEvent event;
    event.event_type = event_type;
    event.data = data;
    ramen::send("http_server", std::move(event));
}

void BaseHttpHandler::broadcastJsonEvent(const std::string& event_type, const nlohmann::json& json_data) {
    broadcastEvent(event_type, json_data.dump());
}

// ============================================================================
// Parameter Extraction
// ============================================================================

std::string BaseHttpHandler::getPathParam(const HttpRequest& request, const std::string& name) {
    auto it = request.params.find(name);
    if (it != request.params.end()) {
        return it->second;
    }
    return "";
}

std::string BaseHttpHandler::getQueryParam(const HttpRequest& request, const std::string& name,
                                            const std::string& default_value) {
    auto it = request.query_params.find(name);
    if (it != request.query_params.end()) {
        return it->second;
    }
    return default_value;
}

// ============================================================================
// JSON Body Parsing
// ============================================================================

nlohmann::json BaseHttpHandler::parseJsonBody(const HttpRequest& request) {
    try {
        return nlohmann::json::parse(request.body);
    } catch (const nlohmann::json::parse_error&) {
        return nlohmann::json();
    }
}

bool BaseHttpHandler::hasValidJsonBody(const HttpRequest& request) {
    return !parseJsonBody(request).is_null();
}

// ============================================================================
// Route Matching Helpers
// ============================================================================

bool BaseHttpHandler::matchesPath(const HttpRequest& request, const std::string& pattern) {
    // Simple check: does the request path match the given pattern?
    // This is a basic implementation - for complex matching, use HttpRoute
    std::string regex_pattern = pattern;
    // Replace {param} with [^/]+
    std::regex param_regex("\\{[^}]+\\}");
    regex_pattern = std::regex_replace(regex_pattern, param_regex, "[^/]+");
    return std::regex_match(request.path, std::regex("^" + regex_pattern + "$"));
}

uint64_t BaseHttpHandler::getSessionId(const HttpRequest& request) {
    std::string id_str = getPathParam(request, "sessionId");
    if (id_str.empty()) return 0;
    try {
        return std::stoull(id_str);
    } catch (...) {
        return 0;
    }
}

std::string BaseHttpHandler::getTrapId(const HttpRequest& request) {
    return getPathParam(request, "trapId");
}

} // namespace ct
