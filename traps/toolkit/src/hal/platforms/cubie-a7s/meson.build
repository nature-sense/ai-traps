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
