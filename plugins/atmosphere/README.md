# Genesis Atmosphere

Genesis-owned Bruneton atmosphere integration. The plugin owns Earth physical
parameters, sun placement, day-cycle state, and the Python-facing API. Genesis
integrates optical depth through spherical Rayleigh, Mie, and ozone density
profiles while baking the renderer's environment cube. The visible atmosphere
uses Genesis's own precomputed Bruneton lookup tables.

Python example:

```python
import genesis

genesis.atmosphere.earth()
genesis.atmosphere.sun(azimuth=30.0, elevation=42.0, intensity=5.0)
genesis.atmosphere.exposure(radiance=1.0)
genesis.atmosphere.ground_albedo(0.30, 0.15, 0.14)
```

For a moving day cycle instead of an explicitly positioned sun:

```python
genesis.atmosphere.time_of_day(-0.4, animate=True, speed=300.0)
```

The model and Earth constants follow Eric Bruneton's precomputed atmospheric
scattering work: https://ebruneton.github.io/precomputed_atmospheric_scattering/
