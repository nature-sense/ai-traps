# ─── Dockerfile for ROCK 3C (RK3566) cross-compilation ───────────────────────
#
# Build:
#   docker build -t ai-trap-builder-rock3c -f tools/docker/rock3c.Dockerfile tools/docker
#
# Usage:
#   docker run --rm -v $(pwd):/workspace ai-trap-builder-rock3c \
#     /workspace/tools/scripts/cross-build.sh rock3c
#
# This image provides:
#   - aarch64-linux-gnu cross-compilation toolchain (gcc-13 from Ubuntu 24.04)
#   - Meson + Ninja build system
#   - SDK stub headers for RKAIQ, librga, and MPP
#   - rknn-toolkit2 for ONNX→RKNN model compilation
#   - All standard library dependencies cross-compiled for aarch64

FROM ubuntu:24.04 AS base

LABEL description="AI Camera Trap cross-compilation environment for ROCK 3C (RK3566)"
LABEL platform="rock3c"

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


# Enable multiarch for arm64 packages (V4L2 headers)
RUN dpkg --add-architecture arm64 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    libv4l-dev:arm64 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


# ── 2. Install SDK stub headers ──────────────────────────────────────────────
# These provide just enough type information for compilation.
# The actual shared libraries are loaded at runtime on the target board.

# Copy stub headers into the cross-compilation sysroot
COPY sdk-stubs/rock3c/rkaiq /usr/aarch64-linux-gnu/include/rkaiq
COPY sdk-stubs/rock3c/rga /usr/aarch64-linux-gnu/include/rga
COPY sdk-stubs/rock3c/rockchip /usr/aarch64-linux-gnu/include/rockchip

# Also install in the native include path for convenience
COPY sdk-stubs/rock3c/rkaiq /usr/include/rkaiq
COPY sdk-stubs/rock3c/rga /usr/include/rga
COPY sdk-stubs/rock3c/rockchip /usr/include/rockchip

# ── 3. Install rknn-toolkit2 for model compilation ───────────────────────────
# This runs natively (x86_64) to convert ONNX models to RKNN format.
RUN python3 -m venv /opt/rknn-venv && \
    /opt/rknn-venv/bin/pip install --upgrade pip setuptools wheel && \
    /opt/rknn-venv/bin/pip install \
        numpy \
        onnx \
        onnxruntime \
        protobuf

# Download and install rknn-toolkit2
# Note: rknn-toolkit2 is available from Rockchip's GitHub releases
RUN cd /tmp && \
    curl -fsSL -o rknn-toolkit2.tar.gz \
        "https://github.com/rockchip-linux/rknn-toolkit2/archive/refs/tags/v2.0.0-beta0.tar.gz" && \
    tar xzf rknn-toolkit2.tar.gz && \
    cd rknn-toolkit2-* && \
    /opt/rknn-venv/bin/pip install \
        packages/rknn_toolkit2-*-cp312-cp312-linux_x86_64.whl 2>/dev/null || \
    echo "Warning: rknn-toolkit2 wheel not found, install manually" && \
    rm -rf /tmp/rknn-toolkit2*

ENV RKNN_TOOLKIT2_PATH=/opt/rknn-venv/bin

# ── 4. Create build script entry point ───────────────────────────────────────
COPY cross-build.sh /usr/local/bin/cross-build
RUN chmod +x /usr/local/bin/cross-build

WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/cross-build"]
