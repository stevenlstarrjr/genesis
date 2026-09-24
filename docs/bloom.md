# Raster bloom

`src/backends/raster/BloomRenderer.*` owns the HDR bloom resources. It does not
change the PBR material, normal, lighting, emission or source HDR scene.

The old direct quarter-resolution reduction skipped twelve of sixteen possible
single-pixel phases, while its short blur could turn a bright reflection into a
small rectangular or pink-white blob. Bloom now uses:

1. A half-resolution, overlapping 13-tap reduction with a soft knee (threshold 1,
   knee 0.5) and normalized bright-outlier weighting on the bloom branch only.
2. Five successive reductions, ending at approximately 1/32 resolution.
3. Four filtered reconstructions combining fine Gaussian detail and coarse tent
   filtering. Convex blending preserves constant radiance instead of multiplying
   it by the number of pyramid levels.

The first reduction's weight is `1 / (1 + peakRGB / 16)`, normalized by the sum
of weights. This attenuates isolated intense pixels without globally dimming a
uniform luminous surface. It follows the normalized weighted-average approach
discussed in Jorge Jimenez's [post-processing presentation and comments](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/).
All intermediate outputs use the existing finite HDR storage boundary.

The final half-resolution bloom is added to HDR at intensity 0.28, before
exposure and tone mapping. Disabling bloom clears its output to black. Explicit
target dimensions are recreated on resize, including odd/small viewports. The
pyramid costs nine passes and nine RGBA16F targets (about two-thirds of one
full-resolution target's pixel count). This is spatial filtering, not temporal
antialiasing; it does not guarantee removal of every moving subpixel glint.

```python
gx.renderer.realtime(bloom=True)
gx.renderer.debug_view("bloom")  # isolated bloom through the normal display transform
```

`--debug-view bloom` is also available from the command line. `invalid` continues
to identify nonfinite HDR/bloom/fog values.

Run `python tests/bloom_regression.py` for production GPU readbacks of all sixteen
single-pixel phases, extreme HDR glints, real emissive panels, black input and
uniform color fields at odd resolutions. It compares bloom-on/off output outside
emitter cores and checks halo consistency, edge smoothness, positive intensity
response and constant-field normalization. `demos/emissive_bloom.py` is the visual
emission showcase.
