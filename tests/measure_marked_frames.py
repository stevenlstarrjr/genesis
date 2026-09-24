"""Temporal QA for artifacts/sparkle-marked/closeup.py (1600x900).

Uses original, unmodified framebuffer PNGs. Compare stationary sequences only;
camera/sun motion changes the image and cannot be counted as noise alone.
Run with CPython, Pillow and NumPy, not PocketPy.
"""
import json
import sys
from pathlib import Path

import measure_frames

measure_frames.REGIONS = {
    "rail": (330, 472, 500, 532),
    "near_ivy": (185, 730, 365, 885),
    "far_ivy": (500, 665, 580, 770),
    "rear_arch": (698, 453, 858, 542),
    "floor": (615, 720, 890, 880),
}

if __name__ == "__main__":
    print(json.dumps({path: measure_frames.measure(Path(path))
                     for path in sys.argv[1:]}, indent=2))
