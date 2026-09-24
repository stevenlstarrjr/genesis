# glTF material ambient occlusion

Genesis imports `material.occlusionTexture` automatically. It samples the red
channel as linear data and evaluates `AO = 1 + strength * (red - 1)`. Omitted
strength is one; explicit zero or a missing/undecodable map means no occlusion.
An ORM image can be shared with metallic/roughness, but sharing is not required:
AO can have its own image resolution, sampler, and texture coordinates.

AO multiplies **indirect diffuse and specular lighting**, including HDRI, local
reflection/diffuse probes, and probe-grid lighting. Direct sun/point/spot/area
lights, emission, opacity, and shadow rendering are not multiplied by material
AO. Probe captures use the same material path. This is artist-baked cavity
occlusion, not dynamic shadows or replacement geometry-based visibility.

The importer resolves AO's `texCoord` (including `TEXCOORD_1` and higher sets)
per primitive and applies its `KHR_texture_transform` scale, rotation, offset,
and optional texCoord override. Missing required UV attributes produce a warning
and disable AO for that primitive instead of sampling arbitrary coordinates.
The same [transform/UV rules](material-texture-coordinates.md) now apply to base
color, metallic/roughness, normals, and emission. Texture-transform animation is
not implemented.

Textures retain independent repeat/clamp/mirror and minification/magnification
filter settings. Cache keys include color space and sampler settings, so one
image referenced with different samplers or as both color and AO cannot alias
incorrectly. Linear data maps (AO, metallic/roughness, and normals) get linear
area-averaged mipmaps with complete 1D tails and odd-size edge coverage. The
shared [mipmap builder](texture-mipmaps.md) also filters base-color and emissive
textures in linear light, retaining sRGB storage and linear alpha. Image format support is unchanged:
embedded GLB or external PNG/JPEG images decoded by the raster image loader.

## GPU resource budget

The PBR pass still binds exactly **16 textures**. Slot 13 is now material AO.
The two old probe-grid resources are packed losslessly into slot 12:

- Every four adjacent R32F visibility distances become one RGBA32F texel.
- Six appended rows store the original RGBA32F lighting coefficients.
- Shader lookups recover the original values with point filtering. No texture
  resolution or float precision is reduced; memory overhead is approximately 5%.

Renderer-independent coordinate, mip, and packing helpers live in
`src/materials/` and `src/lighting/ProbeGridAtlas.h`. GPU texture decoding,
caching, and upload live in `src/backends/raster/GltfTextures.*` rather than
expanding the main renderer file.

## Inspect and verify

```python
gx.renderer.debug_view("material-ao")
```

Or pass `--debug-view material-ao` to the executable. This displays the actual
material AO factor in grayscale, without lighting, exposure, or bloom. White
means unoccluded; black means fully occluded. It is distinct from the existing
`ao` view, which displays screen-space AO. Use `none` to restore normal shading.
The diagnostic reuses the normal buffer's unused alpha channel; independent
MRT blending keeps that metadata separate from transparent color blending.

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis MaterialOcclusionTests
ctest --test-dir build/release/microsoft/.cmake -C Release --output-on-failure
python tests/material_ao_regression.py --exe build/release/microsoft/genesis.exe
build/release/microsoft/genesis.exe --script demos/material_ao.py
build/release/microsoft/genesis.exe --script demos/material_ao.py --debug-view material-ao
```

The GPU regression generates tiny glTF fixtures and checks actual framebuffer
readbacks for strengths, channels, UV sets, texture transforms, independent
samplers, linear minified 1D mips, shared ORM/color images, and blended metadata.
It exercises both local-probe slots and compares AO on/off under isolated direct,
emissive, and indirect lighting. Generated files stay in
`artifacts/material-ao-regression/`. CPU tests cover importer defaults, sampler
mapping, linear/1D/NPOT mips, and lossless atlas round trips up to 405 probes.

Semantics follow the [glTF additional-textures specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials-additional-textures)
and [KHR_texture_transform](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform).
