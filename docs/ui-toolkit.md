# Genesis UI toolkit

`genesis_ui` is a retained-mode C++ UI library in `src/ui`. It is intended to be
the shared foundation for Genesis's editor, launcher and in-game UI—not a layer
inside the raster renderer. Yoga calculates layout. `Document::renderGpu` emits
ordered draw commands for rounded panels and borders, with ThorVG rasterizing
text, SVG icons and bitmaps into a texture atlas. The bgfx adapter draws these
commands on the GPU. `Document::render` remains a software path for tests and
hosts without bgfx. The library has no SDL, bgfx, scene, or editor dependency.

The consumers are the scene editor, project launcher, renderer HUD and editor UI workbench. The launcher
host (`ProjectPrompt.cpp`) handles SDL events, native file/folder dialogs and
dropped paths; `LauncherView` only builds widgets. The raster backend draws the
editor and HUD UI after tone mapping with `BgfxUiRenderer`.

Run the interactive workbench with:

```powershell
build/release/microsoft/genesis.exe --ui-workbench
```

Select a node in the scene tree and edit its text, width, padding, font size,
corner radius, enabled state or visibility. These properties are bound to the
actual controls in the center preview. This is a working editor-control testbed,
not a mockup and not yet a 3D scene editor. Its application model lives separately
in `tools/editor/UiWorkbench.*`.

## Building a view

Link the `genesis_ui` CMake target and include `ui/Toolkit.h`:

```cpp
using namespace genesis::ui;
Document ui;

Style root;
root.padding = 16;
root.gap = 12;
root.background = {12, 16, 25, 255};
ui.root().setStyle(root);

auto& status = ui.root().label("Ready");
auto& actions = ui.root().row();
actions.button("Select camera", [&] { status.setText("Camera selected"); });
actions.button("Unavailable", [] {}).setEnabled(false);

float width = 260;
ui.root().numberField(width, 80, 460, 10, [&](float value) { width = value; });
ui.root().textField("Node name", [&](const std::string& value) { status.setText(value); });
ui.root().checkBox("Enabled", true, [](bool enabled) { /* update model */ });
ui.root().slider(8, 0, 32, 1, [](float radius) { /* update model */ });

const Surface& pixels = ui.render(800, 600, 1.5f);
// Host uploads pixels to a texture and composites it with straight-alpha blending.
```

Construct the widget tree once. Update labels/styles/enabled state when app
state changes; don't rebuild every frame. `render()` returns the cached surface
when clean. `Surface::revision` changes only after rasterization, so hosts can
skip unchanged uploads. Returned surfaces belong to their document and are
updated by subsequent renders.

## Editor controls

`ui/Toolkit.h` supplies labels, buttons, checkboxes, sliders, single-line text
fields and numeric fields. `ui/EditorWidgets.h` supplies `TreeView`, `Tabs`,
`ScrollView`, collapsible `section()` groups and labeled `property()` rows.

`Node::setImage(std::shared_ptr<const Surface>)` displays an immutable ARGB
bitmap in a label or button, fitted and centered inside its padding with the
source aspect ratio intact. It respects clipping, disabled opacity and existing
input behavior. Null clears the bitmap and restores the node's icon fallback.
The surface must contain exactly width times height pixels, with dimensions in
1..4096. Decoding, caching and preview generation belong to the consuming app;
the shared toolkit has no filesystem or image-codec dependency.

- Text fields support click/drag selection, a caret, horizontal scrolling,
  UTF-8 codepoint editing, arrows, Home/End, Backspace/Delete and select-all.
  The SDL adapter handles Ctrl+A/C/X/V and starts/stops platform text input.
  Text-change callbacks are live. Multiline pastes are flattened.
- Numbers keep a draft while typing, commit on Enter or focus change, clamp to
  their configured range and reject nonfinite/invalid input. Invalid drafts
  remain highlighted on Enter and revert on blur. Escape discards an unfinished
  draft. Up/Down increments by the configured step.
- Sliders drag with pointer capture and clamp/quantize to their range/step.
  Arrows step; Home/End jump to bounds. Checkboxes support mouse and Space/Enter.
- Programmatic `setText`, `setValue`, `setChecked`, tree selection and tab
  activation do not emit change callbacks, preventing model-binding loops.
- Tree items have unique stable string IDs, selection and expandable children.
  Tabs switch real content subtrees. Scroll views clip overflow, respond to the
  wheel and reveal offscreen controls during keyboard navigation. Scrollbar
  thumbs currently indicate position; they are not draggable.
- Composite control objects must live as long as their mounted nodes. Keep them
  as members of the owning view, not temporary locals; do not copy them or clear
  their mounts externally. Application selection/undo state stays outside them.

## Layout and styles

- Containers compose rows/columns with gap, padding, margin, grow/shrink,
  alignment, justification and optional flex wrapping.
- Dimensions accept logical pixels, `Length::percent(50)`, or default `Length{}`
  for auto. Min/max dimensions are in logical pixels; max zero means unbounded.
- Backgrounds, border width/color, corner radius, text color, Inter font weight,
  point size and word-wrap/ellipsis are configurable per widget.
- Buttons have normal, hover, pressed, disabled and keyboard-focus appearances.
- Text is measured with ThorVG itself through a Yoga measure callback. It is not
  sized by multiplying string length by a guessed character width.
  Intrinsic measurement includes advance width, not just glyph ink. The shared
  layer supplies word/UTF-8 codepoint line breaks because the pinned ThorVG word
  wrapper assumes a font-specific space glyph index. Measurement and drawing
  use the same prepared text; the vendored dependency is left unchanged.
- Containers clip descendants to rectangular bounds by default. Rounded corners
  affect backgrounds, not the descendant clipping mask. Setting `frameColor` on
  a rounded container paints that color outside its corners and restrokes its
  border above the descendants, so square children read as clipped when the
  surroundings are opaque (dock areas use the canvas color). `clip=false` allows
  overflow within the nearest clipping ancestor; the viewport always clips.
- Hidden nodes take no layout space. Disabled state propagates through a subtree.

Change styles by copying `node.style()`, editing the copy, and calling
`node.setStyle(style)`. This keeps layout/paint invalidation automatic.

## Input, ownership and rendering

All API calls belong on one UI thread. The library keeps shared ThorVG/font
resources alive until the last document is destroyed. The bundled Inter fonts
are currently resolved from `GENESIS_ROOT/assets/fonts/inter`; packaging and
custom font registration are future work.

The host calls `layout(logicalWidth, logicalHeight)` before dispatching input,
then forwards `pointerMove/Down/Up`, `pointerLeave`, `wheel`, `textInput`, `keyDown/Up` and
`cancelInput` on window focus loss. Pointer coordinates are logical window
coordinates, not physical texture pixels. Forward only the primary mouse button.
Use the platform's mouse capture while a press is active, so release outside the
window is received. Tab/Shift+Tab traverse enabled controls and reveal controls
inside scroll views. Enter/Space activate buttons and checkboxes on release,
with auto-repeat suppression. Release outside a pressed button cancels activation.
Focus-loss commits valid numeric drafts and clears pending button activation.
`SdlUiInput.*` is the optional SDL adapter, compiled into the host rather than
the `genesis_ui` library.

`Document::cursor()` reports Arrow, ResizeHorizontal or ResizeVertical after
resolving pending layout. Splitters choose their drag axis; captured splitters
retain that cursor outside their bounds. `SdlUiInput::sync()` maps the result to
cached native system cursors and restores the default after capture/hover ends.
Hidden, disabled or removed splitters cannot retain a resize cursor. Hosts call
`sync()` after layout changes as well as events, even if the pointer stays still.

`ui/Theme.h` supplies shared surface, typography, border and interaction tokens
with `theme::button`, `theme::field` and `theme::label` styles. Editor composites
and primitive defaults use these styles.

`Node::setIcon` displays Godot SVG editor icons for cube, scene, file/folder,
playback, frame, search, console and disclosure controls. Yoga measures icon
extents, and ThorVG renders the SVGs at the requested size. The complete pinned
Godot set is in `assets/editor_icons/godot` with its MIT license. Use
`node.setGodotIcon("Folder")` for any core icon or
`node.setGodotIcon("modules/csg/icons/CSGBox3D")` for a module icon. Names omit
`.svg`. Existing `Icon` values retain a drawn fallback if assets are absent.

`MenuBar` creates real menus with enabled predicates and action callbacks.
Construct it after the document's other content so its popup paints last.
Popups use Yoga absolute positioning, leave workspace geometry unchanged, and
close on Escape, outside click or focus loss. Tab stays within popup commands;
Up/Down/Home/End navigate commands and Left/Right switch menus. The SDL editor
host suppresses scene shortcuts and viewport navigation while a menu is open.

`ResourcePicker` supplies a searchable Yoga popup with disabled items, secondary
descriptions, icons and optional path entry. Mount it after normal content,
then call `open(anchor, title, items, chosen, allowPath)`. Down enters results,
Up returns to search, Enter chooses a result and Escape cancels. The popup closes
before invoking the chosen callback, which may safely open another picker.
Applications validate paths and retain their own model selection.

`ColorField` supplies an editable `#RRGGBB` value, a Pick button and three linear
RGB number fields. `ColorPicker` adds a continuous saturation/brightness square,
hue strip and HSB/hex entries. User commits emit the complete RGB value;
`setValue` updates silently.

Text fields support `setPlaceholder` without changing their value. The existing
text-change callback is live; `onTextCommitted` instead reports a changed draft
on Enter or blur. Programmatic `setText` updates the committed baseline silently.
Escape restores that baseline for fields using a commit callback. Callbacks may
rebuild other subtrees without losing the control clicked during the commit.

Scroll views support wheel input, thumb dragging and track clicks, with pointer
capture outside their bounds. `splitter(Direction::Row, callback)` creates a
vertical panel divider; `Direction::Column` creates a horizontal divider. The
callback receives a logical-pixel delta. Arrow keys resize by 10 logical pixels;
the application sets its panel dimensions and constraints through Yoga styles.
Both controls release capture when hidden, disabled, removed or focus is lost.

Tree views support Up/Down, Home/End, Left to collapse/select the parent, and
Right to expand/enter the first child. Rebuilding items with the same IDs retains
expansion and surviving focus. `focusSelected()` reveals the selected visible
row; programmatic `select()` never emits the application selection callback.

`render(width, height, scale)` scales raster output without changing layout or
hit testing. Surfaces are packed **0xAARRGGBB, straight alpha**, which means BGRA
bytes on little-endian hosts. Upload as SDL ARGB8888 or bgfx BGRA8. Do not apply
scene lighting, bloom, exposure or tone mapping to UI colors.

`renderGpu(width, height)` keeps widget order and clips while putting panel fills,
borders and rounded corners in bgfx geometry. ThorVG still rasterizes text,
icons and images into one atlas when the document changes. The backend uses the
software path if GPU UI initialization or atlas allocation fails.

Parents own their child nodes. `Node&` references remain stable until that node
or an ancestor is cleared/destroyed. `clear()` invalidates references into the
removed subtree and clears any focus/capture pointing there. Callbacks may clear
their own widget subtree; they must not destroy the enclosing document during
event dispatch. Labels and primitive controls are leaves. Application state stays outside the
widget tree; callbacks update it and refresh affected widgets.

## Docking

`ui/DockSpace.h` supplies an editor-independent `DockSpace`. Register stable
content pages with `add(id, title, icon, minWidth, minHeight)`, then apply a
`DockLayout` made from `group` and `split`. Leaves contain ordered panel IDs and
an active ID; split nodes contain an axis, ratio and two children. Layout values
are validated before replacing the workspace. Unknown IDs, duplicate panels,
nonfinite/out-of-range ratios and malformed trees are rejected atomically.

Call `Document::layout`, `DockSpace::arrange`, then `Document::layout` after a
window-size change to resolve minimum pane dimensions. `dock`, `show`, `close`,
`visible`, `panelBounds` and `tabBounds` support host commands and tests. A change
callback lets the host persist the tree without introducing file or JSON
dependencies into the GUI library. Missing panel IDs represent closed pages.

The docking control uses `Node::reparent` to preserve content nodes, field state
and application references when rebuilding its containers. Reparenting only
accepts containers in the same document and rejects ownership cycles. Do not
externally clear a DockSpace's nodes while it is alive.

`Node::onDrag` provides button dragging with a five-pixel threshold, absolute
pointer coordinates and a completion/cancellation callback. A completed drag
suppresses the click. Escape/focus loss cancel; other keys and scrolling do not
interrupt an active drag. Hosts should honor `Document::hasPointerCapture()`
before routing input to an underlying game viewport.

## Editor direction and current scope

The editor can compose a toolbar, scene hierarchy, central viewport host,
inspector and asset browser from shared containers and controls. The native
window and rendering backends should remain adapters; selection, commands and
undo belong to editor code, not the UI core. `genesis_editor_ui` composes the
workbench from the reusable library without adding a dependency from the
library back to editor code.

This version does **not** yet supply IME composition, grapheme-aware cursor
movement, multiline editors, text undo, virtualized trees,
nested menus, tooltips, detachable native dock windows, accessibility
adapters, Python bindings, GPU text rasterization or partial-region redraws.
These are further layers before a full editor; the workbench itself does not
load/save engine scenes or implement editor command undo. Dirty documents repaint the whole surface;
keep rapidly changing HUDs separate from large mostly-static editor surfaces.

## Tests

```powershell
cmake --build build/release/microsoft/.cmake --config Release --target Genesis UiToolkitTests
ctest --test-dir build/release/microsoft/.cmake -C Release --output-on-failure
build/release/microsoft/UiToolkitTests.exe artifacts/ui-toolkit
```

The CPU tests exercise actual Yoga layouts and ThorVG pixels, alpha packing,
DPI scaling, cached rendering, text measurement/wrapping, clipping, focus order,
pointer capture, cancellation, disabled/hidden ancestors, shared font lifetime,
and mutation from callbacks, plus field editing, validation, sliders, checkboxes,
scrolling, tabs and tree state. Optional BMP captures show the real launcher,
narrow launcher, HUD, controls and workbench. No GPU or window is required
for the UI tests. Native file-dialog completion still requires platform testing.
