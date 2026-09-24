"""Offline temporal QA for unmodified Genesis framebuffer PNG sequences.

Run with CPython + Pillow + NumPy, not PocketPy:
  python tests/measure_frames.py artifacts/sparkle/baseline artifacts/sparkle/final
Regions are fixed to the 1600x900 Sponza demo camera. Use stationary captures
for comparisons; camera/sun motion changes the image and is not noise alone.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REGIONS = {
    "gold_trim": (225, 585, 380, 875),
    "foliage": (920, 750, 1080, 890),
    "floor": (680, 670, 870, 870),
    "full_frame": (0, 0, 1600, 900),
}


def measure(directory):
    paths = sorted(directory.glob("frame-*.png"), key=lambda p: int(p.stem.split("-")[1]))
    if len(paths) < 2:
        raise ValueError(f"Need at least two frames: {directory}")
    frames = np.stack([np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) for p in paths])
    if frames.shape[1:3] != (900, 1600):
        raise ValueError("These ROIs require 1600x900 captures")
    result = {"frames": len(paths), "units": "8-bit RGB code values", "regions": {}}
    for name, (x0, y0, x1, y1) in REGIONS.items():
        region = frames[:, y0:y1, x0:x1]
        delta = np.diff(region, axis=0)
        absolute = np.abs(delta)
        result["regions"][name] = {
            "temporal_rms": float(np.sqrt(np.mean(delta * delta))),
            "p99_abs_delta": float(np.percentile(absolute, 99)),
            "fraction_pixels_jumping_over_8": float(np.mean(absolute.max(axis=-1) > 8)),
            "mean_rgb": region.mean(axis=(0, 1, 2)).tolist(),
        }
    return result


if __name__ == "__main__":
    print(json.dumps({path: measure(Path(path)) for path in sys.argv[1:]}, indent=2))
