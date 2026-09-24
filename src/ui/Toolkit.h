#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace genesis::ui {

struct Color { uint8_t r = 0, g = 0, b = 0, a = 0; };
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
    bool contains(float px, float py) const;
};
struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
    Insets() = default;
    Insets(float all) : left(all), top(all), right(all), bottom(all) {}
    Insets(float horizontal, float vertical)
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
};
struct Length {
    enum class Unit { Auto, Pixels, Percent };
    float value = 0;
    Unit unit = Unit::Auto;
    Length() = default;
    Length(float pixels) : value(pixels), unit(Unit::Pixels) {}
    static Length percent(float value) { Length result(value); result.unit = Unit::Percent; return result; }
};
enum class Direction { Column, Row };
enum class Cursor { Arrow, ResizeHorizontal, ResizeVertical, Crosshair };
enum class Align { Start, Center, End, Stretch };
enum class Justify { Start, Center, End, SpaceBetween };
enum class Font { Regular, Medium, Semibold };
enum class TextWrap { None, Word, Ellipsis };
enum class Kind { Container, Label, Button, CheckBox, Slider, TextField, Splitter };
enum class Icon { None, Cube, Folder, File, Play, Pause, Step, Frame, Search, Scene, Console, ChevronDown, ChevronRight };
enum class Key { Tab, Enter, Space, Escape, Left, Right, Up, Down, Home, End, Backspace, Delete, SelectAll };

struct Style {
    Length width, height;
    float minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;
    float grow = 0, shrink = 1, gap = 0;
    Insets padding, margin;
    Direction direction = Direction::Column;
    Align align = Align::Stretch;
    Justify justify = Justify::Start;
    bool wrap = false, clip = true, scroll = false;
    bool verticalText = false; // Rotate button/label text clockwise within its bounds.
    bool hitTest = true; // Decorative overlays can paint without blocking controls below.
    std::optional<Cursor> cursor;
    bool absolute = false;
    float left = 0, top = 0; // Yoga absolute offsets, relative to the parent.
    std::optional<float> right, bottom; // Optional opposite-edge anchors for overlays.
    bool stretchX = false, stretchY = false; // Honor both anchors when filling an absolute edge.
    Color background, borderColor;
    float radius = 0, borderWidth = 0;
    // Rounded containers over opaque surroundings: paints this color outside
    // the corners and redraws the border above descendants, so children with
    // square corners read as clipped to the rounded frame.
    Color frameColor;
    Color textColor{230, 235, 247, 255};
    Font font = Font::Regular;
    float fontSize = 13; // ThorVG points (96 dpi); other dimensions are logical pixels.
    float textAlign = 0; // 0 left, .5 center, 1 right.
    TextWrap textWrap = TextWrap::Ellipsis;
    Color hoverBackground{74, 91, 191, 255};
    Color pressedBackground{46, 60, 147, 255};
    Color focusColor{171, 191, 255, 255};
};

// Packed 0xAARRGGBB, straight alpha (BGRA bytes on little-endian machines).
// The host uploads/composites this surface; the toolkit knows no SDL/bgfx APIs.
struct Surface {
    uint32_t width = 0, height = 0;
    uint64_t revision = 0;
    std::vector<uint32_t> pixels;
};

// Draw order follows the retained widget tree. Geometry is drawn by bgfx;
// ThorVG only rasterizes text, SVG icons and bitmaps into the shared atlas.
struct GpuDrawCommand {
    // RoundedFrame fills outside the rounded bounds and strokes their border.
    enum class Type { RoundedRect, AtlasImage, RoundedFrame } type = Type::RoundedRect;
    Rect bounds, clip, atlas;
    Color fill, border;
    float radius = 0, borderWidth = 0;
};
struct GpuDrawList {
    uint32_t width = 0, height = 0, atlasWidth = 0, atlasHeight = 0;
    uint32_t rasterizedTiles = 0;
    uint64_t revision = 0, atlasRevision = 0;
    std::vector<GpuDrawCommand> commands;
    std::vector<uint32_t> atlasPixels;
};

class Document;
struct ToolkitAccess;

// A retained widget. References remain stable until its parent's clear() or
// document destruction. Labels and buttons are leaves; containers own children.
class Node {
public:
    ~Node();
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node& column();
    Node& row();
    Node& label(std::string text);
    Node& button(std::string text, std::function<void()> onClick);
    Node& checkBox(std::string text, bool checked, std::function<void(bool)> changed);
    Node& slider(float value, float minimum, float maximum, float step, std::function<void(float)> changed);
    Node& textField(std::string value, std::function<void(const std::string&)> changed);
    Node& numberField(float value, float minimum, float maximum, float step, std::function<void(float)> changed);
    Node& splitter(Direction axis, std::function<void(float)> dragged); // Delta in logical pixels; arrows step 10px.
    Node& setChecked(bool checked); // Programmatic setters do not emit callbacks.
    bool checked() const;
    Node& setValue(float value);
    Node& setNumberDecimals(int decimals); // -1 uses compact formatting; 0..9 uses fixed decimals.
    Node& setDragAdjustable(bool enabled = true); // Numeric field: drag horizontally or click to edit.
    float value() const;
    void scrollTo(float y);
    float scrollOffset() const;
    void clear();
    void reparent(Node& parent); // Same-document container; preserves this node and all descendants.
    Node& setStyle(const Style& style);
    const Style& style() const;
    Node& setText(std::string text);
    const std::string& text() const;
    Node& setPlaceholder(std::string text); // Text field hint; never part of its value.
    Node& setIcon(Icon icon, float size=14); // Code-native vector icon, optionally before text.
    Node& setGodotIcon(std::string name, float size=14); // Name under editor/icons, or full modules/.../icons path, without .svg.
    Node& setBlenderIcon(std::string name, float size=14); // Name under assets/editor_icons/blender, without .svg.
    Node& setTooltip(std::string title, std::string description = {}, std::string shortcut = {});
    Node& setImage(std::shared_ptr<const Surface> image); // Immutable bitmap, fitted inside padding; null restores icon.
    Node& onTextCommitted(std::function<void(const std::string&)> callback); // Enter or blur, only when changed.
    Node& onKeyDown(std::function<bool(Key)> callback); // Return true when handled.
    void focus(); // Focus/reveal an enabled interactive node after layout.
    Node& setVisible(bool visible);
    Node& setEnabled(bool enabled);
    Node& onClick(std::function<void()> callback);
    Node& onPointerDown(std::function<void(float,float)> callback); // Document coordinates.
    // Button drag starts after 5 logical pixels. Coordinates are in the document.
    // Finished receives false on Escape/focus loss; a drag suppresses the click.
    Node& onDrag(std::function<void(float,float)> moved, std::function<void(bool)> finished);
    Rect bounds() const; // Logical viewport coordinates after layout/render.
    bool hovered() const;
    bool pressed() const;
    bool focused() const;
private:
    friend struct ToolkitAccess;
    friend class Document;
    struct Impl;
    std::unique_ptr<Impl> m;
    Node(Document& owner, Kind kind);
    Node& add(Kind kind, std::string text);
};

// Single UI thread, renderer/platform independent. Shared font/runtime resources
// live until the last Document dies. Only dirty documents rerasterize.
class Document {
public:
    Document();
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Node& root();
    void layout(float width, float height);
    const Surface& render(float width, float height, float scale = 1);
    const GpuDrawList& renderGpu(float width, float height);
    bool dirty() const;
    bool pointerMove(float x, float y);
    bool pointerDown(float x, float y, bool control = false); // control: Ctrl/Cmd held.
    bool pointerControl() const; // Whether Ctrl/Cmd was held at the latest pointerDown.
    bool pointerUp(float x, float y);
    bool wheel(float x, float y, float deltaY); // Logical pixels; positive scrolls down.
    void pointerLeave();
    bool keyDown(Key key, bool shift = false, bool repeat = false);
    bool keyUp(Key key);
    bool textInput(std::string text);
    bool wantsTextInput() const;
    std::string selectedText() const;
    bool cutSelection();
    void cancelInput(); // Focus loss: release capture/keys and clear focus.
    void openPopup(Node& popup, Node& anchorBar); // Popup must be mounted last for drawing.
    void closePopup();
    bool hasPopup() const;
    bool hasPointerCapture() const;
    Cursor cursor(); // Splitter capture takes priority over hover; updates pending layout.
private:
    friend class Node;
    friend struct ToolkitAccess;
    struct Impl;
    std::unique_ptr<Impl> m;
};

}
