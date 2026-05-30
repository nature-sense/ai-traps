
## What BaseTrapHandler Provides

### 1. Automatic Route Registration/Unregistration

Every trap handler needs to register its routes with the router when it starts, and unregister when it stops. BaseTrapHandler handles this automatically:

**Without BaseTrapHandler** - every handler must repeat this code:
```cpp
class PhotoelectricTrapHandler : public ramen::Actor {
    void onStart() override {
        RegisterRoutes reg;
        reg.trap_type = "photoelectric";
        reg.handler_actor = "photoelectric_trap_handler";
        reg.routes = {
            {"POST", "/api/trap/photoelectric/trigger", "photoelectric_trap_handler"},
            {"GET",  "/api/trap/photoelectric/status", "photoelectric_trap_handler"},
            // ... more routes
        };
        send("router", std::move(reg));
    }
    
    void onStop() override {
        UnregisterRoutes unreg;
        unreg.trap_type = "photoelectric";
        send("router", std::move(unreg));
    }
};
```



**With BaseTrapHandler** - just implement registerRoutes():

cpp

```
class PhotoelectricTrapHandler : public BaseTrapHandler {
    void registerRoutes() override {
        registerRoutes({
            {"POST", "/api/trap/photoelectric/trigger"},
            {"GET",  "/api/trap/photoelectric/status"},
        });
    }
    // onStart/onStop are already handled by base class
};
```



### 2. Helper Methods for Common Operations

Every trap handler needs to send HTTP responses and broadcast SSE events. BaseTrapHandler provides simple one-liners:

**Without BaseTrapHandler**:

cpp

```
void handleTrigger(const HttpRequest& request) {
    // ... logic ...
    HttpResponse response;
    response.request_id = request.request_id;
    response.status_code = 200;
    response.body = R"({"success":true})";
    send("http_server", std::move(response));
}
```



**With BaseTrapHandler**:

cpp

```
void handleTrigger(const HttpRequest& request) {
    // ... logic ...
    sendResponse(request.request_id, 200, R"({"success":true})");
}
```



### 3. Automatic Event Type Prefixing

BaseTrapHandler automatically prefixes SSE events with the trap type, ensuring events from different traps don't collide:

cpp

```
// In BaseTrapHandler
void broadcastEvent(const std::string& event_type, const std::string& data) {
    SseEvent event;
    event.event_type = getTrapType() + "_" + event_type;  // "photoelectric_trigger"
    event.data = data;
    send("http_server", std::move(event));
}

// Handler just calls:
broadcastEvent("trigger", R"({"duration":100})");
// Actual event name becomes "photoelectric_trigger"
```



### 4. Lifecycle Management

BaseTrapHandler ensures proper cleanup even if a handler forgets to unregister:

cpp

```
void onStop() override final {  // 'final' prevents overriding
    unregisterRoutes();  // Always called, even if subclass forgets
}
```



## What Handlers Must Implement

The only things a trap handler must provide are:

| Method                    | Purpose                                          |
| :------------------------ | :----------------------------------------------- |
| `registerRoutes()`        | Define the REST endpoints this trap responds to  |
| `getTrapType()`           | Return unique identifier (e.g., "photoelectric") |
| `onMessage(HttpRequest&)` | Handle incoming requests (business logic)        |