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

# ─── src/hal/platforms/native/meson.build ─────────────────────────────────────────
# Native (host) platform stub HAL sources and dependencies.
# Included by the root meson.build when -Dplatform=host is set.

# ── Include shared V4L2 camera HAL (platform-independent) ─────────────────────
subdir('../common')

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_native.cpp',
  'inference_hal_native.cpp',
  'classifier_hal_native.cpp',
  'video_scaler_native.cpp',
) + common_sources

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.') + common_inc

# ── Platform dependencies ─────────────────────────────────────────────────────
# Native platform uses system libraries only.
platform_deps = []
