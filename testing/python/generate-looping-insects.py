#!/usr/bin/env python3
"""
Generate synthetic multi-insect test sequence with looping paths.

Creates a sequence of PNG frames showing 4 insects moving along smooth,
continuous looping paths where each insect's final position equals its
starting position. This allows the animation to loop seamlessly.

Each insect:
  - Follows a closed-loop parametric path (e.g., ellipse, figure-8, rounded rectangle)
  - Rotates to face its direction of travel
  - Has a unique path shape, size, and phase offset

Usage:
    python3 generate-looping-insects.py [--frames 300] [--width 1920] [--height 1080]
                                        [--output ./test_scenes/insect_loop_4]

Output:
    output_dir/
      frame_0000.png
      frame_0001.png
      ...

The script expects 4 insect images with transparent backgrounds:
  insect1.png, insect2.png, insect3.png, insect4.png
"""

import cv2
import numpy as np
import os
import argparse
import math


# ─── Insect image files (must have transparent backgrounds) ──────────────────
INSECT_FILES = [
    "insect1.png",
    "insect2.png",
    "insect3.png",
    "insect4.png",
]

INSECT_NAMES = [
    "Insect 1",
    "Insect 2",
    "Insect 3",
    "Insect 4",
]


def load_insect_image(path, target_width=120):
    """
    Load an insect PNG with transparency, resize preserving aspect ratio
    so the width matches target_width.
    Returns RGBA image.
    """
    rgba = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if rgba is None:
        raise FileNotFoundError(f"Could not load {path}")

    h, w = rgba.shape[:2]

    # If no alpha channel, create one (assume white = transparent)
    if rgba.shape[2] == 3:
        bgr = rgba
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        _, alpha = cv2.threshold(gray, 240, 255, cv2.THRESH_BINARY_INV)
        rgba = cv2.merge([bgr, alpha])

    # Crop to bounding box of non-transparent pixels
    alpha = rgba[:, :, 3]
    coords = cv2.findNonZero(alpha)
    if coords is not None:
        x, y, bw, bh = cv2.boundingRect(coords)
        rgba = rgba[y:y + bh, x:x + bw]

    # Resize preserving aspect ratio
    h, w = rgba.shape[:2]
    scale = target_width / w
    new_w = target_width
    new_h = int(round(h * scale))
    resized = cv2.resize(rgba, (new_w, new_h), interpolation=cv2.INTER_AREA)

    return resized


def composite_insect(frame, insect_rgba, cx, cy, angle_deg):
    """
    Place the insect image onto frame at (cx, cy), rotated by angle_deg.
    The insect is centred on (cx, cy).
    """
    h, w = insect_rgba.shape[:2]

    # Rotation matrix
    center = (w / 2, h / 2)
    rot_mat = cv2.getRotationMatrix2D(center, angle_deg, 1.0)

    # Rotate the insect image and mask
    cos_a = abs(rot_mat[0, 0])
    sin_a = abs(rot_mat[0, 1])
    new_w = int(h * sin_a + w * cos_a)
    new_h = int(h * cos_a + w * sin_a)
    rot_mat[0, 2] += new_w / 2 - center[0]
    rot_mat[1, 2] += new_h / 2 - center[1]

    insect_rotated = cv2.warpAffine(insect_rgba, rot_mat, (new_w, new_h),
                                    flags=cv2.INTER_LINEAR,
                                    borderMode=cv2.BORDER_CONSTANT,
                                    borderValue=(0, 0, 0, 0))
    mask_rotated = insect_rotated[:, :, 3]

    # Compute top-left corner for placement
    x1 = cx - new_w // 2
    y1 = cy - new_h // 2

    # Clip to frame boundaries
    x1_c = max(x1, 0)
    y1_c = max(y1, 0)
    x2_c = min(x1 + new_w, frame.shape[1])
    y2_c = min(y1 + new_h, frame.shape[0])

    # Corresponding region in the rotated insect
    bx1 = x1_c - x1
    by1 = y1_c - y1
    bx2 = bx1 + (x2_c - x1_c)
    by2 = by1 + (y2_c - y1_c)

    if bx2 <= bx1 or by2 <= by1:
        return  # fully outside frame

    # Extract the region of interest
    roi = frame[y1_c:y2_c, x1_c:x2_c]
    insect_region = insect_rotated[by1:by2, bx1:bx2, :3]
    mask_region = mask_rotated[by1:by2, bx1:bx2]

    # Blend: where mask is non-zero, use insect pixel; otherwise keep background
    mask_3ch = np.stack([mask_region] * 3, axis=-1).astype(np.float32) / 255.0
    roi_float = roi.astype(np.float32)
    insect_float = insect_region.astype(np.float32)
    blended = roi_float * (1.0 - mask_3ch) + insect_float * mask_3ch
    frame[y1_c:y2_c, x1_c:x2_c] = blended.astype(np.uint8)


def get_looping_position(t, path_type, center_x, center_y, radius_x, radius_y, phase_offset):
    """
    Compute (x, y) position along a closed-loop path at parameter t in [0, 1).

    Parameters:
        t: Normalized time parameter (0 to 1, where 1 wraps back to 0)
        path_type: Type of path ('ellipse', 'figure8', 'rounded_rect', 'clover')
        center_x, center_y: Center of the path
        radius_x, radius_y: Radii/extents of the path
        phase_offset: Angular offset to vary starting position

    Returns:
        (x, y) position on the path
    """
    angle = 2 * math.pi * t + phase_offset

    if path_type == 'ellipse':
        # Simple elliptical path
        x = center_x + radius_x * math.cos(angle)
        y = center_y + radius_y * math.sin(angle)

    elif path_type == 'figure8':
        # Figure-8 (lemniscate-like) path
        x = center_x + radius_x * math.sin(angle)
        y = center_y + radius_y * math.sin(2 * angle) * 0.6

    elif path_type == 'rounded_rect':
        # Rounded rectangle path using a superellipse
        # Using a smooth approximation with sin/cos
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        # Smooth step to approximate rounded rect
        denom = (abs(cos_a) ** 0.5 + abs(sin_a) ** 0.5) ** (1 / 0.5)
        if denom == 0:
            denom = 1
        x = center_x + radius_x * cos_a / denom
        y = center_y + radius_y * sin_a / denom

    elif path_type == 'clover':
        # 4-leaf clover path
        r = 0.5 + 0.5 * abs(math.cos(2 * angle))
        x = center_x + radius_x * r * math.cos(angle)
        y = center_y + radius_y * r * math.sin(angle)

    else:
        # Default to ellipse
        x = center_x + radius_x * math.cos(angle)
        y = center_y + radius_y * math.sin(angle)

    return x, y


def get_looping_angle(t, path_type, center_x, center_y, radius_x, radius_y, phase_offset, dt=0.001):
    """
    Compute the direction of travel angle (in degrees) at parameter t
    by numerically differentiating the position function.
    Returns angle in degrees (0° = right, 90° = down).
    """
    x1, y1 = get_looping_position(t, path_type, center_x, center_y, radius_x, radius_y, phase_offset)
    x2, y2 = get_looping_position(t + dt, path_type, center_x, center_y, radius_x, radius_y, phase_offset)
    dx = x2 - x1
    dy = y2 - y1
    traj_angle = math.degrees(math.atan2(dy, dx))
    # Insect image points upward (head at top), so subtract 90°
    return traj_angle - 90.0


def generate_sequence(num_frames=300, width=1920, height=1080,
                      output_dir="test_scenes/insect_loop_4"):
    """Generate a sequence of frames with 4 insects on looping paths."""

    os.makedirs(output_dir, exist_ok=True)

    # Load the 4 insect images
    print("Loading insect images...")
    insects_data = []
    for i, fname in enumerate(INSECT_FILES):
        rgba = load_insect_image(fname, target_width=120)
        insects_data.append(rgba)
        print(f"  {fname}: {rgba.shape[1]}x{rgba.shape[0]} pixels")

    bg_color = (255, 255, 255)  # white background (trap surface)

    # ─── Define looping paths for each insect ───────────────────────────────
    # Each insect gets a unique path type, center, size, and phase offset
    # so they all move differently but each returns to its start position.
    # All paths are constrained so no insect goes within BORDER_MARGIN px of
    # any frame edge.
    BORDER_MARGIN = 200
    insect_size = 120  # target_width used for loading

    # Helper to clamp path radii so the insect's center never goes within
    # BORDER_MARGIN pixels of any frame edge.
    def clamp_radius(cx, cy, rx, ry):
        max_rx = min(cx - BORDER_MARGIN, width - BORDER_MARGIN - cx)
        max_ry = min(cy - BORDER_MARGIN, height - BORDER_MARGIN - cy)
        return min(rx, max_rx), min(ry, max_ry)

    path_defs = [
        {
            'type': 'ellipse',
            'center_x': width * 0.25,
            'center_y': height * 0.35,
            'radius_x': width * 0.12,
            'radius_y': height * 0.10,
            'phase': 0.0,
        },
        {
            'type': 'figure8',
            'center_x': width * 0.75,
            'center_y': height * 0.35,
            'radius_x': width * 0.10,
            'radius_y': height * 0.12,
            'phase': math.pi / 4,
        },
        {
            'type': 'rounded_rect',
            'center_x': width * 0.25,
            'center_y': height * 0.65,
            'radius_x': width * 0.10,
            'radius_y': height * 0.08,
            'phase': math.pi / 2,
        },
        {
            'type': 'clover',
            'center_x': width * 0.75,
            'center_y': height * 0.65,
            'radius_x': width * 0.08,
            'radius_y': height * 0.08,
            'phase': 3 * math.pi / 4,
        },
    ]

    # Apply border margin constraint to all paths
    for pd in path_defs:
        pd['radius_x'], pd['radius_y'] = clamp_radius(
            pd['center_x'], pd['center_y'], pd['radius_x'], pd['radius_y']
        )

    print(f"\nGenerating {num_frames} frames with 4 looping insects...")
    print(f"  Insects: {', '.join(INSECT_NAMES)}")
    print(f"  Paths: ellipse, figure-8, rounded-rect, clover")
    print(f"  Each insect returns to its start position (seamless loop)")

    for frame_idx in range(num_frames):
        img = np.full((height, width, 3), bg_color, dtype=np.uint8)

        # Normalized time parameter (0 to 1, exclusive of 1 so frame 0 == frame N)
        t = frame_idx / num_frames

        # Compute positions and angles for all insects
        positions = []
        for i in range(4):
            pd = path_defs[i]
            cx, cy = get_looping_position(
                t, pd['type'], pd['center_x'], pd['center_y'],
                pd['radius_x'], pd['radius_y'], pd['phase']
            )
            angle = get_looping_angle(
                t, pd['type'], pd['center_x'], pd['center_y'],
                pd['radius_x'], pd['radius_y'], pd['phase']
            )
            positions.append((int(round(cx)), int(round(cy)), angle))

        # Composite insects sorted by y-position for depth ordering
        sorted_indices = sorted(range(4), key=lambda i: positions[i][1])
        for idx in sorted_indices:
            cx, cy, angle = positions[idx]
            composite_insect(img, insects_data[idx], cx, cy, angle)

        filename = os.path.join(output_dir, f"frame_{frame_idx:04d}.png")
        cv2.imwrite(filename, img)

        if (frame_idx + 1) % 30 == 0:
            print(f"  Generated {frame_idx + 1}/{num_frames} frames")

    print(f"\nDone! {num_frames} frames written to: {output_dir}")
    print(f"  Resolution: {width}x{height}")
    print(f"  Insects: 4 on closed-loop paths")
    print(f"  Seamless loop: frame 0 == frame {num_frames} (not generated)")
    print(f"  Each insect rotates to face its direction of travel")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate looping multi-insect test sequence for SceneCameraActor")
    parser.add_argument("--frames", type=int, default=300,
                        help="Number of frames to generate (default: 300)")
    parser.add_argument("--width", type=int, default=1920,
                        help="Frame width in pixels (default: 1920)")
    parser.add_argument("--height", type=int, default=1080,
                        help="Frame height in pixels (default: 1080)")
    parser.add_argument("--output", type=str,
                        default="test_scenes/insect_loop_4",
                        help="Output directory (default: test_scenes/insect_loop_4)")

    args = parser.parse_args()
    generate_sequence(args.frames, args.width, args.height, args.output)
