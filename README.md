# Genesis

An NVIDIA-focused path-traced renderer based on RTXPT, with SDL3 retained as
the engine platform layer.

## Dependencies

- `thirdparty/SDL`: SDL3 source used by the engine shell.
- `thirdparty/RTXPT`: NVIDIA RTXPT and its pinned recursive dependencies.
- `thirdparty/pocketpy`: embedded Python runtime for demos, examples, and
  engine scripting.

Genesis demos and examples are authored in Python. Native C/C++ code provides
the renderer, platform layer, and Python bindings; Python scripts assemble and
drive the demonstrations.

RTXPT currently uses its own Donut application shell. Integrating the RTXPT
renderer with the SDL3 application lifecycle will be done after the unmodified
renderer sample is validated.

## Build the SDL3 shell

```powershell
cmake -S . -B out/genesis
cmake --build out/genesis --config Release --target genesis
./out/genesis/Release/genesis.exe
```

## Build and run the RTXPT demo

```powershell
cmake -S thirdparty/RTXPT -B build/rtxpt -A x64
cmake --build build/rtxpt --config Release --target Rtxpt -j 8
./thirdparty/RTXPT/bin/Rtxpt.exe
```

Run the executable with `thirdparty/RTXPT/bin` as its working directory so it
can find the sibling `Assets` directory. RTXPT's default and most complete path
uses DirectX 12; Vulkan can be enabled separately later.

## Material support

RTXPT imports glTF 2.0 scenes and provides a path-traced BSDF material model.
It supports textured PBR parameters, emissive geometry, alpha-tested surfaces,
transmission, volumes, and nested dielectric materials. The material and BSDF
shaders are intended to be extended for engine-specific shading models.

## Python demo

The SDL3 shell initializes PocketPy and runs `demos/hello.py` during startup.
This is the initial scripting smoke test; engine and renderer bindings will be
added incrementally as the public scripting API takes shape.
