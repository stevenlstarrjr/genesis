# GUI library and editor UI work

All layout uses the existing Yoga-backed `genesis_ui` widgets. ThorVG remains
the vector and text renderer. Updated: 2026-09-22.

The editor follows [Unity's workspace structure](https://docs.unity3d.com/2023.2/Documentation/Manual/UsingTheEditor.html):
Hierarchy left, Scene center, Inspector right, Project/Console below, centered
Play control and status bar. This is the implemented Genesis editor shell;
the remaining items below track the work toward a fuller Unity-style editor.

## Current implementation pass

- [x] Shared theme: consistent typography, spacing, surfaces, borders and control states.
- [x] Unity-style workspace: centered Play, Hierarchy, Scene, Inspector and Project/Console tabs.
- [x] Visual correction from the Unity reference: neutral gray surfaces, 20–24 px controls, thin tabs, compact hierarchy rows, vector icons and single-line XYZ properties.
- [x] Working File/Edit/Assets/GameObject/Component/Window/Help and Layout dropdowns with keyboard navigation and outside-click dismissal.
- [x] Project folder filters, asset search, icon grid, file inspection and Add to Scene for models.
- [x] Compact XYZ transform controls, selection/empty states, object counts and save-state feedback.
- [x] Search placeholders and text commits that create one undo step per rename.
- [x] Draggable scrollbars with safe capture, clipping and focus-loss handling.
- [x] Keyboard hierarchy navigation with selection and expansion feedback.
- [x] Resizable Yoga panels, keyboard-operated dividers and Reset Layout.
- [x] Native directional resize cursors on splitter hover and capture, with safe reset on cancellation, focus loss and layout changes.
- [x] Console for editor actions, import/save errors and game-launch status, with Clear and bounded history.
- [x] Verify Yoga layout at minimum window size, normal desktop size and high DPI; inspect rendered editor output.
- [x] Draggable move/rotate/scale handles, local rotation/scaling, uniform scale, Ctrl/toggle snapping and one undo record per gesture; Escape/focus loss cancel safely.
- [x] Add/remove Mesh Renderer, validated model replacement and searchable resource picker.
- [x] Per-object material color/metallic/roughness override, RGB swatches/presets and restore-source toggle.
- [x] Authored point/spot/area light and reflection-probe inspectors, with live rendering, undo/redo, duplication and scene persistence.
- [x] Reusable Yoga DockSpace: movable tabs, four-sided splits, insertion/drop previews, keyboard tabs and resizable dividers.
- [x] Default/Tall/Wide presets, automatic native workspace persistence and Window-menu recovery of closed panels.
- [x] Embedded Game tab with Play/Pause/Step/Stop, isolated runtime world, fixed ticks, focused game input and exact edit-state restoration.
- [x] Project filesystem tree, scoped search, paged grid, lazy image/model thumbnails, Refresh/Watch and stable file selection.
- [x] Drag model tiles into Scene with ground-plane placement, coordinate preview, snapping, cancellation and one undo record; external Scene drops use the same path.

## Next authoring tools

- [ ] Tooltips, nested dropdowns and right-click context actions in the shared GUI library.
- [ ] Detachable native windows and named custom workspace presets; in-window docking and last-layout persistence are implemented.
- [ ] Asset import settings, textured/animated model previews and reimporting changed models in the renderer.
- [ ] Feed script/renderer diagnostics into Console, with severity filters and source links.
- [ ] Transform plane handles, configurable snap increments, pivot/center and local/global switches.
- [ ] Parenting and multi-object selection.
- [ ] Per-material-slot assets, texture replacement, emissive overrides, light cookie/IES pickers and editing script-supplied lighting.
- [ ] HSV/hex/eyedropper color picking and native file dialogs; shared RGB fields/presets and searchable resource/path picking are implemented.
- [ ] Rebaking diffuse GI grids for newly authored probes and changed scene geometry; authored reflection captures update live.

## Further GUI foundation

- [ ] Accessible control roles, names and platform adapters.
- [ ] IME, grapheme-aware editing, text undo and multiline editing.
- [ ] Virtualized lists and trees for large projects.
- [ ] Profile large UI documents, then add targeted repaint/upload optimizations.

## Verification artifacts

- `artifacts/screenshots/editor-ui-before.png` and `editor-ui-after.png`: native D3D11 editor captures.
- `artifacts/screenshots/editor-unity-layout.png`: corrected Unity-reference appearance.
- `artifacts/screenshots/editor-transform-tools.png`: native D3D11 transform toolbar and draggable handles.
- `artifacts/editor-components/`: native material, light and probe captures plus generated scene fixtures.
- `artifacts/editor-docking/`: native default/wide/inactive-Scene captures and saved-layout fixtures.
- `artifacts/editor-play/`: native Game, Scene/Game split, hidden views and standalone-player captures.
- `artifacts/editor-project/`: native Project browser with real geometry/image thumbnails and project-only workspace.
- `artifacts/editor-ui/`: CPU-rendered desktop, minimum 1100x720, 2x DPI, Console and empty-selection captures.
- `UiToolkitTests`: field commits, keyboard tree navigation, pointer capture, scrollbars, splitters and existing controls.
- `GameEditorTests`: live scene changes/history, Yoga panel resizing, DPI, Console and persistence.
- `EditorNavigationTests`: transform geometry/picking, rotated-object manipulation, snapping, single-step history, cancellation and captured SDL input.
- `tests/editor_components_regression.ps1`: GPU readbacks verify material overrides and enabled/disabled authored lights; probe capture smoke check.
- `tests/editor_docking_regression.ps1`: native D3D11 composition after loading saved layouts, including a hidden Scene tab.
- `tests/editor_playback_regression.ps1`: native D3D11 Game/Scene composition, hidden views and standalone-player compatibility.
- `tests/editor_project_regression.ps1`: native thumbnail composition with both live Scene and Project-only layouts; authored-file hash checks.
- Project tests cover incremental indexing, added/modified/deleted files, Watch, stable selection, malformed assets, real thumbnails and internal/OS drop input isolation.

Mark items complete only after implementation and relevant interaction/render checks.
