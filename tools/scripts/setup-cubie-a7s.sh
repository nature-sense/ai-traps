#!/usr/bin/env bash
#
# setup-cubie-a7s.sh — AI Camera Trap board setup for Cubie A7S (Allwinner A527)
#
# This script runs ON THE BOARD itself. It:
#   - Installs runtime dependencies (if missing)
#   - Creates the runtime directory structure
#   - Creates a systemd service file for the trap binary (disabled)
#   - Enables the camera overlay (IMX415 via sunxi-vin)
#   - Ensures Bluetooth is running
#   - Verifies hardware (V4L2 devices, kernel modules, media pipeline)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/nature-sense/ai-traps/main/tools/scripts/setup-cubie-a7s.sh | sudo bash
#
# Or copy to the board and run:
#   scp tools/scripts/setup-cubie-a7s.sh root@radxa-cubie-a7s.local:/tmp/
#   ssh root@radxa-cubie-a7s.local sudo bash /tmp/setup-cubie-a7s.sh
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
PLATFORM="cubie-a7s"
ARCH="aarch64"
SERVICE_NAME="ai-trap-detection"
SERVICE_DESC="AI Camera Trap Detection Service (Cubie A7S)"
BINARY_PATH="/usr/local/bin/ai-trap-detection"
CONFIG_PATH="/etc/ai-trap/config.yaml"
WORKING_DIR="/var/lib/ai-trap"

# Runtime dependencies for Cubie A7S
RUNTIME_PKGS=(
    # Core libraries
    libsqlite3-0
    libjpeg62-turbo
    libpng16-16
    libyaml-cpp0.8
    libsystemd0

    # V4L2 / camera
    v4l-utils

    # OpenCL (for video scaling on NPU)
    ocl-icd-libopencl1

    # Bluetooth
    bluez
    bluez-tools

    # Python (for model tools, utilities)
    python3
    python3-pip

    # Utilities
    curl
    ca-certificates
)

# ─── Main ──────────────────────────────────────────────────────────────────────
main() {
    echo ""
    echo "=============================================="
    echo "  AI Camera Trap — Board Setup (Cubie A7S)"
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

    # ── 3. Create directory structure ──────────────────────────────────────────
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

    # On Cubie A7S (Allwinner A527) with Radxa OS, camera overlays are enabled
    # by placing .dtbo files in /boot/dtbo/ and running u-boot-update to
    # regenerate extlinux.conf with fdtoverlays entries.
    #
    # Supported camera overlays:
    #   cubie-a7a-radxa-camera-4k-415.dtbo — Radxa Camera 4K (IMX415)
    #
    # Fallback: Some Radxa OS builds use /boot/armbianEnv.txt or /boot/uEnv.txt
    # with overlays= directive (Armbian-style).

    local overlay_name="cubie-a7a-radxa-camera-4k-415"
    local overlay_applied=false

    # ── Method 1: Radxa OS u-boot-update mechanism (dtbo files in /boot/dtbo/) ──
    local dtbo_dir="/boot/dtbo"
    if [ -d "$dtbo_dir" ]; then
        info "Checking ${dtbo_dir} for camera overlays..."

        for dtbo in "$dtbo_dir"/*.dtbo; do
            if [ -f "$dtbo" ]; then
                local name
                name="$(basename "$dtbo")"
                if echo "$name" | grep -qE 'camera|imx415|imx219'; then
                    ok "Camera overlay DTBO found: ${name}"
                    overlay_applied=true
                fi
            fi
        done

        if [ "$overlay_applied" = true ]; then
            info "Regenerating extlinux.conf with camera overlays..."
            u-boot-update
            ok "extlinux.conf updated — reboot to apply camera overlay"
        fi
    fi

    # ── Method 2: Armbian-style /boot/armbianEnv.txt ────────────────────────────
    if [ -f "/boot/armbianEnv.txt" ]; then
        info "Checking /boot/armbianEnv.txt for camera overlay..."
        if grep -q "${overlay_name}" /boot/armbianEnv.txt 2>/dev/null; then
            ok "Camera overlay found in /boot/armbianEnv.txt: ${overlay_name}"
            overlay_applied=true
        else
            warn "Camera overlay not found in /boot/armbianEnv.txt"
            warn "Add the following line to /boot/armbianEnv.txt and reboot:"
            echo ""
            echo "  overlays=${overlay_name}"
            echo ""
        fi
    fi

    # ── Method 3: Armbian-style /boot/uEnv.txt ──────────────────────────────────
    if [ -f "/boot/uEnv.txt" ]; then
        info "Checking /boot/uEnv.txt for camera overlay..."
        if grep -q "${overlay_name}" /boot/uEnv.txt 2>/dev/null; then
            ok "Camera overlay found in /boot/uEnv.txt: ${overlay_name}"
            overlay_applied=true
        else
            warn "Camera overlay not found in /boot/uEnv.txt"
            warn "Add the following line to /boot/uEnv.txt and reboot:"
            echo ""
            echo "  overlays=${overlay_name}"
            echo ""
        fi
    fi

    # ── Check if the overlay DTBO file exists anywhere ──────────────────────────
    local dtbo_paths=(
        "/boot/dtbo/${overlay_name}.dtbo"
        "/boot/dtbs/allwinner/overlay/${overlay_name}.dtbo"
        "/boot/dtb/allwinner/overlay/${overlay_name}.dtbo"
        "/boot/dtbs/${overlay_name}.dtbo"
        "/boot/dtb/${overlay_name}.dtbo"
    )

    local dtbo_found=false
    for dtbo in "${dtbo_paths[@]}"; do
        if [ -f "$dtbo" ]; then
            ok "Camera overlay DTBO found: ${dtbo}"
            dtbo_found=true
            break
        fi
    done

    if [ "$dtbo_found" = false ] && [ "$overlay_applied" = false ]; then
        warn "Camera overlay DTBO file not found in expected locations"
        warn "Expected: ${overlay_name}.dtbo"
        warn ""
        warn "To enable the Radxa Camera 4K (IMX415):"
        warn "  sudo ln -s /boot/dtbo/${overlay_name}.dtbo /boot/dtbo/${overlay_name}.dtbo"
        warn ""
        warn "Then run: sudo u-boot-update && sudo reboot"
    fi

    # Check if the sunxi-vin kernel module is available
    if modprobe -n sunxi-vin 2>/dev/null; then
        info "sunxi-vin kernel module is available"
    else
        warn "sunxi-vin kernel module not found — camera capture may not work"
        warn "This may require a kernel update from Radxa/Allwinner."
    fi

    # Check if the IMX415 sensor driver is available
    if modprobe -n imx415 2>/dev/null; then
        info "imx415 kernel module is available"
    else
        warn "imx415 kernel module not found — may be built into the kernel"
    fi

    # ── 6. Ensure Bluetooth is running ─────────────────────────────────────────
    ensure_bluetooth

    # ── 7. Verify hardware ─────────────────────────────────────────────────────
    section "Hardware Verification"

    # V4L2 devices
    verify_v4l2

    # Media pipeline
    verify_media_pipeline

    # Kernel modules
    section "Kernel Modules"
    check_kernel_module "sunxi-vin" || true
    check_kernel_module "imx415" || true
    check_kernel_module "v4l2_fwnode" || true
    check_kernel_module "v4l2_async" || true
    check_kernel_module "hci_uart" || true
    check_kernel_module "bluetooth" || true

    # Check for NPU (Vivante VIP) kernel module
    check_kernel_module "galcore" || true
    check_kernel_module "vip" || true

    # ── 8. Summary ─────────────────────────────────────────────────────────────
    print_summary \
        "$PLATFORM" \
        "traps/targets/config/cubie-a7s.yaml" \
        "build-cubie-a7s/target/ai-trap-detection"
}

main "$@"
