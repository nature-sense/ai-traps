# ─── Dockerfile for Cubie A7S (Allwinner A527) cross-compilation ──────────────
#
# Build:
#   docker build -t ai-trap-builder-cubie-a7s -f tools/docker/cubie-a7s.Dockerfile tools/docker
#
# Usage:
#   docker run --rm -v $(pwd):/workspace ai-trap-builder-cubie-a7s \
#     /workspace/tools/scripts/cross-build.sh cubie-a7s
#
# This image provides:
#   - aarch64-linux-gnu cross-compilation toolchain (gcc-13 from Ubuntu 24.04)
#   - Meson + Ninja build system
#   - OpenVX stub headers (Khronos standard + Vivante extensions)
#   - OpenCL stub headers (Khronos standard)
#   - All standard library dependencies cross-compiled for aarch64

FROM ubuntu:24.04 AS base

LABEL description="AI Camera Trap cross-compilation environment for Cubie A7S (Allwinner A527)"
LABEL platform="cubie-a7s"

# Prevent interactive prompts during apt
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# ── 1. Install cross-compilation toolchain and build tools ────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    meson \
    ninja-build \
    pkg-config \
    binutils \
    make \
    cmake \
    python3 \
    python3-pip \
    python3-venv \
    git \
    curl \
    ca-certificates \
    libc6-dev-arm64-cross \
    linux-libc-dev-arm64-cross \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


# Enable multiarch for arm64 packages (V4L2 headers, OpenCL)
RUN dpkg --add-architecture arm64 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    libv4l-dev:arm64 \
    ocl-icd-opencl-dev:arm64 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


# ── 2. Install OpenVX stub headers ───────────────────────────────────────────
# These provide just enough type information for compilation.
# The actual OpenVX library is loaded at runtime on the target board.

COPY sdk-stubs/cubie-a7s/VX /usr/aarch64-linux-gnu/include/VX
COPY sdk-stubs/cubie-a7s/VX /usr/include/VX

# ── 3. Install OpenCL headers (from Khronos) ─────────────────────────────────
# OpenCL is an open standard. The headers are available from Khronos.
RUN git clone --depth 1 --branch v2024.05.08 \
    https://github.com/KhronosGroup/OpenCL-Headers.git /tmp/opencl-headers && \
    cp -r /tmp/opencl-headers/CL /usr/aarch64-linux-gnu/include/ && \
    cp -r /tmp/opencl-headers/CL /usr/include/ && \
    rm -rf /tmp/opencl-headers

# ── 4. Create build script entry point ───────────────────────────────────────
COPY cross-build.sh /usr/local/bin/cross-build
RUN chmod +x /usr/local/bin/cross-build

WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/cross-build"]
