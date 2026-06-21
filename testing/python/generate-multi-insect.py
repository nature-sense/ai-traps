#!/usr/bin/env python3
"""
Generate synthetic multi-insect test sequence for SceneCameraActor.

Creates a sequence of PNG frames showing 4 insects wandering randomly across
the field of view. Each insect:
  - Moves with smooth random wandering (velocity-based with gradual steering)
  - Avoids collisions with other insects (repulsion force)
  - Rotates to face its direction of travel
  - Bounces off frame edges

Usage:
    python3 generate-multi-insect.py [--frames 300] [--width 1920] [--height 1080]
                                     [--output ./test_scenes/insect_walk_4]

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


class Insect:
    """Represents a single wandering insect with position, velocity, and appearance."""

    def __init__(self, insect_id, insect_rgba, width, height, margin=40):
        self.id = insect_id
        self.insect_rgba = insect_rgba
        self.margin = margin

        # Random starting position
        self.x = random.uniform(margin, width - margin)
        self.y = random.uniform(margin, height - margin)

        # Random initial velocity (pixels per frame)
        speed = random.uniform(1.5, 3.5)
        angle = random.uniform(0, 2 * math.pi)
        self.vx = speed * math.cos(angle)
        self.vy = speed * math.sin(angle)

        # Wandering parameters
        self.wander_angle = angle
        self.wander_timer = 0
        self.wander_interval = random.randint(20, 60)  # frames between direction changes

        # Size of insect (for collision detection radius)
        self.radius = max(insect_rgba.shape[0], insect_rgba.shape[1]) // 2

    def update(self, width, height, others, frame_width, frame_height):
        """
        Update position based on velocity, wander steering, collision avoidance,
        and edge bouncing.
        """
        # ── Wander steering ────────────────────────────────────────────────
        self.wander_timer += 1
        if self.wander_timer >= self.wander_interval:
            self.wander_timer = 0
            self.wander_interval = random.randint(20, 60)
            # Random steering angle change (max ±60 degrees)
            steer = random.uniform(-math.pi / 3, math.pi / 3)
            self.wander_angle += steer

            # Apply wander: blend current velocity toward wander direction
            wander_speed = math.hypot(self.vx, self.vy)
            target_vx = wander_speed * math.cos(self.wander_angle)
            target_vy = wander_speed * math.sin(self.wander_angle)
            # Smooth steering (lerp factor 0.3)
            self.vx += (target_vx - self.vx) * 0.3
            self.vy += (target_vy - self.vy) * 0.3

        # ── Collision avoidance (repulsion from other insects) ─────────────
        for other in others:
            if other is self:
                continue
            dx = self.x - other.x
            dy = self.y - other.y
            dist = math.hypot(dx, dy)
            min_dist = self.radius + other.radius + 20  # minimum separation
            if 0 < dist < min_dist:
                # Repulsion force: stronger when closer
                force = (min_dist - dist) / min_dist
                # Normalize direction
                if dist > 0:
                    nx = dx / dist
                    ny = dy / dist
                else:
                    nx, ny = random.uniform(-1, 1), random.uniform(-1, 1)
                    norm = math.hypot(nx, ny)
                    nx /= norm
                    ny /= norm
                # Apply repulsion (stronger push)
                self.vx += nx * force * 4.0
                self.vy += ny * force * 4.0

        # ── Speed limiting ─────────────────────────────────────────────────
        speed = math.hypot(self.vx, self.vy)
        max_speed = 4.5
        min_speed = 1.0
        if speed > max_speed:
            self.vx = (self.vx / speed) * max_speed
            self.vy = (self.vy / speed) * max_speed
        elif speed < min_speed and speed > 0:
            self.vx = (self.vx / speed) * min_speed
            self.vy = (self.vy / speed) * min_speed

        # ── Update position ────────────────────────────────────────────────
        self.x += self.vx
        self.y += self.vy

        # ── Edge bouncing ──────────────────────────────────────────────────
        # Bounce off edges with some randomness to prevent sticking
        if self.x < self.margin:
            self.x = self.margin
            self.vx = abs(self.vx) * random.uniform(0.8, 1.2)
            self.wander_angle = math.atan2(self.vy, self.vx)
        elif self.x > width - self.margin:
            self.x = width - self.margin
            self.vx = -abs(self.vx) * random.uniform(0.8, 1.2)
            self.wander_angle = math.atan2(self.vy, self.vx)

        if self.y < self.margin:
            self.y = self.margin
            self.vy = abs(self.vy) * random.uniform(0.8, 1.2)
            self.wander_angle = math.atan2(self.vy, self.vx)
        elif self.y > height - self.margin:
            self.y = height - self.margin
            self.vy = -abs(self.vy) * random.uniform(0.8, 1.2)
            self.wander_angle = math.atan2(self.vy, self.vx)

    @property
    def angle_deg(self):
        """Direction of travel in degrees (0° = right, 90° = down).
        The bee image points upward (head at top), so subtract 90°."""
        traj_angle = math.degrees(math.atan2(self.vy, self.vx))
        return traj_angle - 90.0


def generate_sequence(num_frames=300, width=1920, height=1080,
                      output_dir="test_scenes/insect_walk_4"):
    """Generate a sequence of frames with 4 wandering insects."""

    os.makedirs(output_dir, exist_ok=True)

    # Load the 4 insect images
    print("Loading insect images...")
    insects_data = []
    for i, fname in enumerate(INSECT_FILES):
        rgba = load_insect_image(fname, target_width=120)
        insects_data.append(rgba)
        print(f"  {fname}: {rgba.shape[1]}x{rgba.shape[0]} pixels")

    bg_color = (255, 255, 255)  # white background (trap surface)

    # Create insect agents
    insects = []
    for i in range(4):
        insect = Insect(i, insects_data[i], width, height)
        insects.append(insect)

    print(f"\nGenerating {num_frames} frames with 4 insects...")
    print(f"  Insects: {', '.join(INSECT_NAMES)}")

    for frame_idx in range(num_frames):
        img = np.full((height, width, 3), bg_color, dtype=np.uint8)

        # Update all insects
        for insect in insects:
            insect.update(width, height, insects, width, height)

        # Composite insects in order (back to front by y-position for depth)
        sorted_insects = sorted(insects, key=lambda ins: ins.y)
        for insect in sorted_insects:
            cx = int(round(insect.x))
            cy = int(round(insect.y))
            composite_insect(img, insect.insect_rgba, cx, cy, insect.angle_deg)

        filename = os.path.join(output_dir, f"frame_{frame_idx:04d}.png")
        cv2.imwrite(filename, img)

        if (frame_idx + 1) % 30 == 0:
            print(f"  Generated {frame_idx + 1}/{num_frames} frames")

    print(f"\nDone! {num_frames} frames written to: {output_dir}")
    print(f"  Resolution: {width}x{height}")
    print(f"  Insects: {len(insects)} wandering with collision avoidance")
    print(f"  Each insect rotates to face its direction of travel")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate multi-insect test sequence for SceneCameraActor")
    parser.add_argument("--frames", type=int, default=300,
                        help="Number of frames to generate (default: 300)")
    parser.add_argument("--width", type=int, default=1920,
                        help="Frame width in pixels (default: 1920)")
    parser.add_argument("--height", type=int, default=1080,
                        help="Frame height in pixels (default: 1080)")
    parser.add_argument("--output", type=str,
                        default="test_scenes/insect_walk_4",
                        help="Output directory (default: test_scenes/insect_walk_4)")

    args = parser.parse_args()
    generate_sequence(args.frames, args.width, args.height, args.output)
