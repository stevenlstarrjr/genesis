# Genesis Renderer Handoff

Last updated: 2026-09-22

## PhysX particles (2026-09-22)

Fluid, granular, cloth, and explosion authoring now works in the C++ and Python SDKs and
the editor. The optional Windows GPU build links PhysX 5.9, simulates all
four in Play, shows scene seeds and live particles in the viewports, and
restores authored state on Stop. The latest verified build is
`build/release/microsoft/genesis-physx-explosion.exe`; with no arguments it opens
`examples/physx_particles`. The `Genesis Editor.lnk` shortcut opens this build
with `examples/editor_project`. Visible glTF meshes become cooked GPU
collision surfaces; the sample contains a platform. Fluid and granular
particles render as shaded discs, and cloth as filled lit triangles. GPU,
editor, Python, and scene validation tests pass. Moving particle colliders are
opt-in on mesh renderers and follow entity position/rotation during Play; scale
stays at the Play-start value. Cloth X edges can be pinned left, right, or both
to the cloth entity transform through PhysX deformable attachments. The sample
enables the platform's moving collider and pins the cloth's left edge. The
reproducible Windows build is `tools/build-physx-windows.ps1`; it pins PhysX,
applies the tracked `cmake/physx-cuda13.patch`, and runs GPU and editor tests. PhysX source and
a local CUDA 13.4 toolkit are in ignored `build/.deps`. See
`docs/physx-particles.md` for build flags and limitations.

The sample also has a one-shot explosion below the cloth. PhysX particles burst
outward and a radial velocity impulse moves nearby free cloth vertices while
the pinned edge stays attached. The viewport draws a flash, fireball, shockwave,
sparks, and fading smoke. The GPU test compares blasted cloth against a control
scene and passes; editor persistence and Python API tests pass. Continuous fire,
volumetric smoke, and reusable triggers are future work.
The sample's granular system now starts low above its own platform with higher
friction and damping; its earlier high, unsupported drop looked like a second
explosion in the viewport.

## Current state and continuation

The Project browser milestone is complete and built: real filesystem folders,
model/image thumbnails, Refresh/Watch and undoable drag-to-Scene placement.
Splitters also show native directional resize cursors on hover and throughout
capture, with arrow restoration after release, cancellation and layout changes.
Current verification is **502 checks passing** (199 editor, 172 toolkit,
131 navigation/SDL), plus **11 native Windows cursor checks**. The Project milestone
also passed nine native D3D11 captures across Project, docking and playback
regressions. The representative browser image is
`artifacts/editor-project/browser.png`. Older dated sections below record earlier
milestones; their test counts and pending-feature lists are historical.

Next major authoring milestone: **per-material-slot editing, texture replacement
and emissive overrides**, extending the existing per-object material controls
through persistence, undo/redo and live rendering. Light cookie/IES pickers,
import settings and model reimport remain on the backlog. Continue using
Yoga layout and the shared GUI controls; retain the compact Unity-style editor.
The user expects complete working milestones, builds and visible verification.
No commits were created. Existing modified and untracked work remains intact.

The latest user-requested change was resize cursors on the splitters; it is
implemented, verified and included in `build/release/microsoft/genesis.exe`.
The user also asked about PhysX and Jolt on mobile. That was a design discussion:
no physics library was added and no physics implementation task was started.
The material milestone above remains the proposed continuation, not a new user
instruction to begin it during this handoff update.

### Build, verification and launch

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build/release/microsoft/.cmake --config Release --target Genesis GameEditorTests UiToolkitTests EditorNavigationTests -j 8
& build/release/microsoft/GameEditorTests.exe artifacts/editor-ui
& build/release/microsoft/UiToolkitTests.exe artifacts/ui-toolkit
& build/release/microsoft/EditorNavigationTests.exe
& build/release/microsoft/EditorNavigationTests.exe --native-cursors
& tests/editor_project_regression.ps1
& tests/editor_docking_regression.ps1
& tests/editor_playback_regression.ps1
& build/release/microsoft/genesis.exe --editor examples/editor_project
```

Run from the repository root. MSBuild file tracking and native D3D11 execution
required sandbox escalation. Latest build logs: `artifacts/editor-cursors-build.log`
and `artifacts/editor-cursors-native-build.log`.
The native executable was rebuilt after the cursor change. The earlier editor
launch predates that rebuild; the updated executable has not been relaunched
interactively. Use the launch command above to try it.
Interaction verification uses the real SDL adapter with synthetic events in a
hidden test window. Native GPU captures were generated and inspected; no manual
desktop interaction was needed for this milestone. Browser runtime verification
remains pending.
Keep the artifact directory argument on `UiToolkitTests` when comparing the
172-check count: its image-output checks are skipped when no argument is passed
(164 checks). The nine D3D11 screenshots were verified for the Project milestone;
the later cursor-only change was verified with actual native SDL cursor checks.

## Physics discussion and possible direction (2026-09-22)

The user asked how PhysX works on mobile, then asked about Jolt. The recommendation
was to favor Jolt for Genesis's desktop/mobile/browser goals because its upstream
platform list includes Android and iOS and it has a WebAssembly port. Its core
rigid-body simulation runs on the CPU and supports ARM NEON and multithreading.
It supplies collision shapes, constraints, triggers, character controllers,
ragdolls, vehicles and soft bodies, with C++/CMake integration and an MIT license.
This was a recommendation, not a confirmed engine selection or measured Genesis
performance result.

PhysX CPU simulation can run on mobile, but its CUDA GPU simulation targets
Windows/Linux. NVIDIA's current upstream platform documentation lists those two
platforms; the O3DE fork adds Android/iOS support. A mobile PhysX integration would
therefore require choosing and testing a suitable version/fork.

If physics becomes the next requested task, a proposed first milestone is Rigid
Body and Collider components, scene persistence and inspector controls, and a
physics world owned by each Play session. Step it through the existing 60 Hz
tick, synchronize runtime transforms, and discard it on Stop while preserving
the authored registry/history. Validate simple falling/resting bodies, collision
filtering, Pause/Step/Stop and repeated sessions before expanding the feature set.
Mobile/browser builds and representative scenes still need separate validation.
Cross-platform determinism requires matching build settings and consistent
simulation input ordering; it must not be promised without testing.

Sources checked during the discussion:

- [Jolt platforms and features](https://github.com/jrouwe/JoltPhysics)
- [Jolt build instructions](https://github.com/jrouwe/JoltPhysics/blob/master/Build/README.md)
- [Jolt WebAssembly port](https://github.com/jrouwe/JoltPhysics.js)
- [Jolt determinism requirements](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md#deterministic-simulation)
- [PhysX GPU simulation](https://nvidia-omniverse.github.io/PhysX/physx/5.7.0/docs/GPURigidBodies.html)
- [PhysX upstream platforms](https://github.com/NVIDIA-Omniverse/PhysX/tree/main/physx/documentation/platformreadme)
- [O3DE PhysX fork](https://github.com/o3de/PhysX)

## Native splitter cursors (2026-09-22)

Shared `Document::cursor()` selects horizontal/vertical resize cursors from the
splitter's drag axis, prioritizes capture and refreshes pending layout. The SDL
adapter caches system cursors, falls back to the arrow when unavailable and
releases them on shutdown. Editor host synchronization covers layout changes
without mouse motion and returning to focused Game input. Toolkit tests cover
hover, capture outside the window, cancellation and disabled/hidden dividers.
`EditorNavigationTests --native-cursors` uses a hidden Windows SDL window to
verify actual cursor changes, both axes, release/focus loss, panel closure and
adapter destruction. No renderer or authored scene changes were needed.

## Project browser, thumbnails and placement (2026-09-22)

`tools/editor/ProjectAssets.*` incrementally indexes the launched project root,
not just the scene folder. The tree includes nested and empty folders; quick
filters cover all files, models, images and the three bundled starter models.
Search is scoped to the selected folder and descendants. The tile grid pages at
128 results. File identity and Inspector selection use stable paths, including
Play-session restoration. Renames/deletions clear stale selections safely.

Watch scans every second, 128 entries per UI update, and Refresh requests a new
scan. Publishing waits for capture, text editing and popups to finish. The index
is capped at 10,000 entries and 32 folder levels, skips symlinks, dot folders and
generated dependency/build directories, and reports incomplete scans. There is
no background thread or filesystem mutation. Initial construction completes one
bounded scan. File watching refreshes the browser, not loaded scene GPU resources.

`AssetThumbnails.cpp` decodes PNG/JPEG/BMP/TGA images and rasterizes static glTF/GLB
geometry with base-color factors into 96px previews. Visible tiles decode lazily,
one per UI update, with a 128-entry cache. Invalid/oversized files retain icons;
bounds are 32 MB per source file, 4096 pixels per image axis, 128 MB model buffers
and 120,000 triangles. Model textures, skinning, animation and PBR preview lighting
remain future work. Shared `Node::setImage` handles bitmap fitting, clipping and
ownership without adding codecs/filesystem concerns to the UI toolkit.

`ProjectBrowser.cpp` builds the tree/grid/Inspector and model drag behavior.
Dropping into Scene intersects the camera ray with Y=0; an unavailable ground
intersection falls back to camera pivot distance. The Scene Snap toggle rounds
to 0.5 units. A label previews model origin and coordinates; one release creates
one undo record, without moving the camera. Escape, focus loss, resize and an
invalid target cancel. SDL capture blocks scene shortcuts/camera navigation.
External file drops into Scene use the same placement; Game drops are ignored.

Tests cover incremental publication, file add/modify/delete/rename, automatic
Watch, stable selection, malformed assets, actual geometry/image pixels, drag
cancel/commit/history, capture-safe refresh and OS drop routing. Native captures
in `artifacts/editor-project/` show the live Scene/browser and Project-only
workspace; automated GPU readbacks verify image-thumbnail colors and authored
scene hashes remain unchanged. Documentation: `docs/scene-editor.md`,
`docs/editor-ui-todo.md`, `docs/ui-toolkit.md`.

## Embedded Game and reversible playback (2026-09-22)

`tools/editor/PlaySession.cpp` implements Edit/Playing/Paused states and fixed
60 Hz ticks. Play retains the entire authored EnTT registry and exposes a clone
with the same entity handles. Stop swaps the original registry back and restores
selection (including no selection or an inspected asset), editor camera and dirty
state. Authoring undo/redo remains untouched and unavailable during Play. Runtime
inspector/gizmo edits are temporary, Save is blocked, and Play never saves first.
Closing the editor stops Play before handling unsaved authored changes.

Game is the sixth registered dock page, grouped with Scene in all presets and
available from Window. Version-1 five-panel layouts remain valid. Play adds Game
when missing, and paused/resumed sessions retain their clock. Closing or hiding
Game releases held input without stopping simulation. Click its content to focus
WASD/QE fly movement, Shift acceleration and RMB look; Escape releases focus.
Scene orbit/pan/zoom, menus, text fields and docking retain their own input paths.
`SdlEditorHost` no longer launches a separate process. The standalone `--scene`
player is preserved and included in the native regression.

The runtime clock drives existing glTF/environment animation and runtime camera
movement; Python remains a startup configuration API. Per-frame Python callbacks,
physics and debugger integration are not added by this milestone. A C++ tick
handler supports simulation updates and is tested with entity mutations. Pause
freezes the clock, Step runs exactly one tick, and repeated Play starts at zero.

Both visible viewports submit complete raster graphs with independent cameras,
aspect ratios and exposure histories. Ordered view ranges allow shared HDR,
shadow, AO/fog and bloom scratch targets to be consumed before reuse. bgfx's view
budget is now 512. Scene overlays precede the Game graph and never appear in Game.
Hidden cameras submit no graph. Current scratch targets remain window-sized, so
showing both views costs two full render passes; per-viewport target sizing is a
future optimization. Browser runtime verification remains pending.

Tests cover restoration after runtime creation/deletion/component changes,
unsaved authored work, preserved history, fixed ticks, repeated sessions,
closing/hiding Game, focus loss and held-key release, Scene navigation beside
focused Game, actual SDL playback button clicks and five/six-panel persistence.
Native artifacts under `artifacts/editor-play/` cover Game, Scene/Game split,
hidden views and standalone player; authored fixture files are hash-checked.

## Docking and saved workspace layouts (2026-09-22)

Replaced the editor's fixed panel wrappers with reusable `src/ui/DockSpace.*`.
Hierarchy, Scene, Inspector, Project and Console have stable content pages that
move between validated Yoga split/tab trees. Drag tabs to panel centers to group,
edges to split, or tab headers to reorder; a blue overlay previews the drop.
Splitters retain keyboard and pointer resizing, clamp pane minimums, and adapt
when the window narrows. Tab arrows/Home/End, active-panel close buttons, and
Window-menu restoration work even after closing every panel. Content nodes and
selected scene objects survive layout changes through `Node::reparent`.

`tools/editor/WorkspaceLayout.cpp` implements Default/Tall/Wide presets and
automatic native persistence to `.genesis/editor-layout.json` beside the scene.
It saves the split tree, ratios, tab order, active pages and closed panels after
600 ms idle or shutdown; Layout > Save Layout flushes immediately. Writes use
temporary files and atomic replacement. Invalid/unknown-version trees recover to
Default with a Console message without immediately overwriting the bad file.
Unsaved test documents have no workspace file. `.genesis/` is ignored by Git.

Toolkit button drag capture starts after 5 logical pixels and suppresses click.
Escape/focus loss/window resize cancel docking. Keyboard activation and wheel
scroll do not interrupt an active tab drag. The SDL host routes captured UI
gestures before camera/gizmo/scene shortcuts. Hidden Scene returns an empty
viewport; aspect/view dimensions are guarded and editor overlays skip it.

Verification: 127 GameEditorTests, 161 UiToolkitTests and 101 navigation/SDL
checks (389 total). New tests cover all four edge drops, center grouping, header
reordering, cancellation, minimum-size constraints, close/reopen, invalid layouts,
shutdown/save/load, Window-menu recovery, and input isolation over the viewport.
`tests/editor_docking_regression.ps1` passed native D3D11 default/wide/hidden-Scene
captures and pixel checks. Inspected `artifacts/editor-docking/wide.png` and
`hidden-scene.png`; CPU presets/drop preview are under editor-ui/ui-toolkit.

Remaining: detachable native windows, named custom presets and browser reload
persistence for preferences. Next major authoring milestone is an embedded Game
view with Play/Pause/Step; Play currently launches the separate player.

## Component Inspector and authored rendering (2026-09-22)

Implemented the requested next authoring phase. Add Component is active and
opens a searchable reusable `ResourcePicker`: Mesh Renderer (followed by model
selection), Point/Spot/Area Light and Reflection Probe. Mesh assignment validates
glTF parse/buffers before changing the existing object, preserves its transform,
visibility/material override, and supports removal/undo. Shared RGB `ColorField`
provides swatch, numeric fields and presets. Material overrides are per object
across all slots: color, metallic and roughness; textures/alpha/emission stay sourced
from the model. Override factors apply in every PBR draw, including probe captures.

GameplayWorld snapshots now include optional material/light/probe data. Component
history, duplication, dirty detection and `.gscene` save/load include these values.
`SceneComponents.h` validates fields. `AuthoredLighting.h` composes entity lights
with setup-script lights within the existing 4 point/4 spot/2 area limits, with
disabled components consuming no slot. Renderer startup (editor AND player) and
editor revisions synchronize authored properties. Spot/area local -Y is the
direction. Probe boxes are axis-aligned, use entity position/absolute scale, show
selection bounds and recapture after edits. Script diffuse GI grids remain startup
bakes; newly authored probes provide reflection captures without diffuse GI grids.

Source implementation: `tools/editor/ComponentInspector.cpp`; shared cgltf
implementation moved from the renderer into `src/ModelValidation.cpp` (genesis_scene).
Native fixtures/captures are in `artifacts/editor-components/`; CPU picker/material
captures in `artifacts/editor-ui/`. At this milestone, tests passed: 104 editor, 130 GUI and
93 navigation/transform checks (327). `tests/editor_components_regression.ps1`
also verifies native material color and lighting pixels and a probe capture.
Red override readback R=204/B=65; blue R=86/B=192; disabling point light lowers
red to 33. UI and actual D3D11 images were inspected.

Remaining component work: per-slot material assets/textures/emission, cookie/IES
pickers, editing setup-script lights, diffuse GI rebake and advanced color picker.
Docking and persisted layouts were completed in the subsequent milestone above.
Pause/Step remain disabled.

## Draggable scene transform tools (2026-09-22)

The editor now has Q/Select, W/Move, E/Rotate and R/Scale toolbar tools. Shared
`tools/editor/TransformGizmo.h` provides the world-space geometry used by both
GPU overlays and screen-space picking. Move uses world axes; Rotate and Scale
use the entity's local axes, matching the renderer's bx::mtxSRT Euler convention.
Scale includes a uniform center handle. Handles stay about 90 logical pixels
as the camera zooms, and use screen-width triangle ribbons for visibility.

Ctrl or the Scene toolbar Snap checkbox enables relative increments of 0.5
units, 15 degrees and 10% scale. Each drag updates the real world/Inspector and
creates one history entry on release. Escape, focus loss and window resize
roll back without recreating IDs or discarding redo. Captured drags isolate
camera, deletion and menu shortcuts; text fields suppress Q/W/E/R shortcuts.
Empty objects can also be manipulated and framed. Plane handles, snap settings
and pivot/local/global switches remain pending.

`artifacts/screenshots/editor-transform-tools.png` is the latest native UI
capture. The editor to-do list and scene-editor guide include the new controls.
Verified Release Genesis plus 93 navigation/transform, 67 editor/persistence,
and 123 GUI checks (283 total). Native D3D11 and minimum 1100x720 UI captures
were inspected. No scene assets were changed by the transform-tool work.

## Unity-reference visual correction (2026-09-22)

The user rejected the initial UI as visually unlike Unity. Reworked it after
visually inspecting Unity's official editor screenshot: neutral gray theme,
24 px menu strip, compact centered icon playback strip, 23 px panel tabs,
20 px hierarchy rows, dense single-line XYZ inspector fields and component
headers, 220/320 px sidebars, and a larger Project area with folders and an icon
grid. The native result is `artifacts/screenshots/editor-unity-layout.png`.

Shared GUI now supplies ThorVG vector icons, Yoga absolute popup positioning and
`MenuBar`, with command enable predicates, keyboard navigation and dismissal.
The editor has working File/Edit/Assets/GameObject/Component/Window/Help/Layout
menus. Project scans supported project files alongside starter models, filters
by folder/category and search, and shows selected assets in Inspector with
Add to Scene for models. Create Empty is wired to the real scene/history.

Game/Play still launches the separate player. Pause/Step and Add Component are
disabled; docking and full asset import/material/component workflows remain
queued. Latest tests: 123 toolkit checks and 62 editor checks pass, including
popup input isolation, icon rasterization, file-menu opening and filtered asset
selection without scene mutation. Desktop, minimum-size, menu and asset-inspector
captures were inspected; the previous `editor-ui-after.png` shows the rejected
earlier layout and should not be used as the current screenshot.

## Yoga GUI and Unity-style editor workspace (2026-09-22)

The requested UI to-do list is `docs/editor-ui-todo.md`. The editor now uses a
Unity-style arrangement: centered Play, left Hierarchy, center Scene, right
Inspector, and bottom Project/Console tabs. All panel sizes and control layout
continue through Yoga; ThorVG remains the UI renderer. Shared colors/type/control
styles live in `src/ui/Theme.h`.

Added shared draggable scrollbars and panel splitters (including keyboard
resizing and focus-loss cleanup), field placeholders and Enter/blur text commits,
and arrow-key hierarchy navigation. Tree refreshes preserve expansion/focus.
Commit callbacks can rebuild sibling UI without swallowing the next button
click; destroyed nodes cannot regain focus through reused allocations.

Editor changes include compact XYZ rows, search/result counts, Saved/Unsaved and
empty-selection states, relative mesh paths, resizable panels and Reset Layout.
Renaming creates one undo record; close/import commits inspector drafts. Console
records up to 100 editor messages, with Clear. Project still contains the three
starter-model actions, and Play still launches the separate real game window.
Docking/persisted layouts, menus, full assets, embedded Game/Pause/Step, and
script/renderer console integration are explicitly queued in the to-do list.

Verification: Release `Genesis`, `UiToolkitTests`, and `GameEditorTests` built.
114 GUI + 58 editor + 37 viewport navigation checks passed. Native D3D11 capture
is `artifacts/screenshots/editor-ui-after.png` (before image alongside it).
`artifacts/editor-ui/` contains desktop, minimum 1100x720, 2x DPI, Console and
empty-selection CPU raster captures. Normal/minimum/native and 2x captures were
visually inspected. Existing unrelated renderer work remains in the checkout.

## Spotlight cone-aware capture selection (2026-09-21)

Spotlights now need a visible mesh bound to pass conservative range and
outer-cone checks before taking a local shadow capture. The cone check first
rejects bounds entirely behind the current spot direction, then tests eight
subdivided AABB cells with conservative bounding spheres. This avoids rejecting
a real receiver at the edge of a cone while reducing false overlap from large
model bounds. It follows the spot's current interactive direction.

`tests/local_shadow_third_slot_regression.ps1 -CompetingPoints 3
-SidewaysSpot` reproduces a wasted capture: a high-score spot aimed beside the
floor previously evicted the area shadow (floor red 125). The area shadow is
now red 0, with lit control 181. `-FacingSpot -ExpectAreaUnshadowed` confirms
the same spot aimed at the floor still takes a capture (area shadow pixel 125).
The authored Sponza `demos/spot_lights.py` also rendered successfully in
Release D3D11 at `artifacts/screenshots/spot-selection-regression.png`.

## Coverage-aware shadow capture selection (2026-09-21)

When more than four range-eligible local lights compete, the renderer now
selects captures greedily by strength while discounting a candidate whose
influence volume overlaps already selected lights. The overlap uses the
smaller light radius, so a large area light is not penalized simply because
its range contains another source. The previous emissive-only overflow rule
became unreachable when the capture count rose to four and has been removed.
The existing 15% previous-caster score margin still applies before selection.

`tests/local_shadow_third_slot_regression.ps1 -CompetingPoints 4
-ClusteredPoints` reproduces the loss: four point lights in one floor region
previously displaced the separate area light, leaving its box-shadow pixel red
125. With the new selection, the shadow is red 0 and the lit control remains
181. Four stronger point lights spread across the floor retain their captures;
the fifth area light remains unshadowed, as checked with
`-CompetingPoints 4 -ExpectAreaUnshadowed`. Release D3D11 build and both
render checks pass.

## Shadow capture range selection (2026-09-21)

Before scoring authored point, spot, and area lights for the four local shadow
slots, the renderer checks whether each light's finite range intersects a
visible model's world bounds. One-sided area lights must also face at least
one visible model bound. It caches those bounds once per frame. A small
light near the camera that cannot illuminate any scene geometry no longer
evicts a light whose shadow is visible. The direct-light shader still applies
its exact per-pixel range falloff; this is a conservative capture filter.

`tests/local_shadow_third_slot_regression.ps1 -CompetingPoints 4
-OutOfRangePoints` puts four high-score, one-meter point lights near the camera
and outside the mesh bounds. The area light keeps its shadow: floor red 0,
lit red 181. With three range-intersecting point lights, the area light still
casts from slot four (0/181); two such points verify slot three (0/181). The
four-emitter outer-panel regression remains 13/137. With three in-range point
lights and a high-score area light pointed away from the scene, the real area
light still casts from slot four (0/181). All passed in Release D3D11.

## Four local shadow captures (2026-09-21)

The renderer now selects four independent local shadow casters. Slot zero uses
a comparison cubemap; slots one through three use eighteen faces in one D16
texture array at the existing second shadow sampler. Quality switching rebuilds
all 24 face framebuffers. The extra six faces cost about 3, 12, or 48 MiB at
Low, Medium, or High, respectively. Lights beyond four still contribute direct
lighting but compete for shadow captures by score.

`tests/local_shadow_third_slot_regression.ps1 -CompetingPoints 3` places three
nearby point lights ahead of a shadowed area light. The area light still casts
its box shadow in slot four: floor red 0 versus lit control 181. The same test
with its default two points verifies slot three. A four-panel emissive scene
from `tests/emissive_distant_shadow_regression.ps1 -FourPanels -BoxX 4.5`
renders all four panels; `-BoxX -4.5` checks the opposite outer panel. Both
outer box positions retain their shadows: red 13 and 9 versus a
lit control of 137. The original three-panel center scene remains red 4 versus
lit 121.
All checks passed in a D3D11 Release build. The live four-emitter quality test
also completed High -> Low -> Medium and saved a rendered screenshot at each
preset. Browser WebGPU runtime remains unverified.

## Three local shadow captures (2026-09-21)

At this stage, local shadow selection had three slots. Slot zero was a comparison
cubemap; slots one and two use twelve faces in one D16 texture array. A
comparison array sampler replaces the second cubemap sampler at stage 15, so
the PBR pass stays within its sixteen texture stages. The same face projection
and depth comparison serve point, spot, area, and emissive lights. Quality
switching rebuilds both textures and all eighteen face framebuffers.

In the separated three-panel fixture, a floor pixel under the center blocker
changed from red 117 with two captures to red 4 with three. All three blocker
positions pass `tests/emissive_distant_shadow_regression.ps1`; the center
shadow remains red 3/4/3 at Low/Medium/High, with a lit control of 121. The
third-slot area-light fixture puts two small point lights near the camera;
its box shadow/lit control is red 0/181. Existing emissive shape, texture,
shared-shadow, and live quality-switch tests pass on D3D11. The WebGPU shader
compiles and the full WebGPU build links; browser rendering remains to verify.

This adds six D16 faces: about 3, 12, and 48 MiB at Low, Medium, and High.
The isolated center fixture increased from 75 to 93 draws and from about 1.9
to 2.3 ms total GPU time at Medium on the test machine. Four or more
independent shadow casters still compete by score.

## Editor quality controls and separated emitters (2026-09-21)

The scene editor's viewport toolbar now has Low, Med, and High buttons that
call the existing live raster target rebuild. The selected button follows the
startup preset and F6, which now also works in the editor. Preset preview does
not dirty or save the scene. `GameEditorTests` exercises the buttons at the
1100x720 minimum editor size; `tests/editor_quality_switch_regression.ps1`
verifies the live D3D11 High -> Low -> Medium sequence. Both pass.

Emissive clustering now divides faces at the largest gap between face centers
instead of dividing a partition by triangle count. The old median split mixed
triangles from separated panels into four uneven samples. When comparable
coplanar samples outnumbered the two local captures, selection kept the most
separated pair. That overflow rule was superseded by the coverage selection
above. Under the earlier
two-slot budget, `tests/emissive_distant_shadow_regression.ps1` checked both outer
blocker positions at x=+4 and x=-4: shadow/lit floor red is 57/132 and 56/132.
The nearby three-panel shared-map fixture reads 22/130. Spatial pools, grazing,
wide-triangle, strip, mutual-occlusion, textured-emission, and editor quality
tests pass on the Release D3D11 build.

At the time of this measurement there were two shadow slots; the three-slot
change above now captures the middle island independently.

## Area-weighted emissive capture points (2026-09-21)

Wide single-cluster emitters now accumulate each triangle's exact second
position moment, weighted by triangle area. After transformation, the variance
along the cluster tangent places the two shadow captures one standard deviation
to either side of the emitting centroid, constrained to the projected bounds.
This represents an asymmetric triangle better than half-width offsets. The
shader weights the two visibility samples by receiver cosine, emitter cosine,
and inverse squared distance. Its grazing bias angle also uses each capture's
actual position, including when a nearby cluster shares a map.

`tests/emissive_grazing_shadow_regression.ps1` moves the source beside the box.
At the floor transition, red rises from 54 to 65 while the deep shadow remains
23 and the lit floor stays 167. `tests/emissive_dual_origin_regression.ps1`
checks the wide triangular source: floor red is 0 behind the box and 65 at the
lit control. A lower global emissive slope-bias cap was tried but did not show
a clear contact improvement, so the existing bias cap remains. Release build
and the dual-origin, grazing, shared-map, and layered-emitter fixtures passed.

## Shared emissive shadow visibility (2026-09-21)

When more emissive clusters are lit than the local shadow slots can cover,
an unselected cluster may reuse a selected cluster's map. Sharing requires the
same source model and glTF primitive, nearly aligned normals, a plane offset
under 3 cm, and neighboring bounds. The shader receives the selected map's
actual capture origin so it compares receiver depth from that point. Coplanar
emitter pieces are already omitted from these captures. Separate layers and
distant clusters keep their own unshadowed fallback.

`tests/emissive_shared_shadow_regression.ps1` places three nearby emissive
panels in one primitive with a box under the third panel. After the spatial
partition and capture-pair changes above, the shadow-edge red pixel is 22 and
the lit control is 130. The sharing path does not add maps or draws.

## Live raster quality switching (2026-09-21)

F6 cycles Low, Medium, and High in the standalone viewer and editor. The editor
also has Low, Med, and High buttons in its viewport toolbar. The renderer replaces
its resolution-dependent shadow, reflection, AO, and fog targets, updates the
texture bindings, and rebuilds the environment and local probes. Two empty
bgfx frames retire old framebuffer handles before the replacement targets are
allocated; without that drain, the transition exceeded the configured handle
limit and bloom framebuffer creation failed. The timing average resets after
each switch.

`tests/quality_switch_regression.ps1` sends the three F6 presses to a running
D3D11 window and verifies the Medium -> High -> Low -> Medium sequence plus a
rendered screenshot from every switched preset. Release build passed. The
switch changes the global renderer quality setting and does not interrupt
camera position, scene state, or editor data.

`GameEditorTests` clicks all three quality buttons at the editor's minimum
window size and checks that renderer quality synchronization does not dirty the
scene or request another switch. `tests/editor_quality_switch_regression.ps1`
cycles all three presets in a live D3D11 editor window; release build and both
tests passed.

## Stable local shadow selection (2026-09-21)

The local shadow slots retain their previous casters through a 15%
score margin. This avoids shadow maps switching on and off when camera motion
puts similarly ranked lights near the cutoff. Emissive clusters are identified
by source model and cluster index, rather than by their frame-dependent place
in the sorted four-light list. Removed casters immediately give up their slots.
The selection remains camera dependent and has a four-slot limit.

## Dual-origin shadow capture for a wide emitter (2026-09-21)

When a single emissive sample is the only local shadow candidate and its
world-space half-width reaches 1 m, the renderer uses the first two local
slots. Their capture points use the area-weighted offset described above. The
PBR shader blends their visibility by receiver-facing contribution and halves
the width of each shape-aware filter. Other shadow candidates retain priority
over this extra capture.

`tests/emissive_dual_origin_regression.ps1` builds one 6x1 m emissive triangle
above a box, checks that both capture maps render, and verifies a visible box
shadow. The captured floor red channel is 0 behind the box and 65 at the lit
control point. Release build and the strip, mutual-shadow, spatial, and
textured-emission screenshot regressions passed. Multiple emissive clusters
still receive one capture each, within the current four-slot budget.

## Shape-aware emissive shadow filtering (2026-09-20)

Each emissive cluster now carries a world-space tangent and two half-extents
derived from its transformed bounds. The blocker-distance shadow filter projects
those axes perpendicular to the receiver ray and uses an oriented elliptical
25-tap kernel. A long strip therefore widens its penumbra mainly along the
strip, rather than using a circular kernel based only on area. Compact square
panels retain a near-symmetric filter. The comparison remains bounded to 0.12
radians and still uses one center-projected cubemap per selected sample.

Spatial splitting of one flat strip exposed false mutual shadows between
coplanar pieces. The transient shadow geometry now omits the selected cluster
and any near-coplanar clusters in the same primitive, while retaining separated
layers as occluders. `tests/emissive_strip_shadow_regression.ps1` captures a
6x1 panel over the box stage; the floor red channel is 1 in the box shadow and
86 at the lit control point. The square-panel capture is
`artifacts/screenshots/emissive-mesh-shadow-shape-square.png`. Release build,
mutual-shadow, spatial, texture, and authored area-shadow regressions passed.

## Emissive samples sharing a primitive now shadow each other (2026-09-20)

For each selected emissive shadow, the renderer builds a transient index buffer
containing the source primitive's triangles outside that selected cluster and
its coplanar peers. The source surface stays out of its own near-plane capture,
while separated clusters in the same primitive remain as occluders. The CPU retains one index copy for
emissive primitives; only the selected shadow slots allocate filtered indices
for the current frame. If transient space is unavailable, it safely omits the
source primitive for that capture.

`tests/emissive_mutual_shadow_regression.ps1` creates a bright upper panel and
a dark lower panel in one glTF primitive. The lower panel contributes no direct
light but blocks the upper one. On D3D11, floor-center red falls from 83 in the
clear control to 57 with the blocker. Zero-radiance clusters no longer use a
light or shadow slot. Release build and the spatial two-panel regression passed.

## Spatial emissive-mesh samples (2026-09-20)

Emissive triangles still group by face direction, but each direction group now
splits along its widest axis when its span exceeds 1.5 times the square root of
its emitting area. Splitting stops at four clusters per direction; runtime still
selects the strongest four samples across the scene. Compact tessellated panels
remain one source, while long strips and spatially separated panels can receive
separate lighting and shadow centers. Texture radiance is averaged within each
resulting cluster.

`tests/emissive_spatial_regression.ps1` builds two disjoint panels in one glTF
primitive and checks the rendered floor: red channel 170 under both panels and
132 at the midpoint. Release build, red/blue textured regression, and the
single-panel shadow capture passed. The four-sample and two-shadow-slot limits
still apply.

## Emissive-mesh shadow penumbra (2026-09-20)

The selected emissive cluster is excluded from its own local shadow capture.
This prevents the source panel from becoming a near-plane blocker.
For an emissive shadowed pixel, the shader estimates blocker distance by six
comparison-depth probes along the center ray, then sizes its disk filter from
the gap between blocker and receiver. The fallback remains the bounded filter
when the center ray is lit. Point, spot, and authored area-light filtering are
unchanged. This is still a single center-projected map per selected emitter.

`demos/emissive_mesh_shadows.py` uses the floor, panel, and box stage.
`artifacts/screenshots/emissive-mesh-shadow-before.png` shows the prior hard
rectangle; `emissive-mesh-shadow-center-depth.png` shows the softer edge without
the false dark wedges from offset blocker probes. The unoccluded panel capture
`emissive-mesh-unoccluded-after-shadow.png` remains clean. Release build, area
light capture, and red/blue emissive texture regression passed.

## Textured emissive-mesh lighting (2026-09-20)

Emissive mesh clusters now accumulate four interior texture samples per
triangle, weighted by triangle area. Sampling honors emissive TEXCOORD selection,
KHR_texture_transform, and sampler wrapping, and converts sRGB texels to linear
radiance before multiplying the material emissive factor. CPU image data is
cached during model import and released after the clusters are built. A missing
or invalid texture falls back to the same white factor as the surface renderer.

`tests/emissive_texture_regression.ps1` generates red and blue textured variants
of the isolated emissive panel, captures both on D3D11, and compares the floor
pixels. The red capture totals 543645 red versus 96975 for blue; the blue capture
totals 527151 blue versus 63193 for red. Release build and the existing uniform
panel capture also passed. The cluster average cannot project fine texture
patterns onto receivers; it approximates emitted color and power.

## Emissive-mesh direct lighting (2026-09-20)

Opaque, unskinned glTF triangles with nonzero emissive factor now illuminate
nearby surfaces. Each emissive primitive is grouped by local face direction and
spatial span;
the renderer transforms those groups each frame and selects the four strongest
area/radiance/distance candidates. The PBR pass treats each selected group as a
one- or two-sided finite-area sample, including during reflection capture.
Material emission and bloom still render through their existing path. This
adds no sampler slots or authored point lights.

`demos/emissive_mesh_lighting.py` loads an isolated warm panel over a matte floor
with zero sun and ambient radiance. The D3D11 capture
`artifacts/screenshots/emissive-mesh-lighting.png` shows the resulting light
pool; `emissive-mesh-no-emission.png` removes it when emissive strength is zero.
The existing `demos/emissive_bloom.py` capture also passed. Release build passed.

This is a bounded direct-light approximation. Skinned, blended, and alpha-masked
emitters do not create light samples. Large curved or dispersed emitters may
need more than four samples for faithful lighting.

## Area-light shadows (2026-09-20)

Rectangular area lights now accept `casts_shadows` (default true) and
`shadow_bias` (default 0.05). They compete with point and spot lights for the
local shadow slots, using the same strength/distance
selection. Shadow depth is captured from each selected panel's center and
sampled during main shading and reflection capture. A 25-tap Vogel disk scales
with the panel's apparent size, softening the silhouette without adding another
shadow texture. This approximates the panel penumbra; the center depth map does
not provide blocker-distance contact hardening.
The isolated box-and-floor capture is
`artifacts/screenshots/area-light-shadow-regression.png`; the area light casts
a clean box shadow on D3D11 at Medium quality. The matching
`area-light-shadow-off-control.png` capture removes it. The filtered follow-up is
`area-light-soft-penumbra.png`. The Release build passed; the isolated scene
measured about 0.50 ms versus about 0.44 ms before the wider filter.

## Renderer backend separation and visible sources (2026-09-13)

The executable entry point is again a small `src/main.cpp`. Renderer-neutral
camera, light, probe, window, and quality descriptions now live in
`src/RenderScene.*`. The active bgfx implementation moved from the root-level
`src/bgfx_main.cpp` into `src/backends/raster/BgfxRasterBackend.*`, and the new
emissive-source pass is isolated in `LightSourceRenderer.*`. IES parsing and
cookie decoding are isolated in `LightPatternLoader.*`. Hybrid and path
tracer remain empty future extension points; the deleted NVIDIA RTXPT submodule
is not restored or required. `src/backends/README.md` records the dependency
boundary.

`gx.lights.visualize(...)` optionally draws emissive point spheres, oriented spot
lenses, and area rectangles. It defaults off, writes HDR color and normals into
the raster scene targets, depth-tests normally, and does not cast shadows. The
dark local-light showcase enables it at intensity 12, clearly separating source
bloom from surface-light falloff. Release build, 64 rejected API configurations,
and D3D11 capture passed: `artifacts/screenshots/visible-light-sources.png`.

## HDR bloom (2026-09-13)

`gx.renderer.realtime(bloom=True)` now enables a real post-process rather than
retaining an inert compatibility flag. The renderer soft-thresholds the HDR
scene before tone mapping, downsamples it to quarter resolution, applies a
separable Gaussian blur, and adds the result back ahead of exposure and AgX.
Because extraction operates on the composed HDR buffer, glTF emissive materials
and every supported local-light type bloom through the same path.

`demos/emissive_bloom.py` isolates the Khronos 1x-16x emissive-strength test with
no sun, ambient radiance, or local lights. Bloom-on and bloom-off controls are
`artifacts/screenshots/emissive-bloom.png` and
`artifacts/screenshots/emissive-no-bloom.png`. The local-light integration capture
is `artifacts/screenshots/local-lights-bloom.png`.

## IES profiles and spotlight cookies (2026-09-13)

Point and spot lights accept optional LM-63 Type C `ies=` profiles with
`TILT=NONE`; spot lights additionally accept projected PNG/JPEG/BMP/TGA
`cookie=` textures and an oriented `up=` vector. IES candela distributions are
bilinearly reconstructed over vertical and horizontal angles, normalized to
peak intensity, and support common rotational/quadrant/bilateral symmetries.
Cookies preserve color and alpha. A spot can combine both patterns.

Up to 12 unique patterns occupy reserved layers after the 12 local-probe faces
in the existing RGBA16F reflection texture array. No new sampler is required,
which preserves D3D11's 16-slot limit and keeps patterns available during local
reflection capture. `demos/light_patterns.py` uses the supplied flashlight cookie
and BEGA profile on a zero-ambient floor-and-wall stage so the full distributions
are visible. The BEGA fixture is mounted near the upper wall with its LM-63 zero
axis pointing down, matching the supplied reference rather than projecting the
profile head-on. Capture: `artifacts/screenshots/ies-cookie-wall-oriented.png`.

## Local-light isolation scene (2026-09-13)

`demos/local_light_showcase.py` displays a warm rectangular area light, a sharp
blue spot light, and a red point light as separate pools on a neutral ground
plane. The atmosphere now honors `gx.sky.radiance(0)` across the visible sky,
diffuse ambient, specular IBL, and environment prefilter. Sun intensity also
reaches true zero instead of retaining the former minimum direct-light floor.
The demo therefore contains no sun or ambient contribution. Capture:
`artifacts/screenshots/local-light-showcase-rect.png`.

## Rectangular area lights (2026-09-12)

`gx.lights.area(...)` adds up to two one-sided rectangular emitters. Direction
and up vectors are normalized into an orthogonal panel basis. Shading uses the
closest representative point on the finite rectangle, producing a clear rectangular
core on nearby receivers with soft distance falloff. It uses the same finite-radius
GGX diffuse/specular response as other local lights, with an
emitter-facing term that prevents illumination behind the panel. Area lights
use no texture slots and participate in local reflection capture. Their first
implementation had no shadows; the center-projected shadow path was added later.

Release shader/native build, 58 rejected Python API configurations, a one-probe
area-light capture, and the existing two-probe/full-shadow-sampler regression
all passed on D3D11. Capture: `artifacts/screenshots/area-lights-final.png`.

## Two-probe texture-array blending (2026-09-12)

Two local reflection probes are now resident and blend simultaneously for both
box-projected specular and diffuse irradiance. Each probe renders into temporary
cubemaps, then GPU blits copy all faces and roughness mips into sampling-only
`Texture2DArray` atlases. The PBR pass therefore retains one specular sampler and
one diffuse sampler instead of consuming additional D3D11 resource slots.

Probe selection still sorts by camera containment, priority, and distance. At
the surface, each selected probe receives its bounds/blend-distance weight; the
two local results normalize against one another and their combined coverage fades
to the global sky. The primary probe retains its visibility-aware diffuse grid.

The original failure was also traced to bgfx's 128-framebuffer default: the
second probe exhausted the handle pool and left later render targets invalid.
Genesis now builds bgfx with 256 framebuffer handles. Array uniform uploads were
corrected to submit all elements, also fixing secondary spot/shadow parameters.

Release build, single-probe capture, two-probe center/left/right captures, and a
full 16-slot test with two shadowed point lights all passed on D3D11. Primary
capture: `artifacts/screenshots/probe-atlas-lights-center.png`.

## Shadowed spot lights (2026-09-12)

`gx.lights.spot(...)` adds up to four finite-radius spot lights with normalized
directions, smooth inner/outer cone falloff, GGX diffuse/specular response,
per-light bias, and optional shadows. Point and spot lights share the two D16
cubemap shadow slots and the same strength/distance selection policy. Spot lights
also participate in local reflection capture. `gx.lights.clear()` clears point,
spot, and area lights.

Release build, 55 rejected Python API configurations, and the D3D11 visual
regression passed. Capture: `artifacts/screenshots/spot-lights-final.png`.

Curtain shadow acne was corrected with a slope-scaled normal receiver offset and
a five-tap cubemap PCF kernel. The clean follow-up capture is
`artifacts/screenshots/spot-lights-bias-pcf.png`.

## Point-light cubemap shadows (2026-09-12)

Point lights now cast omnidirectional D16 cubemap shadows. Two cubemaps are
resident, sized by the Low/Medium/High quality presets at 128/256/512 pixels per
face. When more than two lights request shadows, the renderer selects by emitted
strength divided by squared camera distance each frame. `gx.lights.point(...)`
adds `shadow_bias` (world-space, default 0.015) and `casts_shadows` (default true).
Unselected and non-shadow-casting lights still contribute normal GGX lighting.
Local reflection captures sample the same point shadows.

Release build and the embedded Python API validation passed. The updated visual
regression is `artifacts/screenshots/local-lights-shadows.png`.

## Script-authored point lights (2026-09-12)

`gx.lights.point(name, position, color, intensity, radius, shadow_bias, casts_shadows)` adds up to four local
point lights. PBR evaluates their diffuse and specular GGX response with smooth
finite-radius inverse-square falloff, and reflection-probe captures include their
lighting. `gx.lights.clear()` removes them.

The earlier two-resident-probe experiment caused a black frame on D3D11. This is
resolved by the sampling-only texture-array atlas described above.

## Named regression cameras and AO/contact debug views (2026-09-12)

Scripts can register multiple camera poses with `gx.camera.view(name, ...)` and
select one at startup with `--camera <name>`. The default pose remains the one
set by `camera.look_at`; unknown names exit with code 2 and list the registered
views. `demos/shadow_wall_regression.py` now provides `wall-surface`,
`wall-upward`, and `character`.

`gx.renderer.debug_view("ao" | "contact" | "none")` and the overriding
`--debug-view` option expose the two bilateral-upsampled AO target channels as
grayscale visibility. White is unoccluded. Release build and embedded Python API
validation passed. Captures are `artifacts/screenshots/named-wall-upward.png`,
`debug-ao-wall.png`, and `debug-contact-wall.png`.

## Visibility-aware diffuse probe grids (2026-09-12)

Each configured reflection volume now builds a static CPU probe grid at roughly
three-meter spacing from World-layer triangles. A BVH traces a 16x16 distance
cubemap and 128 cosine-weighted directions for each of six lighting lobes per
grid point. PBR trilinearly blends the eight neighboring probes, rejects probe
samples hidden behind static geometry, and combines directional sky, ground, and
blocked fractions with the selected local irradiance cube. If all neighboring
samples are rejected, shading falls back to the local irradiance cube.

The Release build and the wall, upward, and default hidden captures passed:
`artifacts/screenshots/diffuse-grid-wall.png`,
`artifacts/screenshots/diffuse-grid-upward.png`, and
`artifacts/screenshots/diffuse-grid-default-fallback.png`. A controlled grid-off
default capture confirmed that the backlit character silhouette predates the
grid and comes from the corrected direct-sun path. Medium-quality scene time in
the wall capture increased from about 0.94 ms to 1.79 ms on the target GPU.

## Local diffuse lighting (2026-09-12)

Local reflection captures now also generate a 16x16 RGBA16F diffuse irradiance
cube, using 128 cosine-weighted hemisphere samples per texel. PBR samples it
along the surface normal and blends using the existing probe bounds/weight.
Convolution runs only when the local capture is dirty. Capture shading disables
local probe feedback and remains ambient-only; no unshadowed sunlight is added.
This is a single selected probe approximation, not spatially resolved GI or
per-point visibility: separate rooms/alcoves still need finer probe coverage.

Release build and both visual regressions passed:
`artifacts/screenshots/local-diffuse-arches.png` and
`artifacts/screenshots/local-diffuse-upward.png`.
The original upward pose is retained in `demos/shadow_wall_upward_regression.py`.

## Wall sunlight leak correction (2026-09-12)

Follow-up surface fix: removed the PBR shader's 20% direct-sun floor in full
shadow and its additional unshadowed analytic solar reflection. Local probe
surface captures now use ambient-only lighting because camera-fitted cascades
cannot cover all six faces; dedicated probe shadows remain future work.
Updated `demos/shadow_wall_regression.py` to the second reported pose:
(-0.05, 1.45, 3.67), yaw -121.6, pitch 11.9, sun 133.1/65.1.
Release capture `artifacts/screenshots/wall-surface-shadow-fixed.png` verifies
the bright warm patches beneath the covered arches are removed.

The cascade depth fit incorrectly negated light-view Z although bx::mtxLookAt
and bx::mtxOrtho default to left-handed coordinates. This clipped shadow casters
and left volumetric sunlight unoccluded. Near/far now use minZ/maxZ directly,
retaining the 60 m caster allowance. The earlier two-sided geometry and cascade
selection changes alone did not resolve this root cause.

Reproduction: `demos/shadow_wall_regression.py`, camera (-0.75, 1.45, 0),
yaw approximately -50.2, pitch 24.5, sun 145/42. Release build and capture passed;
`artifacts/screenshots/wall-shadow-depth-fixed.png.png` shows the broad glow
removed from the reported arch/wall view. Static World geometry currently also
renders two-sided in the main pass. Camera/sun coordinates appear in the overlay.

## Current direction

Genesis is a Python-driven engine using one native executable and one CMake
build tree. The real-time renderer is bgfx/D3D11 raster PBR.

Use `build/release/microsoft/.cmake` for CMake metadata and output the Release
executable to `build/release/microsoft/genesis.exe`.

## Current demo

Default script: `demos/character_sponza_sky.py`

It loads Sponza plus `assets/characters/character1.glb` and plays the
character's running animation using GPU skinning.

Controls:

- RMB: mouse look
- Ctrl+LMB: aim the first configured cookie spotlight like a flashlight; when no
  cookie exists, move the sun
- Ctrl+Shift+LMB: move the sun even when a cookie exists
- WASD: move
- Q/E: move down/up
- Mouse wheel: change movement speed
- R: reset camera, view direction, movement speed, and sun
- Escape: release captured mouse
- F6: cycle Low, Medium, and High renderer quality
- F12: save a timestamped PNG under `artifacts/screenshots`

## Implemented renderer

- glTF scene and material loading
- GGX metallic/roughness PBR
- Normal, base-color, metallic/roughness, and emissive textures
- Correct sRGB treatment for color textures
- Mipmaps and anisotropic sampling
- HDR `RGBA16F` scene buffer with AgX-style tone mapping
- EnTT gameplay entities
- PocketPy scripting
- Render-layer masks (`World`, `Dynamic`, with `Effects` and `Overlay` reserved)
- F12 PNG capture and deterministic `--screenshot <path>` visual-QA capture
- Script-authored named camera poses selectable with `--camera <name>`
- AO and contact-shadow grayscale inspection through `--debug-view`
- GPU skeletal animation and skinning
- Official Eric Bruneton precomputed atmosphere, vendored at commit `34f14e7`
- Four-order spectral atmosphere bake with compact RGB runtime LUTs
- Physical Bruneton sky and solar disc
- Half-resolution, 16-step local volumetric fog and shadowed sun shafts
- Depth-aware bilateral volumetric upsampling using a separate fog-depth MRT
- Local volumetric fog applies only in front of scene geometry; sky pixels keep
  the already-integrated Bruneton atmosphere instead of double scattering
- Bruneton irradiance-LUT diffuse skylight with restrained warm ground bounce
- Runtime 128x128 `RGBA16F` Bruneton sky cubemap with eight GGX-prefiltered
  roughness mips, regenerated when the sun changes
- Roughness-LOD environment specular with a 256x256 split-sum BRDF integration LUT
- Script-authored box-projected local reflection probes, selected by containment,
  priority, and distance, with boundary blending to the global sky probe
- Runtime local scene capture and eight-mip GGX prefiltering when the selected
  probe or sun changes
- Per-volume static diffuse probe grids with BVH-traced directional visibility,
  eight-probe interpolation, and wall-aware rejection
- Up to four script-authored point lights with GGX diffuse/specular response and
  smooth finite-radius inverse-square falloff
- Up to four script-authored spot lights with smooth cone falloff; point and spot
  lights share two selected omnidirectional shadow slots
- GPU 256-bin log-luminance histogram auto exposure over composed HDR scene
  and volumetric lighting, sampled once per 4x4 block
- Percentile metering (10%-95%), calibrated 7% key, EV bounds, and ping-pong
  temporal adaptation with faster bright-to-dark response
- Half-resolution 12-sample SSAO from full-resolution world normals and depth
- Eight-step, 0.75 m sun-direction screen-space contact shadows with
  same-surface rejection and soft coverage
- Depth-aware bilateral AO/contact reconstruction at final resolution
- Three tightly frustum-fitted, texel-stabilized cascaded sun shadows shared by
  PBR and volumetrics
- Low/Medium/High raster quality presets controlling shadow resolution,
  reflection-cube resolution, effects resolution, AO/contact samples, and
  volumetric integration steps
- Smoothed total and per-view D3D11 GPU timings grouped into shadow, scene,
  AO/contact, volumetric, and post-processing costs; capture runs also print a
  machine-readable `GPU_TIMINGS` line

## Shadow configuration

- Three 1024x1024 D16 shadow maps
- Cascade ranges: 0-12 m, 12-35 m, and 35-90 m
- 12% transition bands between cascades
- Each cascade fits the eight corners of its camera-frustum slice in light space;
  following cascades include the preceding 12% transition region
- Two-percent receiver guard band, quantized projection extents, and projection
  centers snapped to shadow texels in a world-anchored light coordinate system
- Dynamic receiver depth fit with 60 m of sun-facing caster allowance and a
  robust alternate up axis at solar zenith
- Surface and volumetric cascade selection use forward camera depth, matching
  the fitted frustum slices; shadow casters render two-sided so thin glTF walls
  block sunlight from either direction
- 3x3 PCF on visible surfaces
- One comparison sample per volumetric ray step

## Atmosphere assets

Runtime LUTs are in `assets/atmosphere/bruneton`:

- `transmittance.rgb32f`: 256x64
- `scattering.rgb32f`: 256x128x32
- `single_mie.rgb32f`: 256x128x32
- `irradiance.rgb32f`: 64x16 (drives diffuse environment lighting)

The full 47-wavelength bake cache is under
`assets/atmosphere/bruneton/spectral_cache` and is ignored by Git because it is
roughly 795 MB. `BrunetonBake` regenerates both the cache and runtime tables.

## Build and run

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis -j 8
build/release/microsoft/genesis.exe
```

Capture a deterministic verification frame after startup:

```powershell
build/release/microsoft/genesis.exe --screenshot artifacts/screenshots/verification
```

Screenshot runs use a hidden window and terminate automatically after the PNG
callback completes. Fix the sun for repeatable lighting comparisons:

```powershell
build/release/microsoft/genesis.exe --script demos/clouds.py --sun -25 0 --screenshot artifacts/screenshots/sunset
```

Regenerate atmosphere LUTs only when physical atmosphere parameters change:

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target BrunetonBake -j 8
build/release/microsoft/BrunetonBake.exe assets/atmosphere/bruneton
```

Moving the sun does not require rebaking LUTs.

## Important source files

- `src/bgfx_main.cpp`: renderer setup, render graph, controls, scene submission
- `shaders/bgfx/fs_pbr.sc`: surface PBR and cascaded shadow sampling
- `shaders/bgfx/fs_sky.sc`: Bruneton runtime sky lookup
- `shaders/bgfx/fs_volumetric.sc`: half-resolution fog and sun shafts
- `shaders/bgfx/fs_tonemap.sc`: volumetric composition and tone mapping
- `tools/bruneton_bake.cpp`: official spectral LUT bake entry point

## Known limitations

- Two local reflection probes can be selected, captured, and blended at once.
  Additional candidates remain non-resident until selection changes.
- Diffuse grids include static World geometry only. Dynamic objects receive grid
  lighting but do not occlude it, and each grid still reuses one captured
  irradiance cube rather than storing independently captured radiance per point.
- Area-light shadows use a center depth map plus an apparent-size filter. They
  do not have blocker-distance contact hardening and share the four local shadow
  slots with point and spot lights.
- Emissive-mesh lighting uses up to four face/spatial samples. It averages texture
  variation per cluster and uses center-projected depth shadows in the shared
  four-slot local capture set; masked/blended surfaces and skinning are excluded.
- SSAO/contact shadows are screen-space and cannot see off-screen blockers.
  They intentionally remain restrained to avoid replacing real sun shadows.
- Auto exposure currently has a conservative +3 EV ceiling because Genesis's
  light units are not yet fully scene-referred/calibrated.
- Fly-camera collision is not implemented. Press R if the camera leaves the
  Sponza interior; the reset also restores the default sun.
- Named viewpoints are script-authored; there is not yet a batch command that
  captures every registered view in one process.
- Presets are fixed startup choices; live switching would require recreating
  resolution-dependent render targets.

## Latest visual QA

`artifacts/screenshots/verification-20260910-render-layers.png` verified the
capture path and clean depth-aware fog silhouettes. It also shows that the
backlit character and curtains remain too dark: analytic ambient fill is not a
sufficient replacement for physical sky irradiance and environment lighting.

`artifacts/screenshots/verification-20260910-environment-fixed.png` verified
the Bruneton-driven environment-lighting path from the default interior
camera. High-energy horizon samples in the initial directional-reflection
implementation caused blue-white speckles on high-frequency normal maps;
negative samples are now rejected and rare grazing samples are energy-limited.

The 18:53 screenshot that appeared to lose the scene was the fly camera above
the Sponza roof looking into the bright atmospheric horizon, not missing
geometry. R now restores the intended test camera and sun in one step.

The first sunset comparison exposed double scattering: local 90 m gameplay
fog was integrated over depth-clear sky pixels on top of the complete Bruneton
sky, flattening the horizon into beige-white. Sky pixels now bypass local fog
while scene geometry still receives fog and shadowed rays.

`artifacts/screenshots/verification-20260910-sunset-readable-v5.png` is the
hidden deterministic regression at azimuth -25 and elevation 0. It verifies
that the walls, floor, medallion, and curtain colors remain readable at sunset
without changing the corrected sun/sky path. Screenshot runs now exit after
capture, so they no longer leave a visible window or lock the executable.

`artifacts/screenshots/verification-20260910-auto-exposure-final-sunset.png`
and `verification-20260910-auto-exposure-final-day.png` verify the replacement
GPU histogram path. Sunset remains readable without the 32x sun-angle surface
workaround, while the default character view retains daylight contrast. The
histogram meters the composed HDR plus fog signal; metering raw HDR alone
overexposed the volumetric result and was rejected during visual QA.

The first AO capture (`verification-20260910-ssao-contact-day.png`) was rejected:
binary contact tests self-shadowed the floor and randomized half-resolution AO
produced a checker pattern. `verification-20260910-ssao-contact-stable.png`
verifies the corrected pass: fixed spiral sampling, same-surface normal
rejection, soft contact accumulation, reduced range/strength, and bilateral
depth reconstruction produce clean silhouettes without the checker artifacts.
`verification-20260910-ssao-contact-sunset.png` confirms the same pass also
preserves the low-sun auto-exposure and fog-separation regression.

`verification-20260910-prefiltered-probe-day.png` and
`verification-20260910-prefiltered-probe-sunset.png` verify the replacement
environment-specular path. The renderer now builds a persistent HDR cubemap
from the Bruneton LUTs, prefilters all eight roughness mips on the GPU, and
samples it directly from PBR materials. Both captures are free of the isolated
blue-white fireflies seen in the old directional approximation.

`verification-20260911-local-probe-materials.png` exercises the split-sum BRDF
LUT across the Khronos metallic/roughness material chart.
`verification-20260911-local-probe-sponza.png` verifies the default Sponza local
scene capture, box projection, global fallback blend, and the existing fog,
shadow, and exposure composition.

`verification-20260911-tight-cascades-day.png` verifies the tight per-slice
cascade fit at the default camera. `verification-20260911-tight-cascades-low-sun.png`
stresses long caster directions and cascade transitions, while
`verification-20260911-tight-cascades-zenith.png` verifies the non-degenerate
alternate light basis at exactly 90 degrees solar elevation.

`verification-20260911-quality-low.png`, `-medium.png`, and `-high.png` verify
all three preset render graphs at 1280x720. On the target GeForce RTX 2060 Super
(driver 595.79), the smoothed capture samples measured 1.71 ms Low, 1.80 ms
Medium, and 3.78 ms High GPU time. High's full-resolution AO/contact and
volumetrics account for most of its increase; all three captures preserve the
accepted composition.

`verification-20260911-wall-occlusion-fixed.png` stresses low-angle light through
thin Sponza architecture after fixing two leak paths: radial-distance cascade
selection could choose a tight slice that did not contain off-axis pixels, and
unconditional shadow-pass backface culling removed walls viewed from the sun's
back side. `verification-20260911-wall-leak-default-regression.png` confirms the
two-sided caster change does not introduce visible acne at the accepted camera.

## Next work, in priority order

1. Expand Project browsing: filesystem tree, thumbnails, refresh/watch and
   drag-to-scene placement.
2. Add per-slot material resources, textures/emission and remaining light controls.
3. Add detachable native panels, named custom layouts and browser persistence
   for workspace preferences.
4. Extend runtime simulation with script callbacks and debugger tooling; optimize
   render-target sizes when Scene and Game are both visible.

Renderer follow-ups retained from the earlier rendering work:

- Measure four-capture cost in representative scenes and examine capture
   selection for complex model bounds.
- Exercise the four-slot depth array in a browser WebGPU session and compare
   rendering with the D3D11 build.

## Working-tree caution

The repository contains extensive existing modified and untracked work. Treat
it as user-owned, preserve unrelated changes, and do not clean, reset, or
replace the working tree.

Update this file whenever renderer architecture, controls, build instructions,
feature status, known limitations, or the next-work order changes.
