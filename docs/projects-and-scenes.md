# Genesis projects and scenes

Genesis starts from a project directory, a `.gscene` file, or a Python script.
With no target it opens the project launcher; a supported path can be dropped
onto that window.

## Project settings

A project directory contains `genesis.project`. This JSON document designates
the main scene and gives the project a display name:

```json
{
  "format": 1,
  "name": "My Game",
  "application": {
    "main_scene": "scenes/main.gscene"
  }
}
```

Paths in the project settings are relative to the project directory.

## Scene files

Scene files are readable JSON documents with the `.gscene` extension. A root
script can configure the scene, and nodes may attach their own scripts:

```json
{
  "format": 1,
  "name": "Main",
  "script": "main.py",
  "nodes": [
    {"name": "Player", "script": "player.py"},
    {"name": "Camera", "script": "camera.py"}
  ]
}
```

Root scripts execute first, followed by node scripts in document order. Each
relative script path is resolved from the scene file. Asset paths used by a
script resolve from that script's directory, with the Genesis repository kept
as a compatibility fallback for the bundled demos.

## Launching

```powershell
build/release/microsoft/genesis.exe examples/hello_project
build/release/microsoft/genesis.exe examples/hello_project/main.gscene
build/release/microsoft/genesis.exe demos/clouds.py
```

The explicit `--project`, `--scene`, and `--script` forms are also accepted.

Use `--editor [target]` for the [3D scene editor](scene-editor.md). Scene files can
also contain an authored `entities` array; scripts become optional in that case.
When present, the array replaces script-created mesh entities after setup.
