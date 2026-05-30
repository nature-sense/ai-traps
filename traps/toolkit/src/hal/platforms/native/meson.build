# ─── src/hal/platforms/native/meson.build ─────────────────────────────────────────
# Native (host) platform stub HAL sources and dependencies.
# Included by the root meson.build when -Dplatform=host is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_native.cpp',
  'inference_hal_native.cpp',
  'classifier_hal_native.cpp',
  'video_scaler_native.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# Native platform uses system libraries only.
platform_deps = []
