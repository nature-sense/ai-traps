#!/usr/bin/env python3
"""
Generate synthetic insect test sequences for SceneCameraActor.

Creates a sequence of PNG frames showing a bee walking across the field of
view with a wavy (sinusoidal) trajectory. The bee image is loaded from
bee.png, resized to insect scale, and rotated to match its direction of motion.

Usage:
    python3 generate-insect.py [--frames 60] [--width 1920] [--height 1080]
                              [--output ./test_scenes/my_scene]

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


def load_bee_image(path="bee.png", target_width=30):
    """
    Load bee.png, extract the bee from its black background,
    and resize it so the width matches target_width (preserving aspect ratio).
    Returns (bee_rgba, bee_mask) where bee_rgba is a 4-channel image with
    transparency, and bee_mask is the binary mask.
    """
    bee_bgr = cv2.imread(path)
    if bee_bgr is None:
        raise FileNotFoundError(f"Could not load {path}")

    # Create mask: pixels brighter than a threshold are the bee
    gray = cv2.cvtColor(bee_bgr, cv2.COLOR_BGR2GRAY)
    _, mask = cv2.threshold(gray, 20, 255, cv2.THRESH_BINARY)

    # Crop to bounding box of the bee
    coords = cv2.findNonZero(mask)
    x, y, w, h = cv2.boundingRect(coords)
    bee_cropped = bee_bgr[y:y + h, x:x + w]
    mask_cropped = mask[y:y + h, x:x + w]

    # Resize preserving aspect ratio
    scale = target_width / w
    new_w = target_width
    new_h = int(round(h * scale))
    bee_resized = cv2.resize(bee_cropped, (new_w, new_h),
                             interpolation=cv2.INTER_AREA)
    mask_resized = cv2.resize(mask_cropped, (new_w, new_h),
                              interpolation=cv2.INTER_AREA)

    # Convert to 4-channel RGBA (alpha from mask)
    bee_rgba = cv2.cvtColor(bee_resized, cv2.COLOR_BGR2BGRA)
    bee_rgba[:, :, 3] = mask_resized

    return bee_rgba, mask_resized


def composite_bee(frame, bee_rgba, bee_mask, cx, cy, angle_deg):
    """
    Place the bee image onto frame at (cx, cy), rotated by angle_deg.
    The bee is centred on (cx, cy).
    """
    h, w = bee_rgba.shape[:2]

    # Rotation matrix
    center = (w / 2, h / 2)
    rot_mat = cv2.getRotationMatrix2D(center, angle_deg, 1.0)

    # Rotate the bee image and mask
    cos_a = abs(rot_mat[0, 0])
    sin_a = abs(rot_mat[0, 1])
    new_w = int(h * sin_a + w * cos_a)
    new_h = int(h * cos_a + w * sin_a)
    rot_mat[0, 2] += new_w / 2 - center[0]
    rot_mat[1, 2] += new_h / 2 - center[1]

    bee_rotated = cv2.warpAffine(bee_rgba, rot_mat, (new_w, new_h),
                                 flags=cv2.INTER_LINEAR,
                                 borderMode=cv2.BORDER_CONSTANT,
                                 borderValue=(0, 0, 0, 0))
    mask_rotated = bee_rotated[:, :, 3]

    # Compute top-left corner for placement
    x1 = cx - new_w // 2
    y1 = cy - new_h // 2

    # Clip to frame boundaries
    x1_c = max(x1, 0)
    y1_c = max(y1, 0)
    x2_c = min(x1 + new_w, frame.shape[1])
    y2_c = min(y1 + new_h, frame.shape[0])

    # Corresponding region in the rotated bee
    bx1 = x1_c - x1
    by1 = y1_c - y1
    bx2 = bx1 + (x2_c - x1_c)
    by2 = by1 + (y2_c - y1_c)

    if bx2 <= bx1 or by2 <= by1:
        return  # fully outside frame

    # Extract the region of interest
    roi = frame[y1_c:y2_c, x1_c:x2_c]
    bee_region = bee_rotated[by1:by2, bx1:bx2, :3]
    mask_region = mask_rotated[by1:by2, bx1:bx2]

    # Blend: where mask is non-zero, use bee pixel; otherwise keep background
    mask_3ch = np.stack([mask_region] * 3, axis=-1).astype(np.float32) / 255.0
    roi_float = roi.astype(np.float32)
    bee_float = bee_region.astype(np.float32)
    blended = roi_float * (1.0 - mask_3ch) + bee_float * mask_3ch
    frame[y1_c:y2_c, x1_c:x2_c] = blended.astype(np.uint8)


def generate_sequence(num_frames=180, width=1920, height=1080,
                      output_dir="test_scenes/insect_walk",
                      bee_path="bee.png"):
    """Generate a sequence of frames with a bee walking across the view."""

    os.makedirs(output_dir, exist_ok=True)

    # Load and prepare the bee image
    print(f"Loading bee image from {bee_path}...")
    bee_rgba, bee_mask = load_bee_image(bee_path, target_width=150)
    print(f"  Bee resized to {bee_rgba.shape[1]}x{bee_rgba.shape[0]} pixels")

    bg_color = (255, 255, 255)  # white background (trap surface)

    # Trajectory: linear x + sinusoidal y with slight upward drift
    t = np.linspace(0, 1, num_frames)
    margin = 40
    x_positions = margin + t * (width - 2 * margin)
    y_positions = (height // 2
                   + t * (-height * 0.15)          # slight upward drift
                   + 100 * np.sin(2 * np.pi * 3 * t)  # wavy motion (larger amplitude)
                   + 5 * np.random.randn(num_frames))  # jitter

    # Clamp y to stay within frame
    y_positions = np.clip(y_positions, margin, height - margin)

    for i in range(num_frames):
        img = np.full((height, width, 3), bg_color, dtype=np.uint8)

        cx = int(x_positions[i])
        cy = int(y_positions[i])

        # Compute direction angle from trajectory (degrees)
        if i < num_frames - 1:
            dx = x_positions[i + 1] - x_positions[i]
            dy = y_positions[i + 1] - y_positions[i]
        else:
            dx = x_positions[i] - x_positions[i - 1]
            dy = y_positions[i] - y_positions[i - 1]
        # Trajectory angle: 0° = right, 90° = down
        traj_angle = np.degrees(np.arctan2(dy, dx))
        # Bee image points upward (head at top, wings left/right).
        # Subtract 90° so the bee faces the direction of travel.
        angle_deg = traj_angle - 90.0

        composite_bee(img, bee_rgba, bee_mask, cx, cy, angle_deg)

        filename = os.path.join(output_dir, f"frame_{i:04d}.png")
        cv2.imwrite(filename, img)

        if (i + 1) % 10 == 0:
            print(f"  Generated {i + 1}/{num_frames} frames")

    print(f"\nDone! {num_frames} frames written to: {output_dir}")
    print(f"  Resolution: {width}x{height}")
    print(f"  Trajectory: left-to-right with sinusoidal vertical motion")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate synthetic insect test sequence for SceneCameraActor")
    parser.add_argument("--frames", type=int, default=180,
                        help="Number of frames to generate (default: 180)")
    parser.add_argument("--width", type=int, default=1920,
                        help="Frame width in pixels (default: 1920)")
    parser.add_argument("--height", type=int, default=1080,
                        help="Frame height in pixels (default: 1080)")
    parser.add_argument("--output", type=str,
                        default="test_scenes/insect_walk",
                        help="Output directory (default: test_scenes/insect_walk)")
    parser.add_argument("--bee", type=str, default="bee.png",
                        help="Path to bee image (default: bee.png)")

    args = parser.parse_args()
    generate_sequence(args.frames, args.width, args.height, args.output, args.bee)
