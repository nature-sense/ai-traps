# Copyright 2026 Nature Sense
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# ─── src/actors/meson.build ──────────────────────────────────────────────────────
# All Ramen actor sources. No longer split by pipeline stage — actors are
# self-contained and can be composed into any pipeline.

actor_sources = files(
  # JPEG encoders
  'jpeg-encoder/hardware_jpeg_encoder.cpp',
  'jpeg-encoder/software_jpeg_encoder.cpp',

  # Camera actors
  'camera/camera_hal_actor.cpp',
  'camera/camera_factory.cpp',
  'camera/scene_camera_actor.cpp',

  # MJPEG bridge (live preview stream)
  'mjpeg-bridge/mjpeg_bridge_actor.cpp',

  # Motion sensor (PIR)
  'motion-sensor/motion_sensor_actor.cpp',

  # Actuator (relay / solenoid)
  'actuator/actuator_actor.cpp',

  # Inference (object detection)
  'inference/inference_actor.cpp',

  # Cropper (crop detections from frame)
  'cropper/cropper_actor.cpp',

  # Tracker (track objects across frames)
  'tracker/tracker_actor.cpp',

  # Overlay (draw bounding boxes / labels)
  'overlay/overlay_actor.cpp',

  # Decision (decide when to trigger capture)
  'decision/decision_actor.cpp',

  # Storage (SQLite + JPEG persistence)
  'storage2/storage_actor.cpp',

  # HTTP server + SSE + MJPEG streaming
  'http-sse/http_sse_actor.cpp',
  'http-sse/civetweb.c',

  # Base HTTP handler (abstract base for REST endpoint handlers)
  'base-http-handler/base_http_handler.cpp',

  # Event publisher (generic event dispatch to SSE / future backends)
  'event-publisher/event_publisher_actor.cpp',
)
