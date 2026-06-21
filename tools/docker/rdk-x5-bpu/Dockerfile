# ─── RDK X5 BPU Model Converter ──────────────────────────────────────────
# Converts ONNX models to D-Robotics BPU .bin format for the Horizon X5 NPU.
# Build and run on macOS (x86_64 target runs natively via Rosetta 2).
#
# Build:
#   docker build --platform linux/amd64 -t rdk-x5-bpu tools/docker/rdk-x5-bpu/
#
# Run (mount ONNX in /models, output goes to /models):
#   docker run --platform linux/amd64 --rm -it \
#     -v $(pwd)/models/detection/insects/yolo26n:/models \
#     rdk-x5-bpu bash
#   root@container# hb_mapper makertbin --model-type onnx --config /models/yolo26n_bpu.yaml
#
# The D-Robotics hbdk4_compiler and hb_mapper tools are available on x86_64 Linux,
# making this run natively on Apple Silicon via Rosetta 2 without QEMU emulation.

FROM ubuntu:22.04

LABEL description="RDK X5 BPU Model Converter (hbdk4)"
LABEL maintainer="Nature Sense"

ENV DEBIAN_FRONTEND=noninteractive

# ── System dependencies ──
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    python3-venv \
    wget \
    && rm -rf /var/lib/apt/lists/*

# ── Install D-Robotics BPU compiler toolchain ──
# hbdk4_compiler and hb_mapper are available via D-Robotics' SDK PyPI server
RUN pip3 install --no-cache-dir \
    onnx \
    onnxruntime \
    hbdk4_compiler \
    hb_mapper_makertbin \
    -i http://sdk.d-robotics.cc:8080/simple/ \
    --trusted-host sdk.d-robotics.cc \
    2>&1 | tail -5

# Verify installation
RUN which hb_mapper && hb_mapper --help 2>&1 | head -5

WORKDIR /models

# Default command
ENTRYPOINT ["hb_mapper"]
CMD ["--help"]