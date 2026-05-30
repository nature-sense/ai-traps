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

#include "base_detection_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <thread>
#include <chrono>

namespace ct {

// ─── Destructor ───────────────────────────────────────────────────────────────

BaseDetectionPipeline::~BaseDetectionPipeline() {
    shutdown();
}

// ─── Default factory implementations ──────────────────────────────────────────
// These create standard actors. Subclasses override only the factories they
// need to customise (e.g., createCamera for platform-specific cameras).

std::unique_ptr<CameraActor> BaseDetectionPipeline::createCamera() {
    // Default: no camera — subclasses must override
    return nullptr;
}

std::unique_ptr<IInferenceHAL> BaseDetectionPipeline::createInferenceHAL() {
    // Default: no inference HAL — subclasses must override
    return nullptr;
}

std::unique_ptr<InferenceActor> BaseDetectionPipeline::createInferenceActor() {
    auto hal = createInferenceHAL();
    if (!hal) {
        std::cerr << "[BaseDetectionPipeline] createInferenceHAL() returned null\n";
        return nullptr;
    }
    auto actor = std::make_unique<InferenceActor>(std::move(hal));
    return actor;
}

std::unique_ptr<TrackerActor> BaseDetectionPipeline::createTracker() {
    return std::make_unique<TrackerActor>();
}

std::unique_ptr<CropperActor> BaseDetectionPipeline::createCropper() {
    return std::make_unique<CropperActor>();
}

std::unique_ptr<DecisionActor> BaseDetectionPipeline::createDecision() {
    return std::make_unique<DecisionActor>();
}

std::unique_ptr<OverlayActor> BaseDetectionPipeline::createOverlay() {
    return std::make_unique<OverlayActor>();
}

std::unique_ptr<MjpegBridgeActor> BaseDetectionPipeline::createMjpegBridge() {
    return std::make_unique<MjpegBridgeActor>();
}

std::unique_ptr<SessionActor> BaseDetectionPipeline::createSessionActor() {
    return std::make_unique<SessionActor>();
}

std::unique_ptr<HttpSseActor> BaseDetectionPipeline::createHttpServer() {
    return std::make_unique<HttpSseActor>(8080);
}

std::unique_ptr<HttpHandlerActor> BaseDetectionPipeline::createHttpHandler() {
    return std::make_unique<HttpHandlerActor>("http_handler");
}

std::unique_ptr<EventPublisherActor> BaseDetectionPipeline::createEventPublisher() {
    return std::make_unique<EventPublisherActor>();
}

// ─── Init ─────────────────────────────────────────────────────────────────────

bool BaseDetectionPipeline::init(const PipelineConfig& cfg) {
    cfg_ = cfg;
    std::cout << "[BaseDetectionPipeline] init\n"
              << "  camera_model: " << cfg_.camera.model << "\n"
              << "  resolution: " << cfg_.camera.full_w << "x" << cfg_.camera.full_h
              << " @ " << cfg_.camera.fps << "fps\n"
              << "  model: " << cfg_.inference.model_path << "\n"
              << "  output: " << cfg_.storage.output_dir << "\n";

    // ── 1. Create camera ──────────────────────────────────────────────────────
    camera_ = createCamera();
    if (!camera_) {
        std::cerr << "[BaseDetectionPipeline] createCamera() failed\n";
        return false;
    }
    if (!camera_->init(cfg_)) {
        std::cerr << "[BaseDetectionPipeline] camera init failed\n";
        return false;
    }

    // ── 2. Create inference actor (which creates its own HAL) ──────────────────
    inference_ = createInferenceActor();
    if (!inference_) {
        std::cerr << "[BaseDetectionPipeline] createInferenceActor() failed\n";
        return false;
    }
    if (!inference_->init(cfg_.inference.model_path, cfg_.inference.confidence_threshold)) {
        std::cerr << "[BaseDetectionPipeline] inference init failed\n";
        return false;
    }

    // ── 3. Create tracker ─────────────────────────────────────────────────────
    tracker_ = createTracker();
    if (!tracker_) {
        std::cerr << "[BaseDetectionPipeline] createTracker() failed\n";
        return false;
    }
    tracker_->init(cfg_.camera.lores_w, cfg_.camera.lores_h, cfg_.camera.full_w, cfg_.camera.full_h);
    tracker_->min_confidence = cfg_.inference.confidence_threshold;
    tracker_->new_track_min_confidence = 0.6f;
    tracker_->min_detections_for_track = 3;

    // ── 4. Create cropper ─────────────────────────────────────────────────────
    cropper_ = createCropper();
    if (!cropper_) {
        std::cerr << "[BaseDetectionPipeline] createCropper() failed\n";
        return false;
    }
    cropper_->padding_px = cfg_.cropper.padding_px;
    cropper_->min_confidence = cfg_.cropper.min_confidence > 0.f
                               ? cfg_.cropper.min_confidence
                               : cfg_.inference.confidence_threshold;

    // ── 5. Create decision actor ──────────────────────────────────────────────
    decision_ = createDecision();
    if (!decision_) {
        std::cerr << "[BaseDetectionPipeline] createDecision() failed\n";
        return false;
    }
    decision_->trigger_confidence = cfg_.decision.trigger_confidence > 0.f
                                    ? cfg_.decision.trigger_confidence
                                    : cfg_.inference.confidence_threshold;
    decision_->cooldown_ms = cfg_.decision.cooldown_ms;
    decision_->init();

    // ── 6. Create overlay ─────────────────────────────────────────────────────
    overlay_ = createOverlay();
    if (!overlay_) {
        std::cerr << "[BaseDetectionPipeline] createOverlay() failed\n";
        return false;
    }
    overlay_->full_w = cfg_.camera.full_w;
    overlay_->full_h = cfg_.camera.full_h;

    // ── 7. Create MJPEG bridge ────────────────────────────────────────────────
    mjpeg_bridge_ = createMjpegBridge();
    if (!mjpeg_bridge_) {
        std::cerr << "[BaseDetectionPipeline] createMjpegBridge() failed\n";
        return false;
    }

    // ── 8. Create session actor (storage + session lifecycle) ──────────────────
    session_actor_ = createSessionActor();
    if (!session_actor_) {
        std::cerr << "[BaseDetectionPipeline] createSessionActor() failed\n";
        return false;
    }
    if (!session_actor_->init(cfg_.storage.output_dir, cfg_.storage.db_path, "", 0)) {
        std::cerr << "[BaseDetectionPipeline] session actor init failed\n";
        return false;
    }

    // ── 9. Create HTTP actors ──────────────────────────────────────────────────
    http_server_ = createHttpServer();
    if (!http_server_) {
        std::cerr << "[BaseDetectionPipeline] createHttpServer() failed\n";
        return false;
    }

    http_handler_ = createHttpHandler();
    if (!http_handler_) {
        std::cerr << "[BaseDetectionPipeline] createHttpHandler() failed\n";
        return false;
    }

    // Bind the session actor to the HTTP handler
    http_handler_->setSessionActor(session_actor_.get());

    // ── 10. Create event publisher ────────────────────────────────────────────
    event_publisher_ = createEventPublisher();
    if (!event_publisher_) {
        std::cerr << "[BaseDetectionPipeline] createEventPublisher() failed\n";
        return false;
    }

    // Wire the event publisher to the SSE backend
    event_publisher_->setSseActor(http_server_.get());

    // Wire the event publisher to the HTTP handler (for REST action events)
    http_handler_->setEventPublisher(event_publisher_.get());

    // Wire the session actor to publish events when crops are saved
    session_actor_->set_on_saved(
        [this](const JpegCrop& crop, const std::string& path, int64_t classification_id) {
            nlohmann::json evt = {
                {"classificationId", classification_id},
                {"trackId", crop.track_id},
                {"classId", crop.class_id},
                {"confidence", crop.confidence},
                {"timestamp", crop.timestamp_ms},
                {"imagePath", path},
                {"isUpdate", crop.is_update}
            };
            event_publisher_->in_event(Event{
                "classification_saved", evt.dump()
            });
        });

    // ── 11. Register actors in ActorRegistry ──────────────────────────────────
    auto* registry = ramen::ActorRegistry::instance();
    registry->registerActor("http_server", http_server_.get());
    registry->registerActor("http_handler", http_handler_.get());
    registry->registerActor("session_actor", session_actor_.get());
    registry->registerActor("event_publisher", event_publisher_.get());

    // ── 12. Start actors ──────────────────────────────────────────────────────
    http_server_->onStart();
    http_handler_->onStart();
    event_publisher_->onStart();

    // ── 13. Wire the pipeline ─────────────────────────────────────────────────
    wire();

    std::cout << "[BaseDetectionPipeline] ready\n";
    return true;
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void BaseDetectionPipeline::shutdown() {
    std::cout << "[BaseDetectionPipeline] shutdown\n";

    // Stop the run loop
    running_.store(false);

    // Stop event publisher
    if (event_publisher_) {
        event_publisher_->onStop();
    }

    // Stop HTTP actors
    if (http_handler_) {
        http_handler_->onStop();
    }
    if (http_server_) {
        http_server_->onStop();
    }

    // Unregister actors
    auto* registry = ramen::ActorRegistry::instance();
    registry->unregisterActor("event_publisher");
    registry->unregisterActor("session_actor");
    registry->unregisterActor("http_handler");
    registry->unregisterActor("http_server");

    // Shutdown session actor
    if (session_actor_) {
        session_actor_->shutdown();
    }

    // Shutdown inference
    if (inference_) {
        inference_->shutdown();
    }

    // Shutdown camera
    if (camera_) {
        camera_->shutdown();
        camera_.reset();
    }

    std::cout << "[BaseDetectionPipeline] shutdown complete\n";
}

// ── Wire ──────────────────────────────────────────────────────────────────────

void BaseDetectionPipeline::wire() {
    std::cout << "[BaseDetectionPipeline] wiring actors\n";

    // ── Camera → Inference ────────────────────────────────────────────────────
    // Camera pushes lores frames → Inference detects objects
    camera_->out_frame_lores >> inference_->in_frame;

    // ── Inference → Tracker ────────────────────────────────────────────────────
    // Inference emits detections → Tracker assigns stable track IDs
    inference_->out_detections >> tracker_->in_detections;

    // ── Tracker → Decision ─────────────────────────────────────────────────────
    // Tracker emits tracked objects → Decision applies trigger logic
    tracker_->out_tracked >> decision_->in_tracked;

    // ── Decision → Cropper ─────────────────────────────────────────────────────
    // Decision emits trigger → Cropper captures best crop for each track
    // Note: DecisionActor emits bool trigger, but CropperActor receives
    // TrackedObject directly. The decision acts as a gate — when triggered,
    // the cropper processes the tracked objects from the tracker.
    // For now, we wire tracker directly to cropper and use decision for
    // future gating logic.
    tracker_->out_tracked >> cropper_->in_tracked;

    // ── Camera → Cropper (full-res frame for cropping) ─────────────────────────
    camera_->out_frame_full >> cropper_->in_frame_full;

    // ── Cropper → SessionActor (storage) ───────────────────────────────────────
    // Cropper emits JPEG crops → SessionActor saves to disk + SQLite
    cropper_->out_crops >> session_actor_->in_crops;

    // ── Camera → Overlay (medium-res frame for MJPEG stream) ───────────────────
    camera_->out_frame_medium >> overlay_->in_frame;

    // ── Tracker → Overlay (tracked objects for bounding box drawing) ───────────
    tracker_->out_tracked >> overlay_->in_tracked;

    // ── Overlay → MJPEG Bridge (encoded frames for HTTP streaming) ─────────────
    overlay_->out_frame >> mjpeg_bridge_->in_frame;

    std::cout << "[BaseDetectionPipeline] wiring complete\n";
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void BaseDetectionPipeline::run_loop() {
    if (!camera_) {
        std::cerr << "[BaseDetectionPipeline] run_loop: camera not initialised\n";
        return;
    }

    running_.store(true);

    const auto frame_interval = std::chrono::microseconds(1'000'000 / cfg_.camera.fps);
    auto next_frame_time = std::chrono::steady_clock::now();

    std::cout << "[BaseDetectionPipeline] run_loop started @ " << cfg_.camera.fps << " fps\n";

    while (running_.load()) {
        // ── 1. Check if inference is enabled (session active) ──────────────────
        if (!session_actor_->is_inference_enabled()) {
            // No active session — skip frame processing but maintain timing
            next_frame_time += frame_interval;
            std::this_thread::sleep_until(next_frame_time);
            continue;
        }

        // ── 2. Capture frame ───────────────────────────────────────────────────
        // camera->tick() pushes frames to out_frame_* ports, which triggers
        // the entire pipeline via Ramen port wiring:
        //   out_frame_lores  → inference.in_frame  → tracker.in_detections
        //   out_frame_full   → cropper.in_frame_full
        //   out_frame_medium → overlay.in_frame
        camera_->tick();

        // ── 3. Flush overlay ───────────────────────────────────────────────────
        // Push any pending frame that didn't receive tracked objects
        overlay_->flush();

        // ── 4. Release frame buffers ───────────────────────────────────────────
        // camera_->release_frames();

        // ── 5. Frame timing ────────────────────────────────────────────────────
        next_frame_time += frame_interval;
        std::this_thread::sleep_until(next_frame_time);
    }

    std::cout << "[BaseDetectionPipeline] run_loop stopped\n";
}

} // namespace ct
