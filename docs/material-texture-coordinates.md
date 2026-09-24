# Material texture coordinates

All five core glTF material maps have independent texture coordinates: base
color (including alpha), metallic/roughness, normal, emission, and occlusion.
Each resolves `texCoord` against the primitive's `TEXCOORD_n` attributes. The
`KHR_texture_transform` override takes precedence; scale, rotation, and offset
are applied in that order at import time. There is no fixed UV0/UV1 source-set
limit: a referenced UV3 or UV7 works without reserving GPU slots for unused sets.

Missing UVs, a non-vec2 accessor, or mismatched vertex counts log a warning and
disable only that texture on that primitive. The material factor remains in
effect. A shared material is not mutated when one primitive lacks its UV set.
Image/sampler caching remains separate from coordinates, so image reuse does
not force two material maps to share UVs or transforms.
The former shared-base/emission-image heuristic has been removed: emission is
now honored as authored even when both maps reference the same image at full
strength. Assets that incorrectly mark their base color as emissive should be
corrected in the asset, not silently reinterpreted by the renderer.

## Normal maps

- Explicit `normalTexture.scale: 0` bypasses normal perturbation; an omitted
  value remains one. Fractional values scale tangent-space X/Y.
- Authored glTF tangents and their handedness are preserved. Texture transforms
  affect sampling and do not silently replace the artist's tangent basis.
- With vertex normals but no authored tangents, generation uses the **normal map's selected,
  transformed UVs**, not the base-color UVs. Rotated and mirrored coordinates
  therefore generate the corresponding basis and handedness.
- Degenerate UVs get a finite tangent perpendicular to the surface normal.
  The pixel shader orthogonalizes interpolated tangents against the normal and
  guards against zero-length tangents.

The fallback is smooth triangle-derivative accumulation, not MikkTSpace.
Export authored tangents for exact agreement with MikkTSpace-baked normal maps,
especially at mirrored seams. Missing-normal reconstruction, UV-transform
animation, general morph-target support, and alpha-tested shadow silhouettes are
outside this change.

## Code ownership and cost

`src/materials/GltfTextureCoordinates.h` owns the renderer-independent glTF
coordinate resolution and normal-scale defaults. `src/materials/TangentFrame.h`
owns tangent generation. The raster backend uploads five vec2 material UV slots;
the PBR shader packs them into two vec4 varyings plus one vec2. This adds 24 bytes
per vertex and one interpolator compared with the former UV0-plus-AO layout.
The existing 16-texture PBR binding budget is unchanged. Main rendering and local
probe capture share the same draw path.

## Verification

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis TextureCoordinateTests
ctest --test-dir build/release/microsoft/.cmake -C Release --output-on-failure
python tests/texture_coordinate_regression.py
python tests/texture_coordinate_regression.py --probes
python tests/material_ao_regression.py
python tests/texture_mip_regression.py
```

The GPU regression generates multi-UV GLBs and independently UV-baked references,
then compares interior surface patches through production PBR, emission, alpha,
and HDRI lighting. Analytic normal-frame cases compare normal mapping with known
geometry normals, including generated, authored, mirrored, zero-strength, and
degenerate cases. All 46 cases are checked both directly and with a local
reflection probe. Shared-material cases check valid/missing/valid UVs in draw
order. Results and screenshots are in
`artifacts/texture-coordinate-regression/`. `--output` keeps separate runs;
`--report-only` records a known-bad baseline without failing the command.

Verified on the Windows D3D11 release build: 46/46 coordinate cases pass in
both modes, all four CPU suites pass, and existing 18-case AO and 17-case mipmap
regressions pass. The helmet readback differs from the previous build by at most
one 8-bit color value; its 32-frame invalid-value orbit reports zero affected
pixels in scene HDR, bloom, and fog.

Semantics follow [KHR_texture_transform](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform),
the [glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials),
and the [Khronos sample renderer's authored-tangent path](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/material_info.glsl).
