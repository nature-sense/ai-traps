# ─── src/hal/platforms/rdk-x5/meson.build ───────────────────────────────────────
# D-Robotics RDK X5 (X5 SoC) platform sources and dependencies.
# Included by the root meson.build when -Dplatform=rdk-x5 is set.

# ── Platform source files ─────────────────────────────────────────────────────
platform_sources = files(
  'camera_hal_rdkx5.cpp',
  'inference_hal_bpu.cpp',
  'video_scaler_vse.cpp',
)

# ── Platform include directories ──────────────────────────────────────────────
platform_inc = include_directories('.')

# ── Platform dependencies ─────────────────────────────────────────────────────
# These are discovered by the root meson.build and passed down.
# RDK X5 dependencies:
#   - bpu_infer_lib: BPU inference library (C++ API)
#   - V4L2: Standard V4L2 headers (kernel headers)
#   - dma-heap: DMA buffer allocation (kernel headers)
#
# Installation:
#   pip install bpu_infer_lib_x5 -i http://sdk.d-robotics.cc:8080/simple/ \
#     --trusted-host sdk.d-robotics.cc
#
# TODO: Add bpu_infer_lib dependency once the C++ API is confirmed.
platform_deps = []
