# Genesis Python API

Genesis executes the selected script with embedded PocketPy **before opening
the SDL3 window**. The API configures startup and can populate the native EnTT
gameplay registry; it does not yet expose per-frame Python callbacks, renderable
entity spawning, live setters, or hot reload. Standard CPython binary packages
are not available inside PocketPy.

```python
import genesis as gx

gx.window.configure(title="My demo", width=1600, height=900, vsync=True)
gx.renderer.realtime()
gx.color.agx(look="neutral", exposure=3.5)
gx.sky.earth().sun(azimuth=-25, elevation=42, intensity=5)
gx.camera.look_at(position=(-10, 2.5, 0), target=(0, 2.5, 0), fov=60)
gx.camera.fly(speed=1.5)
gx.scene.load("assets/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf")

print(gx.configuration())  # Detached snapshot of the native startup settings.
```

The interface lives in `python/genesis/__init__.py`, not an embedded C++ string.
Native bindings and settings are isolated in `src/ScriptRuntime.cpp` and `.h`.
Helper-module imports search the selected script's directory, then Genesis's
`python` directory, then the engine repository. `__file__` is set for the script.
Add Genesis's `python` directory to your editor's Python analysis paths for
completion and docstrings. Run demos through Genesis, not `python demo.py`.

## Scene, camera, window

- `scene.load(path)`: one `.gltf` or `.glb` model per run. Relative paths first
  resolve from the calling script's directory, independently of the working
  directory; bundled demos retain the engine repository as a compatibility fallback.
  Missing files and a second model load raise `ValueError`.
- `camera.look_at(position, target, up=(0, 1, 0), fov=60)`: three-component vectors;
  vertical FOV in **degrees**, range 5–150. Invalid/parallel vectors are rejected.
- `camera.view(name, position, target, up=(0, 1, 0), fov=60)`: register a unique
  named startup pose. Select it with `--camera name`; registration does not alter
  the script's default `look_at` pose.
- `camera.fly(speed=1.5)`: **world units per second**, range 0.001–10000.
  Hold Shift for 3x speed. Imported scene scaling affects world units.
- `window.configure(title="Genesis", width=1600, height=900, vsync=True)`:
  initial pixel dimensions; width 320–7680, height 240–4320.

Interactive controls remain WASD, mouse-look, Shift to move faster, and
Ctrl + left-drag to move the sun. Escape exits.

## Gameplay entities

`entities` is backed by a native EnTT registry. Entity IDs contain EnTT's
generation bits, so a handle becomes invalid after destruction even if its slot
is later reused.

```python
hero = gx.entities.create(
    "hero",
    position=(0, 0, 0),
    rotation=(0, 90, 0),       # Euler degrees
    scale=(1, 1, 1),
)
hero.tag("player").tag("friendly")
hero.model("assets/characters/character1.glb")
hero.particles("fluid", dimensions=(8, 6, 8), spacing=0.15)
hero.rename("wanderer")
hero.transform(position=(2, 0, 4))

print(hero.id, hero.valid(), hero.snapshot())
print(gx.entities.count(), gx.entities.all())
hero.destroy()
```

Every entity starts with native `Name`, `Transform`, and `Tags` components.
`model(path)` adds a native `Renderable` component. Genesis includes its glTF or
GLB in the generated Donut scene, applies the entity transform, and enables
animations imported from attached models.
`particles(mode, ...)` adds `fluid`, `cloth`, or `granular` PhysX particles;
`particles(None)` removes them. Cloth defaults to a single layer. A Windows
NVIDIA PhysX GPU build is required to simulate them in Play; see
[PhysX particles](physx-particles.md).
`particles("cloth", pin_edge="left")` pins a cloth X edge to its entity
transform; `right` and `both` are also supported. `model(path,
moving_collider=True)` lets a model's collision mesh follow entity position
and rotation during Play.
`particles("explosion", burst_speed=8, blast_radius=3, blast_impulse=8)`
creates a one-shot burst with flash, fire, sparks, smoke, and a cloth impulse.
`snapshot()` and a transform read return detached Python data. Omitted fields in
a transform update retain their current values. `tag(name, enabled=False)`
removes a tag. Calling state-changing methods through a destroyed/stale handle
raises `ValueError`; `valid()` and repeated `destroy()` safely return `False`.

This is the gameplay ownership layer, not a second render scene graph. The bgfx
renderer owns imported meshes, materials, lights, and skeletal animation.
Entity renderables are synchronized into it when the startup scene is generated;
per-frame EnTT transform sync is not yet exposed.

Renderer-neutral startup data lives in `src/RenderScene.*`. The current bgfx
implementation and all of its GPU resources are isolated under
`src/backends/raster/`.

## Rendering

`renderer.realtime()` retains the startup quality interface used by existing
scripts. Genesis currently uses one real-time bgfx raster pipeline. Defaults:

```python
gx.renderer.realtime(
    samples=1, restir_di=True, restir_gi=True,
    denoise=True, taa=True, bloom=True,
    bounces=12, diffuse_bounces=2,
    history_frames=25, specular_history_frames=40,
    render_scale=1.0,
)
```

The `bloom` argument controls the raster pipeline's HDR bloom pass. Bright pixels
are soft-thresholded before tone mapping, filtered through a multiresolution
pyramid with isolated-glint suppression, and composited back into the HDR scene.
See [bloom.md](bloom.md) for the implementation and isolated `bloom` debug view. This applies uniformly
to emissive materials and bright point-, spot-, area-, IES-, and cookie-light
surface contributions. A light in empty space has no visible pixels to bloom;
use emissive source geometry or volumetric scattering when the emitter itself
needs to appear luminous. Run `demos/emissive_bloom.py` for the isolated 1x-16x
emissive-strength regression, or `demos/local_light_showcase.py` for local lights.

Opaque, unskinned glTF meshes with nonzero emissive factor also cast direct
light onto nearby surfaces. The renderer groups emissive triangles by face
direction, then splits broad or separated groups spatially. It uses up to four
finite-area samples per frame, prioritizing their
emitted power and camera distance. This follows node and entity transforms and
requires no Python light call. Emissive textures modulate the emitted color:
each triangle contributes an area-weighted average of four interior texture
samples using its authored UV set and texture transform. The approximation
does not reproduce high-frequency texture detail on receiving surfaces, and
excludes alpha-masked, blended, and skinned emitters. Selected emissive lights
use the local shadow maps, so up to four local lights can cast shadows at a time.
The renderer keeps a selected shadow caster until another candidate exceeds
its score by about 15%, reducing visible switching as the camera moves.
Authored lights whose range misses all visible mesh bounds do not consume a
shadow slot. One-sided area lights facing away from all visible mesh bounds
also do not consume one. A conservative outer-cone check filters spotlights
aimed away from visible meshes.
When more than four eligible lights compete, captures favor distinct influence
regions, reducing redundant maps for closely clustered lights.
Their shadow filter estimates blocker distance and orients its soft edge to the
emitter's span. When a wide emitter is the only shadow candidate, it uses two
local captures at points chosen from the triangles' area-weighted spread.
It blends their visibility by the receiver-facing contribution of each point.
Other emitters use one center capture, so broad or disconnected meshes can
still produce approximate shadow coverage. Nearby coplanar clusters from the
same glTF primitive can reuse one of those captures when all shadow slots are
occupied. The filter uses that capture's actual origin; distant or layered
clusters do not share it.
When multiple samples come from one glTF primitive, each shadow capture omits
its selected cluster and coplanar pieces of the same emitting surface; separated
layers can still block it.
Run `demos/emissive_mesh_lighting.py` for the uniform panel or
`tests/emissive_texture_regression.ps1` for red and blue textured controls.
`tests/emissive_mutual_shadow_regression.ps1` checks occlusion between panels
that share one primitive. `tests/emissive_dual_origin_regression.ps1` checks
the two-capture path for one wide triangle.
`tests/emissive_shared_shadow_regression.ps1` checks the third-cluster case.
`tests/emissive_distant_shadow_regression.ps1` checks three independent
captures under separated panels, including the center shadow. Its `-FourPanels`
option checks four independent captures.

`gx.lights.visualize(enabled=True, intensity=8, point_size=0.30,
spot_size=0.45, area_scale=1)` adds optional emissive source geometry for every
configured local light. Point lights use spheres, spot lights use oriented
circular lenses, and area lights use their configured rectangle. The source
intensity is HDR emission and therefore feeds bloom. Visualization defaults off
so existing scenes remain unchanged; these meshes are presentation geometry and
do not replace light shading or participate in shadow-map casting.

The remaining compatibility arguments are validated and retained in
`configuration()`, but do not alter the current raster render graph.
`renderer.reference(samples=1024, bounces=12)` is likewise a compatibility
configuration entry and does not enable an accumulated renderer.

Select a raster workload with `renderer.quality(preset="medium",
gpu_timings=True)`. Medium is the default and the recommended RTX 2060 Super
starting point. The timing overlay reports smoothed total GPU time plus shadow,
scene, AO/contact, volumetric, and post-processing groups.
Press F6 in the standalone viewer or editor to cycle medium, high, and low
while it is running. The editor also has Low, Med, and High buttons in its
viewport toolbar. The renderer rebuilds the shadow, reflection, AO, and fog
targets for the selected preset and refreshes the environment and local probes.

| Preset | Shadows | Reflection cubes | AO/contact | Volumetrics |
| --- | ---: | ---: | ---: | ---: |
| `low` | 512² | 64², 7 mips | quarter resolution, 6/4 samples | quarter resolution, 8 steps |
| `medium` | 1024² | 128², 8 mips | half resolution, 12/8 samples | half resolution, 16 steps |
| `high` | 2048² | 256², 9 mips | full resolution, 16/12 samples | full resolution, 24 steps |

The first AO/contact number is hemisphere samples; the second is contact-ray
steps. `gpu_timings=False` disables per-view GPU queries and hides the timing
line. For repeatable comparisons, `--quality low|medium|high` overrides the
script setting. A `--screenshot` run prints one machine-readable
`GPU_TIMINGS` line before saving the image.

`renderer.debug_view("ao")` and `renderer.debug_view("contact")` replace the
final shaded image with the selected bilateral-upsampled visibility channel.
White means visible and darker pixels mean more occlusion. Use `"none"` for the
normal composition. `--debug-view ao|contact|material-ao|none` overrides the script setting.

`renderer.debug_view("material-ao")` displays the glTF material's baked AO factor,
including texture strength, separately from screen-space AO. The renderer imports
`occlusionTexture` automatically and applies it only to indirect light; see
[material occlusion](material-occlusion.md) for supported UVs, samplers, and tests.

CLI-only `invalid` and `color-chart` views, plus deterministic `--capture-orbit`,
are described in [HDR render regression](hdr-render-regression.md).

## Reflection probes

Local probes also supply diffuse interior lighting automatically. The renderer
convolves the captured scene into a diffuse irradiance cube and blends it with
outdoor ambient lighting using the same influence bounds. No additional API call
is needed. For each configured volume, Genesis builds a roughly three-meter static
probe grid from World geometry. PBR interpolates the eight neighboring grid probes,
rejects samples hidden behind walls, and uses their directional sky, ground, and
blocked fractions to combine outdoor ambient light with the captured interior
irradiance. Dynamic geometry receives this lighting but does not block the static
grid. Captured surface lighting currently excludes direct sunlight pending
dedicated probe shadows.

The sky supplies the global fallback reflection. Add box-projected local probes
for interiors or other bounded spaces before loading the scene:

```python
gx.reflections.probe(
    "Sponza Interior",
    position=(0, 4, 0),
    bounds_min=(-13, 0, -9),
    bounds_max=(13, 13, 9),
    blend_distance=2,
    priority=10,
)
```

The position is the cubemap capture point. Bounds define the box-projection,
selection, and diffuse-grid volume; the position must lie inside it.
`blend_distance` fades the local result to the global sky near the box boundary
and cannot exceed the smallest half extent. When volumes overlap, Genesis prefers
containing probes, then higher priority, then the closest bounds. The best two
probes are captured and GGX-prefiltered into texture-array atlases. Their specular
and diffuse results blend from per-surface bounds weights, then fade to the global
sky where their combined influence falls below one. Each configured volume has
its own static diffuse grid; the primary resident probe supplies the currently
bound visibility grid.
`gx.reflections.clear()` removes all configured probes.

## Local lights

Genesis supports up to four script-authored point lights. Colors are linear RGB;
`intensity` scales emitted radiance and `radius` is the hard influence distance.
The edge of the radius fades smoothly. Point lights use the same metallic/roughness
GGX response as sunlight and are included when local reflection probes are captured.

```python
gx.lights.point(
    "Warm arch",
    position=(-4.5, 3.2, 0),
    color=(1.0, 0.32, 0.08),
    intensity=85,
    radius=9,
    shadow_bias=0.02,
    casts_shadows=True,
    ies="assets/ies/2fc3e48c6b767571aa1731eacda4c39e.ies",
)
```

The renderer keeps two point-shadow cubemaps resident. When more than two lights
request shadows, it selects the strongest lights relative to camera distance each
frame; the remaining lights continue to illuminate without shadows. `shadow_bias`
is a world-space receiver offset and may need slight adjustment for unusually
small or large scenes. Set `casts_shadows=False` for decorative fill lights that
do not need cubemap rendering. Point shadows also participate in local reflection
probe capture. A slope-scaled normal offset and five-tap cubemap PCF suppress
self-shadowing bands on grazing and finely tessellated surfaces.
Point lights accept an optional LM-63 `ies=` profile. Genesis currently supports
Type C files with `TILT=NONE`; candela values are normalized so the light's
`intensity` remains the peak output control. Point-light profiles use a fixed
world orientation with their photometric zero axis pointing downward.

Up to four spot lights use the same GGX shading and shadow atlas. `direction`
points outward from the light and is normalized automatically. Cone angles are
half-angles in degrees; illumination is full inside `inner_angle`, fades smoothly,
and reaches zero at `outer_angle`.

```python
gx.lights.spot(
    "Warm downlight",
    position=(-4.5, 6.5, 0),
    direction=(0.15, -1, 0),
    color=(1.0, 0.24, 0.05),
    intensity=260,
    radius=12,
    inner_angle=20,
    outer_angle=34,
    shadow_bias=0.02,
    up=(0, 0, 1),
    cookie="assets/light_cookie/300px-Cookie_tutorial_texture_flashlight.png",
    ies="assets/ies/2fc3e48c6b767571aa1731eacda4c39e.ies",
)
```

Point, spot, area, and emissive lights compete for four resident shadow slots.
Spot lights accept color `cookie=` textures in PNG, JPEG, BMP, or TGA format,
plus optional Type C `ies=` profiles. `up` controls cookie rotation and the IES
horizontal basis. Cookies and IES can be combined on the same light; both
multiply its emitted radiance. Up to 12 unique patterns share unused layers in
the reflection texture-array atlas, so this feature consumes no additional D3D11
sampler slots and remains compatible with two local probes and local shadows.
At runtime, Ctrl+left-drag aims the first configured cookie spotlight like a
flashlight, yawing and pitching the complete beam and projected texture together.
Ctrl+Shift+left-drag retains sun rotation, and `R` resets both.

Up to two one-sided rectangular area lights provide a finite-emitter approximation
using the closest representative point on the panel. `direction` points out of the emitting face;
`up` establishes its rectangle orientation and is orthogonalized automatically.
Width and height create a rectangular core with soft illumination edges and broaden
the GGX highlight. Intensity is kept stable as the panel dimensions change.
Area lights cast shadows by default. `casts_shadows=False` disables them, and
`shadow_bias` adjusts the receiver offset. Shadow depth is projected from the
panel center, then a 25-tap disk filter scales with the panel's apparent size to
produce stable soft edges. This is an efficient penumbra approximation; it does
not trace independent rays across the emitter or calculate blocker-distance
contact hardening. Area, point, spot, and emissive lights compete for the same four
shadow-map slots.

```python
gx.lights.area(
    "Studio softbox",
    position=(0, 8, 4),
    direction=(0, -0.8944, -0.4472),
    up=(0, -0.4472, 0.8944),
    color=(1.0, 0.72, 0.48),
    intensity=260,
    radius=18,
    width=8,
    height=3,
    shadow_bias=0.05,
    casts_shadows=True,
)
```

Area lights participate in reflection-probe capture. `gx.lights.clear()` clears
point, spot, and area lights.

## Sky and color

- `sky.earth()` resets atmosphere parameters.
- `sky.hdri(path, intensity=1, rotation=0, visible=True)` loads an equirectangular
  `.exr` or `.hdr` environment. The linear HDR image is used both as the visible
  background and for separate diffuse irradiance (SH9) and filtered GGX specular
  lighting. `rotation` is horizontal degrees. Set `visible=False` to retain its
  lighting and reflections while drawing the procedural atmosphere behind it.
  Source HDRI radiance is retained in RGBA32F (including solar values above the
  half-float range). Non-finite source pixels are rejected during asset loading.
  The source mip chain preserves solid-angle-weighted energy. See
  [environment lighting](environment-lighting.md) for implementation and tests.
- `sky.sun(azimuth=0, elevation=35, intensity=5)` uses **degrees**, not radians.
  It selects manual sun control. Intensity is the atmosphere plugin's multiplier,
  not a calibrated lux value.
- `sky.radiance(multiplier=1)` scales environment radiance; this is **not EV**.
  Set it to `0` for a black environment with no diffuse ambient or specular IBL.
  Together with `sky.sun(intensity=0)`, this isolates script-authored local lights.
- `sky.time_of_day(time=-0.4, animate=False, speed=300)` selects the plugin's
  normalized [-1, 1] day coordinate. Speed is simulated seconds per second;
  animation additionally depends on the renderer's scene-time advancement.
- Advanced controls: `ground_albedo(red, green, blue)`, `rayleigh(red, green,
  blue)`, `aerosols(scattering, extinction, anisotropy)`, `ozone(red, green, blue)`,
  and `radii(ground_km, atmosphere_km)`. Scattering/extinction use inverse km.
- `color.agx(look="neutral", exposure=0, auto_exposure=False)` selects **AgX**.
  Exposure is **EV**, range -12–12; +1 EV doubles linear exposure. Looks are
  `neutral`, `medium_high`, `punchy`, and `golden`.
- `color.auto_exposure(enabled=True, min_ev=-6, max_ev=6)` controls adaptation.
  `color.aces(exposure=0)` remains an explicit compatibility opt-in, never the
  Sponza demo's transform.

All public controls validate types, finite values, and ranges before changing
native settings. `configuration()` returns a copy, including an `entities`
snapshot with `count` and `items`; it is not a live editable object.
`load_model`, `atmosphere`, and `atmosphere.exposure()` remain compatibility
aliases; new code should use `scene.load`, `sky`, and `sky.radiance`.

## Clouds

Clouds live in the atmosphere plugin and are separate from the atmosphere's
scattering settings. `clouds` and `sky.clouds` are the same object.

```python
gx.clouds.preset("partly_cloudy")
gx.clouds.layer("low", kind="cumulus", coverage=0.45, density=1.4,
                altitude=1800, thickness=1400, size=4000, detail=0.5)
gx.clouds.layer("high", kind="cirrus", coverage=0.2, density=0.3,
                altitude=9000, thickness=1200, size=8000)
gx.clouds.wind(speed=8, direction=45)
gx.clouds.quality("balanced", update_hz=2)
gx.clouds.seed(7)
```

Presets returned by `clouds.presets()`:

| Preset | Appearance |
| --- | --- |
| `clear` | Atmosphere and sun, no clouds |
| `partly_cloudy` | Separated cumulus banks with sparse high clouds |
| `broken` | More extensive, deeper cumulus banks |
| `overcast` | Continuous low stratus deck |
| `storm` | Dense, deep low clouds under a second deck |
| `high_clouds` | Thin elongated cirrus bands |
| `legacy` | The original hardcoded Genesis cloud layer |

These are art-directed cloud families, not a complete meteorological taxonomy.
`clouds.types()` returns `cumulus`, `stratus`, `cirrus`, and `storm`; either
of the two layers (`low`, `high`) can use any family.

`layer()` retains omitted appearance fields and enables that layer unless
`enabled=False` is supplied. Distances are **metres above the sky's ground
radius**, independently of imported model scale:

| Control | Range / meaning |
| --- | --- |
| `coverage` | 0–1; weather-map threshold, not an exact visible-sky percentage |
| `density` | 0–8; optical density multiplier; 0 makes the layer transparent |
| `altitude` | 250–20000 m; layer base |
| `thickness` | 100–10000 m; vertical depth |
| `size` | 500–40000 m; horizontal feature scale |
| `detail` | 0–1; edge erosion and billow detail |

Layer tops must stay at or below 25000 m. When both layers are enabled, the low
layer's top must be at or below the high layer's base. Invalid changes raise
an exception without partially changing settings.

- `clouds.clear()` selects the clear preset and disables both layers.
- `clouds.enable(False)` hides clouds without discarding layer settings;
  `clouds.enable()` restores them.
- `clouds.wind(speed=8, direction=45)` uses **m/s** and horizontal sky-plane
  degrees (-360–360). Speed range is 0–150. The default speed is **0** (static).
  Wind advances natively in realtime mode even when model animation is off;
  reference rendering freezes it.
- `clouds.quality("low" | "balanced" | "high", update_hz=2)` selects 16, 24,
  or 40 integration steps per layer, at 64, 128, or 256 pixels per cube face.
  The smooth atmosphere/cloud result is cached together; the sharp sun disk
  is still evaluated at the full environment-map resolution, along with a
  narrow horizon band to avoid blending ground color into the sky. Lower
  settings soften small cloud features but reduce update cost.
  Wind invalidates the cached sky at most
  `update_hz` times per simulated second (0.5–10). Higher rates cost more and
  may cause frame-time spikes. There is no interpolation between sky updates.
- `clouds.seed(integer)` uses 0–65535 for reproducible layouts. Changing the
  seed changes cloud placement, not frame-to-frame noise.

Preset changes replace layer appearance but preserve wind, quality, and seed.
`sky.earth()` resets atmosphere parameters, not clouds. Scripts that never call
the cloud API retain the previous sky; `demos/sponza.py` remains unchanged.

This implementation ray-marches **distant spherical cloud layers into a cached
environment map**. They affect the visible sky, environment lighting, sun
visibility, and reflections. It is not a local fly-through volume: camera
altitude does not move you into the cloud layers, and it does not produce
spatially varying cloud shadows across terrain, rain, lightning, or weather
simulation. Lighting uses a lightweight scattering approximation. Python
configures startup; live Python weather callbacks are not yet exposed.

Run the showcase with `--script demos/clouds.py`. It starts in the courtyard
and uses neutral AgX with automatic exposure and -1.5 EV compensation, so walking
between courtyard and sky views does not retain a sky-only exposure. Wind
remains enabled. Vsync is off for lower latency; set `vsync=True` if tearing
is objectionable. The accepted indoor Sponza demo keeps its fixed 3.5 EV.
Tests: `--script tests/clouds_api.py --validate-script`.

## Validation and framebuffer captures

```powershell
build/release/microsoft/genesis.exe --script demos/sponza.py
build/release/microsoft/genesis.exe --script tests/python_api.py --validate-script
build/release/microsoft/genesis.exe --script tests/reflections_api.py --validate-script
build/release/microsoft/genesis.exe --screenshot artifacts/screenshots/verification
build/release/microsoft/genesis.exe --quality high --screenshot artifacts/screenshots/high
build/release/microsoft/genesis.exe --script demos/shadow_wall_regression.py --camera wall-upward --screenshot artifacts/screenshots/upward
build/release/microsoft/genesis.exe --script demos/shadow_wall_regression.py --camera wall-surface --debug-view ao --screenshot artifacts/screenshots/wall-ao
```

`--validate-script` runs the real embedded API without initializing SDL/D3D11.
Tracebacks and `print()` output go to `genesis.log`; a failed script returns 1.
PowerShell callers needing an exit code from this GUI executable should use
`Start-Process -Wait -PassThru` and read its `ExitCode`.

`--screenshot` uses GPU framebuffer readback in a hidden SDL window, writes a
PNG, and exits after the capture callback completes. It is intended for visual
regression checks, not performance measurement.
An unknown `--camera` name exits with code 2 and prints the names registered by
the selected script.
