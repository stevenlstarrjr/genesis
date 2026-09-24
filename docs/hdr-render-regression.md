# HDR precision and output-transform regression

The meadow EXR reaches 159,744 in linear RGB. Loading it directly as RGBA16F
(maximum 65,504) produced infinities. Reflections sampled those values as the
camera moved; bloom expanded individual invalid pixels into rectangular patches.
Genesis now keeps the HDRI source in RGBA32F, rejects non-finite source data, and
limits radiance only when storing lighting results into half-float render targets.
The 65,000 storage ceiling preserves RGB ratios; it is not an exposure control.
A future pre-exposed HDR pipeline could remove that storage ceiling.

The analytic AgX implementation returns display-encoded values already. The
extra gamma encode has been removed for AgX, while the legacy ACES fit still
receives its output encoding. The hello project's compensating -2.5 EV setting
has consequently been reset to 0 EV, with automatic exposure still disabled.

Fog/AO bilateral reconstruction now normalizes weights in log space. Previously,
all depth weights could underflow at silhouettes, returning opaque black fog.

## Reproduce and verify

Run from the repository root. Capture names must be new to avoid mixing runs.
`--capture-orbit N` requires `--screenshot`, accepts 2–360 views, and rotates around
the scene camera's target at deterministic frame intervals with varied elevation.
Without that option the existing single-frame screenshot behavior is unchanged.

```powershell
build/release/microsoft/genesis.exe examples/hello_project --capture-orbit 32 --screenshot artifacts/screenshots/hdr-final/frame
build/release/microsoft/genesis.exe examples/hello_project --capture-orbit 32 --debug-view invalid --screenshot artifacts/screenshots/hdr-invalid-final/frame
build/release/microsoft/genesis.exe --script tests/color_chart.py --debug-view color-chart --screenshot artifacts/screenshots/agx-chart
build/release/microsoft/genesis.exe --script tests/color_chart_aces.py --debug-view color-chart --screenshot artifacts/screenshots/aces-chart
python tests/hdr_render_regression.py --invalid artifacts/screenshots/hdr-invalid-final --agx artifacts/screenshots/agx-chart.png --aces artifacts/screenshots/aces-chart.png
```

These two extra diagnostic modes are CLI-only:

- `invalid`: red = non-finite scene HDR; green = non-finite bloom; blue = non-finite
  or effectively opaque fog. In this short-distance helmet scene everything below
  the HUD should be black. Dense fog in other scenes may legitimately show blue.
- `color-chart`: eight neutral swatches pass through the production output
  transform, independent of scene lighting and exposure. The fixtures select
  neutral looks; the checker requires all three RGB channels to match within two
  8-bit code values. Linear 18% gray maps to approximately 128 under AgX, not 186.

The checker uses ordinary CPython with Pillow and NumPy, not embedded PocketPy.
`--report-only` measures a known-bad baseline without failing the command.
