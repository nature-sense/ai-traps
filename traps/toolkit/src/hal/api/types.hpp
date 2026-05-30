#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "frame_buffer.hpp"

namespace ct {

// ─── Single bounding-box detection from RKNN ─────────────────────────────────

// Coordinates are in lores (320×320) frame space, normalised [0,1].
struct Detection {
    int   class_id   = 0;
    float confidence = 0.f;
    float x = 0.f;   // left edge
    float y = 0.f;   // top edge
    float w = 0.f;   // width
    float h = 0.f;   // height
};

// ─── Tracked object from ByteTracker ─────────────────────────────────────────
// Carries a persistent track_id across frames.
// bbox in full-res pixel coordinates (for cropping).
struct TrackedObject {
    int       track_id  = 0;
    Detection detection;                        // original lores detection
    int       full_x = 0, full_y = 0;          // full-res bbox origin
    int       full_w = 0, full_h = 0;          // full-res bbox size
};

// ─── JPEG-encoded crop produced by CropperNode ───────────────────────────────
struct JpegCrop {
    int                  track_id     = 0;
    int                  class_id     = 0;
    float                confidence   = 0.f;
    int64_t              timestamp_ms = 0;
    std::vector<uint8_t> data;          // encoded JPEG bytes
    bool                 is_update    = false; // true if this replaces an existing detection
};

// ─── Classification result from IClassifierHAL ───────────────────────────────
struct Classification {
    int       class_id = 0;
    float     confidence = 0.f;
    int64_t   timestamp_ms = 0;  // timestamp of the classified image
};

// ─── Best shot for a track — emitted when track closes ──────────────────────
// Carries the highest-confidence detection's full-res frame + bbox.
struct BestShot {
    int       track_id = 0;
    int       class_id = 0;       // from detection model
    float     confidence = 0.f;   // best detection confidence
    int64_t   timestamp_ms = 0;
    int       bbox_x = 0, bbox_y = 0, bbox_w = 0, bbox_h = 0;
    FrameBuffer frame;
};

// ─── Classified track — emitted by ClassifierActor ──────────────────────────
// Carries the classifier result plus the original detection metadata.
struct ClassifiedTrack {
    int       track_id = 0;
    Classification classification;           // from classifier model
    float     detection_confidence = 0.f;    // original detection confidence
    int       detection_class_id = 0;        // original detection class
    int       bbox_x = 0, bbox_y = 0, bbox_w = 0, bbox_h = 0;
    FrameBuffer frame;  // still needed for JPEG encoding downstream
};

// ─── Per-actor configuration blocks ──────────────────────────────────────────
// Each block groups the configuration fields for a single actor.
// The top-level PipelineConfig contains one block per configurable actor.

struct CameraConfig {
    // Camera model selection
    // Supported: "imx219", "imx415", "uvc"
    std::string model = "imx219";

    // Camera / VPSS
    int full_w  = 1920, full_h  = 1080;   // full-res output
    int med_w   =  640, med_h   =  480;   // medium-res for MJPEG stream
    int lores_w =  320, lores_h =  320;   // lores for YOLO inference
    int fps     = 15;

    // USB camera device path (for UVC cameras)
    std::string device = "/dev/video0";

    // Scene directory (for scene camera — sequence of PNG frames)
    std::string scene_dir = "";

    // ISP tuning (RK_AIQ)
    std::string iq_dir = "/oem/usr/share/iqfiles";
};

struct InferenceConfig {
    // Inference backend selection
    // Supported: "rknn", "bpu", "tflite"
    std::string backend = "rknn";

    // Path to the detection model
    std::string model_path = "/data/yolo.rknn";

    // Global confidence threshold for detections
    float confidence_threshold = 0.5f;
};

struct StorageConfig {
    // Output directory for captured crops
    std::string output_dir = "/data/captures";

    // SQLite database path
    std::string db_path = "/data/detections.db";
};

struct CropperConfig {
    // Padding (in pixels) added around each bounding box when cropping
    int padding_px = 16;

    // Skip crops below this confidence (0 = use global confidence_threshold)
    float min_confidence = 0.0f;
};

struct ClassifierConfig {
    // Path to the classifier model. Empty = no classifier (detection-only mode).
    std::string model_path = "";

    float confidence_threshold = 0.5f;
    int   input_width  = 224;
    int   input_height = 224;
};

struct ActuatorConfig {
    // Actuator type: "relay", "servo", "none"
    std::string type = "none";

    // GPIO pin for the actuator
    int gpio = -1;

    // Duration (ms) to activate the actuator
    int duration_ms = 3000;
};

struct MotionSensorConfig {
    // PIR motion sensor GPIO pin. -1 = disabled (always-on mode)
    int gpio = -1;
};

struct DecisionConfig {
    // Trigger confidence override. 0 = use global confidence_threshold.
    float trigger_confidence = 0.0f;

    // Minimum time (ms) between consecutive triggers
    int64_t cooldown_ms = 5000;
};

// ─── Top-level pipeline configuration ────────────────────────────────────────
struct PipelineConfig {
    CameraConfig      camera;
    InferenceConfig   inference;
    StorageConfig     storage;
    CropperConfig     cropper;
    ClassifierConfig  classifier;
    ActuatorConfig    actuator;
    MotionSensorConfig motion_sensor;
    DecisionConfig    decision;
};


} // namespace ct
