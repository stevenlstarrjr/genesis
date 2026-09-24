# Particle systems and prefabs

Genesis supports a generic visual emitter plus PhysX fluid, cloth, granular, and explosion components in scene files, the C++
SDK, the Python entity API, and the editor Inspector. An enabled component
renders its seed in the Scene viewport. In a Windows PhysX GPU build, Play
creates a PhysX scene for physical modes, advances all effects at 60 Hz, and renders their
positions in both Scene and Game viewports. Stop releases the PhysX scene and
restores the authored state.

Fluid, granular, and explosion debris use `PxPBDParticleSystem`; the fluid phase enables density
constraints. Cloth uses `PxDeformableSurface`, a cooked triangle mesh, and a
deformable surface material. These four modes require an NVIDIA CUDA GPU. The visual emitter
loops its authored layers without a CUDA context. The default
build keeps PhysX optional and rejects Play with a Console message when an
enabled particle component is present.

Open **Project > Particle prefabs** and drag **Campfire** into the Scene viewport.
The prefab at `assets/particle_prefabs/campfire.gprefab` creates one visual emitter
with smoke, flame, spark, and glow layers, plus a warm point light. The Inspector
edits each layer's count, lifetime, spawn position, velocity, acceleration,
radial motion, turbulence, size, colors, opacity fade, and shape. One undo removes the whole
placement, and Save writes its components into the scene. Project files ending
in `.gprefab` can use the same format for future single-entity prefabs. Older
scenes using `mode: "campfire"` load as a visual emitter with editable layers.

```python
import genesis as gx
gx.entities.create("water", position=(0, 3, 0)).particles(
    "fluid", dimensions=(12, 8, 12), spacing=0.15)
gx.entities.create("cloth", position=(3, 4, 0)).particles(
    "cloth", dimensions=(24, 1, 24), stiffness=1000, pin_edge="left")
gx.entities.create("gravel", position=(-3, 3, 0)).particles(
    "granular", dimensions=(12, 8, 12))
gx.entities.create("blast", position=(0, 3.5, 0)).particles(
    "explosion", dimensions=(8, 8, 8), burst_speed=8,
    blast_radius=3, blast_impulse=8, effect_duration=2.5)
gx.entities.create("mist", position=(2, 0, 0)).particles(
    "emitter", layers=[{"name": "Mist", "count": 80, "lifetime": 2,
                        "velocity": [0, 0.7, 0], "size": 0.12,
                        "start_color": [0.7, 0.8, 1, 0.35],
                        "end_color": [0.7, 0.8, 1, 0]}])
gx.entities.create("platform", position=(0, 1, 0)).model(
    "assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb",
    moving_collider=True)
```

`Entity.particles(None)` removes the component. PhysX seeds are centered on the
entity transform. Each physical dimension is 1–128, with at most 65,536 particles total;
cloth needs at least a 2 by 2 grid and exactly one Y layer. The Inspector
exposes spacing, mass, friction, damping, fluid viscosity and cohesion, and
cloth stiffness. Changes participate in undo, redo, duplicate, save, and load.
For cloth, `pin_edge` accepts `none`, `left`, `right`, or `both`. Pinned X-edge
vertices attach through PhysX deformable attachments to the cloth entity's
transform. Moving that transform during Play moves the pins. The Inspector
offers the same edge choices.

An explosion is a one-shot burst when Play starts. Its PBD particles launch
outward while a radial impulse pushes nearby deformable cloth vertices. PhysX
attachments keep pinned edges in place. The viewport draws a short white flash,
expanding fireball and shockwave, sparks, then rising smoke that fades by
`effect_duration`. `burst_speed` controls debris travel; `blast_radius` and
`blast_impulse` control the force on cloth. The effect is procedural and does
not yet include a volumetric fire simulation or repeated triggering.

## Windows GPU build

The validated local build uses PhysX 5.9 source commit
`517a0073715120e114ee055b63b26c95e00d9039`, CUDA 13.4.92, Visual Studio
18 / MSVC 19.51, and an RTX 2060 Super (SM 75). PhysX's published Windows
instructions specify CUDA 12.8; its compiler crashes with the installed MSVC
19.51, so this build uses CUDA 13.4 and the compatibility patch in
`cmake/physx-cuda13.patch`. The patch makes CUDA 13's context and kernel stub
APIs buildable and allows choosing a GPU architecture with `PX_CUDA_SM_ARCH`.
It is local to the PhysX source checkout and is not a change to NVIDIA's
upstream release.

The reproducible build script pins the PhysX checkout, applies the patch,
builds the required libraries and Genesis, and runs the GPU and editor tests:

```powershell
./tools/build-physx-windows.ps1 -CudaRoot 'C:\path\to\cuda-13.4' -SmArchitecture 75
```

It accepts `-BinaryName`, `-PhysxCheckout`, and `-BuildRoot` overrides. The
default output is `build/release/microsoft/genesis-physx-editor.exe`.
`NVCC_PREPEND_FLAGS=-diag-suppress=20011` avoids a CUDA device diagnostic in
the installed MSVC `<cmath>` header. For a manual Genesis build, configure with:

```text
-DGENESIS_WITH_PHYSX_PBD=ON
-DGENESIS_PHYSX_ROOT=<PhysX clone>/physx
-DGENESIS_PHYSX_LIB_DIR=<PhysX Release library and DLL directory>
-DGENESIS_CUDA_ROOT=<CUDA toolkit directory>
```

The Genesis build copies `PhysXGpu_64.dll` beside the executable. The NVIDIA
driver remains the runtime dependency. The `PhysxParticlesTests` target runs
fluid, granular, and cloth on the GPU, then verifies that a moving platform
lifts particles and pinned cloth follows its entity transform.

Play cooks visible glTF meshes in the scene into two-sided triangle colliders.
Their entity and glTF node transforms are applied before cooking. The Mesh
Renderer's **Moving particle collider** option (or Python's
`model(..., moving_collider=True)`) makes a collider kinematic. Its position and
rotation follow entity transform changes at each simulation tick; its scale is
fixed when Play starts. Other mesh colliders remain static. Skinned or animated
meshes are not collision sources. A ground plane at Y=0 also remains. The sample
at `examples/physx_particles` includes a moving-enabled platform and pinned
cloth edge. Its granular particles start just above a separate platform with
higher friction and damping, so they settle as a pile instead of resembling a
second explosion.

The viewport draws fluid and granular particles as shaded camera-facing discs
and cloth as filled, lit triangles, with scene-depth occlusion. Dense fluid and
granular systems draw up to about 4,000 particles per system while simulating
all of them. This is an editor preview rather than a refractive fluid surface.

References: [PhysX GPU simulation](https://nvidia-omniverse.github.io/PhysX/physx/5.7.0/docs/GPURigidBodies.html),
[PhysX Windows SDK](https://github.com/NVIDIA-Omniverse/PhysX/blob/main/physx/documentation/platformreadme/windows/README_WINDOWS.md).
