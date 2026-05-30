# ─── src/hal/platforms/rock3c/meson.build ──────────────────────────────────────────────
# Rockchip RK3566/RK3568 platform sources and dependencies.
# Included by the root meson.build when -Dplatform=rock3c is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_imx219.cpp',
  'camera_hal_imx415.cpp',
  'inference_hal_rknn.cpp',
  'video_scaler_hal_rga.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# These are discovered by the root meson.build and passed down.
# platform_deps is populated by the root meson.build based on SDK detection.
platform_deps = []
