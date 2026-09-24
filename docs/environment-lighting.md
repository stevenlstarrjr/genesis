# HDRI diffuse and specular lighting

Genesis separates the two environment-lighting integrals:

- **Diffuse:** integrate the full linear HDRI using each texel's spherical solid
  angle, then convolve the first three spherical-harmonic bands with the cosine
  kernel. Nine RGB coefficients represent irradiance; the material applies
  albedo/pi. This is the standard low-frequency SH approximation, not the roughest
  GGX reflection mip. HDRI mode no longer adds a second hemisphere blend or an
  artificial ambient-light floor. Rotation matches the visible sky, and HDRI
  intensity scales the coefficients once.
- **Specular:** build a full RGBA32F source mip chain using solid-angle-weighted
  averages. The GGX prefilter uses 128 deterministic samples for an HDRI and
  selects source LOD from each sample's probability density and spherical
  footprint. The mirror mip also integrates its output-texel footprint. This
  prevents tiny sun texels from being hit/missed as isolated enormous samples.
  Maximum roughness retains an 8x8-per-face directional signal instead of six
  1x1 face colors. Global and local reflection probes share this roughness/LOD
  mapping; all lower storage mips remain initialized.
  Procedural atmosphere retains its existing 32-sample path.

Source HDR radiance is never gamma encoded or clipped during these integrations.
The existing finite FP16 storage limit is applied only to the output cubemap.
Prefiltering occurs when the environment is rebuilt, not on every camera move.

The renderer-independent math lives in `src/lighting/EnvironmentLighting.*`.
Image decoding and GPU texture upload remain in `src/backends/raster/HdriTexture.*`.
The SH coefficients use uniforms, so the PBR material pass still uses the same
16 texture bindings. Local probe captures receive the same HDRI diffuse lighting
as the main camera.

## Verification

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis EnvironmentLightingTests
ctest --test-dir build/release/microsoft/.cmake -C Release -R environment_lighting --output-on-failure
build/release/microsoft/genesis.exe --script demos/ibl_materials.py
build/release/microsoft/genesis.exe examples/hello_project --capture-orbit 32 --screenshot artifacts/screenshots/ibl-filtered/frame
build/release/microsoft/genesis.exe examples/hello_project --capture-orbit 32 --debug-view invalid --screenshot artifacts/screenshots/ibl-filtered-invalid/frame
python tests/hdr_render_regression.py --invalid artifacts/screenshots/ibl-filtered-invalid
build/release/microsoft/genesis.exe --script tests/ibl_probe_capture.py --capture-orbit 8 --debug-view invalid --screenshot artifacts/screenshots/ibl-probe-invalid/frame
python tests/hdr_render_regression.py --invalid artifacts/screenshots/ibl-probe-invalid --frames 8
```

The CPU regression covers constant and directional irradiance, positive/negative
rotation, RGB channel ordering, and energy preservation for constant, bright,
polar, power-of-two, and odd-sized inputs. The material demo uses only the meadow
HDRI: no additional sun, atmosphere ambient, local lights, or automatic exposure.
The local-probe regression exercises both overlapping atlas slots with a rotated
HDRI and non-unit intensity. The AgX gray chart and the non-HDRI emission/bloom
fixture provide independent output-transform and procedural-path checks.

glTF [material AO textures](material-occlusion.md) now attenuate indirect diffuse
and specular lighting. Environment occlusion/ray tracing and multiple-scattering
specular compensation are not implemented. SH9 is a low-frequency
approximation, and the split-sum reflection filter has the usual approximation
error at grazing angles.

## References

- [Ramamoorthi and Hanrahan: An Efficient Representation for Irradiance Environment Maps](https://cseweb.ucsd.edu/~ravir/papers/envmap/)
- [Colbert and Krivanek: GPU-Based Importance Sampling, section 20.4](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling)
