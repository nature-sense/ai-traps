#!/usr/bin/env python3
"""
Generate a scene where 4 insects enter the screen a few frames apart in
different locations, wander around, and leave the screen in different locations
a few frames apart.

The scene starts with 10 empty frames and finishes with 10 empty frames,
for a total of ~500 frames.

Usage:
    python3 generate-insect-entrance-exit.py [--frames 500] [--width 1920] [--height 1080]
                                            [--output ./test_scenes/insect_entrance_exit]

Output:
    output_dir/
      frame_0000.png
      frame_0001.png
      ...
"""

import cv2
import numpy as np
import os
import argparse
import math
import random


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


def is_on_screen(x, y, width, height, margin=40):
    """Check if position (x, y) is within the visible frame area."""
    return (margin < x < width - margin) and (margin < y < height - margin)


def generate_sequence(num_frames=500, width=1920, height=1080,
                      output_dir="test_scenes/insect_entrance_exit"):
    """Generate a sequence of frames with 4 insects entering, wandering, and exiting."""

    os.makedirs(output_dir, exist_ok=True)

    # Load the 4 insect images
    print("Loading insect images...")
    insects_data = []
    for i, fname in enumerate(INSECT_FILES):
        rgba = load_insect_image(fname, target_width=120)
        insects_data.append(rgba)
        print(f"  {fname}: {rgba.shape[1]}x{rgba.shape[0]} pixels")

    bg_color = (255, 255, 255)  # white background (trap surface)

    # ─── Scene timing parameters ────────────────────────────────────────────
    empty_start_frames = 10
    empty_end_frames = 10

    # Each insect enters at a different frame (staggered)
    # Insect 0 enters at frame 10, insect 1 at frame 25, etc.
    entrance_frames = [10, 25, 40, 55]

    # Each insect exits at a different frame (staggered)
    # Insect 0 exits at frame 470, insect 1 at 455, etc.
    exit_frames = [470, 455, 440, 425]

    # Duration each insect is on screen (frames of wandering)
    wander_durations = [exit_frames[i] - entrance_frames[i] for i in range(4)]

    # ─── Define entrance and exit positions for each insect ─────────────────
    # Each insect enters from a different edge/location and exits from another
    entrance_positions = [
        (-60, height * 0.25),           # Insect 0: enters from left, upper area
        (width * 0.75, -60),            # Insect 1: enters from top, right area
        (width + 60, height * 0.65),    # Insect 2: enters from right, lower area
        (width * 0.25, height + 60),    # Insect 3: enters from bottom, left area
    ]

    exit_positions = [
        (width + 60, height * 0.35),    # Insect 0: exits right, upper area
        (width * 0.30, height + 60),    # Insect 1: exits bottom, left area
        (-60, height * 0.75),           # Insect 2: exits left, lower area
        (width * 0.70, -60),            # Insect 3: exits top, right area
    ]

    # ─── Generate wandering waypoints for each insect ───────────────────────
    # Each insect will follow a smooth path from its entrance to exit,
    # with some wandering waypoints in between.
    random.seed(42)  # reproducible

    insect_paths = []
    for i in range(4):
        num_frames_on_screen = wander_durations[i]

        # Start at entrance position
        sx, sy = entrance_positions[i]
        # End at exit position
        ex, ey = exit_positions[i]

        # Generate intermediate waypoints for wandering
        num_waypoints = random.randint(4, 7)
        waypoints = [(sx, sy)]

        for wp_idx in range(num_waypoints):
            # Random waypoint within the frame (with some margin)
            margin = 80
            wx = random.uniform(margin, width - margin)
            wy = random.uniform(margin, height - margin)
            waypoints.append((wx, wy))

        waypoints.append((ex, ey))

        # Convert waypoints to a smooth path using cubic interpolation
        # We'll use a simple approach: assign each waypoint a time value
        # and interpolate with Catmull-Rom-like smoothing.

        # Assign time values to each waypoint (0 to 1)
        num_wp = len(waypoints)
        wp_times = [j / (num_wp - 1) for j in range(num_wp)]

        # For each frame, find the position along the path
        path_x = []
        path_y = []

        for frame_idx in range(num_frames_on_screen):
            t = frame_idx / (num_frames_on_screen - 1) if num_frames_on_screen > 1 else 0

            # Find the segment we're in
            seg_idx = 0
            for j in range(num_wp - 1):
                if wp_times[j] <= t <= wp_times[j + 1]:
                    seg_idx = j
                    break

            # If at the very end, use last segment
            if t >= wp_times[-1]:
                seg_idx = num_wp - 2

            # Local t within segment
            seg_start = wp_times[seg_idx]
            seg_end = wp_times[seg_idx + 1]
            local_t = (t - seg_start) / (seg_end - seg_start) if seg_end > seg_start else 0

            # Smooth step for easing
            smooth_t = local_t * local_t * (3 - 2 * local_t)  # smoothstep

            # Get the 4 control points for Catmull-Rom interpolation
            p0 = waypoints[max(0, seg_idx - 1)]
            p1 = waypoints[seg_idx]
            p2 = waypoints[min(num_wp - 1, seg_idx + 1)]
            p3 = waypoints[min(num_wp - 1, seg_idx + 2)]

            # Catmull-Rom interpolation
            t2 = smooth_t
            t3 = t2 * smooth_t

            x = 0.5 * (
                (2 * p1[0]) +
                (-p0[0] + p2[0]) * t2 +
                (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t3 +
                (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3 * smooth_t
            )
            y = 0.5 * (
                (2 * p1[1]) +
                (-p0[1] + p2[1]) * t2 +
                (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t3 +
                (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3 * smooth_t
            )

            path_x.append(x)
            path_y.append(y)

        insect_paths.append((path_x, path_y))

    print(f"\nGenerating {num_frames} frames...")
    print(f"  Empty start frames: {empty_start_frames}")
    print(f"  Empty end frames: {empty_end_frames}")
    print(f"  Entrance frames: {entrance_frames}")
    print(f"  Exit frames: {exit_frames}")
    print(f"  Insects: {', '.join(INSECT_NAMES)}")

    for frame_idx in range(num_frames):
        img = np.full((height, width, 3), bg_color, dtype=np.uint8)

        # Determine which insects are active (on screen) for this frame
        active_insects = []
        for i in range(4):
            if entrance_frames[i] <= frame_idx < exit_frames[i]:
                # Insect is on screen
                local_idx = frame_idx - entrance_frames[i]
                path_x, path_y = insect_paths[i]
                if local_idx < len(path_x):
                    cx = path_x[local_idx]
                    cy = path_y[local_idx]

                    # Compute direction angle from trajectory
                    if local_idx < len(path_x) - 1:
                        dx = path_x[local_idx + 1] - path_x[local_idx]
                        dy = path_y[local_idx + 1] - path_y[local_idx]
                    else:
                        dx = path_x[local_idx] - path_x[local_idx - 1]
                        dy = path_y[local_idx] - path_y[local_idx - 1]

                    if abs(dx) < 0.001 and abs(dy) < 0.001:
                        angle_deg = 0.0
                    else:
                        traj_angle = math.degrees(math.atan2(dy, dx))
                        angle_deg = traj_angle - 90.0

                    active_insects.append((cx, cy, angle_deg, i))

        # Composite insects sorted by y-position for depth ordering
        sorted_insects = sorted(active_insects, key=lambda ins: ins[1])
        for cx, cy, angle, idx in sorted_insects:
            composite_insect(img, insects_data[idx], int(round(cx)), int(round(cy)), angle)

        filename = os.path.join(output_dir, f"frame_{frame_idx:04d}.png")
        cv2.imwrite(filename, img)

        if (frame_idx + 1) % 30 == 0:
            print(f"  Generated {frame_idx + 1}/{num_frames} frames")

    print(f"\nDone! {num_frames} frames written to: {output_dir}")
    print(f"  Resolution: {width}x{height}")
    print(f"  Insects: 4 entering/exiting at different times and locations")
    print(f"  Empty frames at start: {empty_start_frames}")
    print(f"  Empty frames at end: {empty_end_frames}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate insect entrance/exit test sequence for SceneCameraActor")
    parser.add_argument("--frames", type=int, default=500,
                        help="Number of frames to generate (default: 500)")
    parser.add_argument("--width", type=int, default=1920,
                        help="Frame width in pixels (default: 1920)")
    parser.add_argument("--height", type=int, default=1080,
                        help="Frame height in pixels (default: 1080)")
    parser.add_argument("--output", type=str,
                        default="test_scenes/insect_entrance_exit",
                        help="Output directory (default: test_scenes/insect_entrance_exit)")

    args = parser.parse_args()
    generate_sequence(args.frames, args.width, args.height, args.output)
