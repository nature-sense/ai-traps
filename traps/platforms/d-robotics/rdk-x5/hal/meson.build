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

# ─── src/hal/platforms/rdk-x5/meson.build ───────────────────────────────────────
# D-Robotics RDK X5 (X5 SoC) platform sources and dependencies.
# Included by the root meson.build when -Dplatform=rdk-x5 is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_rdkx5.cpp',
  'inference_hal_bpu.cpp',
  'video_scaler_vse.cpp',
  'h264_encoder_hw.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# RDK X5 / D-Robotics X5 SoC dependencies:
#   - SP camera + codec API: loaded at runtime via dlopen (libspcdev.so)
#     No compile-time link deps needed.
dnn_lib = cpp.find_library('dnn', dirs: lib_dirs, required: false)
platform_deps = []
if dnn_lib.found()
  platform_deps += dnn_lib
endif
