#!/usr/bin/env bash
#
# extract-sdk.sh — Extract proprietary SDK artifacts from a board for Docker
#                  cross-compilation.
#
# This script runs ON THE BOARD ITSELF. It collects the proprietary headers
# and shared libraries that are only available on the board (not in standard
# Debian repos) and packages them into a tarball that can be copied to the
# development machine and used in the Docker cross-compilation environment.
#
# Usage:
#   sudo ./tools/scripts/extract-sdk.sh rock3c
#   sudo ./tools/scripts/extract-sdk.sh cubie-a7s
#
# After extraction, copy the tarball off the board:
#   scp root@<board-ip>:/var/lib/ai-trap/sdk/rock3c-sdk-aarch64.tar.gz tools/docker/sdk/
#
# Then rebuild the Docker image with the real SDK:
#   docker build -t ai-trap-builder-rock3c -f tools/docker/rock3c.Dockerfile tools/docker
#

set -euo pipefail

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

# ─── Must be run as root ──────────────────────────────────────────────────────
if [ "$(id -u)" -ne 0 ]; then
    error "This script must be run as root (sudo)."
    exit 1
fi

# ─── Parse platform argument ──────────────────────────────────────────────────
PLATFORM="${1:-}"
if [ -z "$PLATFORM" ]; then
    echo "Usage: $0 <platform>"
    echo "  platform: rock3c, cubie-a7s"
    exit 1
fi

case "$PLATFORM" in
    rock3c|cubie-a7s) ;;
    *) error "Unknown platform '$PLATFORM'. Use: rock3c, cubie-a7s"; exit 1 ;;
esac

# ─── Output directory ─────────────────────────────────────────────────────────
SDK_DIR="/var/lib/ai-trap/sdk"
OUTPUT_DIR="${SDK_DIR}/${PLATFORM}"
TARBALL="${SDK_DIR}/${PLATFORM}-sdk-aarch64.tar.gz"

mkdir -p "$OUTPUT_DIR/include" "$OUTPUT_DIR/lib"

echo ""
echo "=============================================="
echo "  SDK Extraction — ${PLATFORM}"
echo "=============================================="
echo ""
echo "  Output: ${OUTPUT_DIR}"
echo "  Tarball: ${TARBALL}"
echo ""

# ═══════════════════════════════════════════════════════════════════════════════
# ROCK 3C (RK3566) — Rockchip SDK
# ═══════════════════════════════════════════════════════════════════════════════
extract_rock3c() {
    section "Rockchip SDK — Headers"

    local header_dirs=(
        "/usr/include/rkaiq"
        "/usr/include/rga"
        "/usr/include/rockchip"
    )

    for dir in "${header_dirs[@]}"; do
        if [ -d "$dir" ]; then
            local rel_path="${dir#/usr/include/}"
            mkdir -p "$OUTPUT_DIR/include/$(dirname "$rel_path")"
            cp -r "$dir" "$OUTPUT_DIR/include/$(dirname "$rel_path")/"
            ok "Headers copied: ${dir}"
        else
            warn "Header directory not found: ${dir}"
        fi
    done

    # RKNN headers are flat files in /usr/include/ (not a subdirectory)
    local rknn_headers=(
        "rknn_api.h"
        "rknn_custom_op.h"
        "rknn_matmul_api.h"
    )
    for hdr in "${rknn_headers[@]}"; do
        if [ -f "/usr/include/$hdr" ]; then
            cp "/usr/include/$hdr" "$OUTPUT_DIR/include/"
            ok "Header copied: /usr/include/${hdr}"
        else
            warn "Header not found: /usr/include/${hdr}"
        fi
    done

    section "Rockchip SDK — Shared Libraries"

    local lib_patterns=(
        "librkaiq.so*"
        "librga.so*"
        "librknnrt.so*"
        "librockchip_mpp.so*"
    )

    local lib_found=false
    for pattern in "${lib_patterns[@]}"; do
        # Search common library paths
        local found_libs
        found_libs=$(find /usr/lib /usr/lib/aarch64-linux-gnu /lib /lib/aarch64-linux-gnu \
            -name "$pattern" -type f 2>/dev/null || true)

        if [ -n "$found_libs" ]; then
            while IFS= read -r lib; do
                cp -L "$lib" "$OUTPUT_DIR/lib/"
                ok "Library copied: $(basename "$lib")"
                lib_found=true
            done <<< "$found_libs"

            # Also copy symlinks (for .so without version suffix)
            local found_links
            found_links=$(find /usr/lib /usr/lib/aarch64-linux-gnu /lib /lib/aarch64-linux-gnu \
                -name "$pattern" -type l 2>/dev/null || true)
            if [ -n "$found_links" ]; then
                while IFS= read -r link; do
                    cp -a "$link" "$OUTPUT_DIR/lib/"
                done <<< "$found_links"
            fi
        else
            warn "Library not found: ${pattern}"
        fi
    done

    if [ "$lib_found" = false ]; then
        warn "No Rockchip SDK libraries were found on this board."
        warn "The SDK may not be installed. Install it with:"
        warn "  sudo apt-get install librkaiq-dev librga-dev librknnrt-dev librkmpi-dev"
    fi

    section "Rockchip SDK — pkg-config files"

    # Copy any .pc files for the Rockchip libraries
    local pc_files
    pc_files=$(find /usr/lib/aarch64-linux-gnu/pkgconfig /usr/lib/pkgconfig /usr/share/pkgconfig \
        -name "rkaiq*.pc" -o -name "rga*.pc" -o -name "rknn*.pc" -o -name "rockchip_mpp*.pc" \
        2>/dev/null || true)

    if [ -n "$pc_files" ]; then
        mkdir -p "$OUTPUT_DIR/pkgconfig"
        while IFS= read -r pc; do
            cp "$pc" "$OUTPUT_DIR/pkgconfig/"
            ok "pkg-config copied: $(basename "$pc")"
        done <<< "$pc_files"
    else
        warn "No Rockchip pkg-config files found"
    fi

    # ── Verify RKNN runtime version ──────────────────────────────────────────
    local rknn_lib="$OUTPUT_DIR/lib/librknnrt.so.2.3.2"
    if [ -f "$rknn_lib" ]; then
        local actual_size
        actual_size=$(stat -c%s "$rknn_lib" 2>/dev/null || stat -f%z "$rknn_lib" 2>/dev/null)
        ok "RKNN Runtime v2.3.2 extracted ($(echo ${actual_size} | awk '{printf \"%.1f\", $1/1024/1024}') MB)"
    else
        warn "RKNN Runtime v2.3.2 NOT extracted — found:"
        for f in "$OUTPUT_DIR"/lib/librknnrt.so*; do
            [ -f "$f" ] && warn "  $(basename "$f")"
        done
        warn "Run the board setup script first to install v2.3.2, then re-run this script."
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Cubie A7S (Allwinner A527) — Allwinner SDK
# ═══════════════════════════════════════════════════════════════════════════════
extract_cubie_a7s() {
    section "Allwinner SDK — Headers"

    local header_dirs=(
        "/usr/include/VX"
    )

    for dir in "${header_dirs[@]}"; do
        if [ -d "$dir" ]; then
            local rel_path="${dir#/usr/include/}"
            mkdir -p "$OUTPUT_DIR/include/$(dirname "$rel_path")"
            cp -r "$dir" "$OUTPUT_DIR/include/$(dirname "$rel_path")/"
            ok "Headers copied: ${dir}"
        else
            warn "Header directory not found: ${dir}"
        fi
    done

    # OpenCL headers — these are standard Khronos, but include them for completeness
    if [ -d "/usr/include/CL" ]; then
        cp -r /usr/include/CL "$OUTPUT_DIR/include/"
        ok "Headers copied: /usr/include/CL (OpenCL)"
    else
        warn "OpenCL headers not found at /usr/include/CL"
    fi

    section "Allwinner SDK — Shared Libraries"

    local lib_patterns=(
        "libOpenCL.so*"
        "libOpenVX.so*"
        "libOpenVXU.so*"
        "libCLC.so*"
        "libVSC.so*"
        "libGAL.so*"
    )

    local lib_found=false
    for pattern in "${lib_patterns[@]}"; do
        local found_libs
        found_libs=$(find /usr/lib /usr/lib/aarch64-linux-gnu /lib /lib/aarch64-linux-gnu \
            -name "$pattern" -type f 2>/dev/null || true)

        if [ -n "$found_libs" ]; then
            while IFS= read -r lib; do
                cp -L "$lib" "$OUTPUT_DIR/lib/"
                ok "Library copied: $(basename "$lib")"
                lib_found=true
            done <<< "$found_libs"

            local found_links
            found_links=$(find /usr/lib /usr/lib/aarch64-linux-gnu /lib /lib/aarch64-linux-gnu \
                -name "$pattern" -type l 2>/dev/null || true)
            if [ -n "$found_links" ]; then
                while IFS= read -r link; do
                    cp -a "$link" "$OUTPUT_DIR/lib/"
                done <<< "$found_links"
            fi
        else
            warn "Library not found: ${pattern}"
        fi
    done

    if [ "$lib_found" = false ]; then
        warn "No Allwinner SDK libraries were found on this board."
        warn "The SDK may not be installed or may be in a non-standard location."
    fi

    section "Allwinner SDK — pkg-config files"

    local pc_files
    pc_files=$(find /usr/lib/aarch64-linux-gnu/pkgconfig /usr/lib/pkgconfig /usr/share/pkgconfig \
        -name "OpenCL*.pc" -o -name "OpenVX*.pc" -o -name "CLC*.pc" -o -name "GAL*.pc" \
        2>/dev/null || true)

    if [ -n "$pc_files" ]; then
        mkdir -p "$OUTPUT_DIR/pkgconfig"
        while IFS= read -r pc; do
            cp "$pc" "$OUTPUT_DIR/pkgconfig/"
            ok "pkg-config copied: $(basename "$pc")"
        done <<< "$pc_files"
    else
        warn "No Allwinner pkg-config files found"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

case "$PLATFORM" in
    rock3c)     extract_rock3c ;;
    cubie-a7s)  extract_cubie_a7s ;;
esac

# ─── Create tarball ───────────────────────────────────────────────────────────
section "Packaging"

cd "$SDK_DIR"
tar czf "$TARBALL" -C "$SDK_DIR" "$PLATFORM/"
ok "Tarball created: ${TARBALL}"

# Show summary
echo ""
echo "━━━ SDK Extraction Complete ━━━"
echo ""
echo "  Platform: ${PLATFORM}"
echo "  Tarball:  ${TARBALL}"
echo "  Size:     $(du -h "$TARBALL" | cut -f1)"
echo "  Contents:"
echo "    Headers: $(find "${OUTPUT_DIR}/include" -type f 2>/dev/null | wc -l) files"
echo "    Libraries: $(find "${OUTPUT_DIR}/lib" -type f 2>/dev/null | wc -l) files"
echo ""

# Print instructions
echo "  ── Next steps ──"
echo "  1. Copy the tarball to your development machine:"
echo "       scp root@<board-ip>:${TARBALL} tools/docker/sdk/"
echo ""
echo "  2. Rebuild the Docker image with the real SDK:"
echo "       docker build -t ai-trap-builder-${PLATFORM} \\"
echo "         -f tools/docker/${PLATFORM}.Dockerfile tools/docker"
echo ""
echo "  3. The Dockerfile will extract the SDK into the cross-compilation"
echo "     sysroot, replacing the stub headers with the real ones."
echo ""
