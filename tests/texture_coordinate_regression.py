"""GPU material-coordinate tests: transformed/multi-UV maps vs baked references.

Run with CPython + Pillow + NumPy. Fixtures and readbacks are generated only in
artifacts/. References bake UV transforms independently in Python; normal-frame
cases compare against analytic geometry normals, not the engine's tangent helper.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from material_ao_regression import Fixture, ROOT

UV = np.array([[0, 0], [1, 0], [1, 1], [0, 0], [1, 1], [0, 1]], dtype=float)
MAPS = {"base": ("pbrMetallicRoughness", "baseColorTexture"),
        "mr": ("pbrMetallicRoughness", "metallicRoughnessTexture"),
        "normal": ("normalTexture",), "emissive": ("emissiveTexture",), "ao": ("occlusionTexture",)}


def transform(uv, value):
    angle = value.get("rotation", 0)
    rotation = np.array([[math.cos(angle), -math.sin(angle)], [math.sin(angle), math.cos(angle)]])
    return (uv * value.get("scale", [1, 1])) @ rotation.T + value.get("offset", [0, 0])


def assign(material, channel, view):
    parent = material
    for key in MAPS[channel][:-1]:
        parent = parent[key]
    parent[MAPS[channel][-1]] = view


def script(output, name, model, probes=False):
    lines = ["import genesis as gx", "gx.window.configure(width=1280, height=960, vsync=False)",
             'gx.renderer.quality("medium")', 'gx.renderer.realtime(bloom=False)',
             'gx.color.agx(look="neutral", exposure=0, auto_exposure=False)',
             'gx.sky.earth().sun(intensity=0).radiance(0)',
             f'gx.sky.hdri({json.dumps(str(ROOT / "assets/hdri/meadow_2_4k.exr"))}, intensity=.35)',
             'gx.camera.look_at(position=(0,0,14), target=(0,0,0), fov=50)',
             'gx.lights.point("Test", position=(2,3,5), intensity=50, radius=30, casts_shadows=False)',
             f'gx.scene.load({json.dumps(str(model))})']
    if probes:
        lines += ['gx.reflections.probe("Test probe", position=(0,0,2), bounds_min=(-8,-6,-3), bounds_max=(8,6,16), blend_distance=1)']
    (output / (name + ".py")).write_text("\n".join(lines) + "\n")


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    modes = ["uv1", "uv3_only", "override", "rotate_scale_offset", "mirror", "missing"]
    cases = [dict(name=f"{channel}_{mode}", channel=channel, mode=mode)
             for channel in MAPS for mode in modes]
    cases += [dict(name=f"frame_{mode}", channel="normal", frame=mode) for mode in
              ("uv3_generated", "rotated_generated", "mirrored_u", "mirrored_v", "authored_rotated_uv",
               "authored_negative_w", "scale_zero", "scale_half", "degenerate_uv")]
    cases += [dict(name="independent_shared_image", mixed=True),
              dict(name="base_alpha_mask", channel="base", mode="rotate_scale_offset", alpha="MASK"),
              dict(name="base_alpha_blend", channel="base", mode="override", alpha="BLEND")]
    cases += [dict(name="shared_image_full_emission", mixed=True, full_emission=True)]
    cases += [dict(name="shared_material_"+mode,channel="base",mode="uv3_only",shared=mode)
              for mode in ("valid","missing","valid_after_missing")]
    for i, case in enumerate(cases):
        case["center"] = [(i % 6 - 2.5) * 1.85, 4.65 - i // 6 * 1.45]

    for reference in (False, True):
        fixture = Fixture(output)
        shared_material = None
        pixels = [(40+x*60, 35+y*65, 45+((x+2*y)%4)*60, 255) for y in range(4) for x in range(4)]
        normal_pixels = [(55+x*45, 55+y*45, 220, 255) for y in range(4) for x in range(4)]
        pattern = fixture.texture(fixture.image(pixels, 4, 4))
        normal_pattern = fixture.texture(fixture.image(normal_pixels, 4, 4))
        alpha_pattern = fixture.texture(fixture.image([(r,g,b,255 if x%2 else 0)
            for x,(r,g,b,_) in enumerate(pixels)],4,4))
        for case in cases:
            fixture.quad(case["name"], case["center"], size=.77, uv_scale=1)
            material = fixture.doc["materials"][-1]
            pbr = material["pbrMetallicRoughness"]
            pbr.update(baseColorFactor=[.7,.55,.3,1], metallicFactor=.65, roughnessFactor=.7)
            primitive = fixture.doc["meshes"][0]["primitives"][-1]
            attrs = primitive["attributes"]
            # Keep sample coordinates comfortably away from nearest-filter boundaries.
            sets = {0: UV*.7 + [.13,.17], 1: UV[:, ::-1]*.6 + [.23,.13],
                    3: UV*.8 + [.11,.07]}
            if case.get("mixed"):
                # Positive Z in every texel also makes this a valid glTF normal image.
                shared_pixels = [(40,80,200,255),(200,50,180,255),(90,210,160,255),(180,130,240,255)]
                shared = fixture.texture(fixture.image(shared_pixels))
                material["emissiveFactor"] = [1,1,1] if case.get("full_emission") else [.4,.3,.2]
                for slot,(channel,path) in enumerate(MAPS.items()):
                    index = slot % 4
                    uvset = [0,1,3,1,0][slot]
                    origin = {0:.125,1:.375,3:.625}[uvset]
                    sets[uvset] = np.tile([origin,.5],(6,1))
                    view = {"index":shared,"texCoord":uvset,"extensions":{"KHR_texture_transform":
                        {"offset":[(index+.5)/4-origin,0]}}}
                    if reference:
                        view = {"index": fixture.texture(fixture.image([shared_pixels[index]]))}
                    assign(material, channel, view)
                # Constant UVs: provide an explicit, known tangent basis.
                attrs["TANGENT"] = fixture.accessor([(1,0,0,1)]*6,"VEC4",4)
            elif "frame" in case:
                mode = case["frame"]
                mapped = np.array([204,166,223])/255*2-1
                normal_tex = fixture.texture(fixture.image([(204,166,223,255)]))
                sets[3] = UV.copy()
                scale = 0 if mode == "scale_zero" else .5 if mode == "scale_half" else 1
                t, b = np.array([1.,0,0]), np.array([0.,1,0])
                tx = {}
                if mode in ("rotated_generated", "authored_rotated_uv"):
                    tx = {"rotation":math.pi/2, "offset":[1,0]}
                    if mode == "rotated_generated": t,b = np.array([0.,-1,0]),np.array([1.,0,0])
                if mode == "mirrored_u":
                    tx = {"scale":[-1,1], "offset":[1,0]}; t = -t
                if mode == "mirrored_v":
                    tx = {"scale":[1,-1], "offset":[0,1]}; b = -b
                if mode == "authored_negative_w": b = -b
                if mode == "degenerate_uv": sets[3] = np.tile([.5,.5],(6,1))
                # UV0 deliberately differs so generating from the wrong set fails.
                sets[0] = UV[:,::-1]
                view = {"index":normal_tex,"texCoord":3,"scale":scale}
                if tx: view["extensions"] = {"KHR_texture_transform":tx}
                if mode.startswith("authored"):
                    attrs["TANGENT"] = fixture.accessor([(1,0,0,-1 if mode.endswith("negative_w") else 1)]*6,"VEC4",4)
                if reference:
                    n = t*mapped[0]*scale + b*mapped[1]*scale + np.array([0,0,mapped[2]])
                    n /= np.linalg.norm(n)
                    attrs["NORMAL"] = fixture.accessor([n.tolist()]*6,"VEC3",3)
                else: assign(material,"normal",view)
            else:
                channel, mode = case["channel"], case["mode"]
                if channel == "emissive": material["emissiveFactor"] = [.7,.5,.3]
                texture = normal_pattern if channel == "normal" else pattern
                if case.get("alpha"):
                    texture = alpha_pattern
                    material["alphaMode"] = case["alpha"]
                    material["alphaCutoff"] = .5
                selected = 1 if mode in ("uv1","override") else 3 if mode == "uv3_only" else 0
                view = {"index":texture,"texCoord":selected}
                tx = {}
                if mode == "override":
                    view["texCoord"] = 3
                    tx = {"texCoord":1,"offset":[.19,-.11]}
                elif mode == "rotate_scale_offset": tx = {"rotation":.47,"scale":[1.3,.65],"offset":[.3,-.2]}
                elif mode == "mirror": tx = {"scale":[-1,1.3],"offset":[.95,-.12]}
                elif mode == "missing": view["texCoord"] = 7
                if tx: view["extensions"] = {"KHR_texture_transform":tx}
                if reference:
                    sets[0] = transform(sets[selected],tx)
                    view = {"index":texture}
                if not (reference and mode == "missing"): assign(material, channel, view)
                if mode == "uv3_only" and not reference: sets.pop(0)
                if case.get("shared"):
                    if reference:
                        if case["shared"] == "missing": pbr.pop("baseColorTexture")
                    else:
                        if shared_material is None: shared_material = primitive["material"]
                        primitive["material"] = shared_material
                        if case["shared"] == "missing": sets.pop(3)
            for key in list(attrs):
                if key.startswith("TEXCOORD_"): del attrs[key]
            for uvset, values in sets.items():
                attrs[f"TEXCOORD_{uvset}"] = fixture.accessor(values.tolist(),"VEC2",2)
        name = "reference" if reference else "actual"
        model = fixture.write(name)
        script(output,name,model)
        script(output,name+"_probes",model,True)
    (output/"cases.json").write_text(json.dumps(cases,indent=2))
    return cases


def render(exe, output, name):
    result = subprocess.run([str(exe),"--script",str(output/(name+".py")),"--screenshot",str(output/name)],
                            cwd=ROOT,text=True,capture_output=True,timeout=180)
    (output/(name+".log")).write_text(result.stdout+result.stderr)
    if result.returncode or "Cannot decode" in result.stderr:
        raise AssertionError(f"Renderer failed: {output/(name+'.log')}")
    with Image.open(output/(name+".png")) as image:
        return np.asarray(image.convert("RGB"),dtype=np.int16)


def verify(actual, reference, cases):
    focal = actual.shape[0]/(2*math.tan(math.radians(25)))
    results = []
    for case in cases:
        x,y = case["center"]
        px,py = round(actual.shape[1]/2-x*focal/14),round(actual.shape[0]/2-y*focal/14)
        # Interior patch spans many pattern texels. Nearest-filter boundaries can
        # move by a pixel with float roundoff; report both mean and bad-pixel rate.
        region = np.s_[max(118,py-23):py+24,px-37:px+38]
        a,b = actual[region],reference[region]
        delta = np.abs(a-b)
        mean = float(delta.mean()); bad = float((delta.max(axis=2)>3).mean())
        passed = mean <= .7 and bad <= .025 and float(b.mean()) > 3
        results.append(dict(name=case["name"],mean_error=mean,max_error=int(delta.max()),bad_fraction=bad,passed=passed))
        print(f"{'PASS' if passed else 'FAIL'}: {case['name']} mean={mean:.4f} bad={bad:.3%}",flush=True)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"build/release/microsoft/genesis.exe")
    parser.add_argument("--output",type=Path,default=ROOT/"artifacts/texture-coordinate-regression")
    parser.add_argument("--report-only",action="store_true")
    parser.add_argument("--probes",action="store_true")
    args = parser.parse_args()
    output = args.output.resolve(); cases = generate(output)
    suffix = "_probes" if args.probes else ""
    results = verify(render(args.exe.resolve(),output,"actual"+suffix),render(args.exe.resolve(),output,"reference"+suffix),cases)
    (output/("results"+suffix+".json")).write_text(json.dumps(results,indent=2))
    raise SystemExit(0 if args.report_only or all(r["passed"] for r in results) else 1)
