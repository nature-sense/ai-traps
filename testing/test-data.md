# Test Data for SceneCameraActor

The `SceneCameraActor` reads a sequence of numbered PNG frames from a directory
and feeds them through the pipeline one frame per tick. This enables deterministic
testing of inference, tracking, and session lifecycle without a real camera.

## Scene Directory Format

```
test_scenes/<scene_name>/
  frame_0000.png      # Frame 0
  frame_0001.png      # Frame 1
  frame_0002.png      # Frame 2
  ...
```

- Files must be named `frame_NNNN.png` where `NNNN` is a zero-padded frame number.
- Files are sorted numerically by the extracted frame number.
- When the sequence ends, it loops back to frame 0.
- All frames are pre-decoded to NV12 during `init()` — no I/O happens during `tick()`.

## Configuration

```toml
[camera]
model = "scene"
scene_dir = "test_scenes/moth_crossing"
```

## Generating Test Scenes

### Option 1: Python + OpenCV (procedural)

A helper script is available at `testing/python/generate-insect.py` that creates
synthetic test scenes with a procedurally-drawn insect (ellipse body + circle head
+ legs + antennae) walking across the frame with a wavy trajectory.

```bash
# Generate a 60-frame sequence at 640x480
python3 testing/python/generate-insect.py --frames 60 --width 640 --height 480 --output test_scenes/insect_walk
```

The insect is drawn from scratch using basic OpenCV shapes — no external images
or AI generation required. This gives you perfect ground truth (position, speed,
direction known exactly) and full control over trajectory, size, colour, and noise.

### Option 2: AI-generated images

Use an image generation tool (Stable Diffusion, DALL-E, etc.) to create realistic
insect images on a natural background. Name them `frame_0000.png`, `frame_0001.png`, etc.

For tracking validation, create sequences where an insect moves across the frame
gradually — this tests track persistence across frames.

### Option 3: Real camera captures

Capture real frames from a camera and save as PNGs:

```bash
ffmpeg -i input.mp4 -vf "fps=15" test_scenes/my_scene/frame_%04d.png
```

## Test Scenarios

| Scenario | What it validates |
|----------|-------------------|
| Single insect walks through frame | Track persistence (same track_id across frames) |
| Insect leaves, second insect enters | Separate track_ids assigned correctly |
| No insect present | Zero false positive detections |
| Insect approaching camera | Confidence ramping over frames |
| Session start/stop | Pipeline resets cleanly; fresh detections on re-start |

## Resolution

The pipeline will scale frames to the configured resolutions (full/medium/lores)
using letterbox or nearest-neighbour scaling. Native resolution of the source PNGs
does not need to match the pipeline resolution — scaling is handled automatically.
