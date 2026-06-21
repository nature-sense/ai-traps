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

# ─── platforms/radxa/rock-3c/hal/meson.build ──────────────────────────────────────────
# Rockchip RK3566/RK3568 platform sources and dependencies.
# Included by the root meson.build when -Dplatform=rock3c is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_imx219.cpp',
  'camera_hal_imx415.cpp',
  'camera_hal_ov5647.cpp',
  'camera_hal_v4l2_rkaiq.cpp',
  'mpp_h264_encoder.cpp',
  'inference_hal_rknn.cpp',
  'inference_hal_subprocess.cpp',
  'video_scaler_hal_rga.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# These are discovered by the root meson.build and passed down.
platform_deps = []
