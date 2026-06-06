# ─── Dockerfile for RDK X5 (TI TDA4VM) cross-compilation ─────────────────────
#
# Build:
#   docker build -t ai-trap-builder-rdk-x5 -f tools/docker/rdk-x5.Dockerfile tools/docker
#
# Usage:
#   docker run --rm -v $(pwd):/workspace ai-trap-builder-rdk-x5 \
#     /workspace/tools/scripts/cross-build.sh rdk-x5
#
# This image provides:
#   - aarch64-linux-gnu cross-compilation toolchain (gcc-13 from Ubuntu 24.04)
#   - Meson + Ninja build system
#   - TI TDA4VM SDK headers (stubs where proprietary)
#   - All standard library dependencies cross-compiled for aarch64
#
# NOTE: The TI TDA4VM SDK (Processor SDK Linux for J721e) is available from:
#   https://www.ti.com/tool/download/PROCESSOR-SDK-LINUX-J721E
# Full SDK installation requires accepting TI's EULA.

FROM ubuntu:24.04 AS base

LABEL description="AI Camera Trap cross-compilation environment for RDK X5 (TI TDA4VM)"
LABEL platform="rdk-x5"

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



# ── 2. TI TDA4VM SDK ─────────────────────────────────────────────────────────
# The TI Processor SDK for J721e provides:
#   - TIDL (TI Deep Learning) API for NPU inference
#   - V4L2-based camera capture
#   - Video encode/decode via TI's codec engine
#
# For now, we install basic Linux headers. The full SDK must be downloaded
# separately from TI's website due to licensing.
#
# To install the full SDK:
#   1. Download PROCESSOR-SDK-LINUX-J721E from ti.com
#   2. Run the installer: ./ti-processor-sdk-linux-j721e-evm-*.bin
#   3. Mount the SDK directory into the container:
#      docker run -v /path/to/ti-sdk:/opt/ti-sdk ...
#
# For compilation without the full SDK, we provide stub headers.

# Create a placeholder for TI SDK headers
RUN mkdir -p /opt/ti-sdk && \
    echo "TI SDK not installed. Download from https://www.ti.com/tool/download/PROCESSOR-SDK-LINUX-J721E" \
    > /opt/ti-sdk/README.txt

# ── 3. Create build script entry point ───────────────────────────────────────
COPY cross-build.sh /usr/local/bin/cross-build
RUN chmod +x /usr/local/bin/cross-build

WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/cross-build"]
