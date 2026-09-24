# Bruneton atmosphere LUTs

These lookup tables are generated from Eric Bruneton's official
`precomputed_atmospheric_scattering` CPU reference implementation, using its
Earth parameters and four scattering orders. Genesis samples compact RGB
copies at 680, 550, and 440 nm at runtime; the full 47-wavelength cache is
ignored because it is large and can be regenerated.

Regenerate in the Microsoft Release build tree:

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target BrunetonBake
build/release/microsoft/BrunetonBake.exe assets/atmosphere/bruneton
```

Upstream code is vendored in `thirdparty/precomputed_atmospheric_scattering`
at commit `34f14e745cff948f4ca3157d1b62a445ffa7286f` under its BSD 3-Clause license.
