# Genesis scene editor

Start the editor with `build/release/microsoft/genesis.exe --editor`. With no
target, it opens `examples/editor_project`. Pass a project directory, `.gscene`,
or Python script after `--editor` to edit another scene. The normal player and
the separate `--ui-workbench` widget test window remain available.

The center is the actual raster PBR renderer: models, lighting, shadows and
postprocessing. The surrounding controls use the reusable Yoga/ThorVG toolkit.

## Workspace

The Unity-style shell has a left Hierarchy, center Scene viewport, right
Inspector, bottom Project/Console tabs and centered Play/Pause/Step/Stop controls. Scene and Game share the center tab group by default. Neutral gray
chrome, thin tabs, small vector icons and dense fields follow the Unity reference.
Drag the dividers to resize panels, or focus a divider and use its arrow keys.
Hovering a divider shows a horizontal or vertical resize cursor. The cursor
stays active throughout a drag, including outside the window, and returns to
the arrow when released away from a divider or when the gesture is cancelled.
Drag a panel tab onto another panel's center to group them, or onto its left,
right, top or bottom edge to split the workspace. The blue preview shows the
destination. Drop onto a tab strip to insert or reorder tabs. Escape, window
resize or focus loss cancels a tab drag; dropping outside the workspace leaves it
unchanged. Arrow keys and Home/End switch focused tabs.

Area corners work like Blender's: the cursor becomes a crosshair over any of
an area's four corners. Press, drag, and the gesture follows the cursor until
release. Where several areas' corners meet, the area you drag into first is
the one you act on. Inside that area the gesture splits it: a line shows
where it will divide and follows the cursor, and the split direction follows
the drag. A red tint means the area is too small to split that way. Carry the
drag on into an adjacent area to join instead. The part that will be removed
darkens and shows an arrow; on release its editors close and your area
expands into its place. From a corner that belongs to only one area, such as
a corner partway along another area's edge, dragging straight out joins. When the two areas only partly share an edge, the misaligned part is
split off first, as in Blender. Hold Ctrl (Cmd on macOS) when pressing a
corner to swap contents with whichever area you release over. Escape cancels
the gesture. Closed editors reopen from the Window menu.

The editor icon at the left of each area header opens the editor type menu.
Choose Hierarchy, Scene, Project, Console or Inspector to change that area.
Hierarchy, Project and Console can be open in several areas at once, as in
Blender. Splitting one of these areas, or choosing one of these editors while
it is open elsewhere, opens a copy. Each copy keeps its own search, folder and
page. Scene and Inspector are single editors: choosing one that is open
elsewhere swaps the two areas, and splitting them opens an empty area with
the same menu. Choosing a closed editor replaces the current one. The chosen
arrangement is saved with the workspace layout, including copies.

The `x` at the end of a tab strip closes its active panel. Window > Hierarchy,
Scene, Game, Inspector, Project or Console reopens that panel. All panels may be closed;
the Window and Layout menus remain available. A hidden Scene has no pickable
viewport and cannot start camera or transform gestures.

Layout offers three presets: **Default** (Hierarchy/Scene over Project, Inspector
on the right), **Tall** (full-height Hierarchy and Inspector around the center),
and **Wide** (large Scene with Hierarchy/Inspector stacked on the right, Project
across the bottom). Window > Reset Layout restores Default.

Native editors automatically save panel positions, splitter ratios, tab order,
active tabs and closed panels after a short idle period and on shutdown.
Layout > Save Layout writes immediately. Settings live beside the scene in
`.genesis/editor-layout.json`, separately from scene data and excluded from Git.
Invalid/unsupported settings fall back to Default with a Console message; the
original file is retained until the layout changes. Browser reload persistence
for workspace preferences and detachable native windows remain pending.

Project browses the launched project root with a real folder tree, including
nested and empty directories. All assets, All Models, Images and Starter models
provide quick filters. A folder shows its direct children; search includes its
descendants and matches filenames or relative paths. Results use pages of 128
tiles. Selecting a file shows its type, size, full path and preview in Inspector.
Scene/script/project files can be inspected but are not editable here yet.

Refresh rescans immediately through the UI update loop. Watch, enabled by
default, starts an incremental scan every second to discover additions,
modifications, renames and deletions. Selection follows the file path across
reordering. Publishing waits for active drags, popups and text editing to finish.
Scans are bounded to 10,000 entries and 32 directory levels, exclude hidden and
generated dependency/build folders, and do not follow symlinks. Warnings appear
beside the item count when a scan is incomplete. Watching changes browser data
and previews; it does not reimport models already loaded by the scene renderer.

PNG, JPEG, BMP and TGA files show image previews. glTF/GLB previews rasterize the
actual static geometry with material base-color factors; textures, skinning and
PBR lighting are not represented. Previews decode lazily for visible tiles and
use a bounded memory cache. Files over 32 MB, images over 4096 pixels per axis,
models over 128 MB of buffers or 120,000 triangles, and invalid files retain
their file icons. Import settings remain future work.

Drag a model tile into Scene to place it at the pointer's ray intersection with
the Y=0 ground plane. When that plane is behind or parallel to the camera, the
drop uses the orbit pivot distance. Scene Snap rounds placement to 0.5 units.
The drag label previews the model origin and coordinates; release creates one
undoable entity without reframing the camera. Escape, focus loss, window resize
or dropping outside Scene cancels. External model drops over Scene use the same
placement; drops over Game are ignored. Inspector's Add to Scene remains available.

File/Edit/Assets/GameObject/Component/Window/Help menus invoke available editor
commands. Arrow keys navigate open menus; Escape and outside clicks dismiss them.
The hierarchy + button and GameObject > Create Empty create a real empty object.

Console records the last
100 editor messages, newest first, including saves, imports and game-launch
status; Clear removes them. Script/renderer console output is still pending.
The Saved/Unsaved indicator reflects scene
history; changing preview settings or resizing panels does not dirty the scene.

## Authoring

- Select meshes in the scene tree or by clicking their bounds in the viewport.
- Change name, visibility, position, Euler rotation and scale in the inspector.
  Names and numeric edits commit with Enter or focus change. Escape cancels a
  draft; a completed rename is one undo step. XYZ fields share compact rows.
- Use Q to select, W to move, E to rotate, and R to scale, or use the toolbar.
  Drag colored arrows, rings or cubes in the viewport. Move follows world axes;
  rotation and scale follow the selected object's local axes and pivot. Drag
  the center scale cube diagonally up/right for uniform scaling.
- Hold Ctrl while dragging or enable Snap in the Scene toolbar: move snaps in
  0.5-unit increments, rotation in 15-degree increments, and scale in 10% increments
  relative to the starting transform. Scale preserves its sign and cannot cross zero.
  Each completed gesture is one undo step. Escape, window resize or focus loss
  restores the pre-drag transform; releasing outside the viewport commits normally.
- Search objects in the Hierarchy. Up/Down and Home/End move selection;
  Left/Right collapse, expand or move between a parent and its children.
- Blender-style viewport navigation: drag MMB to orbit, Shift+MMB to pan, and
  Ctrl+MMB (up to move closer) or the wheel to zoom. These gestures only start
  inside the 3D viewport; the wheel over panels still scrolls the UI. Escape
  cancels a navigation drag, and releasing MMB or losing focus releases capture.
- Numpad `.` frames the selection (F remains an alias). Selecting the scene root
  and framing includes all visible meshes. Framing sets the orbit/zoom pivot;
  panning moves it. Pitch is constrained to keep the Y-up turntable upright.
- RMB/WASD no longer drive the editor camera. The standalone game/player keeps
  its original fly controls. Typing in inspector fields never moves the view.
- Add bundled meshes through Assets menu or Project asset inspection, or drop an existing `.glb`/`.gltf`.
- Duplicate with Ctrl+D; delete with Delete. Ctrl+Z / Ctrl+Y undo/redo object edits
  outside text fields (up to 100 edits).
- Ctrl+S saves the scene in Edit mode. Play starts an embedded session without
  saving; Pause freezes it, Step advances exactly 1/60 second, and Stop restores
  the authored scene. See Playback below.
- Grid toggles the fading ground grid and the red X / green Z origin axes.
  Cell size follows camera zoom, with stronger near lines and a softer distant fade.
  The Scene viewport has a neutral grey background until the project explicitly
  configures an atmosphere or visible HDRI with `gx.sky`; authored skies appear
  in the Scene viewport in Rendered mode.
  Animate previews the first glTF animation. Selected meshes have an orange
  silhouette outline; nonmesh objects use selection bounds. Q/W/E/R/T select
  Select, Move, Rotate, Scale, and the combined Transform gizmo. Shift+RMB
  places the 3D cursor on the ground plane; Shift+C resets it to the origin.
  Gizmo colors are red X, blue up (Y), and green ground depth (Z). The Move
  gizmo's small colored squares drag in the plane excluding that color's axis;
  the combined gizmo has the same plane handles alongside axis scale cubes.
  Empty objects also have transform handles and can be framed from the Hierarchy.
- The viewport toolbar's Quality buttons switch Low, Medium, and High raster
  presets while the editor stays open. F6 cycles the same presets. The selected
  button reflects the current renderer setting; this preview choice does not
  mark the scene dirty or change its saved data.
- The four Blender icon buttons in the Scene toolbar switch viewport shading.
  Wireframe draws mesh edges; Solid uses neutral clay lighting; Material Preview
  shows authored materials and textures under fixed studio lighting; Rendered
  uses the project's lights, sky, shadows and effects. The Game viewport always
  uses Rendered. Shading is an editor preview setting and does not mark the scene
  dirty. Screenshot capture accepts `--capture-shading wireframe|solid|material|rendered`
  with `--editor --screenshot PATH`.
- Closing a modified scene asks whether to save, discard or cancel.

## Playback

Play clones the current world, including unsaved edits, and activates Game.
Pause freezes simulation; Play resumes it. Step is available only while paused
and advances one fixed 1/60-second tick. The Game toolbar shows the tick count.
The clock drives glTF animation, animated atmosphere and runtime camera movement.
Python remains a startup configuration API; this milestone does not add Python
update callbacks, physics or a gameplay debugger.

Game uses the setup script's camera independently of the Scene orbit camera.
Click inside Game to focus runtime input: WASD moves, Q/E moves down/up, Shift
accelerates, and held RMB looks around. Escape releases focus and mouse capture.
Editor fields, menus, dock drags and Scene navigation remain available. Hiding
or closing Game releases its input; simulation continues until paused or stopped.
Window > Game reopens it. Dock Game beside Scene to see both cameras at once.
Both use the full raster pipeline; only Scene has editor grids and gizmos.

Runtime edits in the Inspector or Scene affect the temporary world. Saving and
authoring undo/redo are unavailable during Play. Stop restores the exact authored
world, entity identities, selection, editor camera, dirty state and undo/redo
history. Closing the editor also stops Play before handling unsaved authored work.
Play never silently saves the scene. The standalone `--scene` player remains
available. Older workspace layouts without a Game panel remain valid; Play adds
it beside Scene when needed.

## Components and materials

Select an object and click **Add Component**, or use Component > Add Component.
The searchable picker offers Mesh Renderer, Point Light, Spot Light, Area Light
and Reflection Probe. Existing components and unavailable light slots are disabled.
Mesh Renderer opens a second picker for a bundled/project model or an existing
`.glb`/`.gltf` path. Choosing the Mesh field replaces that object's model, keeping
its transform, visibility and material override. Files are parsed and buffers
validated before assignment. Removing a component retains the object.

**Override material** enables linear RGB color, Metallic and Roughness controls.
It replaces the source factors for every material slot on this object while
retaining source textures, alpha behavior, normals, occlusion and emission.
Clear the checkbox to use the original source materials. Per-slot material
assets, texture replacement and emissive editing remain future work.

Lights expose enable, color, intensity, range, shadows and bias. Spot lights add
inner/outer cone angles; area lights add width/height. Their position follows the
object transform and spot/area direction follows its rotated local -Y axis.
The renderer has four point, four spot and two area slots, including setup-script
lights; disabled authored lights release their slot. Script-owned lights still
use their Python setup and are not editable through these entity inspectors.

Reflection probes expose enable, box size, blend distance and priority. Their
axis-aligned boxes follow object position and absolute scale; rotation does not
rotate the box. Selected bounds appear in the viewport. Local reflection captures
are invalidated by scene edits, and two probes can blend at a time. New authored
probes do not bake diffuse GI visibility grids; existing script probe grids retain
their startup bake behavior.

Component changes, removals, model replacements and material edits participate
in scene undo/redo, duplication and save/load. Use the Hierarchy to select lights
and probes; viewport picking currently targets mesh bounds.

## Persistence

An optional top-level `entities` array in `.gscene` stores flat scene objects:

```json
{
  "format": 1,
  "name": "My World",
  "script": "environment.py",
  "entities": [
    {
      "name": "Crate",
      "model": "assets/crate.glb",
      "position": [0, 1, 0],
      "rotation": [0, 45, 0],
      "scale": [1, 1, 1],
      "visible": true,
      "tags": []
    }
  ]
}
```

Optional entity `material`, `light` and `reflection_probe` objects store the
Inspector properties. Material requires a model; lights/probes can belong to
empty objects. Invalid colors, sizes, ranges and cone angles are rejected on load.

Scripts run first and still configure the environment, sun, game and any
script-owned lights. Authored lights/probes are composed with that setup in both
the editor and player. When `entities` exists, it replaces the script-created mesh world
and legacy `scene.load` model. An empty array intentionally means no entities.
Without it, existing script scenes behave as before. Scriptless entity scenes
are also supported. Python files are never rewritten by the editor.

Model paths are relative to the scene file. Unknown scene fields and script
references are preserved. Direct `.py` targets save a companion
`<script>.editor.gscene`; an existing companion is not silently overwritten.
The last saved editor camera, field of view, and orbit pivot are stored
separately from the game's camera. Old fly-camera saves recover a scene-depth
pivot on load without changing the view.
Scene writes use an adjacent temporary file and atomic replacement; a changed
source file is rejected to protect external edits.

## Current scope

The scene is flat: parenting,
asset import settings, script editing and debugger integration are still future
work. Transform plane handles, editable snap increments and pivot/coordinate-space
switches remain pending. Picking/framing use model bind-pose bounds, not exact animated triangles.
Existing local diffuse-probe grids are startup bakes; relaunch after moving
occluders when testing those scenes. HDR/effects targets currently retain the
window resolution and are presented at the viewport's correct aspect ratio.

## Code layout and tests

- `src/SceneDocument.*`: renderer-neutral scene loading/saving.
- `tools/editor/GameEditor.*`: application model, history and Yoga/ThorVG controls.
- `tools/editor/ComponentInspector.cpp`: component commands, model picker and property controls.
- `src/ui/DockSpace.*`: reusable Yoga split tree, dock tabs, resizing, drop previews and stable panel contents.
- `tools/editor/WorkspaceLayout.cpp`: presets and validated, atomic native workspace persistence.
- `tools/editor/PlaySession.cpp`: runtime world isolation, fixed ticks and edit-state restoration.
- `src/SceneComponents.h`: validated component scene serialization.
- `src/AuthoredLighting.h`: authored light/probe conversion and bounded shader-slot composition.
- `tools/editor/SdlEditorHost.*`: platform input, separate runtime camera and close protection.
- `tools/editor/ViewportMath.h`: picking/framing math.
- `tools/editor/ViewportCamera.h`: platform-independent orbit, pan and zoom.
- `tools/editor/TransformGizmo.h`: shared render/pick geometry, ray/plane dragging, local rotation composition and snapping.
- `src/backends/raster/EditorOverlay.*`: GPU grid and selection visualization.
- `GameEditorTests`: persistence, external-write protection, history, layout,
  transparent viewport composition, quality control input and ray/bounds picking.
- `EditorNavigationTests`: camera and transform math plus captured SDL gestures,
  cancellation, text-input isolation and one undo record per completed drag.
- `tests/editor_quality_switch_regression.ps1`: live D3D11 editor quality
  transitions through High, Low, and Medium.
- `tests/editor_components_regression.ps1`: native material/light readbacks and probe capture.
- `tests/editor_docking_regression.ps1`: native saved-layout viewport composition, including hidden Scene.
- `tests/editor_playback_regression.ps1`: native Game, simultaneous Scene/Game, hidden tabs and standalone player rendering; authored scene files remain unchanged.
