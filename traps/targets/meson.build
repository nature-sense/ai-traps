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
    'cpp_std=c++20',
    'buildtype=debugoptimized',
  ]
)

cpp = meson.get_compiler('cpp')

# ─── Library search paths ───────────────────────────────────────────────────────
lib_dirs = ['/usr/lib', '/lib', '/usr/local/lib']

# ─── Dependencies ───────────────────────────────────────────────────────────────
sqlite3_dep   = dependency('sqlite3', required: false)
jpeg_dep      = dependency('libjpeg', required: false)
turbojpeg_dep = dependency('libturbojpeg', required: false)
png_dep       = dependency('libpng', required: false)

platform_deps = []
foreach dep : [sqlite3_dep, jpeg_dep, turbojpeg_dep, png_dep]
  if dep.found()
    platform_deps += dep
  endif
endforeach

# Rockchip hardware libraries (needed by toolkit static lib)
mpp_lib   = cpp.find_library('rockchip_mpp', dirs: lib_dirs, required: false)
rga_lib   = cpp.find_library('rga', dirs: lib_dirs, required: false)
rknn_lib  = cpp.find_library('rknnrt', dirs: lib_dirs, required: false)
rkaiq_lib = cpp.find_library('rkaiq', dirs: lib_dirs, required: false)

foreach hw_lib : [mpp_lib, rga_lib, rknn_lib, rkaiq_lib]
  if hw_lib.found()
    platform_deps += hw_lib
  endif
endforeach

# ─── Toolkit Library ────────────────────────────────────────────────────────────
# Path to the pre-built toolkit library and its headers.
# Meson requires absolute paths for find_library search dirs.
toolkit_build_dir = get_option('toolkit_build_dir')
if not toolkit_build_dir.startswith('/')
  toolkit_build_dir = meson.project_source_root() / toolkit_build_dir
endif
toolkit_lib = cpp.find_library('ai-trap-toolkit',
  dirs: [toolkit_build_dir],
  required: true
)

# Include paths for toolkit headers
toolkit_inc_dirs = [
  '../toolkit/src',
  '../toolkit/src/hal/api',
  '../toolkit/src/actors',
  '../toolkit/src/actors/http-sse',
  '../toolkit/include',
]

# macOS Homebrew paths (only add if they exist)
foreach d : ['/opt/homebrew/include', '/opt/homebrew/Cellar/nlohmann-json/3.12.0/include']
  if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    toolkit_inc_dirs += [d]
  endif
endforeach

# Only add Rockchip-specific include dirs if they exist (macOS doesn't have them)
foreach d : ['/usr/include/rockchip', '/usr/include/rkaiq/common', '/usr/include/rkaiq/algos',
             '/usr/include/rkaiq/xcore', '/usr/include/rkaiq/iq_parser',
             '/usr/include/rkaiq/iq_parser_v2', '/usr/include/rkaiq/uAPI2', '/usr/include/rkaiq']
  if run_command('sh', '-c', 'test -d ' + d + ' && echo yes || echo no', check: false).stdout().strip() == 'yes'
    toolkit_inc_dirs += [d]
  endif
endforeach

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
) + pipeline_sources

detection_inc = [toolkit_inc, pipeline_inc,
  include_directories('src/detection'),
  include_directories('src/detection/actors/session'),
  include_directories('src/detection/actors/http-handler')]

if platform == 'host'
  detection_sources += files(
    'src/detection/platforms/native/main.cpp',
    'src/detection/platforms/native/native_detection_pipeline.cpp',
  )
endif

if platform == 'rock3c'
  detection_sources += files(
    'src/detection/platforms/rock3c/main.cpp',
    'src/detection/platforms/rock3c/rock3c_detection_pipeline.cpp',
  )
endif

if platform == 'cubie-a7s'
  detection_sources += files(
    'src/detection/platforms/cubie-a7s/main.cpp',
    'src/detection/platforms/cubie-a7s/cubie_a7s_detection_pipeline.cpp',
  )
endif

executable('ai-trap-detection',
  detection_sources,
  include_directories : detection_inc,
  dependencies : [toolkit_lib] + platform_deps,
  install : true,
  install_dir : '/usr/bin')
