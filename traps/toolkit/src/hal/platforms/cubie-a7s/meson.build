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

# ─── src/hal/platforms/cubie-a7s/meson.build ────────────────────────────────────
# Radxa Cubie A7S (Allwinner A527) platform sources and dependencies.
# Included by the root meson.build when -Dplatform=cubie-a7s is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_a7s.cpp',
  'inference_hal_vip.cpp',
  'video_scaler_opencl.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# These are discovered by the root meson.build and passed down.
# platform_deps is populated by the root meson.build based on SDK detection.
platform_deps = []
