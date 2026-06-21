#!/usr/bin/env bash
#
# setup-rock3c.sh — AI Camera Trap board setup for ROCK 3C (RK3566)
#
# This script runs ON THE BOARD itself. It:
#   - Installs runtime dependencies (if missing), including Rockchip NPU runtime
#   - Creates the runtime directory structure
#   - Creates a systemd service file for the trap binary (disabled)
#   - Enables the camera overlay (RK_AIQ ISP + IMX219/IMX415)
#   - Enables the NPU overlay (rk3568-npu-enable.dtbo)
#   - Ensures Bluetooth is running
#   - Verifies hardware (V4L2 devices, kernel modules, media pipeline)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/nature-sense/ai-traps/main/tools/scripts/setup-rock3c.sh | sudo bash
#
# Or copy to the board and run:
#   scp tools/scripts/setup-rock3c.sh root@rock-3c.local:/tmp/
#   ssh root@rock-3c.local sudo bash /tmp/setup-rock3c.sh
#

set -euo pipefail

# ─── Source shared library ─────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "${SCRIPT_DIR}/lib/setup-common.sh" ]; then
    source "${SCRIPT_DIR}/lib/setup-common.sh"
elif [ -f "/tmp/lib/setup-common.sh" ]; then
    source "/tmp/lib/setup-common.sh"
else
    # Fallback: download the shared library
    echo "Downloading shared library..."
    curl -fsSL -o /tmp/setup-common.sh \
        "https://raw.githubusercontent.com/nature-sense/ai-traps/main/tools/scripts/lib/setup-common.sh"
    source /tmp/setup-common.sh
fi

# ─── Configuration ─────────────────────────────────────────────────────────────
PLATFORM="rock3c"
ARCH="aarch64"
SERVICE_NAME="ai-trap-detection"
SERVICE_DESC="AI Camera Trap Detection Service (ROCK 3C)"
BINARY_PATH="/usr/local/bin/ai-trap-detection"
CONFIG_PATH="/etc/ai-trap/config.yaml"
WORKING_DIR="/var/lib/ai-trap"

# Runtime dependencies for ROCK 3C
RUNTIME_PKGS=(
    # Core libraries (runtime)
    libsqlite3-0
    libjpeg62-turbo
    libturbojpeg0
    libpng16-16
    libyaml-cpp0.7
    libsystemd0

    # Development headers (needed for cross-compilation sysroot)
    libsqlite3-dev
    libjpeg62-turbo-dev
    libpng-dev
    libyaml-cpp-dev
    libsystemd-dev

    # V4L2 / camera
    v4l-utils

    # Rockchip NPU headers/libraries (runtime .so installed separately below)
    camera-engine-rkaiq
    librga-dev

    # Bluetooth
    bluez
    bluez-tools

    # Python (for build server, model tools)
    python3
    python3-pip

    # Utilities
    curl
    ca-certificates
)

# ─── Install RKNN Runtime v2.3.2 ────────────────────────────────────────────────
# Download librknnrt.so and headers from the official Rockchip rknn-toolkit2 repo
# at tag v2.3.2, replacing the outdated apt package rknpu2-rk356x.
RKNN_VERSION="2.3.2"
RKNN_SO_URL="https://raw.githubusercontent.com/airockchip/rknn-toolkit2/v${RKNN_VERSION}/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so"
RKNN_HDR_BASE="https://raw.githubusercontent.com/airockchip/rknn-toolkit2/v${RKNN_VERSION}/rknpu2/runtime/Linux/librknn_api/include"

install_rknn_runtime() {
    section "RKNN Runtime ${RKNN_VERSION}"

    local lib_dir="/usr/lib/aarch64-linux-gnu"
    local inc_dir="/usr/include"
    local so_name="librknnrt.so"
    local so_versioned="${so_name}.${RKNN_VERSION}"

    # ── 1. Remove old apt package if installed ────────────────────────────────
    if dpkg -l rknpu2-rk356x 2>/dev/null | grep -q '^ii'; then
        info "Removing old rknpu2-rk356x apt package..."
        DEBIAN_FRONTEND=noninteractive apt-get remove -y rknpu2-rk356x 2>/dev/null || true
        ok "Old apt package removed"
    fi

    # ── 2. Remove any stale librknnrt .so files from the old install ──────────
    info "Cleaning stale librknnrt files..."
    for f in "${lib_dir}/${so_name}"*; do
        [ -f "$f" ] || [ -L "$f" ] || continue
        rm -f "$f"
    done
    ok "Stale files removed"

    # ── 3. Download the versioned .so ─────────────────────────────────────────
    info "Downloading ${so_versioned}..."
    wget -q --show-progress -O "${lib_dir}/${so_versioned}" "${RKNN_SO_URL}"
    chmod 755 "${lib_dir}/${so_versioned}"
    ok "Downloaded: ${so_versioned}"

    # ── 4. Create symlinks ────────────────────────────────────────────────────
    ln -sf "${so_versioned}" "${lib_dir}/${so_name}.1"
    ln -sf "${so_versioned}" "${lib_dir}/${so_name}"
    ok "Symlinks created: ${so_name}.1 → ${so_versioned}, ${so_name} → ${so_versioned}"

    # ── 5. Download headers ──────────────────────────────────────────────────
    local headers=("rknn_api.h" "rknn_custom_op.h" "rknn_matmul_api.h")
    for hdr in "${headers[@]}"; do
        info "Downloading ${hdr}..."
        wget -q --show-progress -O "${inc_dir}/${hdr}" "${RKNN_HDR_BASE}/${hdr}"
        ok "Downloaded: ${inc_dir}/${hdr}"
    done

    # ── 6. Run ldconfig ──────────────────────────────────────────────────────
    ldconfig
    ok "ldconfig updated"

    # ── 7. Verify ─────────────────────────────────────────────────────────────
    if [ -f "${lib_dir}/${so_versioned}" ]; then
        local actual_size
        actual_size=$(stat -c%s "${lib_dir}/${so_versioned}" 2>/dev/null || stat -f%z "${lib_dir}/${so_versioned}" 2>/dev/null)
        ok "RKNN Runtime ${RKNN_VERSION} installed (${actual_size} bytes)"
    else
        error "RKNN Runtime ${RKNN_VERSION} installation FAILED — ${lib_dir}/${so_versioned} not found"
        exit 1
    fi
}

# ─── Install rknn-toolkit2 (Python) ─────────────────────────────────────────────
install_rknn_toolkit2() {
    section "RKNN Toolkit2 (Python)"

    info "Installing rknn-toolkit2 via pip..."
    if pip3 install rknn-toolkit2 2>/dev/null; then
        ok "rknn-toolkit2 installed successfully"
    else
        warn "pip3 install rknn-toolkit2 failed"
        warn "This may be because the package is not available for this Python version."
        warn "Try installing from the Radxa repository or a compatible wheel."
        warn ""
        warn "The model conversion step will fail without rknn-toolkit2."
        warn "You can still run the trap binary with a pre-converted .rknn model."
    fi
}

# ─── Main ──────────────────────────────────────────────────────────────────────
main() {
    echo ""
    echo "=============================================="
    echo "  AI Camera Trap — Board Setup (ROCK 3C)"
    echo "=============================================="
    echo ""

    require_root
    print_system_info

    # ── 1. Update package lists ────────────────────────────────────────────────
    section "Package Repository"
    info "Updating package lists..."
    apt-get update
    ok "Package lists updated"

    # ── 2. Install runtime dependencies ────────────────────────────────────────
    section "Runtime Dependencies"
    ensure_pkgs "${RUNTIME_PKGS[@]}"

    # ── 3. Install RKNN Runtime v2.3.2 ─────────────────────────────────────────
    install_rknn_runtime

    # ── 4. Create directory structure ──────────────────────────────────────────
    section "Directory Structure"
    ensure_dir "/var/lib/ai-trap/detections" "root:root" "755"
    ensure_dir "/var/lib/ai-trap/sessions"   "root:root" "755"
    ensure_dir "/usr/share/ai-trap/models"   "root:root" "755"
    ensure_dir "/etc/ai-trap"                "root:root" "755"

    # ── 4. Create systemd service file ─────────────────────────────────────────
    section "Systemd Service"
    create_systemd_service \
        "$SERVICE_NAME" \
        "$SERVICE_DESC" \
        "$BINARY_PATH" \
        "$CONFIG_PATH" \
        "$WORKING_DIR"

    # ── 5. Enable camera overlay ───────────────────────────────────────────────
    section "Camera Overlay"

    # On ROCK 3C with Radxa OS, camera overlays are enabled by placing .dtbo
    # files in /boot/dtbo/ and running u-boot-update to regenerate extlinux.conf
    # with fdtoverlays entries. The RK_AIQ ISP driver (rkisp) must be loaded.
    #
    # Supported camera overlays:
    #   rock-3c-rpi-camera-v1p3.dtbo  — Raspberry Pi Camera v1.3 (OV5647)
    #   rock-3c-rpi-camera-v2.dtbo    — Raspberry Pi Camera v2 (IMX219)
    #   rock-3c-radxa-camera-8m.dtbo  — Radxa Camera 8M (IMX415)
    #   rock-3c-okdo-5mp-camera.dtbo  — OKDO 5MP Camera
    #
    local dtbo_dir="/boot/dtbo"
    local overlays_applied=false
    local camera_overlays=()

    # Detect which camera overlays are available (not .disabled)
    if [ -d "$dtbo_dir" ]; then
        info "Checking ${dtbo_dir} for camera overlays..."

        for dtbo in "$dtbo_dir"/rock-3c-*.dtbo; do
            if [ -f "$dtbo" ]; then
                local name
                name="$(basename "$dtbo")"
                # Check if it's a camera overlay
                if echo "$name" | grep -qE 'camera|imx219|imx415|ov5647|v1p3|v2'; then
                    camera_overlays+=("$name")
                    ok "Camera overlay DTBO found: ${name}"
                    overlays_applied=true
                fi
            fi
        done

        if [ "$overlays_applied" = false ]; then
            warn "No camera overlay DTBO found in ${dtbo_dir}"
            warn ""
            warn "Available camera overlays (enable by removing .disabled suffix):"
            for dtbo in "$dtbo_dir"/rock-3c-*.dtbo.disabled; do
                if [ -f "$dtbo" ]; then
                    local name
                    name="$(basename "$dtbo" .disabled)"
                    if echo "$name" | grep -qE 'camera|imx219|imx415|ov5647|v1p3|v2'; then
                        warn "  ${name}"
                    fi
                fi
            done
            warn ""
            warn "To enable a camera overlay:"
            warn "  sudo mv /boot/dtbo/<overlay>.dtbo.disabled /boot/dtbo/<overlay>.dtbo"
            warn "  sudo u-boot-update && sudo reboot"
        fi
    else
        warn "No ${dtbo_dir} directory found"
        warn "This board may use a different overlay mechanism."
        warn "Check /boot/extlinux/extlinux.conf for fdtoverlays entries."
    fi

    # Check if rkisp kernel module is available
    if modprobe -n rkisp 2>/dev/null; then
        info "rkisp kernel module is available"
    else
        warn "rkisp kernel module not found — camera ISP may not work"
    fi

    # ── 6. Enable NPU overlay ──────────────────────────────────────────────────
    section "NPU Overlay"

    # The RK3566 NPU is disabled by default in the device tree on Radxa OS.
    # It must be enabled via the rk3568-npu-enable.dtbo overlay.
    local npu_overlay="rk3568-npu-enable.dtbo"
    local npu_overlay_path="${dtbo_dir}/${npu_overlay}"
    local npu_overlay_disabled="${dtbo_dir}/${npu_overlay}.disabled"

    if [ -f "$npu_overlay_path" ]; then
        ok "NPU overlay already enabled: ${npu_overlay}"
    elif [ -f "$npu_overlay_disabled" ]; then
        info "Enabling NPU overlay: ${npu_overlay}"
        mv "$npu_overlay_disabled" "$npu_overlay_path"
        ok "NPU overlay enabled: ${npu_overlay}"
    else
        warn "NPU overlay not found at ${npu_overlay_path} or ${npu_overlay_disabled}"
        warn "The NPU may already be enabled in the kernel, or this board uses a different mechanism."
    fi

    # Add NPU overlay to extlinux.conf fdtoverlays if not already present
    local extlinux="/boot/extlinux/extlinux.conf"
    if [ -f "$extlinux" ]; then
        if grep -q "fdtoverlays" "$extlinux"; then
            if ! grep -q "rk3568-npu-enable" "$extlinux"; then
                info "Adding NPU overlay to fdtoverlays in ${extlinux}..."
                sed -i "s|fdtoverlays  |fdtoverlays  ${dtbo_dir}/${npu_overlay} |" "$extlinux"
                ok "NPU overlay added to fdtoverlays"
            else
                ok "NPU overlay already in fdtoverlays"
            fi
        else
            warn "No fdtoverlays line found in ${extlinux}"
            warn "Add manually: fdtoverlays ${dtbo_dir}/${npu_overlay}"
        fi
    else
        warn "extlinux.conf not found at ${extlinux}"
        warn "This board may use a different boot mechanism."
    fi

    # Regenerate extlinux.conf if any overlays were changed
    if [ "$overlays_applied" = true ] || [ -f "$npu_overlay_path" ]; then
        info "Regenerating extlinux.conf with all overlays..."
        u-boot-update
        ok "extlinux.conf updated — reboot to apply overlays"
    fi

    # ── 7. Ensure Bluetooth is running ─────────────────────────────────────────
    ensure_bluetooth

    # ── 8. Verify hardware ─────────────────────────────────────────────────────
    section "Hardware Verification"

    # V4L2 devices
    verify_v4l2

    # Media pipeline
    verify_media_pipeline

    # Kernel modules
    section "Kernel Modules"
    check_kernel_module "rkisp" || true
    check_kernel_module "rkisp_v32" || true
    check_kernel_module "rkaiq" || true
    check_kernel_module "rga" || true
    check_kernel_module "mpp" || true
    check_kernel_module "hci_uart" || true
    check_kernel_module "bluetooth" || true
    check_kernel_module "rknn" || true
    check_kernel_module "galcore" || true

    # ── 9. Validate camera overlay vs config ──────────────────────────────────
    validate_camera_overlay "$CONFIG_PATH" || true

    # ── 10. Install rknn-toolkit2 for on-board model conversion ───────────────
    install_rknn_toolkit2

    # ── 11. Summary ────────────────────────────────────────────────────────────
    print_summary \
        "$PLATFORM" \
        "traps/targets/config/rock3c.yaml" \
        "build-rock3c/target/ai-trap-detection"
}

main "$@"
