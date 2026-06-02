#!/usr/bin/env python3

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


"""
Generate a comprehensive project graph for the AI Camera Trap monorepo.

Produces a PDF showing:
  - Top-level module structure (traps/, apps/, tools/, models/)
  - Actor pipeline diagram (reusing actor_diagram logic)
  - Build & deploy architecture (MCP servers, Build Server, target boards)
  - Data flow between components

Output: project_graph.pdf
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import numpy as np
import os

# ============================================================================
# Color Scheme
# ============================================================================

COLOR_TRAPS     = '#E3F2FD'
COLOR_TOOLKIT   = '#BBDEFB'
COLOR_TARGETS   = '#90CAF9'
COLOR_APPS      = '#E8F5E9'
COLOR_TOOLS     = '#FFF3E0'
COLOR_MODELS    = '#F3E5F5'
COLOR_MCP       = '#FFE0B2'
COLOR_BUILD_SRV = '#FFCC80'
COLOR_HAL       = '#FCE4EC'
COLOR_PIPELINE  = '#E3F2FD'
COLOR_SESSION   = '#FFF3E0'
COLOR_HTTP      = '#E8F5E9'
COLOR_EVENT     = '#F3E5F5'

COLOR_BORDER    = '#333333'
COLOR_TEXT      = '#1a1a1a'
COLOR_ARROW     = '#555555'
COLOR_EVENT_ARROW = '#9C27B0'
COLOR_SSE_ARROW = '#4CAF50'
COLOR_DEPLOY    = '#FF6F00'
COLOR_BUILD     = '#1565C0'
COLOR_DATA      = '#2E7D32'

# ============================================================================
# Section 1: Top-Level Module Structure
# ============================================================================

def draw_module_structure(ax):
    x0, y0 = 0.5, 7.5
    module_w = 2.8
    module_h = 1.8
    gap = 0.3

    modules = [
        {'name': 'traps/', 'desc': 'C++ Firmware\nActor pipeline\nHAL abstraction', 'color': COLOR_TRAPS,
         'sub': [('toolkit/', 'Shared lib\n(actors, HAL)', COLOR_TOOLKIT),
                 ('targets/', 'Platform binaries\n(rock3c, cubie-a7s)', COLOR_TARGETS)]},
        {'name': 'apps/', 'desc': 'Flutter App\nTrap management\nMJPEG + SSE client', 'color': COLOR_APPS,
         'sub': [('ai_trap_manager/', 'iOS/macOS\nmanagement UI', COLOR_APPS)]},
        {'name': 'tools/', 'desc': 'Python Tools\nMCP servers\nBuild automation', 'color': COLOR_TOOLS,
         'sub': [('mcp/scripts/', 'MCP servers\nBuild scripts', COLOR_MCP)]},
        {'name': 'models/', 'desc': 'ML Models\nYOLO training\nONNX->RKNN/NBG', 'color': COLOR_MODELS,
         'sub': [('detection/insects/', 'YOLO26n\ninsect detection', COLOR_MODELS)]},
    ]

    for i, mod in enumerate(modules):
        x = x0 + i * (module_w + gap)
        y = y0
        box = FancyBboxPatch((x, y), module_w, module_h, boxstyle="round,pad=0.1",
                              facecolor=mod['color'], edgecolor=COLOR_BORDER, linewidth=2.0, zorder=2)
        ax.add_patch(box)
        ax.text(x + module_w/2, y + module_h - 0.3, mod['name'],
                fontsize=11, fontweight='bold', ha='center', va='center', color=COLOR_TEXT, zorder=3)
        ax.text(x + module_w/2, y + module_h/2 - 0.1, mod['desc'],
                fontsize=7.5, ha='center', va='center', color='#555555', zorder=3, fontfamily='monospace')
        for j, (sub_name, sub_desc, sub_color) in enumerate(mod['sub']):
            sy = y - 0.5 - j * 0.9
            sub_box = FancyBboxPatch((x + 0.2, sy), module_w - 0.4, 0.75, boxstyle="round,pad=0.05",
                                      facecolor=sub_color, edgecolor='#666666', linewidth=1.0, zorder=2)
            ax.add_patch(sub_box)
            ax.text(x + module_w/2, sy + 0.45, sub_name, fontsize=7.5, fontweight='bold',
                    ha='center', va='center', color=COLOR_TEXT, zorder=3)
            ax.text(x + module_w/2, sy + 0.15, sub_desc, fontsize=6, ha='center', va='center',
                    color='#666666', zorder=3, fontfamily='monospace')

    ax.text(x0 + (module_w * 4 + gap * 3)/2, y0 + module_h + 0.4,
            'AI Camera Trap - Monorepo Module Structure',
            fontsize=14, fontweight='bold', ha='center', va='center', color=COLOR_TEXT)
    return y0 - 0.5 - len(max(modules, key=lambda m: len(m['sub']))['sub']) * 0.9


# ============================================================================
# Section 2: Actor Pipeline Diagram
# ============================================================================

def draw_actor_pipeline(ax, y_offset):
    actors = [
        {'name': 'CameraActor', 'x': 1, 'y': y_offset, 'w': 2.2, 'h': 2.0, 'color': COLOR_PIPELINE,
         'ports_left': [], 'ports_right': [('out_frame_lores', 'Pusher<Frame>'), ('out_frame_full', 'Pusher<Frame>'), ('out_frame_medium', 'Pusher<Frame>')],
         'notes': 'tick() captures\nframe at 3 resolutions'},
        {'name': 'InferenceActor', 'x': 4.5, 'y': y_offset, 'w': 2.2, 'h': 1.6, 'color': COLOR_PIPELINE,
         'ports_left': [('in_frame', 'Pushable<Frame>')], 'ports_right': [('out_detections', 'Pusher<Detections>')],
         'notes': 'Wraps IInferenceHAL\n(RKNN / Native)'},
        {'name': 'TrackerActor', 'x': 8, 'y': y_offset, 'w': 2.2, 'h': 1.6, 'color': COLOR_PIPELINE,
         'ports_left': [('in_detections', 'Pushable<Detections>')], 'ports_right': [('out_tracked', 'Pusher<TrackedObject[]>')],
         'notes': 'ByteTracker\nstable track IDs'},
        {'name': 'DecisionActor', 'x': 11.5, 'y': y_offset + 1.5, 'w': 2.0, 'h': 1.4, 'color': COLOR_PIPELINE,
         'ports_left': [('in_tracked', 'Pushable<TrackedObject[]>')], 'ports_right': [],
         'notes': 'Gate: confidence\ncooldown, whitelist'},
        {'name': 'CropperActor', 'x': 11.5, 'y': y_offset - 1.0, 'w': 2.2, 'h': 1.8, 'color': COLOR_PIPELINE,
         'ports_left': [('in_tracked', 'Pushable<TrackedObject[]>'), ('in_frame_full', 'Pushable<Frame>')],
         'ports_right': [('out_crops', 'Pusher<JpegCrop[]>')], 'notes': 'Crops detections\nfrom full-res frame'},
        {'name': 'OverlayActor', 'x': 8, 'y': y_offset - 2.5, 'w': 2.2, 'h': 1.6, 'color': COLOR_PIPELINE,
         'ports_left': [('in_frame', 'Pushable<Frame>'), ('in_tracked', 'Pushable<TrackedObject[]>')],
         'ports_right': [('out_frame', 'Pusher<Frame>')], 'notes': 'Draws bounding\nboxes on frame'},
        {'name': 'MjpegBridgeActor', 'x': 11.5, 'y': y_offset - 2.5, 'w': 2.2, 'h': 1.4, 'color': COLOR_PIPELINE,
         'ports_left': [('in_frame', 'Pushable<Frame>')], 'ports_right': [], 'notes': 'JPEG encode\n+ ring buffer'},
        {'name': 'SessionActor', 'x': 15, 'y': y_offset - 0.5, 'w': 2.4, 'h': 2.0, 'color': COLOR_SESSION,
         'ports_left': [('in_crops', 'Pushable<JpegCrop[]>')], 'ports_right': [],
         'notes': 'SQLite storage\nsession lifecycle\non_saved callback'},
        {'name': 'HttpSseActor', 'x': 15, 'y': y_offset - 3.5, 'w': 2.4, 'h': 1.8, 'color': COLOR_HTTP,
         'ports_left': [], 'ports_right': [], 'notes': 'CivetWeb server\nREST + SSE + MJPEG\nport 8080'},
        {'name': 'HttpHandlerActor', 'x': 15, 'y': y_offset + 2.5, 'w': 2.4, 'h': 1.8, 'color': COLOR_HTTP,
         'ports_left': [], 'ports_right': [], 'notes': 'REST API handler\nOpenAPI endpoints\npublishes events'},
        {'name': 'EventPublisherActor', 'x': 18.5, 'y': y_offset - 0.5, 'w': 2.4, 'h': 1.6, 'color': COLOR_EVENT,
         'ports_left': [('in_event', 'Pushable<Event>')], 'ports_right': [],
         'notes': 'Generic dispatch\nSSE backend\n(future: MQTT, BLE)'},
    ]

    connections = [
        ('CameraActor', 'out_frame_lores', 'InferenceActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
        ('InferenceActor', 'out_detections', 'TrackerActor', 'in_detections', 'ramen', COLOR_ARROW, ''),
        ('TrackerActor', 'out_tracked', 'DecisionActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
        ('TrackerActor', 'out_tracked', 'CropperActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
        ('CameraActor', 'out_frame_full', 'CropperActor', 'in_frame_full', 'ramen', COLOR_ARROW, ''),
        ('CameraActor', 'out_frame_medium', 'OverlayActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
        ('TrackerActor', 'out_tracked', 'OverlayActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
        ('OverlayActor', 'out_frame', 'MjpegBridgeActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
        ('CropperActor', 'out_crops', 'SessionActor', 'in_crops', 'ramen', COLOR_ARROW, ''),
        ('SessionActor', None, 'EventPublisherActor', 'in_event', 'event', COLOR_EVENT_ARROW, 'on_saved callback'),
        ('HttpHandlerActor', None, 'EventPublisherActor', 'in_event', 'event', COLOR_EVENT_ARROW, 'provision, session'),
        ('EventPublisherActor', None, 'HttpSseActor', None, 'sse', COLOR_SSE_ARROW, 'SseEvent'),
    ]

    def get_port_position(actor_name, port_name, is_input):
        for actor in actors:
            if actor['name'] == actor_name:
                x, y, w, h = actor['x'], actor['y'], actor['w'], actor['h']
                ports = actor['ports_left'] if is_input else actor['ports_right']
                for i, (pn, _) in enumerate(ports):
                    if pn == port_name:
                        py = y + h - 0.6 - i * 0.35
                        return (x, py) if is_input else (x + w, py)
                return (x, y + h/2) if is_input else (x + w, y + h/2)
        return (0, 0)

    for actor in actors:
        x, y, w, h = actor['x'], actor['y'], actor['w'], actor['h']
        box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                              facecolor=actor['color'], edgecolor=COLOR_BORDER, linewidth=1.5, zorder=2)
        ax.add_patch(box)
        ax.text(x + w/2, y + h - 0.35, actor['name'], fontsize=10, fontweight='bold',
                ha='center', va='center', color=COLOR_TEXT, zorder=3)
        ax.text(x + w/2, y + h/2 - 0.2, actor['notes'], fontsize=7, ha='center', va='center',
                color='#555555', zorder=3, fontfamily='monospace')
        for i, (port_name, port_type) in enumerate(actor['ports_left']):
            py = y + h - 0.6 - i * 0.35
            ax.add_patch(plt.Circle((x, py), 0.08, color='#2E7D32', zorder=3))
            ax.text(x - 0.15, py, '< ' + port_name, fontsize=6, ha='right', va='center',
                    color='#666666', zorder=3, fontfamily='monospace')
            ax.text(x - 0.15, py - 0.15, port_type, fontsize=5, ha='right', va='center',
                    color='#999999', zorder=3, fontfamily='monospace')
        for i, (port_name, port_type) in enumerate(actor['ports_right']):
            py = y + h - 0.6 - i * 0.35
            ax.add_patch(plt.Circle((x + w, py), 0.08, color='#1565C0', zorder=3))
            ax.text(x + w + 0.15, py, port_name + ' >', fontsize=6, ha='left', va='center',
                    color='#666666', zorder=3, fontfamily='monospace')
            ax.text(x + w + 0.15, py - 0.15, port_type, fontsize=5, ha='left', va='center',
                    color='#999999', zorder=3, fontfamily='monospace')

    for conn in connections:
        from_actor, from_port, to_actor, to_port, style, color, label = conn
        if style == 'ramen':
            start = get_port_position(from_actor, from_port, is_input=False)
            end = get_port_position(to_actor, to_port, is_input=True)
            ls, lw = 'solid', 1.5
        elif style == 'event':
            for a in actors:
                if a['name'] == from_actor: start = (a['x'] + a['w'], a['y'] + a['h']/2)
                if a['name'] == to_actor: end = (a['x'], a['y'] + a['h']/2)
            ls, lw = 'dashed', 1.5
        elif style == 'sse':
            for a in actors:
                if a['name'] == from_actor: start = (a['x'] + a['w']/2, a['y'])
                if a['name'] == to_actor: end = (a['x'] + a['w']/2, a['y'] + a['h'])
            ls, lw = 'dotted', 2.0
        arrow = FancyArrowPatch(start, end, arrowstyle='->', linestyle=ls, color=color,
                                linewidth=lw, connectionstyle='arc3,rad=0.1', zorder=1)
        ax.add_patch(arrow)
        if label:
            mid = ((start[0] + end[0])/2, (start[1] + end[1])/2)
            ax.text(mid[0], mid[1] + 0.2, label, fontsize=6.5, ha='center', va='bottom',
                    color=color, zorder=4, fontfamily='monospace',
                    bbox=dict(boxstyle='round,pad=0.1', facecolor='white', edgecolor='none', alpha=0.8))

    pipeline_top = max(a['y'] + a['h'] for a in actors)
    ax.text(10, pipeline_top + 0.6, 'Detection Pipeline - Actor Model (Ramen Framework)',
            fontsize=13, fontweight='bold', ha='center', va='center', color=COLOR_TEXT)
    return min(a['y'] for a in actors) - 0.5


# ============================================================================
# Section 3: Build & Deploy Architecture
# ============================================================================

def draw_build_deploy(ax, y_offset):
    x0 = 0.5
    dev_x, dev_y = x0, y_offset
    dev_w, dev_h = 4.5, 3.5

    # Developer Machine
    dev_box = FancyBboxPatch((dev_x, dev_y), dev_w, dev_h, boxstyle="round,pad=0.1",
                              facecolor=COLOR_TOOLS, edgecolor=COLOR_BORDER, linewidth=2.0, zorder=2)
    ax.add_patch(dev_box)
    ax.text(dev_x + dev_w/2, dev_y + dev_h - 0.3, 'Developer Machine',
            fontsize=11, fontweight='bold', ha='center', va='center', zorder=3)
    ax.text(dev_x + dev_w/2, dev_y + dev_h - 0.6, '(macOS / Linux)',
            fontsize=8, ha='center', va='center', color='#666666', zorder=3)

    # MCP Servers
    mcp_x, mcp_y = dev_x + 0.3, dev_y + 0.3
    mcp_w, mcp_h = dev_w - 0.6, 1.2
    mcp_box = FancyBboxPatch((mcp_x, mcp_y), mcp_w, mcp_h, boxstyle="round,pad=0.05",
                              facecolor=COLOR_MCP, edgecolor='#E65100', linewidth=1.5, zorder=2)
    ax.add_patch(mcp_box)
    ax.text(mcp_x + mcp_w/2, mcp_y + mcp_h - 0.25, 'MCP Servers (Python)',
            fontsize=9, fontweight='bold', ha='center', va='center', zorder=3)
    ax.text(mcp_x + mcp_w/2, mcp_y + mcp_h/2 - 0.1,
            'ai-trap-build  |  trap-ops\nBuildServerClient (httpx)  |  Direct HTTP',
            fontsize=6.5, ha='center', va='center', color='#555555', zorder=3, fontfamily='monospace')

    # VS Code + Cline
    cline_x, cline_y = dev_x + 0.3, dev_y + 1.7
    cline_w, cline_h = dev_w - 0.6, 0.8
    cline_box = FancyBboxPatch((cline_x, cline_y), cline_w, cline_h, boxstyle="round,pad=0.05",
                                facecolor='#E8EAF6', edgecolor='#3F51B5', linewidth=1.5, zorder=2)
    ax.add_patch(cline_box)
    ax.text(cline_x + cline_w/2, cline_y + cline_h/2, 'VS Code + Cline (AI Assistant)',
            fontsize=9, fontweight='bold', ha='center', va='center', zorder=3)

    # Git repo
    git_x, git_y = dev_x + 0.3, dev_y + 2.7
    git_w, git_h = dev_w - 0.6, 0.6
    git_box = FancyBboxPatch((git_x, git_y), git_w, git_h, boxstyle="round,pad=0.05",
                              facecolor='#FFF8E1', edgecolor='#F9A825', linewidth=1.0, zorder=2)
    ax.add_patch(git_box)
    ax.text(git_x + git_w/2, git_y + git_h/2, 'Git Repo -> git bundle -> HTTP upload',
            fontsize=7.5, ha='center', va='center', zorder=3, fontfamily='monospace')

    # Network
    net_x, net_y = dev_x + dev_w + 0.5, y_offset + 1.0
    net_w, net_h = 1.5, 1.5
    net_box = FancyBboxPatch((net_x, net_y), net_w, net_h, boxstyle="circle",
                              facecolor='#ECEFF1', edgecolor='#78909C', linewidth=2.0, zorder=2)
    ax.add_patch(net_box)
    ax.text(net_x + net_w/2, net_y + net_h/2, 'HTTP\n(REST)', fontsize=9, fontweight='bold',
            ha='center', va='center', zorder=3, color='#546E7A')

    # Arrow: Dev -> Network
    arrow1 = FancyArrowPatch((dev_x + dev_w, dev_y + dev_h/2), (net_x, net_y + net_h/2),
                              arrowstyle='->', color=COLOR_BUILD, lw=2.0, connectionstyle='arc3,rad=0.0', zorder=1)
    ax.add_patch(arrow1)
    ax.text(dev_x + dev_w + 0.3, dev_y + dev_h/2 + 0.3, 'git bundle\nPOST /api/v1/build',
            fontsize=6.5, ha='center', va='bottom', color=COLOR_BUILD, fontfamily='monospace')

    # Target Board
    board_x, board_y = net_x + net_w + 0.5, y_offset
    board_w, board_h = 5.5, 3.5
    board_box = FancyBboxPatch((board_x, board_y), board_w, board_h, boxstyle="round,pad=0.1",
                                facecolor=COLOR_TRAPS, edgecolor=COLOR_BORDER, linewidth=2.0, zorder=2)
    ax.add_patch(board_box)
    ax.text(board_x + board_w/2, board_y + board_h - 0.3, 'Target Board (rock-3c.local)',
            fontsize=11, fontweight='bold', ha='center', va='center', zorder=3)
    ax.text(board_x + board_w/2, board_y + board_h - 0.6, 'Radxa ROCK 3C . RK3566 . 4GB RAM',
            fontsize=8, ha='center', va='center', color='#666666', zorder=3)

    # Build Server
    bs_x, bs_y = board_x + 0.3, board_y + 0.3
    bs_w, bs_h = board_w - 0.6, 1.4
    bs_box = FancyBboxPatch((bs_x, bs_y), bs_w, bs_h, boxstyle="round,pad=0.05",
                              facecolor=COLOR_BUILD_SRV, edgecolor='#E65100', linewidth=1.5, zorder=2)
    ax.add_patch(bs_box)
    ax.text(bs_x + bs_w/2, bs_y + bs_h - 0.25, 'Build Server (FastAPI)',
            fontsize=9, fontweight='bold', ha='center', va='center', zorder=3)
    ax.text(bs_x + bs_w/2, bs_y + bs_h/2 - 0.1,
            'systemd: ai-trap-build-server.service\nPort 8081 . Auto-restart on crash',
            fontsize=6.5, ha='center', va='center', color='#555555', zorder=3, fontfamily='monospace')

    # Build Manager
    bm_x, bm_y = board_x + 0.3, board_y + 1.9
    bm_w, bm_h = board_w - 0.6, 0.7
    bm_box = FancyBboxPatch((bm_x, bm_y), bm_w, bm_h, boxstyle="round,pad=0.05",
                              facecolor='#E8EAF6', edgecolor='#3F51B5', linewidth=1.0, zorder=2)
    ax.add_patch(bm_box)
    ax.text(bm_x + bm_w/2, bm_y + bm_h/2, 'Build Manager: git clone -> meson setup -> meson compile',
            fontsize=7, ha='center', va='center', zorder=3, fontfamily='monospace')

    # Trap Binary
    trap_x, trap_y = board_x + 0.3, board_y + 2.7
    trap_w, trap_h = board_w - 0.6, 0.6
    trap_box = FancyBboxPatch((trap_x, trap_y), trap_w, trap_h, boxstyle="round,pad=0.05",
                               facecolor=COLOR_TARGETS, edgecolor='#1565C0', linewidth=1.5, zorder=2)
    ax.add_patch(trap_box)
    ax.text(trap_x + trap_w/2, trap_y + trap_h/2, 'Trap Binary (rock3c-imx219) . Port 8080',
            fontsize=7.5, fontweight='bold', ha='center', va='center', zorder=3)

    # Arrow: Build Server -> Trap Binary
    arrow2 = FancyArrowPatch((bs_x + bs_w/2, bs_y + bs_h), (trap_x + trap_w/2, trap_y),
                              arrowstyle='->', color=COLOR_DEPLOY, lw=2.0, connectionstyle='arc3,rad=0.0', zorder=1)
    ax.add_patch(arrow2)
    ax.text(bs_x + bs_w/2 + 0.8, bs_y + bs_h + 0.2, 'create_runtime:\ninstall binary + model + config',
            fontsize=6.5, ha='center', va='bottom', color=COLOR_DEPLOY, fontfamily='monospace')

    # Arrow: Network -> Build Server
    arrow3 = FancyArrowPatch((net_x + net_w, net_y + net_h/2), (board_x, board_y + board_h/2),
                              arrowstyle='->', color=COLOR_BUILD, lw=2.0, connectionstyle='arc3,rad=0.0', zorder=1)
    ax.add_patch(arrow3)

    ax.text(x0 + (board_x + board_w - x0)/2, y_offset + board_h + 0.5,
            'Build & Deploy Architecture', fontsize=13, fontweight='bold', ha='center', va='center', color=COLOR_TEXT)
    return y_offset - 0.5


# ============================================================================
# Section 4: Data Flow Overview
# ============================================================================

def draw_data_flow(ax, y_offset):
    x0 = 0.5
    boxes = [
        {'name': 'Camera\n(Hardware)', 'x': x0, 'color': COLOR_HAL},
        {'name': 'V4L2 + ISP\nFrame Capture', 'x': x0 + 2.5, 'color': COLOR_PIPELINE},
        {'name': 'YOLO Inference\n(NPU: RKNN)', 'x': x0 + 5.0, 'color': COLOR_PIPELINE},
        {'name': 'ByteTracker\nMulti-Object\nTracking', 'x': x0 + 7.5, 'color': COLOR_PIPELINE},
        {'name': 'Decision\nGate Logic', 'x': x0 + 10.0, 'color': COLOR_PIPELINE},
        {'name': 'Cropper +\nJPEG Encode', 'x': x0 + 12.5, 'color': COLOR_PIPELINE},
        {'name': 'SQLite +\nDisk Storage', 'x': x0 + 15.0, 'color': COLOR_SESSION},
    ]
    box_w, box_h = 2.0, 1.5

    for i, b in enumerate(boxes):
        x, y = b['x'], y_offset
        box = FancyBboxPatch((x, y), box_w, box_h, boxstyle="round,pad=0.1",
                              facecolor=b['color'], edgecolor=COLOR_BORDER, linewidth=1.5, zorder=2)
        ax.add_patch(box)
        ax.text(x + box_w/2, y + box_h/2, b['name'], fontsize=8, ha='center', va='center',
                color=COLOR_TEXT, zorder=3, fontfamily='monospace')
        if i < len(boxes) - 1:
            next_x = boxes[i+1]['x']
            arrow = FancyArrowPatch((x + box_w, y + box_h/2), (next_x, y + box_h/2),
                                     arrowstyle='->', color=COLOR_DATA, lw=2.0, connectionstyle='arc3,rad=0.0', zorder=1)
            ax.add_patch(arrow)

    # Overlay + MJPEG path
    overlay_x, overlay_y = x0 + 7.5, y_offset - 1.8
    overlay_box = FancyBboxPatch((overlay_x, overlay_y), 2.0, 1.2, boxstyle="round,pad=0.1",
                                  facecolor=COLOR_PIPELINE, edgecolor=COLOR_BORDER, linewidth=1.5, zorder=2)
    ax.add_patch(overlay_box)
    ax.text(overlay_x + 1.0, overlay_y + 0.6, 'Overlay +\nMJPEG Stream',
            fontsize=8, ha='center', va='center', zorder=3, fontfamily='monospace')
    arrow_overlay = FancyArrowPatch((x0 + 7.5 + box_w, y_offset + box_h - 0.3),
                                     (overlay_x + box_w/2, overlay_y + box_h),
                                     arrowstyle='->', color=COLOR_DATA, lw=1.5, connectionstyle='arc3,rad=-0.3', zorder=1)
    ax.add_patch(arrow_overlay)
    ax.text(x0 + 8.5, y_offset + box_h - 0.1, 'tracked objects',
            fontsize=6, ha='center', va='bottom', color=COLOR_DATA, fontfamily='monospace')

    # HTTP SSE path
    sse_x, sse_y = x0 + 15.0, y_offset - 1.8
    sse_box = FancyBboxPatch((sse_x, sse_y), 2.0, 1.2, boxstyle="round,pad=0.1",
                              facecolor=COLOR_HTTP, edgecolor=COLOR_BORDER, linewidth=1.5, zorder=2)
    ax.add_patch(sse_box)
    ax.text(sse_x + 1.0, sse_y + 0.6, 'HTTP SSE\nEvent Stream',
            fontsize=8, ha='center', va='center', zorder=3, fontfamily='monospace')
    arrow_sse = FancyArrowPatch((x0 + 15.0 + box_w, y_offset + box_h - 0.3),
                                 (sse_x, sse_y + box_h/2),
                                 arrowstyle='->', color=COLOR_SSE_ARROW, lw=1.5, connectionstyle='arc3,rad=-0.3', zorder=1)
    ax.add_patch(arrow_sse)
    ax.text(x0 + 15.5, y_offset + box_h - 0.1, 'events',
            fontsize=6, ha='center', va='bottom', color=COLOR_SSE_ARROW, fontfamily='monospace')

    # Flutter App
    flutter_x, flutter_y = x0 + 17.5, y_offset - 1.8
    flutter_box = FancyBboxPatch((flutter_x, flutter_y), 2.0, 1.2, boxstyle="round,pad=0.1",
                                  facecolor=COLOR_APPS, edgecolor=COLOR_BORDER, linewidth=1.5, zorder=2)
    ax.add_patch(flutter_box)
    ax.text(flutter_x + 1.0, flutter_y + 0.6, 'Flutter App\n(MJPEG + SSE)',
            fontsize=8, ha='center', va='center', zorder=3, fontfamily='monospace')

    # Arrows from MJPEG and SSE to Flutter
    arrow_mjpeg_to_flutter = FancyArrowPatch((overlay_x + box_w, overlay_y + box_h/2),
                                              (flutter_x, flutter_y + box_h/2),
                                              arrowstyle='->', color=COLOR_DATA, lw=1.5,
                                              connectionstyle='arc3,rad=0.0', zorder=1)
    ax.add_patch(arrow_mjpeg_to_flutter)
    ax.text(overlay_x + box_w + 0.3, overlay_y + box_h/2 + 0.2, 'MJPEG stream',
            fontsize=6, ha='center', va='bottom', color=COLOR_DATA, fontfamily='monospace')

    arrow_sse_to_flutter = FancyArrowPatch((sse_x + box_w, sse_y + box_h/2),
                                            (flutter_x, flutter_y + box_h/2),
                                            arrowstyle='->', color=COLOR_SSE_ARROW, lw=1.5,
                                            connectionstyle='arc3,rad=0.0', zorder=1)
    ax.add_patch(arrow_sse_to_flutter)
    ax.text(sse_x + box_w + 0.3, sse_y + box_h/2 + 0.2, 'SSE events',
            fontsize=6, ha='center', va='bottom', color=COLOR_SSE_ARROW, fontfamily='monospace')

    # Section title
    ax.text(x0 + (flutter_x + box_w - x0)/2, y_offset + box_h + 0.5,
            'Data Flow Overview', fontsize=13, fontweight='bold', ha='center', va='center', color=COLOR_TEXT)
    return y_offset - 0.5


# ============================================================================
# Main
# ============================================================================

def main():
    fig, ax = plt.subplots(1, 1, figsize=(24, 28))
    ax.set_xlim(0, 22)
    ax.set_ylim(0, 28)
    ax.set_aspect('equal')
    ax.axis('off')

    # Draw all sections top to bottom
    y = draw_module_structure(ax)
    y = draw_actor_pipeline(ax, y - 1.0)
    y = draw_build_deploy(ax, y - 1.0)
    y = draw_data_flow(ax, y - 1.0)

    # Legend
    legend_x, legend_y = 0.5, 0.3
    legend_box = FancyBboxPatch((legend_x, legend_y), 20, 1.8, boxstyle="round,pad=0.1",
                                 facecolor='#FAFAFA', edgecolor='#CCCCCC', linewidth=1, zorder=5)
    ax.add_patch(legend_box)
    ax.text(legend_x + 10, legend_y + 1.6, 'Legend',
            fontsize=10, fontweight='bold', ha='center', va='center', zorder=6)

    legend_items = [
        (COLOR_PIPELINE, 'Pipeline Actor', COLOR_SESSION, 'Session/Storage'),
        (COLOR_HTTP, 'HTTP/SSE', COLOR_EVENT, 'Event Publisher'),
        (COLOR_HAL, 'HAL Interface', COLOR_APPS, 'Flutter App'),
        (COLOR_TOOLS, 'Python Tools', COLOR_MODELS, 'ML Models'),
    ]
    for i, (c1, l1, c2, l2) in enumerate(legend_items):
        col = i % 2
        row = i // 2
        lx = legend_x + 0.5 + col * 10
        ly = legend_y + 1.0 - row * 0.4
        rect1 = mpatches.Rectangle((lx, ly), 0.3, 0.2, facecolor=c1, edgecolor=COLOR_BORDER, linewidth=0.5, zorder=6)
        ax.add_patch(rect1)
        ax.text(lx + 0.4, ly + 0.1, l1, fontsize=7, va='center', zorder=6)
        rect2 = mpatches.Rectangle((lx + 3.5, ly), 0.3, 0.2, facecolor=c2, edgecolor=COLOR_BORDER, linewidth=0.5, zorder=6)
        ax.add_patch(rect2)
        ax.text(lx + 3.9, ly + 0.1, l2, fontsize=7, va='center', zorder=6)

    # Arrow legend
    arrow_lx = legend_x + 0.5
    arrow_ly = legend_y + 0.1
    ax.annotate('', xy=(arrow_lx + 0.5, arrow_ly), xytext=(arrow_lx + 1.5, arrow_ly),
                arrowprops=dict(arrowstyle='->', color=COLOR_ARROW, lw=1.5), zorder=6)
    ax.text(arrow_lx + 1.7, arrow_ly, 'Ramen port wiring (data flow)', fontsize=7, va='center', zorder=6)

    ax.annotate('', xy=(arrow_lx + 5.5, arrow_ly), xytext=(arrow_lx + 6.5, arrow_ly),
                arrowprops=dict(arrowstyle='->', color=COLOR_EVENT_ARROW, lw=1.5, linestyle='dashed'), zorder=6)
    ax.text(arrow_lx + 6.7, arrow_ly, 'Event dispatch', fontsize=7, va='center', zorder=6)

    ax.annotate('', xy=(arrow_lx + 10.5, arrow_ly), xytext=(arrow_lx + 11.5, arrow_ly),
                arrowprops=dict(arrowstyle='->', color=COLOR_SSE_ARROW, lw=2.0, linestyle='dotted'), zorder=6)
    ax.text(arrow_lx + 11.7, arrow_ly, 'SSE broadcast', fontsize=7, va='center', zorder=6)

    ax.annotate('', xy=(arrow_lx + 15.5, arrow_ly), xytext=(arrow_lx + 16.5, arrow_ly),
                arrowprops=dict(arrowstyle='->', color=COLOR_DEPLOY, lw=2.0), zorder=6)
    ax.text(arrow_lx + 16.7, arrow_ly, 'Deploy', fontsize=7, va='center', zorder=6)

    # Save
    output_dir = os.path.dirname(os.path.abspath(__file__))
    output_path = os.path.join(output_dir, 'project_graph.pdf')
    plt.tight_layout()
    plt.savefig(output_path, format='pdf', dpi=200, bbox_inches='tight')
    plt.close()
    print(f"Project graph saved to: {output_path}")
    print(f"Figure size: 24x28 inches")


if __name__ == '__main__':
    main()
