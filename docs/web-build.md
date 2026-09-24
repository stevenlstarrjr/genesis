# WebAssembly / WebGPU editor

The browser target compiles the real C++ editor, Yoga/ThorVG UI, pocketpy runtime,
and bgfx raster renderer. It is not a JavaScript recreation of the editor.
Desktop builds continue to use D3D11; browser builds use WGSL shaders and WebGPU.

## Build on Windows

Prerequisites: an activated Emscripten 6.0.9 SDK, CMake, Ninja, and a **native**
bgfx `shaderc` built from this checkout. Tools must not come from another bgfx
revision. The SDK can live under `build/release/web/.tools/emsdk` without changing
the system environment. Install and activate it there with the official
`emsdk install 6.0.9` and `emsdk activate 6.0.9` commands.

```powershell
# Build the host shader compiler using the existing native CMake tree first.
cmake --build build/release/microsoft/.cmake --config Release --target shaderc
./tools/build-web.ps1
python tools/serve-web.py
```

The script accepts `-Sdk`, `-ShaderCompiler`, `-CMake`, `-Ninja`, `-Jobs`, and
`-Configuration Debug`. It finds the local SDK and VS 18 tool locations by default,
and restores the calling process's environment after building.

For other hosts, configure with Emscripten's CMake toolchain and Ninja, supplying
`-DGENESIS_HOST_SHADERC=/absolute/path/to/native/shaderc`, `-DCMAKE_BUILD_TYPE=Release`,
and `-DBUILD_TESTING=OFF`, then build the `Genesis` target.

Output lives in `build/release/web/` (or `build/debug/web/`): `genesis.html`,
`genesis.js`, `genesis.wasm`, and `genesis.data`. Serve these four files together.
Do not open the HTML as `file://`; use localhost or HTTPS in a WebGPU-enabled
browser with WebAssembly JSPI (current desktop Chrome/Edge). JSPI lets C++ yield
to GPU promises and browser input without rewriting the renderer's stack-based
frame lifetime. The development server binds only to `127.0.0.1` and does not expose
the SDK, CMake cache, or source tree.

## Browser workflow

- The starter scene opens in the same editor with its live 3D viewport.
- MMB orbits; Shift+MMB pans; Ctrl+MMB or the wheel zooms; F frames selection.
- Import self-contained `.glb` models with the browser toolbar or drag and drop
  (64 MB per file). Multi-file `.gltf` imports are not included in this first port.
- **Save scene / Ctrl+S** commits the scene and imported models to IndexedDB.
  A failed storage write leaves the scene marked dirty. Saving does not modify
  the desktop project; data belongs to this browser profile and URL origin.
- **Run scene** saves, then loads the player in the same tab. **Return to editor**
  reloads the saved browser project.
- **Export scene** downloads `main.gscene`. This is a scene description, not an
  asset bundle: imported models and environment scripts must be supplied alongside
  it if transferring the scene to desktop. Keep original imported GLB files.

Only the starter project's three models, Python package, fonts, and atmosphere
lookup tables are bundled. Other desktop assets and native process/file dialogs
are not available in the browser. Clearing site data removes browser-local saves;
they are not cloud backups. This first target is intended for desktop browsers,
not small touch screens.

The browser adapter lives in `src/platform/web/`; reusable runtime UI stays in
`src/ui/`, the editor application lives in `tools/editor/`, and GPU-specific code
remains in `src/backends/raster/`.

## Validation and compatibility notes

The local WebGPU smoke test covers startup, the rendered starter scene and
shadows, adding/renaming an object, undo/redo, framing and wheel zoom, saving and
restoring after reload, and the editor/player round trip. No WebGPU validation
errors were reported during that run. GLB import is implemented but has not yet
been exercised through the browser file chooser. MMB pointer-lock gestures and
all advanced lighting combinations still need broader browser testing.

Run the serving regression with `python tests/web_server.py`; it checks all four
bundle files, WASM MIME type, cache policy, and denial of private build paths.
The seven native CTest suites and the Windows application build also pass.

Keep these compatibility fixes when updating the vendored bgfx stack:

- The Emscripten CMake target must compile WebGPU, not merely link its port.
- Host shaderc emits WGSL; native builds continue to embed DXBC.
- Browser builds use native Wasm exceptions, JSPI, and Wasm SIMD, not x86 SSE.
- The WebGPU timestamp pass uses valid beginning/end query indices; the pinned
  browser bindings do not omit the undefined end-index sentinel.
- Sampled depth/stencil textures use a depth-only view with an inferred format.
- The sky shader writes both attachments of the HDR scene framebuffer.
- Initial rendering dimensions come from SDL's actual CSS-sized window, keeping
  drawing and mouse hit testing in the same coordinate system.
