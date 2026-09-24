"""Generate tiny glTF fixtures and check production GPU AO readbacks.

Run with CPython + Pillow + NumPy, not the engine's embedded Python:
  python tests/material_ao_regression.py --exe build/release/microsoft/genesis.exe
Generated fixtures/captures stay under artifacts/material-ao-regression/.
"""
import argparse
import copy
import io
import json
import math
from pathlib import Path
import struct
import subprocess

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "artifacts/material-ao-regression"


class Fixture:
    def __init__(self, output=OUTPUT):
        self.output = output
        self.data = bytearray()
        self.doc = {"asset": {"version": "2.0"}, "scene": 0,
                    "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
                    "meshes": [{"primitives": []}], "bufferViews": [],
                    "accessors": [], "materials": [], "images": [],
                    "textures": [], "samplers": [], "extensionsUsed": ["KHR_texture_transform"]}

    def buffer(self, payload):
        self.data.extend(b"\0" * (-len(self.data) % 4))
        result = len(self.doc["bufferViews"])
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)})
        self.data.extend(payload)
        return result

    def accessor(self, values, kind, dimensions):
        flat = [component for value in values for component in value]
        result = len(self.doc["accessors"])
        item = {"bufferView": self.buffer(struct.pack("<" + "f" * len(flat), *flat)),
                "componentType": 5126, "count": len(values), "type": kind}
        if dimensions == 3:
            item.update(min=[min(v[c] for v in values) for c in range(3)],
                        max=[max(v[c] for v in values) for c in range(3)])
        self.doc["accessors"].append(item)
        return result

    def image(self, pixels, width=None, height=1):
        width = len(pixels) if width is None else width
        if len(pixels) != width * height:
            raise ValueError("Fixture pixel count must match dimensions")
        image = Image.new("RGBA", (width, height))
        image.putdata(pixels)
        encoded = io.BytesIO()
        image.save(encoded, format="PNG")
        index = len(self.doc["images"])
        self.doc["images"].append({"bufferView": self.buffer(encoded.getvalue()), "mimeType": "image/png"})
        return index

    def texture(self, image, wrap=10497, nearest=True):
        sampler = {"wrapS": wrap, "wrapT": wrap,
                   "minFilter": 9728 if nearest else 9987, "magFilter": 9728 if nearest else 9729}
        index = len(self.doc["textures"])
        self.doc["textures"].append({"source": image, "sampler": len(self.doc["samplers"])})
        self.doc["samplers"].append(sampler)
        return index

    def quad(self, name, center, ao=None, uv0=(.375, .5), uv1=(.875, .5), mr=None,
             emissive=False, size=.72, uv_scale=None, base_texture=None, alpha=1):
        material = {"name": name, "doubleSided": True,
                    "pbrMetallicRoughness": {"baseColorFactor": [.5, .5, .5, alpha],
                                            "metallicFactor": 0, "roughnessFactor": .7}}
        if ao is not None:
            material["occlusionTexture"] = ao
        if mr is not None:
            material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": mr}
        if base_texture is not None:
            material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": base_texture}
        if alpha < 1:
            material["alphaMode"] = "BLEND"
        if emissive:
            material["emissiveFactor"] = [.6, .15, .02]
        index = len(self.doc["materials"])
        self.doc["materials"].append(material)
        x, y = center
        points = [(x-size, y-size*.65, 0), (x+size, y-size*.65, 0), (x+size, y+size*.65, 0),
                  (x-size, y-size*.65, 0), (x+size, y+size*.65, 0), (x-size, y+size*.65, 0)]
        uvs = [uv0] * 6 if uv_scale is None else [(0,0),(uv_scale,0),(uv_scale,uv_scale),
                                                (0,0),(uv_scale,uv_scale),(0,uv_scale)]
        attrs = {"POSITION": self.accessor(points, "VEC3", 3),
                 "NORMAL": self.accessor([(0, 0, 1)] * 6, "VEC3", 3),
                 "TEXCOORD_0": self.accessor(uvs, "VEC2", 2),
                 "TEXCOORD_1": self.accessor([uv1] * 6, "VEC2", 2),
                 "TEXCOORD_3": self.accessor([(.625, .5)] * 6, "VEC2", 2)}
        self.doc["meshes"][0]["primitives"].append({"attributes": attrs, "material": index, "mode": 4})

    def write(self, name):
        doc = copy.deepcopy(self.doc)
        doc["buffers"] = [{"byteLength": len(self.data)}]
        encoded = json.dumps(doc, separators=(",", ":")).encode()
        encoded += b" " * (-len(encoded) % 4)
        binary = self.data + b"\0" * (-len(self.data) % 4)
        payload = struct.pack("<III", 0x46546C67, 2, 28 + len(encoded) + len(binary))
        payload += struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
        payload += struct.pack("<II", len(binary), 0x004E4942) + binary
        path = self.output / (name + ".glb")
        path.write_bytes(payload)
        return path


def script(name, model, light="none", probes=False):
    lines = ["import genesis as gx", "gx.window.configure(width=1280, height=720, vsync=False)",
             'gx.renderer.quality("medium")', 'gx.renderer.realtime(bloom=False)',
             'gx.color.agx(look="neutral", exposure=0, auto_exposure=False)',
             'gx.sky.earth().sun(intensity=0).radiance(0)',
             'gx.camera.look_at(position=(0,0,10), target=(0,0,0), fov=50)',
             f'gx.scene.load({json.dumps(str(model))})']
    if light == "direct":
        lines += ['gx.lights.point("Direct", position=(0,0,3), color=(1,.6,.3), intensity=30, radius=20, casts_shadows=False)']
    if light == "indirect" or probes:
        lines += [f'gx.sky.hdri({json.dumps(str(ROOT / "assets/hdri/meadow_2_4k.exr"))}, intensity=1)']
    if probes:
        lines += ['gx.reflections.probe("Left", position=(-1,0,2), bounds_min=(-6,-4,-3), bounds_max=(6,4,12), blend_distance=1)',
                  'gx.reflections.probe("Right", position=(1,0,2), bounds_min=(-6,-4,-3), bounds_max=(6,4,12), blend_distance=1)']
    path = OUTPUT / (name + ".py")
    path.write_text("\n".join(lines) + "\n")
    return path


def generate():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    fixture = Fixture()
    # Contradictory G/B/A values catch channel mistakes and unintended sRGB decoding.
    image = fixture.image([(0, 255, 255, 255), (64, 255, 0, 0), (128, 0, 255, 255), (255, 0, 0, 0)])
    repeat = fixture.texture(image)
    clamp = fixture.texture(image, wrap=33071)
    mirror = fixture.texture(image, wrap=33648)
    linear = fixture.texture(image, nearest=False)
    full = {"index": repeat}
    cases = [
        ("missing", None, {}, 255),
        ("strength_zero", {"index": repeat, "strength": 0}, {}, 255),
        ("strength_half", {"index": repeat, "strength": .5}, {}, 159.5),
        ("default_red_linear", full, {}, 64),
        ("uv1", {"index": repeat, "texCoord": 1}, {"mr": repeat}, 255),
        ("uv3", {"index": repeat, "texCoord": 3}, {}, 128),
        ("transform_uv_override", {"index": repeat, "extensions": {"KHR_texture_transform":
            {"texCoord": 1, "offset": [-.25, 0]}}}, {}, 128),
        ("rotation_scale_offset", {"index": repeat, "extensions": {"KHR_texture_transform":
            {"rotation": math.pi / 2, "scale": [2, .5], "offset": [.875, 0]}}}, {}, 128),
        ("repeat_sampler", full, {"uv0": (1.375, .5)}, 64),
        ("clamp_sampler_same_image", {"index": clamp}, {"uv0": (1.375, .5)}, 255),
        ("mirror_sampler_same_image", {"index": mirror}, {"uv0": (1.375, .5)}, 128),
        ("shared_orm_image", full, {"mr": repeat}, 64),
        ("linear_filter_same_image", {"index": linear}, {"uv0": (.25, .5)}, 32),
        ("black", full, {"uv0": (.125, .5)}, 0),
        ("white", full, {"uv0": (.875, .5)}, 255),
        ("minified_linear_1d_mips", {"index": linear}, {"uv_scale": 2048}, 112),
        ("same_image_srgb_base_linear_ao", full, {"base_texture": repeat}, 64),
        ("blended_metadata", full, {"alpha": .5}, 64),
    ]
    swatches = []
    for i, (name, ao, kwargs, expected) in enumerate(cases):
        center = ((i % 5 - 2) * 2, (1 - i // 5) * 1.5)
        fixture.quad(name, center, copy.deepcopy(ao), **kwargs)
        swatches.append(dict(name=name, center=center, expected=expected))
    script("swatches", fixture.write("swatches"))
    script("swatches_probes", OUTPUT / "swatches.glb", probes=True)
    (OUTPUT / "swatches.json").write_text(json.dumps(swatches, indent=2))
    for light in ("direct", "emissive", "indirect", "direct_blended", "emissive_blended"):
        for strength in (0, 1):
            fixture = Fixture()
            texture = fixture.texture(fixture.image([(0, 255, 255, 255)]))
            fixture.quad("Isolation", (0, 0), {"index": texture, "strength": strength},
                         emissive=light.startswith("emissive"), size=2.5,
                         alpha=.5 if light.endswith("blended") else 1)
            name = f"{light}_{strength}"
            script(name, fixture.write(name), light=light.split("_")[0])


def render(exe, name, debug=None):
    command = [str(exe), "--script", str(OUTPUT / (name + ".py")),
               "--screenshot", str(OUTPUT / name)]
    if debug:
        command += ["--debug-view", debug]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
    (OUTPUT / (name + ".log")).write_text(result.stdout + result.stderr)
    if result.returncode or "Cannot decode" in result.stderr or "Missing or invalid" in result.stderr:
        raise AssertionError(f"{name}: renderer failed; see {OUTPUT / (name + '.log')}")
    with Image.open(OUTPUT / (name + ".png")) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def check_swatches(rgb):
    focal = rgb.shape[0] / (2 * math.tan(math.radians(25)))
    # bgfx's default left-handed look-at gives camera-right -X looking down -Z.
    for case in json.loads((OUTPUT / "swatches.json").read_text()):
        x, y = case["center"]
        px, py = round(rgb.shape[1] / 2 - x * focal / 10), round(rgb.shape[0] / 2 - y * focal / 10)
        actual = float(np.median(rgb[py-3:py+4, px-3:px+4]))
        if abs(actual - case["expected"]) > 2:
            raise AssertionError(f"{case['name']}: expected {case['expected']}, got {actual} at {px},{py}")
    print("PASS: 18 GPU material-AO swatches (strength, UV0/1/3, transforms, samplers, shared ORM, minified mips, blending)", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/release/microsoft/genesis.exe")
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    generate()
    if not args.generate_only:
        exe = args.exe.resolve()
        check_swatches(render(exe, "swatches", "material-ao"))
        check_swatches(render(exe, "swatches_probes", "material-ao"))
        for light in ("direct", "emissive", "indirect", "direct_blended", "emissive_blended"):
            off = render(exe, light + "_0")
            on = render(exe, light + "_1")
            if light == "indirect":
                off_value = float(off[350:370, 630:650].mean())
                on_value = float(on[350:370, 630:650].mean())
                assert off_value > 30 and on_value < off_value * .1, (off_value, on_value)
                print(f"PASS: AO blocks indirect lighting: {off_value:.1f} -> {on_value:.1f}", flush=True)
            else:
                error = int(np.abs(off[118:] - on[118:]).max())
                assert error <= 1, (light, error)
                assert off[350:370, 630:650].mean() > 20, "isolation fixture must be lit"
                print(f"PASS: {light} unchanged, maximum RGB delta {error}", flush=True)
