// actors/HttpSseActor.hpp
#pragma once

#include <ramen.hpp>
#include "civetweb.h"
#include <regex>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include <optional>
#include <cstdint>
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
// HttpSseActor - Combined HTTP Server + Router + MJPEG + SSE
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

private:
    // CivetWeb callbacks
    static int requestHandler(struct mg_connection* conn);
    
    // Request type handlers
    int handleRestRequest(struct mg_connection* conn);
    int handleSseConnection(struct mg_connection* conn);
    int handleMjpegStream(struct mg_connection* conn);
    int handleHealthCheck(struct mg_connection* conn);
    
    // Route management
    bool findRoute(const std::string& method, const std::string& path,
                   std::string& handler_actor,
                   std::unordered_map<std::string, std::string>& params);
    void addRoutes(const RegisterRoutes& registration);
    void removeRoutes(const std::string& handler_actor);
    
    // Response handling
    void sendResponse(uint64_t request_id, int status_code, const std::string& body);
    void sendErrorResponse(uint64_t request_id, int status_code, const std::string& message);
    
    // SSE
    void broadcastSse(const SseEvent& event);
    
    // MJPEG
    void broadcastMjpegFrame(const MjpegFrame& frame);
    std::string formatMjpegPart(const MjpegFrame& frame, const std::string& boundary);
    uint64_t registerMjpegClient(struct mg_connection* conn, int quality);
    void unregisterMjpegClient(uint64_t client_id);
    void cleanupStaleMjpegClients();
    
    // Helpers
    std::string generateEventId();
    std::unordered_map<std::string, std::string> parseQueryParams(const std::string& query);
    bool writeToConnection(struct mg_connection* conn, const std::string& data);
    bool writeToConnection(struct mg_connection* conn, const std::vector<uint8_t>& data);
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
    
    // SSE clients
    std::vector<struct mg_connection*> sse_clients_;
    std::mutex sse_mutex_;
    
    // MJPEG clients
    struct MjpegClient {
        uint64_t id;
        struct mg_connection* conn;
        std::string boundary;
        int quality;
        std::chrono::steady_clock::time_point last_activity;
    };
    std::unordered_map<uint64_t, MjpegClient> mjpeg_clients_;
    std::mutex mjpeg_mutex_;
    std::atomic<uint64_t> next_client_id_{1};
    
    // Latest frame cache (for new clients)
    std::optional<MjpegFrame> latest_frame_;
    std::mutex frame_mutex_;
    
    // Stale client cleanup
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    static constexpr int STALE_TIMEOUT_SECONDS = 30;
};

} // namespace ct
