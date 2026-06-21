#!/usr/bin/env bash
#
# build.sh — Native cross-compilation entry point for AI Camera Trap
#
# This script provides a convenient wrapper around Meson + Clang for
# native cross-compilation on macOS. It handles:
#   - Checking prerequisites (Clang, Meson, Ninja)
#   - Ensuring the sysroot exists (or creating it)
#   - Running meson setup and ninja build
#
# Usage:
#   # Build toolkit only
#   ./tools/native/build.sh rock3c toolkit
#
#   # Build target binary (requires toolkit build first)
#   ./tools/native/build.sh rock3c target
#
#   # Build both
#   ./tools/native/build.sh rock3c all
#
#   # Build with debug symbols
#   ./tools/native/build.sh rock3c all --buildtype=debug
#
#   # Reconfigure (clean build)
#   ./tools/native/build.sh rock3c all --reconfigure
#
#   # Setup sysroot only
#   ./tools/native/build.sh rock3c sysroot root@192.168.1.100
#
#   # Build and populate runtime directory
#   ./tools/native/build.sh rock3c all --populate-runtime

set -euo pipefail

# ─── Configuration ─────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
NATIVE_DIR="${PROJECT_ROOT}/tools/native"
SYSROOTS_DIR="${NATIVE_DIR}/sysroots"
CROSS_DIR="${NATIVE_DIR}/cross"

# ─── Color output ─────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()    { echo -e "${BLUE}ℹ${NC} $1"; }
ok()      { echo -e "${GREEN}✓${NC} $1"; }
warn()    { echo -e "${YELLOW}⚠${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1"; }
section() { echo ""; echo "━━━ $1 ━━━"; echo ""; }

# ─── Help ──────────────────────────────────────────────────────────────────────
usage() {
    cat << EOF
Usage: $0 <platform> <target> [options]

Arguments:
  platform    Target platform: rock3c, cubie-a7s, rdk-x5
  target      Build target: toolkit, target, all, sysroot

Options:
  --buildtype=<type>   Build type: release (default), debug, debugoptimized
  --reconfigure        Force meson reconfigure (clean build)
  --help               Show this help message

Examples:
  $0 rock3c all                    # Build toolkit + target for rock3c
  $0 rock3c toolkit                # Build toolkit only
  $0 rock3c target                 # Build target binary only
  $0 rock3c all --buildtype=debug  # Debug build
  $0 rock3c sysroot root@10.0.0.1 # Setup sysroot from board
EOF
    exit 0
}

# ─── Parse arguments ───────────────────────────────────────────────────────────
PLATFORM="${1:-}"
TARGET="${2:-}"
shift 2 2>/dev/null || true

BUILDTYPE="release"
RECONFIGURE=false
POPULATE_RUNTIME=false

for arg in "$@"; do
    case "$arg" in
        --buildtype=*) BUILDTYPE="${arg#*=}" ;;
        --reconfigure) RECONFIGURE=true ;;
        --populate-runtime) POPULATE_RUNTIME=true ;;
        --help) usage ;;
        *) warn "Unknown option: $arg" ;;
    esac
done

if [ -z "$PLATFORM" ] || [ -z "$TARGET" ]; then
    usage
fi

case "$PLATFORM" in
    rock3c|cubie-a7s|rdk-x5) ;;
    *) error "Unknown platform '$PLATFORM'. Use: rock3c, cubie-a7s, rdk-x5"; usage ;;
esac

case "$TARGET" in
    toolkit|target|all|sysroot) ;;
    *) error "Unknown target '$TARGET'. Use: toolkit, target, all, sysroot"; usage ;;
esac

SYSROOT_DIR="${SYSROOTS_DIR}/${PLATFORM}"
CROSS_FILE="${CROSS_DIR}/aarch64-${PLATFORM}.ini"
BUILD_DIR="${PROJECT_ROOT}/build-${PLATFORM}"

# ═══════════════════════════════════════════════════════════════════════════════
# Prerequisites check
# ═══════════════════════════════════════════════════════════════════════════════
check_prereqs() {
    section "Prerequisites"

    local missing=0

    if ! command -v clang &>/dev/null; then
        error "clang not found. Install with: brew install llvm"
        missing=1
    else
        ok "clang: $(clang --version | head -1)"
    fi

    if ! command -v meson &>/dev/null; then
        error "meson not found. Install with: brew install meson"
        missing=1
    else
        ok "meson: $(meson --version)"
    fi

    if ! command -v ninja &>/dev/null; then
        error "ninja not found. Install with: brew install ninja"
        missing=1
    else
        ok "ninja: $(ninja --version)"
    fi

    if ! command -v pkg-config &>/dev/null; then
        error "pkg-config not found. Install with: brew install pkg-config"
        missing=1
    else
        ok "pkg-config: $(pkg-config --version)"
    fi

    if [ $missing -ne 0 ]; then
        echo ""
        error "Missing prerequisites. Install them:"
        echo "  brew install llvm meson ninja pkg-config"
        echo "  export PATH=\"/opt/homebrew/opt/llvm/bin:\$PATH\""
        exit 1
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Sysroot check
# ═══════════════════════════════════════════════════════════════════════════════
check_sysroot() {
    section "Sysroot Check"

    if [ ! -d "${SYSROOT_DIR}/usr/include" ]; then
        warn "Sysroot not found at ${SYSROOT_DIR}"
        warn ""
        warn "Create it by running:"
        warn "  ${SCRIPT_DIR}/setup-sysroot.sh ${PLATFORM} root@<board-ip>"
        warn ""
        warn "Or if you have an SDK tarball:"
        warn "  ${SCRIPT_DIR}/setup-sysroot.sh ${PLATFORM} --from-tarball tools/docker/sdk/${PLATFORM}-sdk-aarch64.tar.gz"
        echo ""
        read -rp "Enter board SSH address (user@host) to create sysroot now, or press Enter to abort: " ssh_addr
        if [ -n "$ssh_addr" ]; then
            "${SCRIPT_DIR}/setup-sysroot.sh" "$PLATFORM" "$ssh_addr"
        else
            error "Sysroot required. Aborting."
            exit 1
        fi
    else
        local inc_count
        inc_count=$(find "${SYSROOT_DIR}/usr/include" -type f 2>/dev/null | wc -l | tr -d ' ')
        local lib_count
        lib_count=$(find "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu" -type f 2>/dev/null | wc -l | tr -d ' ')
        ok "Sysroot found: ${inc_count} headers, ${lib_count} libraries"
    fi

    if [ ! -f "${CROSS_FILE}" ]; then
        error "Cross file not found: ${CROSS_FILE}"
        exit 1
    fi
    ok "Cross file: ${CROSS_FILE}"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Build functions
# ═══════════════════════════════════════════════════════════════════════════════

build_toolkit() {
    local build_dir="${BUILD_DIR}/toolkit"
    local reconfigure_flag=""

    section "Building Toolkit (${PLATFORM})"

    if [ "$RECONFIGURE" = true ] && [ -d "$build_dir" ]; then
        info "Reconfiguring (removing existing build directory)..."
        rm -rf "$build_dir"
    fi

    if [ ! -d "$build_dir" ]; then
        info "Running meson setup..."
        meson setup "$build_dir" \
            --cross-file "$CROSS_FILE" \
            -Dplatform="$PLATFORM" \
            -Dbuildtype="$BUILDTYPE" \
            "${PROJECT_ROOT}/traps/toolkit"
        ok "Meson setup complete"
    else
        info "Build directory exists, running meson --reconfigure..."
        meson setup "$build_dir" --reconfigure \
            --cross-file "$CROSS_FILE" \
            -Dplatform="$PLATFORM" \
            -Dbuildtype="$BUILDTYPE" \
            "${PROJECT_ROOT}/traps/toolkit"
    fi

    info "Building with ninja..."
    ninja -C "$build_dir"
    ok "Toolkit build complete"

    # Show output
    echo ""
    info "Toolkit build output:"
    find "$build_dir" -maxdepth 2 -type f -name "*.a" -o -name "*.so" 2>/dev/null | while IFS= read -r f; do
        echo "  $(ls -lh "$f" | awk '{print $5, $NF}')"
    done
}

build_target() {
    local build_dir="${BUILD_DIR}/target"
    local toolkit_build_dir="${BUILD_DIR}/toolkit"
    local reconfigure_flag=""

    section "Building Target Binary (${PLATFORM})"

    if [ ! -d "$toolkit_build_dir" ]; then
        error "Toolkit build directory not found: ${toolkit_build_dir}"
        error "Build the toolkit first: $0 ${PLATFORM} toolkit"
        exit 1
    fi

    if [ "$RECONFIGURE" = true ] && [ -d "$build_dir" ]; then
        info "Reconfiguring (removing existing build directory)..."
        rm -rf "$build_dir"
    fi

    if [ ! -d "$build_dir" ]; then
        info "Running meson setup..."
        meson setup "$build_dir" \
            --cross-file "$CROSS_FILE" \
            -Dplatform="$PLATFORM" \
            -Dtoolkit_build_dir="$toolkit_build_dir" \
            -Dbuildtype="$BUILDTYPE" \
            "${PROJECT_ROOT}/traps/targets"
        ok "Meson setup complete"
    else
        info "Build directory exists, running meson --reconfigure..."
        meson setup "$build_dir" --reconfigure \
            --cross-file "$CROSS_FILE" \
            -Dplatform="$PLATFORM" \
            -Dtoolkit_build_dir="$toolkit_build_dir" \
            -Dbuildtype="$BUILDTYPE" \
            "${PROJECT_ROOT}/traps/targets"
    fi

    info "Building with ninja..."
    ninja -C "$build_dir"
    ok "Target build complete"

    # Show output binary
    local binary="${build_dir}/ai-trap-detection"
    if [ -f "$binary" ]; then
        echo ""
        info "Binary:"
        file "$binary"
        echo ""
        ls -lh "$binary"
        echo ""
        info "Architecture check:"
        llvm-readelf -h "$binary" 2>/dev/null | grep -E "Machine|Class" || \
            aarch64-linux-gnu-readelf -h "$binary" 2>/dev/null | grep -E "Machine|Class" || \
            warn "Could not read ELF header (readelf not available)"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Populate runtime directory
# ═══════════════════════════════════════════════════════════════════════════════

populate_runtime() {
    # Map platform name to runtime directory name
    # rock3c → rock-3c, cubie-a7s → cubie-a7s, rdk-x5 → rdk-x5
    local runtime_platform="${PLATFORM}"
    case "$runtime_platform" in
        rock3c) runtime_platform="rock-3c" ;;
    esac

    local runtime_dir="${PROJECT_ROOT}/traps/runtimes/${runtime_platform}"
    local binary_src="${BUILD_DIR}/target/ai-trap-detection"
    local model_rknn_src="${PROJECT_ROOT}/models/detection/insects/yolo26n/yolo26n.rknn"
    local model_onnx_src="${PROJECT_ROOT}/models/detection/insects/yolo26n/best.onnx"
    local convert_script_src="${runtime_dir}/convert_rknn_onboard.py"

    section "Populating Runtime Directory (${runtime_platform})"

    if [ ! -d "$runtime_dir" ]; then
        warn "Runtime directory not found: ${runtime_dir}"
        warn "Create it first with the appropriate config files."
        warn "See: traps/runtimes/README.md"
        return
    fi

    # Copy binary
    if [ -f "$binary_src" ]; then
        cp "$binary_src" "${runtime_dir}/ai-trap-detection"
        ok "Binary copied: $(ls -lh "${runtime_dir}/ai-trap-detection" | awk '{print $5}')"
    else
        warn "Binary not found at ${binary_src}. Build the target first."
    fi

    # Copy pre-converted RKNN model (if available)
    if [ -f "$model_rknn_src" ]; then
        cp "$model_rknn_src" "${runtime_dir}/yolo26n.rknn"
        ok "RKNN model copied: $(ls -lh "${runtime_dir}/yolo26n.rknn" | awk '{print $5}')"
    else
        warn "Pre-converted RKNN model not found at ${model_rknn_src}."
        warn "The ONNX model will be converted on the board instead."
    fi

    # Copy ONNX model for on-board conversion
    if [ -f "$model_onnx_src" ]; then
        cp "$model_onnx_src" "${runtime_dir}/yolo26n.onnx"
        ok "ONNX model copied: $(ls -lh "${runtime_dir}/yolo26n.onnx" | awk '{print $5}')"
    else
        warn "ONNX model not found at ${model_onnx_src}."
    fi

    # Copy conversion script (always present, tracked in git)
    if [ -f "$convert_script_src" ]; then
        ok "Conversion script present: convert_rknn_onboard.py"
    else
        warn "Conversion script not found at ${convert_script_src}."
    fi

    echo ""
    info "Runtime directory contents:"
    ls -lh "$runtime_dir"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo "  AI Camera Trap — Native Build"
echo "=============================================="
echo ""
echo "  Platform:   ${PLATFORM}"
echo "  Target:     ${TARGET}"
echo "  Build type: ${BUILDTYPE}"
echo ""

case "$TARGET" in
    sysroot)
        # Shift remaining args to pass to setup-sysroot.sh
        shift 2 2>/dev/null || true
        exec "${SCRIPT_DIR}/setup-sysroot.sh" "$PLATFORM" "$@"
        ;;
    toolkit)
        check_prereqs
        check_sysroot
        build_toolkit
        ;;
    target)
        check_prereqs
        check_sysroot
        build_target
        ;;
    all)
        check_prereqs
        check_sysroot
        build_toolkit
        build_target
        if [ "$POPULATE_RUNTIME" = true ]; then
            populate_runtime
        fi
        ;;
esac

section "Done"
ok "Build completed successfully"
echo ""
