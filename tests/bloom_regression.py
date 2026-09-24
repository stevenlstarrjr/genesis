"""Production bloom readbacks: subpixel glints, smooth halos, genuine emission.

No synthetic bloom shader. Tiny emissive glTF quads feed the normal PBR pipeline.
Compare bloom-on/off screenshots on a black background, excluding geometry cores.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
from material_ao_regression import Fixture, ROOT


def script(output,name,model,enabled,width=1280,height=720,camera_distance=10):
    (output/(name+".py")).write_text("\n".join([
        "import genesis as gx",f"gx.window.configure(width={width},height={height},vsync=False)",
        'gx.renderer.quality("medium")',f"gx.renderer.realtime(bloom={enabled})",
        'gx.color.agx(look="neutral",exposure=0,auto_exposure=False)',
        'gx.sky.earth().sun(intensity=0).radiance(0)',
        f'gx.camera.look_at(position=(0,0,{camera_distance}),target=(0,0,0),fov=50)',
        f"gx.scene.load({json.dumps(str(model))})"]) + "\n")


def generate(output, width=1280, height=720):
    output.mkdir(parents=True,exist_ok=True)
    focal = height/(2*math.tan(math.radians(25)))
    def quad(fixture, cx, cy, size, intensity):
        fixture.quad("emitter",(0,0))
        material = fixture.doc["materials"][-1]
        material["pbrMetallicRoughness"].update(baseColorFactor=[0,0,0,1],metallicFactor=0)
        material["emissiveFactor"] = [1,1,1]
        material["extensions"] = {"KHR_materials_emissive_strength":{"emissiveStrength":intensity}}
        corners = [(cx-size/2,cy-size/2),(cx+size/2,cy-size/2),(cx+size/2,cy+size/2),
                   (cx-size/2,cy-size/2),(cx+size/2,cy+size/2),(cx-size/2,cy+size/2)]
        positions = [((width/2-x)*10/focal,(height/2-y)*10/focal,0) for x,y in corners]
        fixture.doc["meshes"][0]["primitives"][-1]["attributes"]["POSITION"] = fixture.accessor(positions,"VEC3",3)
    records = {}
    for name,intensity in (("glints",64),("extreme",60000),("emitters",16),("black",0)):
        fixture = Fixture(output)
        fixture.doc["extensionsUsed"].append("KHR_materials_emissive_strength")
        centers = []
        if name in ("glints","extreme"):
            # Cover every integer phase of the old quarter-resolution sampling
            # grid. Whole-pixel translation preserves source coverage exactly.
            for row in range(4):
                for col in range(4):
                    x = 192+col*160+.5+col; y = 210+row*128+.5+row
                    quad(fixture,x,y,1,intensity); centers.append([x,y])
        else:
            centers = [[320,420],[640,420],[960,420]]
            for i,(x,y) in enumerate(centers): quad(fixture,x,y,32,intensity*(.25 if i==0 else 1 if i==1 else 4))
        model = fixture.write(name)
        records[name] = centers
        for enabled in (False,True):
            script(output,name+("_on" if enabled else "_off"),model,enabled,width,height)
    (output/"cases.json").write_text(json.dumps(records,indent=2))
    return records


def uniform_checks(exe,output):
    results = []
    # Odd dimensions exercise every non-power-of-two pyramid level. A uniform
    # field must retain thresholded radiance: no gain from adding mip levels and
    # no global dimming from normalized bright-outlier suppression.
    for i,intensity in enumerate((.25,4,60000)):
        for reference in (False,True):
            fixture = Fixture(output)
            fixture.doc["extensionsUsed"].append("KHR_materials_emissive_strength")
            fixture.quad("uniform",(0,0),size=50)
            material = fixture.doc["materials"][-1]
            material["pbrMetallicRoughness"].update(baseColorFactor=[0,0,0,1],metallicFactor=0)
            material["emissiveFactor"] = [1,.4,.1]
            # Intensities selected outside the soft knee; reference emission is
            # exactly the expected bloom composite, with bloom itself disabled.
            value = .28*max(intensity-1,0) if reference else intensity
            material["extensions"] = {"KHR_materials_emissive_strength":{"emissiveStrength":value}}
            name = f"uniform_{i}_"+("reference" if reference else "actual")
            # Keep the reference close: the normal composite includes distance
            # fog, while isolated bloom deliberately does not.
            script(output,name,fixture.write(name),not reference,1001,733,.2)
        a = render(exe,output,f"uniform_{i}_actual","bloom")
        b = render(exe,output,f"uniform_{i}_reference")
        error = float(np.abs(a[118:]-b[118:]).max())
        result = dict(intensity=intensity,max_error=error,passed=error<=2)
        results.append(result)
        print(f"{'PASS' if result['passed'] else 'FAIL'}: uniform {intensity} RGB error {error}",flush=True)
    return dict(samples=results,passed=all(r["passed"] for r in results))


def render(exe,output,name,debug=None):
    args = [str(exe),"--script",str(output/(name+".py")),"--screenshot",str(output/name)]
    if debug: args += ["--debug-view",debug]
    result = subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=120)
    (output/(name+".log")).write_text(result.stdout+result.stderr)
    if result.returncode: raise AssertionError(f"Renderer failed: {output/(name+'.log')}")
    with Image.open(output/(name+".png")) as img:
        return np.asarray(img.convert("RGB"),dtype=np.float64)


def measure(on,off,centers,core):
    delta = np.maximum(on-off,0).mean(axis=2)
    records = []
    yy,xx = np.mgrid[-48:49,-48:49]
    radius = np.sqrt(xx*xx+yy*yy)
    for x,y in centers:
        x,y = int(x),int(y)
        patch = delta[y-48:y+49,x-48:x+49]
        halo = patch[(radius>core)&(radius<40)]
        # Large neighboring-pixel steps away from the source expose the bright
        # rectangular cutoff of the old finite blur kernel.
        dx = np.abs(np.diff(patch,axis=1)); dy = np.abs(np.diff(patch,axis=0))
        edge = max(float(dx[(radius[:,:-1]>core)&(radius[:,:-1]<40)].max()),
                   float(dy[(radius[:-1]>core)&(radius[:-1]<40)].max()))
        records.append(dict(halo_energy=float(halo.sum()),halo_peak=float(halo.max()),edge_step=edge))
    return records


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,default=ROOT/"build/release/microsoft/genesis.exe")
    parser.add_argument("--output",type=Path,default=ROOT/"artifacts/bloom-regression")
    parser.add_argument("--report-only",action="store_true")
    parser.add_argument("--skip-uniform",action="store_true",help="For old builds without the isolated bloom debug view")
    args = parser.parse_args(); output = args.output.resolve(); exe = args.exe.resolve()
    cases = generate(output); results = {}; passed = True
    for name,centers in cases.items():
        off = render(exe,output,name+"_off"); on = render(exe,output,name+"_on")
        if name == "black":
            error = float(np.abs(on[118:]-off[118:]).max())
            result = dict(max_error=error,passed=error==0)
        else:
            metrics = measure(on,off,centers,24 if name=="emitters" else 4)
            energy = [r["halo_energy"] for r in metrics]
            spread = (max(energy)-min(energy))/max(max(energy),1)
            edge = max(r["edge_step"] for r in metrics)
            good = min(energy)>50
            if name != "emitters": good = good and spread<.12 and edge<25
            else: good = good and energy[0]<energy[1]<energy[2]
            result = dict(samples=metrics,phase_spread=spread,max_edge_step=edge,passed=bool(good))
        results[name] = result; passed = passed and result["passed"]
        summary = {key:value for key,value in result.items() if key!="samples"}
        print(f"{'PASS' if result['passed'] else 'FAIL'}: {name} {json.dumps(summary)}",flush=True)
    if not args.skip_uniform:
        results["uniform"] = uniform_checks(exe,output)
        passed = passed and results["uniform"]["passed"]
    (output/"results.json").write_text(json.dumps(results,indent=2))
    raise SystemExit(0 if args.report_only or passed else 1)
