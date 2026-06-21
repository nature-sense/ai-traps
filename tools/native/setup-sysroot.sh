#!/usr/bin/env bash
#
# setup-sysroot.sh — Create a complete ARM64 Linux sysroot for native Clang
#                    cross-compilation on macOS.
#
# This script runs on the DEVELOPMENT MACHINE (macOS). It creates a sysroot
# directory by copying ALL headers and libraries from a live target board
# via SSH, then overlays SDK stubs and real SDK tarballs.
#
# The resulting sysroot is used by Clang's --sysroot flag for native
# cross-compilation with Meson.
#
# Usage:
#   # Copy from a live board (recommended — complete sysroot)
#   ./tools/native/setup-sysroot.sh rock3c root@192.168.1.100
#
#   # Copy from a live board using hostname
#   ./tools/native/setup-sysroot.sh rock3c root@rock-3c.local
#
#   # Rebuild from existing SDK tarball + stubs only (no board needed)
#   ./tools/native/setup-sysroot.sh rock3c --from-tarball tools/docker/sdk/rock3c-sdk-aarch64.tar.gz
#
#   # Just overlay SDK stubs (for an already-existing sysroot)
#   ./tools/native/setup-sysroot.sh rock3c --stubs-only
#
# Output:
#   tools/native/sysroots/{platform}/
#     ├── bin/
#     │   └── aarch64-pkg-config    # pkg-config wrapper for cross-compilation
#     ├── usr/
#     │   ├── include/              # Target headers
#     │   ├── lib/aarch64-linux-gnu/ # Target libraries
#     │   └── share/pkgconfig/      # pkg-config files
#     ├── lib/                      # Base libraries
#     └── lib64 -> lib/             # Symlink for compatibility

set -euo pipefail

# ─── Configuration ─────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
NATIVE_DIR="${PROJECT_ROOT}/tools/native"
SYSROOTS_DIR="${NATIVE_DIR}/sysroots"
DOCKER_DIR="${PROJECT_ROOT}/tools/docker"
SDK_STUBS_DIR="${DOCKER_DIR}/sdk-stubs"
SDK_TARBALL_DIR="${DOCKER_DIR}/sdk"

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
Usage: $0 <platform> [source] [options]

Create a complete ARM64 Linux sysroot for native Clang cross-compilation.

Arguments:
  platform    Target platform: rock3c, cubie-a7s, rdk-x5

Source (one of):
  user@host   SSH connection string to copy from a live board (default method)
  --from-tarball <path>  Extract sysroot from an existing SDK tarball
  --stubs-only           Only overlay SDK stubs (sysroot must already exist)

Options:
  --help      Show this help message

Examples:
  # Copy from live board
  $0 rock3c root@192.168.1.100

  # Rebuild from SDK tarball (no board needed)
  $0 rock3c --from-tarball tools/docker/sdk/rock3c-sdk-aarch64.tar.gz

  # Update stubs only
  $0 rock3c --stubs-only
EOF
    exit 0
}

# ─── Parse arguments ───────────────────────────────────────────────────────────
PLATFORM="${1:-}"
SOURCE="${2:-}"
THIRD_ARG="${3:-}"

if [ -z "$PLATFORM" ] || [ "$PLATFORM" = "--help" ]; then
    usage
fi

case "$PLATFORM" in
    rock3c|cubie-a7s|rdk-x5) ;;
    *) error "Unknown platform '$PLATFORM'. Use: rock3c, cubie-a7s, rdk-x5"; usage ;;
esac

MODE="board"
TARBALL_PATH=""
if [ "$SOURCE" = "--from-tarball" ]; then
    MODE="tarball"
    TARBALL_PATH="$THIRD_ARG"
    if [ -z "$TARBALL_PATH" ]; then
        error "--from-tarball requires a path argument"
        exit 1
    fi
    # Resolve relative paths
    if [[ "$TARBALL_PATH" != /* ]]; then
        TARBALL_PATH="${PROJECT_ROOT}/${TARBALL_PATH}"
    fi
elif [ "$SOURCE" = "--stubs-only" ]; then
    MODE="stubs"
elif [ -z "$SOURCE" ]; then
    error "Missing source argument. Provide user@host or --from-tarball <path>"
    usage
fi

SSH_TARGET="$SOURCE"
SYSROOT_DIR="${SYSROOTS_DIR}/${PLATFORM}"

echo ""
echo "=============================================="
echo "  Native Sysroot Setup — ${PLATFORM}"
echo "=============================================="
echo ""
echo "  Platform:  ${PLATFORM}"
echo "  Sysroot:   ${SYSROOT_DIR}"
echo "  Mode:      ${MODE}"
if [ "$MODE" = "board" ]; then
    echo "  Board:     ${SSH_TARGET}"
elif [ "$MODE" = "tarball" ]; then
    echo "  Tarball:   ${TARBALL_PATH}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════════════════════
# Step 1: Create or verify sysroot directory
# ═══════════════════════════════════════════════════════════════════════════════
section "Sysroot Directory"

mkdir -p "${SYSROOT_DIR}/usr/include"
mkdir -p "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu"
mkdir -p "${SYSROOT_DIR}/usr/share/pkgconfig"
mkdir -p "${SYSROOT_DIR}/lib/aarch64-linux-gnu"
mkdir -p "${SYSROOT_DIR}/bin"
ok "Sysroot directories created at ${SYSROOT_DIR}"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 2: Copy files from board (or extract from tarball)
# ═══════════════════════════════════════════════════════════════════════════════

# ── Mode: Copy from live board via SSH ─────────────────────────────────────────
copy_from_board() {
    local target="$1"
    local sysroot="$2"

    section "Copying from Board (SSH)"

    info "Testing SSH connection to ${target}..."
    if ! ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new "$target" "echo connected" 2>/dev/null; then
        error "Cannot connect to ${target}. Check:"
        error "  - Is the board powered on and network-reachable?"
        error "  - Can you SSH manually? ssh ${target}"
        exit 1
    fi
    ok "SSH connection established"

    # ── Copy headers ───────────────────────────────────────────────────────────
    info "Copying headers from /usr/include/ (this may take a while)..."
    ssh "$target" "tar czf /tmp/sysroot-include.tar.gz -C /usr/include ." 2>&1
    scp "$target:/tmp/sysroot-include.tar.gz" /tmp/sysroot-include.tar.gz >/dev/null 2>&1
    tar xzf /tmp/sysroot-include.tar.gz -C "${sysroot}/usr/include/"
    ssh "$target" "rm -f /tmp/sysroot-include.tar.gz" 2>&1
    rm -f /tmp/sysroot-include.tar.gz
    local inc_count
    inc_count=$(find "${sysroot}/usr/include" -type f 2>/dev/null | wc -l | tr -d ' ')
    ok "Headers copied: ${inc_count} files"

    # ── Copy libraries (aarch64-linux-gnu) ─────────────────────────────────────
    info "Copying libraries from /usr/lib/aarch64-linux-gnu/..."
    ssh "$target" "tar czf /tmp/sysroot-lib-usr.tar.gz -C /usr/lib/aarch64-linux-gnu ." 2>&1
    scp "$target:/tmp/sysroot-lib-usr.tar.gz" /tmp/sysroot-lib-usr.tar.gz >/dev/null 2>&1
    tar xzf /tmp/sysroot-lib-usr.tar.gz -C "${sysroot}/usr/lib/aarch64-linux-gnu/"
    ssh "$target" "rm -f /tmp/sysroot-lib-usr.tar.gz" 2>&1
    rm -f /tmp/sysroot-lib-usr.tar.gz
    local lib_count
    lib_count=$(find "${sysroot}/usr/lib/aarch64-linux-gnu" -type f 2>/dev/null | wc -l | tr -d ' ')
    ok "Libraries copied: ${lib_count} files"

    # ── Copy base libraries (/lib/aarch64-linux-gnu) ───────────────────────────
    info "Copying libraries from /lib/aarch64-linux-gnu/..."
    ssh "$target" "tar czf /tmp/sysroot-lib-base.tar.gz -C /lib/aarch64-linux-gnu ." 2>&1
    scp "$target:/tmp/sysroot-lib-base.tar.gz" /tmp/sysroot-lib-base.tar.gz >/dev/null 2>&1
    tar xzf /tmp/sysroot-lib-base.tar.gz -C "${sysroot}/lib/aarch64-linux-gnu/"
    ssh "$target" "rm -f /tmp/sysroot-lib-base.tar.gz" 2>&1
    rm -f /tmp/sysroot-lib-base.tar.gz
    local base_lib_count
    base_lib_count=$(find "${sysroot}/lib/aarch64-linux-gnu" -type f 2>/dev/null | wc -l | tr -d ' ')
    ok "Base libraries copied: ${base_lib_count} files"

    # ── Copy pkg-config files ──────────────────────────────────────────────────
    info "Copying pkg-config files..."
    ssh "$target" "tar czf /tmp/sysroot-pc.tar.gz -C /usr/lib/aarch64-linux-gnu/pkgconfig . 2>/dev/null; tar czf /tmp/sysroot-pc-share.tar.gz -C /usr/share/pkgconfig . 2>/dev/null; echo done" 2>&1
    scp "$target:/tmp/sysroot-pc.tar.gz" /tmp/sysroot-pc.tar.gz >/dev/null 2>&1 || true
    scp "$target:/tmp/sysroot-pc-share.tar.gz" /tmp/sysroot-pc-share.tar.gz >/dev/null 2>&1 || true
    if [ -f /tmp/sysroot-pc.tar.gz ]; then
        tar xzf /tmp/sysroot-pc.tar.gz -C "${sysroot}/usr/lib/aarch64-linux-gnu/pkgconfig/" 2>/dev/null || true
        rm -f /tmp/sysroot-pc.tar.gz
    fi
    if [ -f /tmp/sysroot-pc-share.tar.gz ]; then
        tar xzf /tmp/sysroot-pc-share.tar.gz -C "${sysroot}/usr/share/pkgconfig/" 2>/dev/null || true
        rm -f /tmp/sysroot-pc-share.tar.gz
    fi
    ssh "$target" "rm -f /tmp/sysroot-pc.tar.gz /tmp/sysroot-pc-share.tar.gz" 2>&1
    local pc_count
    pc_count=$(find "${sysroot}/usr/lib/aarch64-linux-gnu/pkgconfig" -name '*.pc' 2>/dev/null | wc -l | tr -d ' ')
    ok "pkg-config files copied: ${pc_count} files"

    # ── Copy GCC runtime files (/usr/lib/gcc/aarch64-linux-gnu) ──────────────
    info "Copying GCC runtime files from /usr/lib/gcc/aarch64-linux-gnu/..."
    ssh "$target" "tar czf /tmp/sysroot-gcc.tar.gz -C /usr/lib/gcc/aarch64-linux-gnu ." 2>&1
    scp "$target:/tmp/sysroot-gcc.tar.gz" /tmp/sysroot-gcc.tar.gz >/dev/null 2>&1 || true
    if [ -f /tmp/sysroot-gcc.tar.gz ]; then
        tar xzf /tmp/sysroot-gcc.tar.gz -C "${sysroot}/usr/lib/gcc/"
        rm -f /tmp/sysroot-gcc.tar.gz
    fi
    ssh "$target" "rm -f /tmp/sysroot-gcc.tar.gz" 2>&1
    local gcc_count
    gcc_count=$(find "${sysroot}/usr/lib/gcc" -type f 2>/dev/null | wc -l | tr -d ' ')
    ok "GCC runtime files copied: ${gcc_count} files"

    # ── Copy /usr/lib/pkgconfig (some platforms put .pc files here) ────────────
    info "Copying pkg-config files from /usr/lib/pkgconfig..."
    ssh "$target" "tar czf /tmp/sysroot-pc-lib.tar.gz -C /usr/lib/pkgconfig . 2>/dev/null; echo done" 2>&1
    scp "$target:/tmp/sysroot-pc-lib.tar.gz" /tmp/sysroot-pc-lib.tar.gz >/dev/null 2>&1 || true
    if [ -f /tmp/sysroot-pc-lib.tar.gz ]; then
        tar xzf /tmp/sysroot-pc-lib.tar.gz -C "${sysroot}/usr/lib/pkgconfig/" 2>/dev/null || true
        rm -f /tmp/sysroot-pc-lib.tar.gz
    fi
    ssh "$target" "rm -f /tmp/sysroot-pc-lib.tar.gz" 2>&1

    # ── Create lib64 symlink ───────────────────────────────────────────────────
    if [ ! -L "${sysroot}/lib64" ]; then
        ln -sf "lib" "${sysroot}/lib64"
    fi

    ok "Sysroot copied from board successfully"
}

# ── Mode: Extract from SDK tarball ─────────────────────────────────────────────
extract_from_tarball() {
    local tarball="$1"
    local sysroot="$2"

    section "Extracting from SDK Tarball"

    if [ ! -f "$tarball" ]; then
        warn "SDK tarball not found: ${tarball}"
        warn "This is expected if you haven't run extract-sdk.sh on the board yet."
        warn "Continuing with stub headers only..."
        return 0
    fi

    info "Extracting ${tarball}..."
    tar xzf "$tarball" -C /tmp/
    local dirname
    dirname=$(basename "$tarball" -sdk-aarch64.tar.gz)
    if [ -d "/tmp/${dirname}" ]; then
        # Copy headers
        if [ -d "/tmp/${dirname}/include" ]; then
            cp -r "/tmp/${dirname}/include/"* "${sysroot}/usr/include/"
            ok "SDK headers extracted"
        fi
        # Copy libraries
        if [ -d "/tmp/${dirname}/lib" ]; then
            cp -r "/tmp/${dirname}/lib/"* "${sysroot}/usr/lib/aarch64-linux-gnu/"
            ok "SDK libraries extracted"
        fi
        # Copy pkg-config files
        if [ -d "/tmp/${dirname}/pkgconfig" ]; then
            cp -r "/tmp/${dirname}/pkgconfig/"* "${sysroot}/usr/lib/aarch64-linux-gnu/pkgconfig/"
            ok "SDK pkg-config files extracted"
        fi
        rm -rf "/tmp/${dirname}"
    fi
    ok "SDK tarball extracted"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Step 3: Overlay SDK stub headers
# ═══════════════════════════════════════════════════════════════════════════════
overlay_sdk_stubs() {
    local sysroot="$1"

    section "SDK Stub Headers"

    local stub_dir="${SDK_STUBS_DIR}/${PLATFORM}"
    if [ ! -d "$stub_dir" ]; then
        warn "No SDK stubs directory found: ${stub_dir}"
        return 0
    fi

    info "Copying SDK stub headers from ${stub_dir}..."
    cp -r "${stub_dir}/"* "${sysroot}/usr/include/"
    ok "SDK stub headers overlaid"

    # Also copy to /usr/lib/aarch64-linux-gnu/include for cross-compiler compat
    local stub_lib_inc="${sysroot}/usr/lib/aarch64-linux-gnu/include"
    mkdir -p "$stub_lib_inc"
    cp -r "${stub_dir}/"* "$stub_lib_inc/"
    ok "SDK stub headers also copied to cross-compiler include path"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Step 4: Create pkg-config wrapper
# ═══════════════════════════════════════════════════════════════════════════════
create_pkg_config_wrapper() {
    local sysroot="$1"

    section "pkg-config Wrapper"

    local wrapper="${sysroot}/bin/aarch64-pkg-config"

    cat > "$wrapper" << 'PKGCONFIGEOF'
#!/bin/bash
# aarch64-pkg-config — pkg-config wrapper for cross-compilation
#
# This wrapper ensures pkg-config looks for .pc files in the sysroot
# rather than in the host system (macOS).
#
# Usage:
#   aarch64-pkg-config [options] <library>
#
# Environment variables (set by the wrapper):
#   PKG_CONFIG_DIR=           (unset — prevents searching host paths)
#   PKG_CONFIG_LIBDIR         (set to sysroot's pkgconfig directory)
#   PKG_CONFIG_SYSROOT_DIR    (set to sysroot root)

export PKG_CONFIG_DIR=
export PKG_CONFIG_LIBDIR=/Users/steve/naturesense/ai-traps/tools/native/sysroots/__PLATFORM__/usr/lib/aarch64-linux-gnu/pkgconfig:/Users/steve/naturesense/ai-traps/tools/native/sysroots/__PLATFORM__/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/Users/steve/naturesense/ai-traps/tools/native/sysroots/__PLATFORM__

exec pkg-config "$@"
PKGCONFIGEOF

    # Replace __PLATFORM__ placeholder with actual platform name
    sed -i '' "s|__PLATFORM__|${PLATFORM}|g" "$wrapper"
    chmod +x "$wrapper"
    ok "pkg-config wrapper created: ${wrapper}"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Step 5: Verify sysroot completeness
# ═══════════════════════════════════════════════════════════════════════════════
verify_sysroot() {
    local sysroot="$1"

    section "Sysroot Verification"

    local errors=0

    # Check essential headers
    local essential_headers=(
        "stdio.h"
        "stdlib.h"
        "string.h"
        "unistd.h"
        "fcntl.h"
        "pthread.h"
        "dlfcn.h"
        "signal.h"
        "sys/stat.h"
        "sys/types.h"
        "linux/videodev2.h"
    )

    info "Checking essential headers..."
    for header in "${essential_headers[@]}"; do
        if [ -f "${sysroot}/usr/include/${header}" ]; then
            ok "  ${header}"
        else
            warn "  ${header} — NOT FOUND"
            errors=$((errors + 1))
        fi
    done

    # Check essential libraries
    local essential_libs=(
        "libc.so"
        "libpthread.so"
        "libdl.so"
        "libm.so"
        "librt.so"
    )

    info "Checking essential libraries..."
    for lib in "${essential_libs[@]}"; do
        if [ -f "${sysroot}/usr/lib/aarch64-linux-gnu/${lib}" ] || \
           [ -f "${sysroot}/lib/aarch64-linux-gnu/${lib}" ] || \
           [ -L "${sysroot}/usr/lib/aarch64-linux-gnu/${lib}" ] || \
           [ -L "${sysroot}/lib/aarch64-linux-gnu/${lib}" ]; then
            ok "  ${lib}"
        else
            warn "  ${lib} — NOT FOUND"
            errors=$((errors + 1))
        fi
    done

    # Check project-specific dependencies
    info "Checking project-specific dependencies..."
    local project_deps=(
        "yaml-cpp/yaml.h"
        "sqlite3.h"
        "jpeglib.h"
        "png.h"
        "v4l2_nv12.h"
    )

    for dep in "${project_deps[@]}"; do
        if [ -f "${sysroot}/usr/include/${dep}" ]; then
            ok "  ${dep}"
        else
            warn "  ${dep} — NOT FOUND (may be in a different location)"
        fi
    done

    # Check platform-specific SDK headers
    case "$PLATFORM" in
        rock3c)
            local rk_headers=(
                "rknn/rknn_api.h"
                "rga/RgaApi.h"
                "rockchip/rk_mpi.h"
                "rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h"
            )
            info "Checking Rockchip SDK headers..."
            for header in "${rk_headers[@]}"; do
                if [ -f "${sysroot}/usr/include/${header}" ]; then
                    ok "  ${header}"
                else
                    warn "  ${header} — NOT FOUND (may need SDK tarball)"
                fi
            done
            ;;
        cubie-a7s)
            local aw_headers=(
                "VX/vx.h"
                "CL/cl.h"
            )
            info "Checking Allwinner SDK headers..."
            for header in "${aw_headers[@]}"; do
                if [ -f "${sysroot}/usr/include/${header}" ]; then
                    ok "  ${header}"
                else
                    warn "  ${header} — NOT FOUND (may need SDK tarball)"
                fi
            done
            ;;
    esac

    # Check essential GCC runtime files (needed for linking with Clang/lld)
    local essential_gcc_files=(
        "crtbeginS.o"
        "crtendS.o"
        "crtbegin.o"
        "crtend.o"
    )

    info "Checking GCC runtime files..."
    local gcc_dir="${sysroot}/usr/lib/gcc"
    local gcc_found=0
    for file in "${essential_gcc_files[@]}"; do
        found=$(find "$gcc_dir" -name "$file" -type f 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            ok "  ${file}"
            gcc_found=$((gcc_found + 1))
        else
            warn "  ${file} — NOT FOUND (expected under /usr/lib/gcc/aarch64-linux-gnu/)"
            errors=$((errors + 1))
        fi
    done

    # Check for libgcc_s.so (GCC runtime shared library)
    local libgcc
    libgcc=$(find "$gcc_dir" -name "libgcc_s.so" -type f 2>/dev/null | head -1)
    if [ -n "$libgcc" ]; then
        ok "  libgcc_s.so"
    else
        warn "  libgcc_s.so — NOT FOUND (may be linked via --sysroot fallback)"
    fi

    echo ""
    if [ $errors -eq 0 ]; then
        ok "Sysroot verification passed — all essential files found"
    else
        warn "Sysroot verification: ${errors} essential files missing"
        warn "The sysroot may still work if the missing files are not needed by your code."
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

case "$MODE" in
    board)
        copy_from_board "$SSH_TARGET" "$SYSROOT_DIR"
        overlay_sdk_stubs "$SYSROOT_DIR"
        # Try to extract SDK tarball if available
        tarball="${SDK_TARBALL_DIR}/${PLATFORM}-sdk-aarch64.tar.gz"
        if [ -f "$tarball" ]; then
            extract_from_tarball "$tarball" "$SYSROOT_DIR"
        else
            info "No SDK tarball found at ${tarball}"
            info "SDK stub headers will be used instead (sufficient for compilation)"
        fi
        ;;
    tarball)
        # Start with stubs, then overlay tarball
        overlay_sdk_stubs "$SYSROOT_DIR"
        extract_from_tarball "$TARBALL_PATH" "$SYSROOT_DIR"
        ;;
    stubs)
        overlay_sdk_stubs "$SYSROOT_DIR"
        # Try to extract SDK tarball if available
        tarball="${SDK_TARBALL_DIR}/${PLATFORM}-sdk-aarch64.tar.gz"
        if [ -f "$tarball" ]; then
            extract_from_tarball "$tarball" "$SYSROOT_DIR"
        fi
        ;;
esac

# Always create pkg-config wrapper
create_pkg_config_wrapper "$SYSROOT_DIR"

# Verify the sysroot
verify_sysroot "$SYSROOT_DIR"

# ─── Summary ───────────────────────────────────────────────────────────────────
section "Setup Complete"

echo "  Platform: ${PLATFORM}"
echo "  Sysroot:  ${SYSROOT_DIR}"
echo ""
echo "  Contents:"
echo "    Headers:  $(find "${SYSROOT_DIR}/usr/include" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo "    Libraries: $(find "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo "    Base libs: $(find "${SYSROOT_DIR}/lib/aarch64-linux-gnu" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo "    .pc files: $(find "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/pkgconfig" -name '*.pc' 2>/dev/null | wc -l | tr -d ' ') files"
echo ""
echo "  ── Next steps ──"
echo ""
echo "  1. Ensure LLVM/Clang is installed:"
echo "       brew install llvm meson ninja"
echo "       export PATH=\"/opt/homebrew/opt/llvm/bin:\$PATH\""
echo ""
echo "  2. Build the toolkit:"
echo "       cd ${PROJECT_ROOT}"
echo "       meson setup build-${PLATFORM}/toolkit \\"
echo "         --cross-file tools/native/cross/aarch64-${PLATFORM}.ini \\"
echo "         -Dplatform=${PLATFORM} \\"
echo "         -Dbuildtype=release \\"
echo "         traps/toolkit"
echo "       ninja -C build-${PLATFORM}/toolkit"
echo ""
echo "  3. Build the target binary:"
echo "       meson setup build-${PLATFORM}/target \\"
echo "         --cross-file tools/native/cross/aarch64-${PLATFORM}.ini \\"
echo "         -Dplatform=${PLATFORM} \\"
echo "         -Dtoolkit_build_dir=${PROJECT_ROOT}/build-${PLATFORM}/toolkit \\"
echo "         -Dbuildtype=release \\"
echo "         traps/targets"
echo "       ninja -C build-${PLATFORM}/target"
echo ""
echo "  4. Check the binary:"
echo "       file build-${PLATFORM}/target/ai-trap-detection"
echo "       llvm-readelf -h build-${PLATFORM}/target/ai-trap-detection"
echo ""
echo "  5. Deploy to board:"
echo "       scp build-${PLATFORM}/target/ai-trap-detection root@<board>:/usr/local/bin/"
echo ""
