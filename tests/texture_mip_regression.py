"""Compare real minified glTF color textures against solid-color references.

CPython + Pillow + NumPy. No diagnostic shader: this exercises the production
image importer, GPU sRGB sampling, PBR/emission, alpha blending, and tonemapper.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from material_ao_regression import Fixture, ROOT


def reference_color(pixels):
    """Analytic mean over the whole image; tolerate only mip quantization error."""
    values = np.asarray(pixels, dtype=np.float64) / 255
    rgb = values[:, :3]
    linear = np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)
    average = linear.mean(axis=0)
    encoded = np.where(average <= .0031308, average * 12.92, 1.055 * average ** (1 / 2.4) - .055)
    return tuple(np.floor(np.r_[encoded, values[:, 3].mean()] * 255 + .5).astype(int).tolist())


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    black, white = (0, 0, 0, 255), (255, 255, 255, 255)
    sources = [
        ("horizontal_1d", 4, 1, [black, white] * 2),
        ("vertical_1d", 1, 8, [white, black] * 4),
        ("rectangular_tail", 2, 8, [(255, 0, 0, 255), (0, 0, 255, 255)] * 8),
        ("odd_corner", 7, 5, [black] * 34 + [white]),
        ("odd_1d", 1, 9, [black] * 8 + [white]),
        ("constant_npot", 13, 5, [(32, 64, 192, 255)] * 65),
        ("single_texel", 1, 1, [(40, 140, 230, 255)]),
        ("srgb_average", 2, 2, [white, black, black, white]),
    ]
    cases = [(f"{channel}_{name}", channel, width, height, pixels)
             for channel in ("base", "emissive") for name, width, height, pixels in sources]
    cases += [("alpha_is_linear", "alpha", 2, 2, [(255, 128, 32, 0), (255, 128, 32, 255)] * 2)]
    records = []
    for reference in (False, True):
        fixture = Fixture(output)
        for i, (name, channel, width, height, pixels) in enumerate(cases):
            expected = reference_color(pixels)
            image = fixture.image([expected] if reference else pixels,
                                  width=1 if reference else width, height=1 if reference else height)
            texture = fixture.texture(image, nearest=False)
            center = ((i % 5 - 2) * 2, 2.25 - (i // 5) * 1.5)
            fixture.quad(name, center, uv_scale=2048,
                         base_texture=texture if channel != "emissive" else None)
            material = fixture.doc["materials"][-1]
            material["pbrMetallicRoughness"]["baseColorFactor"] = [1, 1, 1, 1]
            if channel == "emissive":
                material["pbrMetallicRoughness"]["baseColorFactor"] = [0, 0, 0, 1]
                material["emissiveTexture"] = {"index": texture}
                material["emissiveFactor"] = [1, 1, 1]
            if channel == "alpha":
                material["alphaMode"] = "BLEND"
            if not reference:
                records.append(dict(name=name, center=center, expected_rgba=expected))
        name = "reference" if reference else "minified"
        model = fixture.write(name)
        lines = ["import genesis as gx", "gx.window.configure(width=1280, height=720, vsync=False)",
                 'gx.renderer.quality("medium")', 'gx.renderer.realtime(bloom=False)',
                 'gx.color.agx(look="neutral", exposure=0, auto_exposure=False)',
                 'gx.sky.earth().sun(intensity=0).radiance(0)',
                 'gx.camera.look_at(position=(0,0,10), target=(0,0,0), fov=50)',
                 'gx.lights.point("Test", position=(0,0,4), intensity=60, radius=30, casts_shadows=False)',
                 f'gx.scene.load({json.dumps(str(model.resolve()))})']
        (output / (name + ".py")).write_text("\n".join(lines) + "\n")
    (output / "cases.json").write_text(json.dumps(records, indent=2))
    return records


def render(exe, output, name):
    result = subprocess.run([str(exe), "--script", str(output / (name + ".py")),
                             "--screenshot", str(output / name)], cwd=ROOT,
                            text=True, capture_output=True, timeout=90)
    (output / (name + ".log")).write_text(result.stdout + result.stderr)
    if result.returncode or "Cannot decode" in result.stderr:
        raise AssertionError(f"Renderer failed; see {output / (name + '.log')}")
    with Image.open(output / (name + ".png")) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def verify(actual, reference, cases):
    focal = actual.shape[0] / (2 * math.tan(math.radians(25)))
    results = []
    for case in cases:
        x, y = case["center"]
        px, py = round(actual.shape[1] / 2 - x * focal / 10), round(actual.shape[0] / 2 - y * focal / 10)
        region = np.s_[py-4:py+5, px-4:px+5]
        error = int(np.abs(actual[region] - reference[region]).max())
        reference_rgb = reference[py, px].tolist()
        passed = error <= 2 and max(reference_rgb) > 5
        results.append(dict(name=case["name"], actual=actual[py, px].tolist(),
                            reference=reference_rgb, max_error=error, passed=passed))
        print(f"{'PASS' if passed else 'FAIL'}: {case['name']} RGB delta {error}", flush=True)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/release/microsoft/genesis.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/texture-mip-regression")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    cases = generate(output)
    results = verify(render(args.exe.resolve(), output, "minified"),
                     render(args.exe.resolve(), output, "reference"), cases)
    (output / "results.json").write_text(json.dumps(results, indent=2))
    raise SystemExit(0 if args.report_only or all(r["passed"] for r in results) else 1)
