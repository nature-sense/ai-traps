#!/usr/bin/env bash

# Copyright 2026 Nature Sense
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# deploy.sh — Deploy MCP servers and Cline config to their installed locations
#
# Usage:
#   ./scripts/deploy.sh              # Deploy everything (default)
#   ./scripts/deploy.sh --servers    # Deploy only MCP server scripts
#   ./scripts/deploy.sh --config     # Deploy only Cline MCP config
#   ./scripts/deploy.sh --check      # Check what would be deployed (dry run)
#
# Installed Locations:
#   scripts/mcp-build-server.py  →  ~/.local/bin/mcp-build-server.py
#   scripts/trap-mcp-server.py   →  ~/.local/bin/trap-mcp-server.py
#   config/cline_mcp_settings.json → ~/.cline/data/settings/cline_mcp_settings.json
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Paths ──────────────────────────────────────────────────────────────────
MCP_BUILD_SRC="$REPO_ROOT/scripts/mcp-build-server.py"
MCP_TRAP_SRC="$REPO_ROOT/scripts/trap-mcp-server.py"
MCP_CONFIG_SRC="$REPO_ROOT/config/cline_mcp_settings.json"

MCP_BUILD_DST="$HOME/.local/bin/mcp-build-server.py"
MCP_TRAP_DST="$HOME/.local/bin/trap-mcp-server.py"
MCP_CONFIG_DST="$HOME/.cline/data/settings/cline_mcp_settings.json"

# ── Colors ─────────────────────────────────────────────────────────────────
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ── Helpers ────────────────────────────────────────────────────────────────
info()  { echo -e "${BLUE}ℹ${NC} $1"; }
ok()    { echo -e "${GREEN}✓${NC} $1"; }
warn()  { echo -e "${YELLOW}⚠${NC} $1"; }

deploy_file() {
    local src="$1" dst="$2" desc="$3"
    if [ ! -f "$src" ]; then
        warn "Skipping $desc — source not found: $src"
        return 1
    fi

    local dst_dir
    dst_dir="$(dirname "$dst")"
    mkdir -p "$dst_dir"

    if [ "$DRY_RUN" = true ]; then
        echo -e "  ${BLUE}→${NC} $desc: $src → $dst"
        return 0
    fi

    cp "$src" "$dst"
    chmod +x "$dst" 2>/dev/null || true
    ok "$desc deployed → $dst"
}

# ── Parse args ─────────────────────────────────────────────────────────────
DEPLOY_SERVERS=true
DEPLOY_CONFIG=true
DRY_RUN=false

if [ $# -gt 0 ]; then
    HAS_SPECIFIC=false
    for arg in "$@"; do
        case "$arg" in
            --servers) HAS_SPECIFIC=true ;;
            --config)  HAS_SPECIFIC=true ;;
            --check|--dry-run) DRY_RUN=true ;;
            --help|-h)
                echo "Usage: $0 [--servers] [--config] [--check]"
                echo ""
                echo "  (no args)  Deploy MCP servers and Cline config"
                echo "  --servers  Deploy only MCP server scripts"
                echo "  --config   Deploy only Cline MCP config"
                echo "  --check    Dry run — show what would be deployed"
                exit 0
                ;;
            *)
                echo "Unknown option: $arg"
                echo "Usage: $0 [--servers] [--config] [--check]"
                exit 1
                ;;
        esac
    done
    if [ "$HAS_SPECIFIC" = true ]; then
        DEPLOY_SERVERS=false
        DEPLOY_CONFIG=false
        for arg in "$@"; do
            case "$arg" in
                --servers) DEPLOY_SERVERS=true ;;
                --config)  DEPLOY_CONFIG=true ;;
            esac
        done
    fi
fi

# ── Deploy ─────────────────────────────────────────────────────────────────
echo ""
echo "=============================================="
echo "  AI Camera Trap — MCP Server Deploy"
echo "=============================================="
echo ""

if [ "$DRY_RUN" = true ]; then
    info "Dry run — showing what would be deployed:"
    echo ""
fi

# Deploy MCP server scripts
if [ "$DEPLOY_SERVERS" = true ]; then
    info "Deploying MCP server scripts..."
    deploy_file "$MCP_BUILD_SRC" "$MCP_BUILD_DST" "mcp-build-server.py"
    deploy_file "$MCP_TRAP_SRC" "$MCP_TRAP_DST" "trap-mcp-server.py"
    echo ""
fi

# Deploy Cline MCP config
if [ "$DEPLOY_CONFIG" = true ]; then
    info "Deploying Cline MCP config..."
    if [ "$DRY_RUN" = false ] && [ -f "$MCP_CONFIG_DST" ]; then
        backup="${MCP_CONFIG_DST}.bak.$(date +%Y%m%d-%H%M%S)"
        cp "$MCP_CONFIG_DST" "$backup"
        warn "Existing config backed up → $backup"
    fi
    deploy_file "$MCP_CONFIG_SRC" "$MCP_CONFIG_DST" "cline_mcp_settings.json"
    echo ""
fi

# Summary
if [ "$DRY_RUN" = false ]; then
    echo "──────────────────────────────────────────────"
    echo "  Installed files:"
    echo ""
    for f in "$MCP_BUILD_DST" "$MCP_TRAP_DST" "$MCP_CONFIG_DST"; do
        if [ -f "$f" ]; then
            ls -la "$f" | awk '{print "    " $1, $5, $6, $7, $8, $9}'
        fi
    done
    echo ""
    info "Deploy complete. Restart Cline to pick up changes."
else
    echo ""
    info "Dry run complete. Run without --check to deploy."
fi
echo ""
