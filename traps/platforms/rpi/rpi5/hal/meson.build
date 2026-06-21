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

# ─── src/hal/platforms/rpi5/meson.build ─────────────────────────────────────────
# Raspberry Pi 5 platform sources and dependencies.
# Included by the root meson.build when -Dplatform=rpi5 is set.
#
# Dependencies:
#   - libcamera (camera capture + ISP)
#   - libedgetpu (Coral Edge TPU runtime)
#   - tensorflow-lite (TFLite C++ API)
#   - V4L2 (kernel headers only, no extra linkage needed)

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_rpi5.cpp',
  'inference_hal_coral.cpp',
  'h264_encoder_rpi5.cpp',
  'jpeg_encoder_rpi5.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies (all required: true — no fallbacks) ─────────────────
libcamera_dep = dependency('libcamera', required: true)
tflite_dep = cpp.find_library('tensorflow-lite', dirs: lib_dirs, required: true)
edgetpu_dep = cpp.find_library('edgetpu', dirs: lib_dirs, required: true)
platform_deps = [libcamera_dep, tflite_dep, edgetpu_dep]

# Coral Edge TPU headers may be in /usr/include/edgetpu/ or similar
edgetpu_inc_dir = ''
edgetpu_candidates = ['/usr/include/edgetpu', '/usr/local/include/edgetpu']
if meson.is_cross_build() and sysroot_arg != ''
  edgetpu_candidates += [sysroot_arg + '/usr/include/edgetpu']
endif
foreach cand : edgetpu_candidates
  if run_command('sh', '-c', 'test -d ' + cand + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    edgetpu_inc_dir = cand
    break
  endif
endforeach
if edgetpu_inc_dir != ''
  inc_dirs += [edgetpu_inc_dir]
endif

# TFLite headers may be in /usr/include/tensorflow/ or similar
tflite_inc_dir = ''
tflite_candidates = ['/usr/include/tensorflow', '/usr/local/include/tensorflow']
if meson.is_cross_build() and sysroot_arg != ''
  tflite_candidates += [sysroot_arg + '/usr/include/tensorflow']
endif
foreach cand : tflite_candidates
  if run_command('sh', '-c', 'test -d ' + cand + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    tflite_inc_dir = cand
    break
  endif
endforeach
if tflite_inc_dir != ''
  inc_dirs += [tflite_inc_dir]
endif