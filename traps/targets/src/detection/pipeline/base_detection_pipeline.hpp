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

#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include "hal/api/camera_hal.hpp"
#include "hal/api/inference_hal.hpp"

#include "camera/camera_actor.hpp"
#include "inference/inference_actor.hpp"
#include "tracker/tracker_actor.hpp"
#include "cropper/cropper_actor.hpp"
#include "decision/decision_actor.hpp"
#include "overlay/overlay_actor.hpp"
#include "mjpeg-bridge/mjpeg_bridge_actor.hpp"
#include "http-sse/http_sse_actor.h"
#include "base-http-handler/base_http_handler.h"
#include "event-publisher/event_publisher_actor.hpp"

#include "session_actor.hpp"
#include "http_handler_actor.hpp"

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>

namespace ct {

// ─── BaseDetectionPipeline ────────────────────────────────────────────────────
// Abstract base class for the detection pipeline.
//
// Owns all actor instances, wires them together via Ramen ports, and provides
// the main run loop. Platform-specific subclasses override the factory methods
// to create platform-appropriate implementations for any actor.
//
// By default, the base class creates standard implementations for all actors.
// Subclasses override only the factory methods they need to customise.
//
// Actor registration:
//   HttpSseActor, HttpHandlerActor, SessionActor, and EventPublisherActor are
//   registered in the ActorRegistry for message-passing communication.
//
// Pipeline actors (CameraActor, InferenceActor, TrackerActor, etc.) communicate
// via direct Ramen port wiring and are NOT registered in the registry.
//
// Data flow:
//   Camera → Inference → Tracker → Cropper → SessionActor (storage)
//                                 ↘ Overlay → MJPEG Bridge → HttpSseActor
//
// The DecisionActor sits between Tracker and Cropper, gating crops based on
// trigger conditions (confidence threshold, class whitelist, cooldown).
class BaseDetectionPipeline {
public:
    virtual ~BaseDetectionPipeline();

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    // Initialise the pipeline: create actors, wire ports, start HTTP server.
    // Returns true on success.
    bool init(const PipelineConfig& cfg);

    // Shutdown the pipeline: stop actors, close connections, release resources.
    void shutdown();

    // ── Main loop ──────────────────────────────────────────────────────────────
    // Runs the pipeline main loop. Blocks until stop() is called from another
    // thread (e.g., via HTTP endpoint).
    //
    // Each tick:
    //   1. Check motion sensor (if configured) — skip frame if no motion
    //   2. Capture frame via camera->tick()
    //   3. Inference runs automatically via Ramen port wiring
    //   4. Flush overlay (push any pending frame without detections)
    //   5. Release frame buffers
    //   6. Sleep until next frame time (maintains configured FPS)
    void run_loop();

    // Request graceful shutdown of the main loop.
    void stop() { running_.store(false); }

    // ── Accessors ──────────────────────────────────────────────────────────────
    // Access the HTTP server actor (for external wiring / configuration).
    HttpSseActor*            httpServer()       { return http_server_.get(); }
    HttpHandlerActor*        httpHandler()      { return http_handler_.get(); }
    SessionActor*            sessionActor()     { return session_actor_.get(); }
    EventPublisherActor*     eventPublisher()   { return event_publisher_.get(); }

    // Access the pipeline config.
    const PipelineConfig& config() const { return cfg_; }

protected:
    // ── Factory methods (all overridable by subclasses) ────────────────────────
    // Each factory creates and returns the corresponding actor.
    // The base class provides default implementations that create standard actors.
    // Subclasses override only the factories they need to customise.
    //
    // Returns nullptr on failure (init will abort).

    // Camera — platform-specific (native synthetic, rock3c IMX219, etc.)
    virtual std::unique_ptr<CameraActor> createCamera();

    // Inference HAL — platform-specific (native stub, rock3c RKNN, etc.)
    virtual std::unique_ptr<IInferenceHAL> createInferenceHAL();

    // Inference actor — wraps an IInferenceHAL
    virtual std::unique_ptr<InferenceActor> createInferenceActor();

    // Tracker — ByteTracker-based object tracker
    virtual std::unique_ptr<TrackerActor> createTracker();

    // Cropper — crops detections from full-res frames
    virtual std::unique_ptr<CropperActor> createCropper();

    // Decision — trigger logic (confidence, cooldown, class whitelist)
    virtual std::unique_ptr<DecisionActor> createDecision();

    // Overlay — draws bounding boxes on medium-res frames
    virtual std::unique_ptr<OverlayActor> createOverlay();

    // MJPEG bridge — encodes frames for HTTP MJPEG streaming
    virtual std::unique_ptr<MjpegBridgeActor> createMjpegBridge();

    // Session actor — session lifecycle + classification storage
    virtual std::unique_ptr<SessionActor> createSessionActor();

    // HTTP server — CivetWeb-based HTTP/SSE/MJPEG server
    virtual std::unique_ptr<HttpSseActor> createHttpServer();

    // HTTP handler — REST API handler implementing the OpenAPI interface
    virtual std::unique_ptr<HttpHandlerActor> createHttpHandler();

    // Event publisher — generic event dispatch to SSE / future backends
    virtual std::unique_ptr<EventPublisherActor> createEventPublisher();

    // ── Actor instances (owned by the pipeline) ────────────────────────────────
    PipelineConfig cfg_;

    // Pipeline actors (communicate via Ramen port wiring)
    std::unique_ptr<CameraActor>      camera_;
    std::unique_ptr<InferenceActor>   inference_;
    std::unique_ptr<TrackerActor>     tracker_;
    std::unique_ptr<CropperActor>     cropper_;
    std::unique_ptr<DecisionActor>    decision_;
    std::unique_ptr<OverlayActor>     overlay_;
    std::unique_ptr<MjpegBridgeActor> mjpeg_bridge_;

    // Session actor (storage + session lifecycle, registered in ActorRegistry)
    std::unique_ptr<SessionActor>     session_actor_;

    // HTTP actors (registered in ActorRegistry for message passing)
    std::unique_ptr<HttpSseActor>     http_server_;
    std::unique_ptr<HttpHandlerActor> http_handler_;

    // Event publisher (registered in ActorRegistry for message passing)
    std::unique_ptr<EventPublisherActor> event_publisher_;

    // ── Wiring ─────────────────────────────────────────────────────────────────
    // Wire all actors together. Called during init().
    // Virtual so subclasses can customise the wiring (e.g., add/remove actors).
    virtual void wire();

    // ── Run loop control ───────────────────────────────────────────────────────
    std::atomic<bool> running_{false};
};

} // namespace ct
