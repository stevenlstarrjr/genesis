"""Check actual GPU framebuffer readbacks (CPython + Pillow + NumPy).

See docs/hdr-render-regression.md for capture commands. No source-text tests:
the swatches go through the production tonemapper, and invalid diagnostics
classify FP32 bits from the production HDR/bloom/fog buffers.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def check_invalid(directory, expected_frames):
    paths = sorted(directory.glob("frame-*.png"))
    if len(paths) != expected_frames:
        raise AssertionError(f"Expected {expected_frames} captures in {directory}, got {len(paths)}")
    totals = np.zeros(3, dtype=np.int64)
    affected_frames = 0
    for path in paths:
        with Image.open(path) as image:
            rgb = np.asarray(image.convert("RGB"))
        if rgb.shape[0] <= 118:
            raise AssertionError("Capture has no scene area below the HUD")
        # HUD is intentionally not part of the diagnostic output.
        counts = (rgb[118:] > 128).sum(axis=(0, 1))
        totals += counts
        affected_frames += int(np.any(counts))
    result = dict(frames=len(paths), affected_frames=affected_frames,
                  scene_invalid_pixels=int(totals[0]), bloom_invalid_pixels=int(totals[1]),
                  fog_invalid_or_opaque_pixels=int(totals[2]))
    print(json.dumps({str(directory): result}))
    return affected_frames == 0


def check_chart(path, transform):
    # Neutral analytic AgX / legacy ACES fit at .18 * 2**[-4..3], encoded once.
    # In particular 18% gray must be ~128 in AgX, not ~186 (double encoding).
    expected = {"agx": [27, 44, 68, 97, 128, 158, 188, 214],
                "aces": [22, 35, 57, 93, 140, 187, 220, 239]}[transform]
    with Image.open(path) as image:
        rgb = np.asarray(image.convert("RGB"), dtype=np.int16)
    height, width, _ = rgb.shape
    actual = rgb[int(height * .75), [int(width * (i + .5) / 8) for i in range(8)]]
    error = int(np.abs(actual - np.array(expected)[:, None]).max())
    print(json.dumps({transform: {"swatches": actual[:, 0].tolist(), "max_error": error}}))
    return error <= 2


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--invalid", type=Path)
    parser.add_argument("--frames", type=int, default=32)
    parser.add_argument("--agx", type=Path)
    parser.add_argument("--aces", type=Path)
    parser.add_argument("--report-only", action="store_true", help="Measure the known-bad baseline without failing")
    args = parser.parse_args()
    if not any((args.invalid, args.agx, args.aces)):
        parser.error("Specify at least one captured diagnostic")
    results = []
    if args.invalid:
        results.append(check_invalid(args.invalid, args.frames))
    for transform in ("agx", "aces"):
        path = getattr(args, transform)
        if path:
            results.append(check_chart(path, transform))
    raise SystemExit(0 if args.report_only or all(results) else 1)
