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
Generate a PDF diagram showing all actors in the detection trap pipeline
with their Ramen port connections and event flow.

Output: actor_diagram.pdf
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import numpy as np

# ============================================================================
# Layout Configuration
# ============================================================================

# Each actor is a box with:
#   - Name (bold, centered)
#   - Input ports on the left (Pushable / Puller)
#   - Output ports on the right (Pusher / Pullable)
#   - Internal state / notes below name

# We'll use a grid-based layout with explicit positions

# Color scheme
COLOR_PIPELINE = '#E3F2FD'      # Light blue - pipeline actors
COLOR_SESSION  = '#FFF3E0'      # Light orange - session/storage
COLOR_HTTP     = '#E8F5E9'      # Light green - HTTP/SSE
COLOR_EVENT    = '#F3E5F5'      # Light purple - event publisher
COLOR_HAL      = '#FCE4EC'      # Light pink - HAL interfaces
COLOR_BORDER   = '#333333'
COLOR_TEXT     = '#1a1a1a'
COLOR_PORT     = '#666666'
COLOR_ARROW    = '#555555'
COLOR_EVENT_ARROW = '#9C27B0'   # Purple for event flow
COLOR_SSE_ARROW = '#4CAF50'     # Green for SSE flow

# ============================================================================
# Define Actors and Connections
# ============================================================================

actors = [
    # (name, x, y, width, height, color, ports_left, ports_right, notes)
    {
        'name': 'CameraActor',
        'x': 1, 'y': 5, 'w': 2.2, 'h': 2.0,
        'color': COLOR_PIPELINE,
        'ports_left': [],
        'ports_right': [
            ('out_frame_lores', 'Pusher<Frame>'),
            ('out_frame_full', 'Pusher<Frame>'),
            ('out_frame_medium', 'Pusher<Frame>'),
        ],
        'notes': 'tick() captures\nframe at 3 resolutions',
    },
    {
        'name': 'InferenceActor',
        'x': 4.5, 'y': 5, 'w': 2.2, 'h': 1.6,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_frame', 'Pushable<Frame>'),
        ],
        'ports_right': [
            ('out_detections', 'Pusher<Detections>'),
        ],
        'notes': 'Wraps IInferenceHAL\n(RKNN / Native)',
    },
    {
        'name': 'TrackerActor',
        'x': 8, 'y': 5, 'w': 2.2, 'h': 1.6,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_detections', 'Pushable<Detections>'),
        ],
        'ports_right': [
            ('out_tracked', 'Pusher<TrackedObject[]>'),
        ],
        'notes': 'ByteTracker\nstable track IDs',
    },
    {
        'name': 'DecisionActor',
        'x': 11.5, 'y': 6.5, 'w': 2.0, 'h': 1.4,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_tracked', 'Pushable<TrackedObject[]>'),
        ],
        'ports_right': [],
        'notes': 'Gate: confidence\ncooldown, whitelist',
    },
    {
        'name': 'CropperActor',
        'x': 11.5, 'y': 4, 'w': 2.2, 'h': 1.8,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_tracked', 'Pushable<TrackedObject[]>'),
            ('in_frame_full', 'Pushable<Frame>'),
        ],
        'ports_right': [
            ('out_crops', 'Pusher<JpegCrop[]>'),
        ],
        'notes': 'Crops detections\nfrom full-res frame',
    },
    {
        'name': 'OverlayActor',
        'x': 8, 'y': 2.5, 'w': 2.2, 'h': 1.6,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_frame', 'Pushable<Frame>'),
            ('in_tracked', 'Pushable<TrackedObject[]>'),
        ],
        'ports_right': [
            ('out_frame', 'Pusher<Frame>'),
        ],
        'notes': 'Draws bounding\nboxes on frame',
    },
    {
        'name': 'MjpegBridgeActor',
        'x': 11.5, 'y': 2.5, 'w': 2.2, 'h': 1.4,
        'color': COLOR_PIPELINE,
        'ports_left': [
            ('in_frame', 'Pushable<Frame>'),
        ],
        'ports_right': [],
        'notes': 'JPEG encode\n+ ring buffer',
    },
    {
        'name': 'SessionActor',
        'x': 15, 'y': 4.5, 'w': 2.4, 'h': 2.0,
        'color': COLOR_SESSION,
        'ports_left': [
            ('in_crops', 'Pushable<JpegCrop[]>'),
        ],
        'ports_right': [],
        'notes': 'SQLite storage\nsession lifecycle\non_saved callback',
    },
    {
        'name': 'HttpSseActor',
        'x': 15, 'y': 1.5, 'w': 2.4, 'h': 1.8,
        'color': COLOR_HTTP,
        'ports_left': [],
        'ports_right': [],
        'notes': 'CivetWeb server\nREST + SSE + MJPEG\nport 8080',
    },
    {
        'name': 'HttpHandlerActor',
        'x': 15, 'y': 7.5, 'w': 2.4, 'h': 1.8,
        'color': COLOR_HTTP,
        'ports_left': [],
        'ports_right': [],
        'notes': 'REST API handler\nOpenAPI endpoints\npublishes events',
    },
    {
        'name': 'EventPublisherActor',
        'x': 18.5, 'y': 4.5, 'w': 2.4, 'h': 1.6,
        'color': COLOR_EVENT,
        'ports_left': [
            ('in_event', 'Pushable<Event>'),
        ],
        'ports_right': [],
        'notes': 'Generic dispatch\nSSE backend\n(future: MQTT, BLE)',
    },
]

# Connections: (from_actor, from_port, to_actor, to_port, style, color, label)
# style: 'ramen' = solid arrow, 'event' = dashed, 'sse' = dotted
connections = [
    # Pipeline data flow (Ramen port wiring)
    ('CameraActor', 'out_frame_lores', 'InferenceActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
    ('InferenceActor', 'out_detections', 'TrackerActor', 'in_detections', 'ramen', COLOR_ARROW, ''),
    ('TrackerActor', 'out_tracked', 'DecisionActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
    ('TrackerActor', 'out_tracked', 'CropperActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
    ('CameraActor', 'out_frame_full', 'CropperActor', 'in_frame_full', 'ramen', COLOR_ARROW, ''),
    ('CameraActor', 'out_frame_medium', 'OverlayActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
    ('TrackerActor', 'out_tracked', 'OverlayActor', 'in_tracked', 'ramen', COLOR_ARROW, ''),
    ('OverlayActor', 'out_frame', 'MjpegBridgeActor', 'in_frame', 'ramen', COLOR_ARROW, ''),
    ('CropperActor', 'out_crops', 'SessionActor', 'in_crops', 'ramen', COLOR_ARROW, ''),

    # Event flow (dashed purple)
    ('SessionActor', None, 'EventPublisherActor', 'in_event', 'event', COLOR_EVENT_ARROW, 'on_saved callback\n→ Event'),
    ('HttpHandlerActor', None, 'EventPublisherActor', 'in_event', 'event', COLOR_EVENT_ARROW, 'provision, session\nstart/stop → Event'),

    # SSE flow (dotted green)
    ('EventPublisherActor', None, 'HttpSseActor', None, 'sse', COLOR_SSE_ARROW, 'SseEvent'),
]

# ============================================================================
# Build the Diagram
# ============================================================================

fig, ax = plt.subplots(1, 1, figsize=(24, 12))
ax.set_xlim(0, 22)
ax.set_ylim(0, 10)
ax.set_aspect('equal')
ax.axis('off')

# Title
ax.text(11, 9.5, 'AI Camera Trap — Detection Pipeline Actor Diagram',
        fontsize=18, fontweight='bold', ha='center', va='center',
        fontfamily='sans-serif')

# Subtitle
ax.text(11, 9.0, 'Ramen Actor Model — Arrows show control flow direction (→ = push, ← = pull)',
        fontsize=11, ha='center', va='center', color='#666666',
        fontfamily='sans-serif', fontstyle='italic')

# ── Draw actors ──────────────────────────────────────────────────────────────

for actor in actors:
    x, y = actor['x'], actor['y']
    w, h = actor['w'], actor['h']

    # Box with rounded corners
    box = FancyBboxPatch((x, y), w, h,
                          boxstyle="round,pad=0.15",
                          facecolor=actor['color'],
                          edgecolor=COLOR_BORDER,
                          linewidth=1.5,
                          zorder=2)
    ax.add_patch(box)

    # Actor name
    ax.text(x + w/2, y + h - 0.35, actor['name'],
            fontsize=11, fontweight='bold', ha='center', va='center',
            color=COLOR_TEXT, zorder=3)

    # Notes
    ax.text(x + w/2, y + h/2 - 0.2, actor['notes'],
            fontsize=7.5, ha='center', va='center',
            color='#555555', zorder=3,
            fontfamily='monospace')

    # Left ports (inputs)
    for i, (port_name, port_type) in enumerate(actor['ports_left']):
        py = y + h - 0.6 - i * 0.35
        # Port circle
        circle = plt.Circle((x, py), 0.08, color='#2E7D32', zorder=3)
        ax.add_patch(circle)
        # Port label
        ax.text(x - 0.15, py, f'◀ {port_name}',
                fontsize=6.5, ha='right', va='center',
                color=COLOR_PORT, zorder=3, fontfamily='monospace')
        # Type label
        ax.text(x - 0.15, py - 0.15, port_type,
                fontsize=5.5, ha='right', va='center',
                color='#999999', zorder=3, fontfamily='monospace')

    # Right ports (outputs)
    for i, (port_name, port_type) in enumerate(actor['ports_right']):
        py = y + h - 0.6 - i * 0.35
        circle = plt.Circle((x + w, py), 0.08, color='#1565C0', zorder=3)
        ax.add_patch(circle)
        ax.text(x + w + 0.15, py, f'{port_name} ▶',
                fontsize=6.5, ha='left', va='center',
                color=COLOR_PORT, zorder=3, fontfamily='monospace')
        ax.text(x + w + 0.15, py - 0.15, port_type,
                fontsize=5.5, ha='left', va='center',
                color='#999999', zorder=3, fontfamily='monospace')

# ── Draw connections ─────────────────────────────────────────────────────────

def get_port_position(actor_name, port_name, is_input):
    """Get the (x, y) position of a port on an actor."""
    for actor in actors:
        if actor['name'] == actor_name:
            x, y, w, h = actor['x'], actor['y'], actor['w'], actor['h']
            ports = actor['ports_left'] if is_input else actor['ports_right']
            for i, (pn, _) in enumerate(ports):
                if pn == port_name:
                    py = y + h - 0.6 - i * 0.35
                    if is_input:
                        return (x, py)
                    else:
                        return (x + w, py)
            # If port not found, return center of left/right edge
            if is_input:
                return (x, y + h/2)
            else:
                return (x + w, y + h/2)
    return (0, 0)

for conn in connections:
    from_actor, from_port, to_actor, to_port, style, color, label = conn

    if style == 'ramen':
        # Ramen port wiring: from output port to input port
        start = get_port_position(from_actor, from_port, is_input=False)
        end = get_port_position(to_actor, to_port, is_input=True)
        arrow_style = '->'
        line_style = 'solid'
        lw = 1.5
    elif style == 'event':
        # Event flow: from actor center-right to event publisher input
        for a in actors:
            if a['name'] == from_actor:
                start = (a['x'] + a['w'], a['y'] + a['h']/2)
                break
        for a in actors:
            if a['name'] == to_actor:
                end = (a['x'], a['y'] + a['h']/2)
                break
        arrow_style = '->'
        line_style = 'dashed'
        lw = 1.5
    elif style == 'sse':
        # SSE flow: from event publisher to HTTP SSE actor
        for a in actors:
            if a['name'] == from_actor:
                start = (a['x'] + a['w']/2, a['y'])
                break
        for a in actors:
            if a['name'] == to_actor:
                end = (a['x'] + a['w']/2, a['y'] + a['h'])
                break
        arrow_style = '->'
        line_style = 'dotted'
        lw = 2.0

    # Draw the arrow
    arrow = FancyArrowPatch(start, end,
                            arrowstyle=arrow_style,
                            linestyle=line_style,
                            color=color,
                            linewidth=lw,
                            connectionstyle='arc3,rad=0.1',
                            zorder=1)
    ax.add_patch(arrow)

    # Label
    if label:
        mid = ((start[0] + end[0])/2, (start[1] + end[1])/2)
        ax.text(mid[0], mid[1] + 0.2, label,
                fontsize=7, ha='center', va='bottom',
                color=color, zorder=4,
                fontfamily='monospace',
                bbox=dict(boxstyle='round,pad=0.1',
                          facecolor='white', edgecolor='none', alpha=0.8))

# ── Legend ────────────────────────────────────────────────────────────────────

legend_x = 0.5
legend_y = 0.3

# Legend box
legend_box = FancyBboxPatch((legend_x, legend_y), 6, 2.2,
                              boxstyle="round,pad=0.1",
                              facecolor='#FAFAFA',
                              edgecolor='#CCCCCC',
                              linewidth=1,
                              zorder=5)
ax.add_patch(legend_box)

ax.text(legend_x + 3, legend_y + 1.9, 'Legend',
        fontsize=10, fontweight='bold', ha='center', va='center', zorder=6)

# Ramen arrow
ax.annotate('', xy=(legend_x + 0.5, legend_y + 1.4), xytext=(legend_x + 1.8, legend_y + 1.4),
            arrowprops=dict(arrowstyle='->', color=COLOR_ARROW, lw=1.5), zorder=6)
ax.text(legend_x + 2.0, legend_y + 1.4, 'Ramen port wiring\n(push model data flow)',
        fontsize=7, va='center', zorder=6)

# Event arrow
ax.annotate('', xy=(legend_x + 0.5, legend_y + 0.9), xytext=(legend_x + 1.8, legend_y + 0.9),
            arrowprops=dict(arrowstyle='->', color=COLOR_EVENT_ARROW, lw=1.5, linestyle='dashed'), zorder=6)
ax.text(legend_x + 2.0, legend_y + 0.9, 'Event dispatch\n(EventPublisherActor)',
        fontsize=7, va='center', zorder=6)

# SSE arrow
ax.annotate('', xy=(legend_x + 0.5, legend_y + 0.4), xytext=(legend_x + 1.8, legend_y + 0.4),
            arrowprops=dict(arrowstyle='->', color=COLOR_SSE_ARROW, lw=2.0, linestyle='dotted'), zorder=6)
ax.text(legend_x + 2.0, legend_y + 0.4, 'SSE broadcast\n(HttpSseActor → clients)',
        fontsize=7, va='center', zorder=6)

# Color swatches
swatch_x = legend_x + 4.5
swatch_y = legend_y + 1.6
colors = [
    (COLOR_PIPELINE, 'Pipeline actor'),
    (COLOR_SESSION, 'Session / Storage'),
    (COLOR_HTTP, 'HTTP / SSE'),
    (COLOR_EVENT, 'Event Publisher'),
]
for i, (c, l) in enumerate(colors):
    rect = mpatches.Rectangle((swatch_x, swatch_y - i * 0.35), 0.3, 0.2,
                               facecolor=c, edgecolor=COLOR_BORDER, linewidth=0.5, zorder=6)
    ax.add_patch(rect)
    ax.text(swatch_x + 0.4, swatch_y - i * 0.35 + 0.1, l,
            fontsize=7, va='center', zorder=6)

# ── Port type indicators ──────────────────────────────────────────────────────

port_legend_x = 8
port_legend_y = 0.3

port_box = FancyBboxPatch((port_legend_x, port_legend_y), 5.5, 1.2,
                            boxstyle="round,pad=0.1",
                            facecolor='#FAFAFA',
                            edgecolor='#CCCCCC',
                            linewidth=1,
                            zorder=5)
ax.add_patch(port_box)

ax.text(port_legend_x + 2.75, port_legend_y + 0.9, 'Port Types',
        fontsize=10, fontweight='bold', ha='center', va='center', zorder=6)

# Input port
circle_in = plt.Circle((port_legend_x + 0.5, port_legend_y + 0.45), 0.08, color='#2E7D32', zorder=6)
ax.add_patch(circle_in)
ax.text(port_legend_x + 0.7, port_legend_y + 0.45, 'Input port (Pushable / Puller)',
        fontsize=7, va='center', zorder=6)

# Output port
circle_out = plt.Circle((port_legend_x + 3.5, port_legend_y + 0.45), 0.08, color='#1565C0', zorder=6)
ax.add_patch(circle_out)
ax.text(port_legend_x + 3.7, port_legend_y + 0.45, 'Output port (Pusher / Pullable)',
        fontsize=7, va='center', zorder=6)

# ── Actor Registry note ───────────────────────────────────────────────────────

reg_x = 14.5
reg_y = 0.3
reg_box = FancyBboxPatch((reg_x, reg_y), 6.5, 1.2,
                           boxstyle="round,pad=0.1",
                           facecolor='#FFF8E1',
                           edgecolor='#F9A825',
                           linewidth=1,
                           zorder=5)
ax.add_patch(reg_box)

ax.text(reg_x + 3.25, reg_y + 0.6,
        'ActorRegistry: http_server, http_handler, session_actor, event_publisher\n'
        'Pipeline actors communicate via direct Ramen port wiring (not registry)',
        fontsize=7.5, ha='center', va='center', zorder=6,
        fontfamily='monospace')

# ── Save ──────────────────────────────────────────────────────────────────────

plt.tight_layout()
output_path = '/detection/insects/yolo26n/convert/actor_diagram.pdf'
plt.savefig(output_path, format='pdf', dpi=200, bbox_inches='tight')
plt.close()

print(f"Diagram saved to: {output_path}")
print(f"Figure size: 24x12 inches")
