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

# ─── targets/meson.build ─────────────────────────────────────────────────────────
# Separate build for target-specific trap executables.
# Depends on the toolkit static library (libai-trap-toolkit.a).
#
# Usage:
#   meson setup build -Dplatform=host
#   meson compile -C build
#
# The toolkit library path is expected at ../toolkit/build/libai-trap-toolkit.a
# or can be overridden via the toolkit_lib_dir option.

project('ai-trap-targets', ['c', 'cpp'],
  version : '0.1.0',
  default_options : [
    'warning_level=3',
    'werror=false',
    'cpp_std=c++2a',
    'buildtype=debugoptimized',
  ]
)

cpp = meson.get_compiler('cpp')

# ─── Library search paths ───────────────────────────────────────────────────────
# Cross-compilation sysroot paths (used inside Docker for aarch64 cross-builds)
# For native cross-compilation on macOS, extract sysroot from c_args
sysroot_dir = ''
if meson.is_cross_build()
  foreach a : get_option('c_args')
    if a.startswith('--sysroot=')
      sysroot_dir = a.substring(10)
    endif
  endforeach
endif
lib_dirs = ['/usr/lib', '/lib', '/usr/local/lib',
            '/usr/aarch64-linux-gnu/lib', '/usr/lib/aarch64-linux-gnu',
            '/usr/lib/aarch64-linux-gnu/include/lib']
if sysroot_dir != ''
  lib_dirs += [sysroot_dir + '/usr/lib/aarch64-linux-gnu',
               sysroot_dir + '/lib/aarch64-linux-gnu',
               sysroot_dir + '/usr/lib/gcc/aarch64-linux-gnu/10']
endif

# ─── Dependencies ───────────────────────────────────────────────────────────────
thread_dep    = dependency('threads', required: true)
jpeg_dep      = dependency('libjpeg', required: false)
turbojpeg_dep = dependency('libturbojpeg', required: false)
png_dep       = dependency('libpng', required: false)
yaml_cpp_dep  = dependency('yaml-cpp', required: false)
systemd_dep   = dependency('libsystemd', required: false)

platform_deps = [thread_dep]
# When cross-compiling without pkg-config .pc files, find_library won't
# work for sysroot libraries. Use link_args to add them directly.
# Only use direct link args when the .pc files are NOT available.
if meson.is_cross_build() and sysroot_dir != '' and not yaml_cpp_dep.found()
  message('Using sysroot direct link args for libraries')
  extra_link_args = []
  foreach lib : ['libjpeg.so', 'libyaml-cpp.so', 'libpng16.so', 'libc_nonshared.a']
    libpath = sysroot_dir + '/usr/lib/aarch64-linux-gnu/' + lib
    if run_command('sh', '-c', 'test -f ' + libpath + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
      extra_link_args += [libpath]
    endif
  endforeach
  if run_command('sh', '-c', 'test -f ' + sysroot_dir + '/usr/lib/aarch64-linux-gnu/libsystemd.so && echo yes || echo no', check: false).stdout().strip() == 'yes'
    extra_link_args += [sysroot_dir + '/usr/lib/aarch64-linux-gnu/libsystemd.so']
  endif
  if extra_link_args.length() > 0
    platform_deps += declare_dependency(link_args: extra_link_args)
  endif
endif
# Hardware libraries are needed by the toolkit static lib regardless of
# whether pkg-config found yaml-cpp. Add them unconditionally during
# cross-compilation using direct link_args.
if meson.is_cross_build() and sysroot_dir != ''
  hw_link_args = []
  foreach hw_lib : ['librockchip_mpp.so', 'librga.so', 'librknnrt.so', 'librkaiq.so']
    libpath = sysroot_dir + '/usr/lib/aarch64-linux-gnu/' + hw_lib
    if run_command('sh', '-c', 'test -f ' + libpath + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
      hw_link_args += [libpath]
    endif
  endforeach
  if hw_link_args.length() > 0
    platform_deps += declare_dependency(link_args: hw_link_args)
  endif
endif
jpeg_lib      = cpp.find_library('jpeg', dirs: lib_dirs, required: false)
png_lib       = cpp.find_library('png16', dirs: lib_dirs, required: false)
yaml_cpp_lib  = cpp.find_library('yaml-cpp', dirs: lib_dirs, required: false)
systemd_lib   = cpp.find_library('systemd', dirs: lib_dirs, required: false)
turbojpeg_lib = cpp.find_library('turbojpeg', dirs: lib_dirs, required: false)
foreach dep : [jpeg_lib, turbojpeg_lib, png_lib, yaml_cpp_lib, systemd_lib]
  if dep.found()
    platform_deps += dep
  endif
endforeach

# ─── Toolkit Library ────────────────────────────────────────────────────────────
# Path to the pre-built toolkit library and its headers.
# Use declare_dependency with the static library directly to avoid
# find_library's link test (which fails in cross-compilation without
# crtbeginS.o / libstdc++ symlinks in the sysroot).
toolkit_build_dir = get_option('toolkit_build_dir')
if not toolkit_build_dir.startswith('/')
  toolkit_build_dir = meson.project_source_root() / toolkit_build_dir
endif
toolkit_lib = declare_dependency(
  link_args: [toolkit_build_dir / 'libai-trap-toolkit.a'],
  version: '0.3.0'
)

# Include paths for toolkit headers
toolkit_inc_dirs = [
  '../toolkit/src',
  '../toolkit/src/hal/api',
  '../toolkit/src/actors',
  '../toolkit/src/actors/http-sse',
  '../toolkit/include',
]

# macOS Homebrew paths (only add if they exist and we're not cross-compiling)
# When cross-compiling, Homebrew headers can conflict with sysroot headers.
foreach d : ['/opt/homebrew/include', '/opt/homebrew/Cellar/nlohmann-json/3.12.0/include']
  if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    if meson.is_cross_build()
      message('Skipping Homebrew include path ' + d + ' during cross-compilation')
    else
      toolkit_inc_dirs += [d]
    endif
  endif
endforeach

# Try to get SDK path from environment variable (for cross-compilation)
sdk_path = run_command('sh', '-c', 'echo $RADXA_SDK', check: false).stdout().strip()
if sdk_path != ''
  # Add Rockchip SDK include dirs from the sysroot
  foreach d : [sdk_path + '/include/rkaiq/common', sdk_path + '/include/rkaiq/algos',
               sdk_path + '/include/rkaiq/xcore', sdk_path + '/include/rkaiq/iq_parser',
               sdk_path + '/include/rkaiq/iq_parser_v2', sdk_path + '/include/rkaiq/uAPI2',
               sdk_path + '/include/rkaiq', sdk_path + '/include/rknn',
               sdk_path + '/include/rockchip', sdk_path + '/include/rga']
    if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
      toolkit_inc_dirs += [d]
    endif
  endforeach
else
  # When cross-compiling, the SDK headers live inside the sysroot, not on the
  # build machine.  Check both the native path and the sysroot path.
  rkaiq_candidates = ['/usr/include/rockchip', '/usr/include/rkaiq/common', '/usr/include/rkaiq/algos',
                      '/usr/include/rkaiq/xcore', '/usr/include/rkaiq/iq_parser',
                      '/usr/include/rkaiq/iq_parser_v2', '/usr/include/rkaiq/uAPI2', '/usr/include/rkaiq',
                      '/usr/include/rknn']
  if meson.is_cross_build()
    # Extract sysroot from the cross-compilation c_args (--sysroot=...)
    sysroot_arg = ''
    foreach a : get_option('c_args')
      if a.startswith('--sysroot=')
        sysroot_arg = a.strip('--sysroot=')
      endif
    endforeach
    if sysroot_arg != ''
      rkaiq_candidates += [sysroot_arg + '/usr/include/rockchip',
                           sysroot_arg + '/usr/include/rkaiq/common',
                           sysroot_arg + '/usr/include/rkaiq/algos',
                           sysroot_arg + '/usr/include/rkaiq/xcore',
                           sysroot_arg + '/usr/include/rkaiq/iq_parser',
                           sysroot_arg + '/usr/include/rkaiq/iq_parser_v2',
                           sysroot_arg + '/usr/include/rkaiq/uAPI2',
                           sysroot_arg + '/usr/include/rkaiq',
                           sysroot_arg + '/usr/include/rknn']
    endif
  endif
  foreach d : rkaiq_candidates
    if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
      toolkit_inc_dirs += [d]
    endif
  endforeach
endif

toolkit_inc = include_directories(toolkit_inc_dirs)

# ─── Platform Selection ─────────────────────────────────────────────────────────
platform = get_option('platform')

# ─── Target: detection ──────────────────────────────────────────────────────────
# Detection-only trap: uses toolkit actors + detection-specific storage actor
# + pipeline assembly.

# Pipeline sources
subdir('src/detection/pipeline/')

detection_sources = files(
  'src/detection/actors/session/session_actor.cpp',
  'src/detection/actors/http-handler/http_handler_actor.cpp',
  'src/detection/actors/ws-api/ws_api_handler.cpp',
) + pipeline_sources

detection_inc = [toolkit_inc, pipeline_inc,
  include_directories('src/detection'),
  include_directories('src/detection/actors/session'),
  include_directories('src/detection/actors/http-handler'),
  include_directories('src/detection/actors/ws-api')]

if platform == 'host'
  detection_sources += files(
    '../platforms/native/pipeline/main.cpp',
    '../platforms/native/pipeline/native_detection_pipeline.cpp',
  )
endif

if platform == 'rock3c'
  detection_sources += files(
    '../platforms/radxa/rock-3c/pipeline/main.cpp',
    '../platforms/radxa/rock-3c/pipeline/rock3c_detection_pipeline.cpp',
  )
endif

if platform == 'cubie-a7s'
  detection_sources += files(
    '../platforms/radxa/cubie-a7s/pipeline/main.cpp',
    '../platforms/radxa/cubie-a7s/pipeline/cubie_a7s_detection_pipeline.cpp',
  )
endif

if platform == 'rdk-x5'
  detection_sources += files(
    '../platforms/d-robotics/rdk-x5/pipeline/main.cpp',
    '../platforms/d-robotics/rdk-x5/pipeline/rdk_x5_detection_pipeline.cpp',
  )
endif

executable('ai-trap-detection',
  detection_sources,
  include_directories : detection_inc,
  dependencies : [toolkit_lib] + platform_deps,
  install : true,
  install_dir : '/usr/bin')
