#!/bin/bash
#
# Deploy the Build Server to a target board as a systemd service.
#
# Usage:
#   ./deploy_build_server.sh rock-3c.local [user]
#
# This script:
# 1. Copies the build_server package to /usr/local/lib/ai-trap-build-server/
# 2. Copies the entry point script to /usr/local/bin
# 3. Installs Python dependencies system-wide (for root/systemd)
# 4. Installs and enables the systemd service (auto-start on boot)
# 5. Starts (or restarts) the build server service
#
# The build server runs as a systemd service so it:
# - Starts automatically on boot
# - Restarts automatically if it crashes
# - Can be managed with systemctl commands
# - Logs to journald
#

set -euo pipefail

HOST="${1:-rock-3c.local}"
USER="${2:-radxa}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Deploying Build Server to ${USER}@${HOST} ==="

# Step 1: Create directories on the board
echo "--- Creating directories ---"
ssh "${USER}@${HOST}" "mkdir -p ~/ai-trap-build-server/build_server"

# Step 2: Copy build_server package to temp location
echo "--- Copying build server files ---"
rsync -avz --delete \
    "${SCRIPT_DIR}/build_server/" \
    "${USER}@${HOST}:~/ai-trap-build-server/build_server/"

# Step 3: Copy entry point script
echo "--- Copying entry point ---"
rsync -avz \
    "${SCRIPT_DIR}/build_server.py" \
    "${USER}@${HOST}:~/ai-trap-build-server/"

# Step 4: Install everything to system locations via sudo
echo "--- Installing to system locations ---"
ssh "${USER}@${HOST}" "
  # Create system directories
  sudo mkdir -p /usr/local/lib/ai-trap-build-server

  # Copy package to system location
  sudo cp -r ~/ai-trap-build-server/build_server /usr/local/lib/ai-trap-build-server/

  # Copy entry point
  sudo cp ~/ai-trap-build-server/build_server.py /usr/local/bin/ai-trap-build-server
  sudo chmod +x /usr/local/bin/ai-trap-build-server

  # Install systemd unit file
  sudo cp /usr/local/lib/ai-trap-build-server/build_server/ai-trap-build-server.service /etc/systemd/system/
"

# Step 5: Install Python dependencies system-wide (service runs as root)
echo "--- Installing Python dependencies ---"
# Try with --break-system-packages first (newer pip), fall back to without (older pip)
ssh "${USER}@${HOST}" "sudo pip3 install --break-system-packages fastapi uvicorn pydantic python-multipart 2>&1 | tail -5" || \
  ssh "${USER}@${HOST}" "sudo pip3 install fastapi uvicorn pydantic python-multipart 2>&1 | tail -5"

# Step 6: Enable and start systemd service
echo "--- Enabling and starting systemd service ---"
ssh "${USER}@${HOST}" "
  sudo systemctl daemon-reload
  sudo systemctl enable ai-trap-build-server
  sudo systemctl restart ai-trap-build-server
"

# Step 7: Verify the service is running
echo "--- Verifying service ---"
sleep 3
SERVICE_STATUS=$(ssh "${USER}@${HOST}" "sudo systemctl is-active ai-trap-build-server" 2>&1 || true)
echo "Service status: ${SERVICE_STATUS}"

# Also verify the API responds
echo "--- Verifying API ---"
curl -s --max-time 5 "http://${HOST}:8081/api/v1/status" | python3 -m json.tool || echo "WARNING: Could not verify build server status"

echo ""
echo "=== Build Server deployed successfully ==="
echo "API: http://${HOST}:8081/api/v1/status"
echo ""
echo "Service management commands:"
echo "  ssh ${USER}@${HOST} 'sudo systemctl status ai-trap-build-server'"
echo "  ssh ${USER}@${HOST} 'sudo systemctl restart ai-trap-build-server'"
echo "  ssh ${USER}@${HOST} 'sudo systemctl stop ai-trap-build-server'"
echo "  ssh ${USER}@${HOST} 'sudo journalctl -u ai-trap-build-server -f'"
