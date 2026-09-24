# Renderer backend boundaries

Genesis keeps scene descriptions and engine-facing APIs independent from any
rendering implementation.

## Shared `src/` code

- `RenderScene.*`: renderer-neutral camera, light, probe, window, and quality data.
- `ScriptRuntime.*`: Python bindings that populate the shared render scene.
- `GameplayWorld.*`: entity ownership and transforms.
- `DiffuseProbeGrid.h`: CPU geometry visibility structures with no GPU API types.
- `materials/`: glTF texture-coordinate resolution, tangent generation, and color/data mipmaps.
- `lighting/`: CPU environment-lighting math and probe-atlas packing.

Shared code must not include bgfx, D3D, or backend shader headers.

## Backend code

- `raster/`: the active bgfx raster renderer and its GPU passes/resources.
- `hybrid/`: reserved for a future raster-plus-ray-query implementation.
- `path_tracer/`: reserved for a future accumulated path tracer.

Backend code may consume `RenderScene`, but shared code must never depend on a
backend. New GPU passes belong in focused backend modules rather than in the
backend coordinator. The removed NVIDIA RTXPT submodule is not required or
preserved.
