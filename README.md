# Genesis

Browser/WebAssembly build instructions: [WebGPU editor](docs/web-build.md).

Genesis is a Python-driven real-time engine with a bgfx/D3D11 raster PBR
renderer. PocketPy is embedded for authoring demos, examples, and engine
scripts.

## Dependencies

- `thirdparty/entt`: vendored EnTT 3.16 gameplay ECS.
- `thirdparty/pocketpy`: embedded Python runtime.
- `thirdparty/SDL`: Genesis platform layer.
- `thirdparty/bgfx`, `bimg`, and `bx`: real-time raster renderer.
- `thirdparty/thorvg`: vector rendering for engine UI surfaces.
- `thirdparty/yoga`: responsive engine UI layout.
- `assets/glTF-Sample-Models`: Khronos glTF sample assets.

Clone all dependencies with:

```powershell
git submodule update --init --recursive
```

## Build

```powershell
cmake -S . -B build/release/microsoft/.cmake
cmake --build build/release/microsoft/.cmake --config Release --target Genesis -j 8
```

The Release executable is written to `build/release/microsoft/genesis.exe`.
Generated CMake metadata remains in the platform's hidden `.cmake` directory.
Debug builds use `build/debug/microsoft`; other targets select `apple`, `linux`,
or `web` beneath the same Release/Debug level.

## Projects, scenes, and Python

Genesis accepts a project directory containing `genesis.project`, a `.gscene`
file, or a Python script. The project settings designate a main scene, and the
scene references its root and node Python scripts. With no target, Genesis
opens a ThorVG/Yoga project launcher that also accepts dropped paths.

```powershell
build/release/microsoft/genesis.exe examples/hello_project
```

Run another script with `--script`, for example:

```powershell
build/release/microsoft/genesis.exe --script demos/clouds.py
```

See [docs/projects-and-scenes.md](docs/projects-and-scenes.md) for the project
and scene formats.

The launcher and HUD share the renderer-independent `genesis_ui` C++ library:
retained widgets, Yoga flex layout, ThorVG drawing, pointer/keyboard input and
DPI-aware surfaces. Editor controls include text/number fields, checkboxes,
sliders, trees, tabs and scrollable inspectors. Run `genesis.exe --ui-workbench`
to try the interactive editor-control workbench. See
[docs/ui-toolkit.md](docs/ui-toolkit.md) for the API, tests and current scope.

Capture a deterministic verification frame with:

```powershell
build/release/microsoft/genesis.exe --screenshot artifacts/screenshots/verification
```

Scripts can register named camera poses for broader regression coverage. The
wall test also exposes AO and contact-shadow diagnostics:

```powershell
build/release/microsoft/genesis.exe --script demos/shadow_wall_regression.py --camera wall-upward --screenshot artifacts/screenshots/upward
build/release/microsoft/genesis.exe --script demos/shadow_wall_regression.py --camera wall-surface --debug-view contact --screenshot artifacts/screenshots/contact
```

## Renderer

The renderer currently includes glTF materials, GGX metallic/roughness PBR,
GPU skeletal animation, Bruneton atmosphere, an HDR prefiltered sky cubemap,
two-way blended box-projected local reflection probes stored in texture-array
atlases with split-sum image-based lighting,
script-authored point lights with GGX shading, finite-radius falloff, and
distance/intensity-selected cubemap shadows,
shadowed spot lights with smooth inner/outer cone falloff,
LM-63 Type C IES photometric profiles and projected spotlight cookies,
one-sided rectangular area lights with finite-emitter GGX response,
volumetric fog and sun shafts, cascaded sun shadows, SSAO, contact shadows,
HDR emissive materials with bounded direct-light sampling, soft-knee bloom,
histogram auto exposure,
AgX-style tone mapping, Low/Medium/High GPU quality
presets, and per-pass GPU timing diagnostics.

Controls and current renderer status are documented in [handoff.md](handoff.md).
The public Python module is in `python/genesis/__init__.py`; see
[docs/python-api.md](docs/python-api.md) for the scripting API.

Renderer-neutral scene and light descriptions live in `src/RenderScene.*`.
The active bgfx implementation lives under `src/backends/raster/`; the hybrid
and path-tracer directories are intentionally empty extension points. Backend
ownership rules are documented in `src/backends/README.md`.

Run `demos/local_light_showcase.py` to compare point, spot, and rectangular area
lights on a black-environment ground plane without sun or ambient illumination.
The demo also enables optional emissive source meshes so point bulbs, spot
lenses, and rectangular panels are visibly distinct and produce true bloom.
Run `demos/emissive_bloom.py` to compare the 1x-16x glTF emissive-strength
panels and their HDR bloom response without any scene lighting.
