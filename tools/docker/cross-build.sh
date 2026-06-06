#!/bin/bash
# ─── cross-build.sh ───────────────────────────────────────────────────────────
# Cross-compilation entry point for Docker-based builds.
#
# This script is designed to run INSIDE the Docker container.
# It is also the ENTRYPOINT for the Docker images.
#
# Usage (inside container):
#   cross-build.sh rock3c [component] [trap_type]
#   cross-build.sh cubie-a7s [component] [trap_type]
#   cross-build.sh rdk-x5 [component] [trap_type]
#
# Usage (from host):
#   docker run --rm -v $(pwd):/workspace ai-trap-builder-rock3c rock3c
#
# Arguments:
#   platform   - Target platform: rock3c, cubie-a7s, or rdk-x5
#   component  - What to build: model, toolkit, target, or all (default: all)
#   trap_type  - Trap type: detection or classification (default: detection)

set -euo pipefail

# ── Parse arguments ───────────────────────────────────────────────────────────
PLATFORM="${1:-}"
COMPONENT="${2:-all}"
TRAP_TYPE="${3:-detection}"

if [ -z "$PLATFORM" ]; then
    echo "Usage: $0 <platform> [component] [trap_type]"
    echo "  platform:  rock3c, cubie-a7s, rdk-x5"
    echo "  component: model, toolkit, target, all (default: all)"
    echo "  trap_type: detection, classification (default: detection)"
    exit 1
fi

# Validate platform
case "$PLATFORM" in
    rock3c|cubie-a7s|rdk-x5) ;;
    *) echo "Error: Unknown platform '$PLATFORM'. Use: rock3c, cubie-a7s, rdk-x5"; exit 1 ;;
esac

# Validate component
case "$COMPONENT" in
    model|toolkit|target|all) ;;
    *) echo "Error: Unknown component '$COMPONENT'. Use: model, toolkit, target, all"; exit 1 ;;
esac

echo "═══ Cross-compiling for $PLATFORM ═══"
echo "  Component: $COMPONENT"
echo "  Trap type: $TRAP_TYPE"
echo "  Workspace: $(pwd)"
echo ""

# ── Detect workspace root ────────────────────────────────────────────────────
# The workspace should be the ai-traps repository root
WORKSPACE="$(pwd)"
BUILD_DIR="${WORKSPACE}/build-${PLATFORM}"
CROSS_FILE="${WORKSPACE}/tools/docker/cross/aarch64-${PLATFORM}.ini"

if [ ! -f "$CROSS_FILE" ]; then
    echo "Error: Cross-compilation file not found: $CROSS_FILE"
    exit 1
fi

# ── Build model (ONNX → RKNN/NBG) ────────────────────────────────────────────
build_model() {
    echo "─── Building model for $PLATFORM ───"
    MODEL_DIR="${WORKSPACE}/models/${TRAP_TYPE}"
    if [ ! -d "$MODEL_DIR" ]; then
        echo "Warning: Model directory not found: $MODEL_DIR"
        echo "Skipping model compilation."
        return 0
    fi

    case "$PLATFORM" in
        rock3c)
            if command -v rknn_toolkit2 &>/dev/null || [ -f "${RKNN_TOOLKIT2_PATH}/rknn_toolkit2" ]; then
                echo "Compiling ONNX model to RKNN for Rockchip NPU..."
                # Find ONNX models in the model directory
                for onnx_model in "$MODEL_DIR"/*.onnx; do
                    if [ -f "$onnx_model" ]; then
                        echo "  Converting: $(basename "$onnx_model")"
                        # rknn_toolkit2 conversion command would go here
                        # python3 -m rknn.api.rknn ...
                    fi
                done
            else
                echo "Warning: rknn-toolkit2 not installed. Skipping RKNN model compilation."
                echo "Install manually or build the Docker image with rknn-toolkit2."
            fi
            ;;
        cubie-a7s)
            echo "Model compilation for Allwinner NPU requires OpenVX tools."
            echo "Skipping (run on target board)."
            ;;
        rdk-x5)
            echo "Model compilation for TI TDA4VM requires TIDL tools."
            echo "Skipping (run on target board)."
            ;;
    esac
    echo ""
}

# ── Build toolkit (libtoolkit.a) ──────────────────────────────────────────────
build_toolkit() {
    echo "─── Building toolkit for $PLATFORM ───"
    mkdir -p "$BUILD_DIR/toolkit"
    cd "$BUILD_DIR/toolkit"

    meson setup \
        --cross-file "$CROSS_FILE" \
        -Dplatform="$PLATFORM" \
        -Dbuildtype=release \
        "${WORKSPACE}/traps/toolkit" \
        "$BUILD_DIR/toolkit"

    ninja -j$(nproc)
    echo "Toolkit built: $(find "$BUILD_DIR/toolkit" -name '*.a' -o -name '*.so' 2>/dev/null | head -5)"
    echo ""
}

# ── Build target (trap binary) ────────────────────────────────────────────────
build_target() {
    echo "─── Building target for $PLATFORM ───"
    mkdir -p "$BUILD_DIR/target"
    cd "$BUILD_DIR/target"

    meson setup \
        --cross-file "$CROSS_FILE" \
        -Dplatform="$PLATFORM" \
        -Dtrap_type="$TRAP_TYPE" \
        -Dbuildtype=release \
        "${WORKSPACE}/traps/targets" \
        "$BUILD_DIR/target"

    ninja -j$(nproc)

    # Find the built binary
    BINARY=$(find "$BUILD_DIR/target" -maxdepth 2 -type f -executable -name "${PLATFORM}-*" 2>/dev/null | head -1)
    if [ -z "$BINARY" ]; then
        BINARY=$(find "$BUILD_DIR/target" -maxdepth 2 -type f -executable 2>/dev/null | head -1)
    fi

    if [ -n "$BINARY" ]; then
        echo "Target binary: $BINARY"
        file "$BINARY"
    else
        echo "Warning: No executable binary found in $BUILD_DIR/target"
    fi
    echo ""
}

# ── Main build logic ──────────────────────────────────────────────────────────
case "$COMPONENT" in
    model)
        build_model
        ;;
    toolkit)
        build_toolkit
        ;;
    target)
        build_target
        ;;
    all)
        build_model
        build_toolkit
        build_target
        ;;
esac

echo "═══ Build complete for $PLATFORM ═══"
echo "Output directory: $BUILD_DIR"
echo ""
echo "To deploy to a board:"
echo "  scp $BUILD_DIR/target/${PLATFORM}-* root@<board-ip>:/usr/local/bin/"
echo "  scp ${WORKSPACE}/traps/targets/config/${PLATFORM}.yaml root@<board-ip>:/etc/ai-trap/config.yaml"
