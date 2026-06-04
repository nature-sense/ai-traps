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

# AI Camera Trap — Root Meson Build

project('radxa-camera-trap', ['c', 'cpp'],
  version : '0.3.0',
  default_options : [
    'warning_level=3',
    'werror=false',
    'cpp_std=c++20',
    'buildtype=debugoptimized',
  ]
)

cpp = meson.get_compiler('cpp')

# ─── Dependencies ─────────────────────────────────────────────────────────────

# Try to get SDK path from environment variable
sdk_path = run_command('sh', '-c', 'echo $RADXA_SDK', check: false).stdout().strip()
if sdk_path == ''
  sdk_path = '/opt/radxa-sdk'
endif

# Check if SDK path exists, fallback to /usr if not
sdk_exists = run_command('sh', '-c', 'test -d ' + sdk_path + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
if not sdk_exists
  sdk_path = '/usr'
endif

lib_dirs = [sdk_path + '/lib', '/usr/lib', '/lib', '/usr/local/lib']
inc_dirs = ['/usr/local/include']

# macOS Homebrew paths (only add if they exist)
foreach d : ['/opt/homebrew/include', '/opt/homebrew/Cellar/nlohmann-json/3.12.0/include']
  if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    inc_dirs += [d]
  endif
endforeach

# nlohmann-json: check pkg-config first, then common paths
nlohmann_dep = dependency('nlohmann_json', required: false)
if not nlohmann_dep.found()
  nlohmann_dep = dependency('nlohmann-json3-dev', required: false)
endif
if not nlohmann_dep.found()
  # Check common include paths for the single-header file
  foreach d : ['/usr/include', '/usr/local/include', '/usr/include/nlohmann',
               meson.current_source_dir() + '/build-rock3c/include',
               meson.current_source_dir() + '/build-native/include']
    if run_command('sh', '-c', 'test -f ' + d + '/nlohmann/json.hpp && echo yes || echo no', check: false).stdout().strip() == 'yes'
      inc_dirs += [d]
    endif
  endforeach
endif

# Only add include dirs that actually exist (macOS doesn't have /usr/include)
foreach d : [sdk_path + '/include', '/usr/include']
  if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    inc_dirs += [d]
  endif
endforeach

# Rockchip-specific include paths
rockchip_inc = sdk_path + '/include/rockchip'
if run_command('sh', '-c', 'test -d ' + rockchip_inc + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
  inc_dirs += [rockchip_inc]
endif

# Add RKAIQ include subdirectories (mandatory for uAPI2 headers)
rkaiq_inc_dir = sdk_path + '/include/rkaiq'
rkaiq_inc_exists = run_command('sh', '-c', 'test -d ' + rkaiq_inc_dir + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
if rkaiq_inc_exists
    # rkaiq headers use relative includes across sub-directories
    foreach sub : ['common', 'algos', 'xcore', 'iq_parser', 'iq_parser_v2', 'uAPI2']
        subdir = rkaiq_inc_dir + '/' + sub
        if run_command('sh', '-c', 'test -d ' + subdir + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
            inc_dirs += [subdir]
        endif
    endforeach
    inc_dirs += [rkaiq_inc_dir]
endif

# Find hardware libraries (Rockchip)
mpp_lib    = cpp.find_library('rockchip_mpp', dirs: lib_dirs, required: false)
rga_lib    = cpp.find_library('rga', dirs: lib_dirs, required: false)
rknn_lib   = cpp.find_library('rknnrt', dirs: lib_dirs, required: false)
rkaiq_lib  = cpp.find_library('rkaiq', dirs: lib_dirs, required: false)

# Find hardware libraries (Cubie A7S / Allwinner A527)
libcamera_dep = dependency('libcamera', required: false)
opencl_lib     = cpp.find_library('OpenCL', dirs: lib_dirs, required: false)
openvx_lib     = cpp.find_library('OpenVX', dirs: lib_dirs, required: false)
openvxu_lib    = cpp.find_library('OpenVXU', dirs: lib_dirs, required: false)
clc_lib        = cpp.find_library('CLC', dirs: lib_dirs, required: false)
vsc_lib        = cpp.find_library('VSC', dirs: lib_dirs, required: false)
gal_lib        = cpp.find_library('GAL', dirs: lib_dirs, required: false)

# Standard dependencies
yaml_cpp_dep  = dependency('yaml-cpp', required: false)
sqlite3_dep   = dependency('sqlite3', required: false)
jpeg_dep      = dependency('libjpeg', required: false)
turbojpeg_dep = dependency('libturbojpeg', required: false)
png_dep       = dependency('libpng', required: false)

# systemd (sd-bus for BlueZ D-Bus GATT integration)
systemd_dep   = dependency('libsystemd', required: false)

# Build internal include list
rk_inc = []
foreach d : inc_dirs
  rk_inc += include_directories(d)
endforeach

# Declare dependency objects (Rockchip)
rkaiq_dep = declare_dependency(
  dependencies: [rkaiq_lib],
  include_directories: rk_inc
)
mpp_dep = declare_dependency(
  dependencies: [mpp_lib],
  include_directories: rk_inc
)
rga_dep = declare_dependency(
  dependencies: [rga_lib],
  include_directories: rk_inc
)
rknn_dep = declare_dependency(
  dependencies: [rknn_lib],
  include_directories: rk_inc
)

# Declare dependency objects (Cubie A7S / Allwinner A527)
opencl_dep = declare_dependency(
  dependencies: [opencl_lib],
  include_directories: rk_inc
)
openvx_dep = declare_dependency(
  dependencies: [openvx_lib, openvxu_lib, clc_lib, vsc_lib, gal_lib],
  include_directories: rk_inc
)
gal_dep = declare_dependency(
  dependencies: [gal_lib],
  include_directories: rk_inc
)

# ─── Global Include Paths ─────────────────────────────────────────────────────
inc = [include_directories(['src', 'src/hal/api', 'src/actors', 'src/actors/http-sse', 'include']), rk_inc]

# ─── Global Compile Arguments ─────────────────────────────────────────────────
if rknn_lib.found()
  add_project_arguments('-DHAVE_RKNN', language: 'cpp')
endif
if rga_lib.found()
  add_project_arguments('-DHAVE_RGA', language: 'cpp')
endif
if mpp_lib.found()
  add_project_arguments('-DHAVE_MPP', language: 'cpp')
endif
if rkaiq_lib.found()
  add_project_arguments('-DHAVE_RKAIQ', language: 'cpp')
endif

# ─── Platform Dependencies ────────────────────────────────────────────────────
# Standard system dependencies used by all platforms.
platform_deps = []
foreach dep : [yaml_cpp_dep, sqlite3_dep, jpeg_dep, turbojpeg_dep, png_dep, systemd_dep]
  if dep.found()
    platform_deps += dep
  endif
endforeach

# Rockchip-specific dependencies (linked when platform=rock3c)
if rkaiq_lib.found()
  platform_deps += rkaiq_dep
endif
if rga_lib.found()
  platform_deps += rga_dep
endif
if rknn_lib.found()
  platform_deps += rknn_dep
endif
if mpp_lib.found()
  platform_deps += mpp_dep
endif

# ─── Platform Selection ───────────────────────────────────────────────────────
platform = get_option('platform')

# ─── Subdirectories ───────────────────────────────────────────────────────────
subdir('src/actors')

if platform == 'host'
  subdir('src/hal/platforms/native')
endif

if platform == 'rock3c'
  subdir('src/hal/platforms/rock3c')
endif

if platform == 'cubie-a7s'
  subdir('src/hal/platforms/cubie-a7s')
endif

if platform == 'rdk-x5'
  subdir('src/hal/platforms/rdk-x5')
endif

# ─── Static Library ───────────────────────────────────────────────────────────
# Build all actor sources into a static library.
# Platform-specific HAL sources are added per-platform below.
all_sources = actor_sources

# Add config_loader.cpp (YAML config parser)
config_loader_sources = files('src/hal/api/config_loader.cpp')

# CivetWeb HTTP/SSE server (C library, compiled alongside C++ sources)
civetweb_sources = files('src/actors/http-sse/civetweb.c')

if platform == 'host' or platform == 'rock3c' or platform == 'cubie-a7s'
  all_sources += platform_sources
endif

all_sources += config_loader_sources
all_sources += civetweb_sources

# CivetWeb needs NO_SSL when building without OpenSSL
add_project_arguments('-DNO_SSL', language: 'c')
# Suppress unused-variable warning in civetweb (upstream issue with USE_ZLIB)
add_project_arguments('-Wno-unused-variable', language: 'c')

lib_toolkit = static_library('ai-trap-toolkit',
  all_sources,
  include_directories : inc,
  dependencies : platform_deps,
  install : true,
  install_dir : '/usr/lib')

# ─── Standalone Tools ─────────────────────────────────────────────────────────
# test_inference.cpp is optional — only build if the file exists
test_inf_exists = run_command('sh', '-c', 'test -f test_inference.cpp && echo yes || echo no', check: false).stdout().strip() == 'yes'
if rknn_lib.found() and test_inf_exists
  executable('test_inference',
             'test_inference.cpp',
             include_directories : inc,
             dependencies: [rknn_dep],
             install : true,
             install_dir : '/usr/bin')
endif

# ble_advert.cpp — standalone BLE advertisement test tool
ble_advert_src = '../tools/scripts/ble_advert.cpp'
ble_advert_exists = run_command('sh', '-c', 'test -f ' + ble_advert_src + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
if ble_advert_exists
  glib_dep = dependency('glib-2.0', required: false)
  gio_dep = dependency('gio-2.0', required: false)
  if glib_dep.found() and gio_dep.found()
    executable('ble_advert',
               ble_advert_src,
               dependencies: [glib_dep, gio_dep],
               install : true,
               install_dir : '/usr/bin')
  endif
endif
