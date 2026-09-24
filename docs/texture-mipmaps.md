# Material texture mipmaps

All glTF material textures now use the renderer-independent builder in
`src/materials/TextureMips.h`. Image decoding, cache ownership, and bgfx uploads
stay in `src/backends/raster/GltfTextures.*`.

- **Base color and emission:** decode RGB with the piecewise sRGB transfer
  function, average in linear light, and re-encode the mip for the GPU's sRGB
  texture sampler. The decode table covers all 256 input codes, including the
  near-black linear segment.
- **AO, metallic/roughness, and normals:** average their stored linear values
  without a color-space conversion. The shader normalizes sampled normals.
- **Alpha:** average linearly in both modes. Alpha never goes through sRGB.

The original level is copied byte-for-byte. Every subsequent level is explicitly
initialized down to 1x1, including Nx1, 1xN, and rectangular tails. Area-weighted
filtering retains odd-sized edge texels instead of discarding the last row or
column. Non-mip samplers still upload only level zero. GPU texture formats,
sampler settings, and the 16-slot PBR resource budget are unchanged.

This removes the material loader's dependency on the old RGBA8 mip generator,
which did not fill 1D tails. The regression also caught incorrect minified color
values in ordinary 2x2 textures. Before the change, 14 of 17 GPU cases failed;
afterward, every measured case matches its solid-color reference exactly.

## Verification

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis TextureMipTests
ctest --test-dir build/release/microsoft/.cmake -C Release --output-on-failure
python tests/texture_mip_regression.py
python tests/material_ao_regression.py
```

The GPU test generates tiny GLB fixtures under `artifacts/texture-mip-regression/`.
It compares strongly minified textures with independently calculated solid-color
references through the production loader, PBR/emissive shader, alpha blending,
and tonemapper. It covers horizontal/vertical 1D images, rectangular tails,
odd-sized edges, constant colors, a single texel, sRGB averaging, and linear
alpha. `results.json` records the per-case RGB error. `--output <directory>`
keeps separate runs, and `--report-only` measures a known-bad baseline without
returning failure.

CPU checks cover all 256 constant codes, source preservation, independent RGB
channels, linear alpha, complete level dimensions, 1D/NPOT edge energy, and
malformed/overflowing input. Intermediate 8-bit levels introduce normal
quantization error; this is not an HDR texture prefilter. HDRI filtering remains
in the separate [environment-lighting pipeline](environment-lighting.md).

Alpha-coverage preservation for cutout foliage and alpha-weighted color dilation
are not implemented by this change.

The color-space rules follow the [glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
and the [W3C sRGB transfer-function definition](https://www.w3.org/TR/WCAG21/relative-luminance.html).
