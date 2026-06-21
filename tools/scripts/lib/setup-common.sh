#!/usr/bin/env bash
#
# setup-common.sh — Shared helper library for AI Camera Trap board setup scripts
#
# This file is sourced by platform-specific setup scripts (setup-rock3c.sh,
# setup-cubie-a7s.sh). It provides idempotent helper functions for common
# setup tasks.
#
# Usage (in platform script):
#   SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
#   source "${SCRIPT_DIR}/lib/setup-common.sh"
#

set -euo pipefail

# ─── Color output ─────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()    { echo -e "${BLUE}ℹ${NC} $1"; }
ok()      { echo -e "${GREEN}✓${NC} $1"; }
warn()    { echo -e "${YELLOW}⚠${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1"; }
section() { echo ""; echo "━━━ $1 ━━━"; echo ""; }

# ─── Must be run as root ──────────────────────────────────────────────────────
require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        error "This script must be run as root (sudo)."
        exit 1
    fi
}

# ─── Check if running on the expected platform ────────────────────────────────
# Usage: require_platform "rock3c" "aarch64"
# Checks that the architecture matches and optionally that a platform-specific
# indicator file or device exists.
require_platform() {
    local expected_arch="$2"
    local actual_arch
    actual_arch="$(uname -m)"

    if [ "$actual_arch" != "$expected_arch" ]; then
        error "Wrong architecture: expected ${expected_arch}, got ${actual_arch}"
        exit 1
    fi
}

# ─── Package management (idempotent) ───────────────────────────────────────────
# Install a package only if it is not already installed.
# Usage: ensure_pkg "package-name"
ensure_pkg() {
    local pkg="$1"
    if dpkg -l "$pkg" 2>/dev/null | grep -q '^ii'; then
        ok "Package already installed: ${pkg}"
    else
        info "Installing package: ${pkg}"
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$pkg"
        ok "Package installed: ${pkg}"
    fi
}

# Install multiple packages at once.
# Usage: ensure_pkgs pkg1 pkg2 pkg3 ...
ensure_pkgs() {
    local missing=()
    for pkg in "$@"; do
        if ! dpkg -l "$pkg" 2>/dev/null | grep -q '^ii'; then
            missing+=("$pkg")
        fi
    done

    if [ ${#missing[@]} -eq 0 ]; then
        ok "All packages already installed"
    else
        info "Installing packages: ${missing[*]}"
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
        ok "Packages installed: ${missing[*]}"
    fi
}

# ─── Directory management ──────────────────────────────────────────────────────
# Create a directory with the specified owner and permissions if it doesn't exist.
# Usage: ensure_dir "/path/to/dir" [owner:group] [permissions]
ensure_dir() {
    local path="$1"
    local owner="${2:-root:root}"
    local perms="${3:-755}"

    if [ ! -d "$path" ]; then
        mkdir -p "$path"
        chown "$owner" "$path"
        chmod "$perms" "$path"
        ok "Created directory: ${path}"
    else
        ok "Directory already exists: ${path}"
    fi
}

# ─── Systemd service management ────────────────────────────────────────────────
# Create a systemd service unit file (but do NOT enable or start it).
# Usage: create_systemd_service "service-name" "Description" "/path/to/executable" "/path/to/config" "/path/to/working/dir"
create_systemd_service() {
    local name="$1"
    local description="$2"
    local exec_path="$3"
    local config_path="$4"
    local working_dir="$5"
    local unit_file="/etc/systemd/system/${name}.service"

    if [ -f "$unit_file" ]; then
        ok "Systemd service file already exists: ${unit_file}"
        return 0
    fi

    cat > "$unit_file" << EOF
[Unit]
Description=${description}
After=network.target bluetooth.target
Wants=bluetooth.target

[Service]
Type=simple
ExecStart=${exec_path} --config ${config_path}
WorkingDirectory=${working_dir}
Restart=on-failure
RestartSec=5
User=root
Group=root
StandardOutput=journal
StandardError=journal
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

    chmod 644 "$unit_file"
    systemctl daemon-reload
    ok "Created systemd service file: ${unit_file} (disabled — enable with: systemctl enable ${name})"
}

# ─── Bluetooth ─────────────────────────────────────────────────────────────────
# Ensure Bluetooth service is running.
ensure_bluetooth() {
    section "Bluetooth"

    if systemctl is-active --quiet bluetooth 2>/dev/null; then
        ok "Bluetooth service is already running"
    else
        info "Starting Bluetooth service..."
        systemctl start bluetooth 2>/dev/null || true
        systemctl enable bluetooth 2>/dev/null || true
        if systemctl is-active --quiet bluetooth 2>/dev/null; then
            ok "Bluetooth service started"
        else
            warn "Could not start Bluetooth service (may not be available on this board)"
        fi
    fi

    # Check for Bluetooth hardware
    if hciconfig dev 2>/dev/null | grep -q hci; then
        ok "Bluetooth hardware detected"
    else
        warn "No Bluetooth hardware detected (hciconfig shows no devices)"
    fi
}

# ─── V4L2 device verification ──────────────────────────────────────────────────
# List all V4L2 video devices and their names.
verify_v4l2() {
    section "V4L2 Devices"

    if ! command -v v4l2-ctl &>/dev/null; then
        info "v4l2-ctl not installed, installing v4l-utils..."
        ensure_pkg v4l-utils
    fi

    local found=false
    for dev in /dev/video*; do
        if [ -c "$dev" ]; then
            local name=""
            local idx="${dev#/dev/video}"
            if [ -f "/sys/class/video4linux/video${idx}/name" ]; then
                name="$(cat "/sys/class/video4linux/video${idx}/name")"
            fi
            echo "  ${dev}: ${name}"
            found=true
        fi
    done

    if [ "$found" = false ]; then
        warn "No V4L2 video devices found"
    else
        ok "V4L2 devices listed above"
    fi
}

# ─── Kernel module verification ────────────────────────────────────────────────
# Check if a kernel module is loaded.
# Usage: check_kernel_module "module_name"
check_kernel_module() {
    local module="$1"
    if lsmod | grep -q "^${module}"; then
        ok "Kernel module loaded: ${module}"
        return 0
    else
        warn "Kernel module not loaded: ${module}"
        return 1
    fi
}

# ─── Device tree overlay management ────────────────────────────────────────────
# Check if a device tree overlay is applied.
# Usage: check_dt_overlay "overlay-name"
check_dt_overlay() {
    local overlay="$1"
    if [ -d "/sys/kernel/config/device-tree/overlays" ]; then
        for d in /sys/kernel/config/device-tree/overlays/*/; do
            if [ -f "${d}status" ] && [ "$(cat "${d}status")" = "applied" ]; then
                local name
                name="$(basename "$d")"
                if [ "$name" = "$overlay" ] || [ -f "${d}path" ] && grep -q "$overlay" "${d}path" 2>/dev/null; then
                    ok "Device tree overlay applied: ${overlay}"
                    return 0
                fi
            fi
        done
    fi

    # Alternative: check /proc/device-tree for the overlay
    if [ -d "/proc/device-tree" ]; then
        local found_overlay
        found_overlay=$(find /proc/device-tree -name "*${overlay}*" 2>/dev/null | head -1)
        if [ -n "$found_overlay" ]; then
            ok "Device tree overlay found: ${overlay}"
            return 0
        fi
    fi

    warn "Device tree overlay not detected: ${overlay}"
    return 1
}

# ─── Media pipeline verification ───────────────────────────────────────────────
# Check media controller devices and their pipeline topology.
verify_media_pipeline() {
    section "Media Pipeline"

    if ! command -v media-ctl &>/dev/null; then
        info "media-ctl not installed, installing v4l-utils..."
        ensure_pkg v4l-utils
    fi

    local found=false
    for dev in /dev/media*; do
        if [ -c "$dev" ]; then
            echo "  ${dev}:"
            media-ctl -d "$dev" -p 2>/dev/null | while IFS= read -r line; do
                echo "    ${line}"
            done
            found=true
        fi
    done

    if [ "$found" = false ]; then
        warn "No media controller devices found"
    else
        ok "Media devices listed above"
    fi
}

# ─── System info ───────────────────────────────────────────────────────────────
print_system_info() {
    section "System Information"
    echo "  Hostname:  $(hostname)"
    echo "  Kernel:    $(uname -r)"
    echo "  Arch:      $(uname -m)"
    echo "  OS:        $(. /etc/os-release && echo "${PRETTY_NAME:-unknown}")"
    echo "  Memory:    $(free -h | awk '/^Mem:/ {print $2}')"
    echo "  Disk:      $(df -h / | awk 'NR==2 {print $4 " free of " $2}')"
}

# ─── Camera overlay validation ─────────────────────────────────────────────────
# Detect which camera overlay is currently active (enabled) in /boot/dtbo/.
# Returns the overlay filename (without .dtbo) or empty string if none found.
# Usage: detect_active_camera_overlay
detect_active_camera_overlay() {
    local dtbo_dir="/boot/dtbo"
    local overlay_name=""

    if [ ! -d "$dtbo_dir" ]; then
        echo ""
        return 1
    fi

    for dtbo in "$dtbo_dir"/rock-3c-*.dtbo; do
        if [ -f "$dtbo" ]; then
            local name
            name="$(basename "$dtbo" .dtbo)"
            # Check if it's a camera overlay
            if echo "$name" | grep -qE 'camera|imx219|imx415|ov5647|v1p3|v2'; then
                if [ -n "$overlay_name" ]; then
                    # Multiple camera overlays active — this is a problem
                    echo "multiple"
                    return 2
                fi
                overlay_name="$name"
            fi
        fi
    done

    echo "$overlay_name"
    [ -n "$overlay_name" ]
}

# Map a camera model name (from config) to the expected DTBO overlay filename.
# Usage: model_to_overlay "ov5647"  →  "rock-3c-rpi-camera-v1p3"
model_to_overlay() {
    local model="$1"
    case "$model" in
        ov5647) echo "rock-3c-rpi-camera-v1p3" ;;
        imx219) echo "rock-3c-rpi-camera-v2" ;;
        imx415) echo "rock-3c-radxa-camera-8m" ;;
        *)      echo "" ;;
    esac
}

# Map a DTBO overlay filename to the camera model name (for config).
# Usage: overlay_to_model "rock-3c-rpi-camera-v1p3"  →  "ov5647"
overlay_to_model() {
    local overlay="$1"
    case "$overlay" in
        rock-3c-rpi-camera-v1p3)  echo "ov5647" ;;
        rock-3c-rpi-camera-v2)    echo "imx219" ;;
        rock-3c-radxa-camera-8m)  echo "imx415" ;;
        *)                        echo "" ;;
    esac
}

# Read the camera model from a YAML config file.
# Usage: read_camera_model "/etc/ai-trap/config.yaml"
# Returns the model string or empty string if not found.
read_camera_model() {
    local config_path="$1"
    if [ ! -f "$config_path" ]; then
        echo ""
        return 1
    fi
    grep -E '^\s*model:' "$config_path" 2>/dev/null | \
        sed 's/.*model:\s*"\{0,1\}\([^"]*\)"\{0,1\}/\1/' | \
        head -1
}

# Validate that the active camera overlay matches the config file's camera model.
# Usage: validate_camera_overlay "/etc/ai-trap/config.yaml"
validate_camera_overlay() {
    local config_path="$1"
    local active_overlay
    active_overlay="$(detect_active_camera_overlay)"
    local detect_ret=$?

    echo ""
    section "Camera Config Validation"

    if [ $detect_ret -eq 2 ]; then
        warn "Multiple camera overlays are active — only one should be enabled"
        warn "Check /boot/dtbo/ and disable all but one camera overlay"
        return 1
    fi

    if [ -z "$active_overlay" ]; then
        warn "No camera overlay is currently active"
        return 1
    fi

    local active_model
    active_model="$(overlay_to_model "$active_overlay")"
    echo "  Active overlay: ${active_overlay}.dtbo"
    echo "  Active model:   ${active_model:-unknown}"

    if [ ! -f "$config_path" ]; then
        warn "Config file not found: ${config_path}"
        warn "Cannot validate camera model against config"
        return 1
    fi

    local config_model
    config_model="$(read_camera_model "$config_path")"
    if [ -z "$config_model" ]; then
        warn "Could not read camera model from config: ${config_path}"
        return 1
    fi

    echo "  Config model:   ${config_model}"

    if [ "$active_model" = "$config_model" ]; then
        ok "Camera overlay matches config (${config_model})"
        return 0
    fi

    # Mismatch — suggest the correct overlay
    local expected_overlay
    expected_overlay="$(model_to_overlay "$config_model")"
    echo ""
    warn "Camera overlay mismatch!"
    warn "  Config expects model \"${config_model}\""
    warn "  Active overlay is for \"${active_model:-unknown}\""
    if [ -n "$expected_overlay" ]; then
        echo ""
        warn "To fix, enable the correct overlay:"
        warn "  sudo mv /boot/dtbo/${active_overlay}.dtbo /boot/dtbo/${active_overlay}.dtbo.disabled"
        warn "  sudo mv /boot/dtbo/${expected_overlay}.dtbo.disabled /boot/dtbo/${expected_overlay}.dtbo"
        warn "  sudo u-boot-update && sudo reboot"
    fi
    return 1
}

# ─── Summary ───────────────────────────────────────────────────────────────────
print_summary() {
    local platform="$1"
    local config_src="$2"
    local binary_src="$3"

    section "Setup Complete"
    echo "  Platform: ${platform}"
    echo ""
    echo "  ── What was done ──"
    echo "  ✓ Runtime dependencies installed (including NPU runtime)"
    echo "  ✓ Directory structure created"
    echo "  ✓ Systemd service file created (disabled)"
    echo "  ✓ Camera overlay enabled"
    echo "  ✓ NPU overlay enabled (rk3568-npu-enable.dtbo)"
    echo "  ✓ Bluetooth service ensured"
    echo ""
    echo "  ── Manual steps required ──"
    echo "  1. Copy the trap binary:"
    echo "       scp ${binary_src} root@<board-ip>:/usr/local/bin/ai-trap-detection"
    echo ""
    echo "  2. Copy the config file:"
    echo "       scp ${config_src} root@<board-ip>:/etc/ai-trap/config.yaml"
    echo ""
    echo "  3. Copy model files:"
    echo "       scp <model-file> root@<board-ip>:/usr/share/ai-trap/models/"
    echo ""
    echo "  4. Enable and start the service:"
    echo "       ssh root@<board-ip> systemctl enable ai-trap-detection"
    echo "       ssh root@<board-ip> systemctl start ai-trap-detection"
    echo ""
    echo "  5. Check status:"
    echo "       ssh root@<board-ip> systemctl status ai-trap-detection"
    echo "       ssh root@<board-ip> journalctl -u ai-trap-detection -f"
    echo ""
}
