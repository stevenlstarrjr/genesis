#include "ui/Toolkit.h"
#include "ui/TextEdit.h"
#include "ui/Theme.h"

#include <thorvg.h>
#include <yoga/Yoga.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace genesis::ui {
namespace {
void check(tvg::Result result, const char* operation) {
    if (result != tvg::Result::Success) throw std::runtime_error(operation);
}
const char* fontName(Font font) {
    constexpr const char* names[] = {"Inter-Regular", "Inter-Medium", "Inter-SemiBold"};
    return names[static_cast<int>(font)];
}
struct Runtime {
    std::array<std::string, 3> paths;
    Runtime() {
        check(tvg::Initializer::init(0), "UI: ThorVG initialization failed");
        size_t loaded = 0;
        try {
            for (size_t i = 0; i < paths.size(); ++i) {
                paths[i] = (std::filesystem::path(GENESIS_ROOT) / "assets/fonts/inter" /
                    (std::string(fontName(static_cast<Font>(i))) + ".ttf")).string();
                check(tvg::Text::load(paths[i].c_str()), "UI: unable to load Inter fonts");
                ++loaded;
            }
        } catch (...) {
            for (size_t i = 0; i < loaded; ++i) tvg::Text::unload(paths[i].c_str());
            tvg::Initializer::term();
            throw;
        }
    }
    ~Runtime() {
        for (const auto& path : paths) tvg::Text::unload(path.c_str());
        tvg::Initializer::term();
    }
};
std::shared_ptr<Runtime> runtime() {
    static std::weak_ptr<Runtime> shared;
    auto result = shared.lock();
    if (!result) { result = std::make_shared<Runtime>(); shared = result; }
    return result;
}
Rect intersect(Rect a, Rect b) {
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    a.x = std::max(a.x, b.x); a.y = std::max(a.y, b.y);
    a.width = std::max(0.0f, right - a.x); a.height = std::max(0.0f, bottom - a.y);
    return a;
}
void dimension(YGNodeRef node, Length value, bool width) {
    if (value.unit == Length::Unit::Auto) {
        if (width) YGNodeStyleSetWidthAuto(node); else YGNodeStyleSetHeightAuto(node);
    } else if (value.unit == Length::Unit::Percent) {
        if (width) YGNodeStyleSetWidthPercent(node, value.value); else YGNodeStyleSetHeightPercent(node, value.value);
    } else {
        if (width) YGNodeStyleSetWidth(node, value.value); else YGNodeStyleSetHeight(node, value.value);
    }
}
tvg::Text* textPaint(const Style& style, const std::string& value) {
    auto* text = tvg::Text::gen();
    check(text->font(fontName(style.font)), "UI: Inter font selection failed");
    text->size(style.fontSize);
    text->text(value.c_str());
    text->fill(style.textColor.r, style.textColor.g, style.textColor.b);
    text->opacity(style.textColor.a);
    text->wrap(style.textWrap == TextWrap::Ellipsis ? tvg::TextWrap::Ellipsis : tvg::TextWrap::None);
    return text;
}
void validate(const Style& s) {
    const float values[] = {s.minWidth,s.minHeight,s.maxWidth,s.maxHeight,s.grow,s.shrink,s.gap,
        s.padding.left,s.padding.top,s.padding.right,s.padding.bottom,s.radius,s.borderWidth};
    for (float value : values)
        if (!std::isfinite(value) || value < 0) throw std::invalid_argument("UI: invalid nonnegative style dimension");
    for(const auto edge:{s.right,s.bottom})
        if(edge && (!std::isfinite(*edge) || *edge<0))throw std::invalid_argument("UI: invalid absolute anchor");
    for (float value : {s.margin.left,s.margin.top,s.margin.right,s.margin.bottom})
        if (!std::isfinite(value)) throw std::invalid_argument("UI: invalid margin");
    if (!std::isfinite(s.left) || !std::isfinite(s.top)) throw std::invalid_argument("UI: invalid absolute position");
    for (Length value : {s.width,s.height})
        if (!std::isfinite(value.value) || value.value < 0) throw std::invalid_argument("UI: invalid length");
    if (!std::isfinite(s.fontSize) || s.fontSize <= 0) throw std::invalid_argument("UI: invalid font size");
    if (!std::isfinite(s.textAlign) || s.textAlign < 0 || s.textAlign > 1) throw std::invalid_argument("UI: invalid text alignment");
}
}

struct Node::Impl {
    Document& owner;
    Kind kind;
    uint64_t identity = 0;
    YGNodeRef yoga = YGNodeNew();
    Style style;
    std::string text;
    std::string tooltipTitle, tooltipDescription, tooltipShortcut;
    std::string placeholder, committedText;
    Icon icon = Icon::None;
    std::string godotIcon;
    float iconSize = 14;
    std::shared_ptr<const Surface> image;
    std::string preparedText;
    float preparedWidth = -1;
    std::string measuredText;
    float measuredWidth = 0, measuredHeight = 0;
    bool measuredValid = false;
    // Splitter drags move many unchanged labels and icons. Keep their software
    // raster tiles while the GPU geometry follows the new Yoga positions.
    std::vector<uint32_t> overlayPixels;
    uint32_t overlayWidth = 0, overlayHeight = 0;
    Rect overlayClip;
    bool overlayDirty = true, overlayEnabled = true;
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;
    Rect box, clipBox;
    bool visible = true, enabled = true, effectiveVisible = true, effectiveEnabled = true;
    std::function<void()> click;
    std::function<void(float,float)> pointerDown;
    std::function<void(bool)> toggle;
    std::function<void(float)> numberChanged;
    std::function<void(const std::string&)> textChanged;
    std::function<void(const std::string&)> textCommitted;
    std::function<bool(Key)> keyDown;
    std::function<void(float)> dragged;
    std::function<void(float,float)> dragMoved;
    std::function<void(bool)> dragFinished;
    Direction dragAxis = Direction::Row;
    bool checked = false, numeric = false, invalid = false, dragAdjustable = false;
    int numberDecimals = -1;
    float value = 0, minimum = 0, maximum = 1, step = .1f;
    float scrollY = 0, scrollMax = 0, textScroll = 0;
    TextEdit edit;
    Impl(Document& document, Kind type) : owner(document), kind(type) {}
    ~Impl() {
        YGNodeRemoveAllChildren(yoga);
        children.clear();
        YGNodeFree(yoga);
    }
};
struct Document::Impl {
    std::shared_ptr<Runtime> resources = runtime();
    std::unique_ptr<Node> root;
    std::unique_ptr<tvg::SwCanvas> measureCanvas{tvg::SwCanvas::gen()};
    uint32_t measurePixel = 0;
    Surface surface;
    GpuDrawList gpuDrawList;
    std::vector<uint32_t> atlasScratch;
    bool dirty = true, layoutDirty = true;
    float width = 0, height = 0, scale = 0;
    Node* hover = nullptr;
    Node* tooltip = nullptr;
    Node* tooltipOwner = nullptr;
    Node *tooltipTitle = nullptr, *tooltipDescription = nullptr, *tooltipShortcut = nullptr;
    std::chrono::steady_clock::time_point hoverSince = std::chrono::steady_clock::now();
    Node* capture = nullptr;
    Node* scrollCapture = nullptr;
    float scrollGrab = 0;
    float dragX = 0, dragY = 0;
    float dragStartValue = 0;
    bool dragActive = false;
    bool pointerControl = false;
    Node* focus = nullptr;
    Node* keyPressed = nullptr;
    Node *popup = nullptr, *popupBar = nullptr, *popupFocus = nullptr;
    Key pressedKey = Key::Enter;
    float pointerX = -1, pointerY = -1;
    bool pointerInside = false;
    uint64_t treeRevision = 0;
    uint64_t nextIdentity = 0;
};

struct ToolkitAccess {
    static void hideTooltip(Document& doc) {
        if (doc.m->tooltip && doc.m->tooltip->m->visible) doc.m->tooltip->setVisible(false);
        doc.m->tooltipOwner = nullptr;
    }
    static void updateTooltip(Document& doc) {
        auto& state=*doc.m;
        auto* target=state.pointerInside && !state.popup && !state.capture && !state.scrollCapture
            ? state.hover : nullptr;
        if (!target || target->m->tooltipTitle.empty()) { hideTooltip(doc); return; }
        if (state.tooltipOwner != target) {
            hideTooltip(doc);
            state.tooltipOwner=target;
            state.hoverSince=std::chrono::steady_clock::now();
        }
        if (std::chrono::steady_clock::now()-state.hoverSince < std::chrono::milliseconds(500)) return;
        if (!state.tooltip) {
            state.tooltip=&doc.root().column();
            state.tooltipTitle=&state.tooltip->label("");
            state.tooltipDescription=&state.tooltip->label("");
            state.tooltipShortcut=&state.tooltip->label("");
            Style title=theme::label(11);title.height=16;title.shrink=0;
            title.font=Font::Semibold;title.textColor={235,235,235,255};
            state.tooltipTitle->setStyle(title);
            Style secondary=theme::label(10);secondary.height=15;secondary.shrink=0;
            secondary.textColor={202,202,202,255};
            state.tooltipDescription->setStyle(secondary);
            secondary.textColor={169,169,169,255};
            state.tooltipShortcut->setStyle(secondary);
        }
        const auto& data=*target->m;
        const bool description=!data.tooltipDescription.empty(), shortcut=!data.tooltipShortcut.empty();
        const size_t longest=std::max({data.tooltipTitle.size(),data.tooltipDescription.size(),
            shortcut ? data.tooltipShortcut.size()+10 : size_t(0)});
        const float boxWidth=std::clamp(float(longest)*6.2f+18.0f,155.0f,330.0f);
        const float boxHeight=16.0f+(description?15.0f:0.0f)+(shortcut?15.0f:0.0f)+14.0f+
            (description?2.0f:0.0f)+(shortcut?2.0f:0.0f);
        const Rect anchor=target->m->box;
        float x=anchor.x, y=anchor.y+anchor.height+5;
        if (anchor.x < 54 && anchor.y > 35) { x=anchor.x+anchor.width+7; y=anchor.y; }
        if (y+boxHeight>state.height-4) y=anchor.y-boxHeight-5;
        x=std::clamp(x,4.0f,std::max(4.0f,state.width-boxWidth-4));
        y=std::clamp(y,4.0f,std::max(4.0f,state.height-boxHeight-4));
        const auto& previous=state.tooltip->style();
        if(state.tooltip->m->visible && previous.left==x && previous.top==y &&
            previous.width.value==boxWidth && previous.height.value==boxHeight &&
            state.tooltipTitle->text()==data.tooltipTitle &&
            state.tooltipDescription->text()==data.tooltipDescription &&
            state.tooltipShortcut->text()==(shortcut ? "Shortcut: "+data.tooltipShortcut : "")) return;
        auto style=state.tooltip->style();
        style.absolute=true;style.left=x;style.top=y;style.width=boxWidth;style.height=boxHeight;
        style.padding=Insets(8,7);style.gap=2;style.background={35,35,35,250};
        style.borderWidth=1;style.borderColor={20,20,20,255};style.radius=4;
        state.tooltip->setStyle(style);
        state.tooltipTitle->setText(data.tooltipTitle);
        state.tooltipDescription->setText(data.tooltipDescription).setVisible(description);
        state.tooltipShortcut->setText(shortcut ? "Shortcut: "+data.tooltipShortcut : "").setVisible(shortcut);
        state.tooltip->setVisible(true);
    }
    static bool interactive(Kind kind) { return kind != Kind::Container && kind != Kind::Label; }
    static bool contains(Node& node, const Node* target, uint64_t identity) {
        if (&node == target) return node.m->identity == identity;
        for (auto& child : node.m->children) if (contains(*child,target,identity)) return true;
        return false;
    }
    static void activate(Node& node) {
        if (node.m->kind == Kind::CheckBox) {
            const bool value = !node.m->checked; node.setChecked(value);
            auto callback = node.m->toggle; if (callback) callback(value);
        } else {
            auto callback = node.m->click; if (callback) callback();
        }
    }
    static void commit(Node& node, bool blur) {
        auto& n = *node.m;
        if (!n.numeric) {
            if (n.kind != Kind::TextField || n.committedText == n.text) return;
            n.committedText = n.text;
            const auto value = n.text; auto callback = n.textCommitted;
            if (callback) callback(value);
            return;
        }
        float value = 0;
        const auto result = std::from_chars(n.text.data(),n.text.data()+n.text.size(),value);
        if (result.ec != std::errc{} || result.ptr != n.text.data()+n.text.size() || !std::isfinite(value)) {
            if (blur) node.setValue(n.value);
            else { n.invalid = true; n.owner.m->dirty = true; }
            return;
        }
        const auto old = n.value; node.setValue(value);
        value = n.value; auto callback = n.numberChanged;
        if (callback && old != value) callback(value);
    }
    static void focus(Document& doc, Node* target) {
        if (doc.m->focus == target) return;
        auto* old = doc.m->focus; doc.m->focus = nullptr;
        const auto revision = doc.m->treeRevision;
        const auto identity = target ? target->m->identity : 0;
        if (old) commit(*old,true);
        if (revision != doc.m->treeRevision && !contains(doc.root(),target,identity)) target = nullptr;
        doc.m->focus = target; doc.m->dirty = true;
        if (target && target->m->kind == Kind::TextField) target->m->edit.all();
        if (target) {
            for (auto* parent = target->m->parent; parent; parent = parent->m->parent) {
                if (!parent->m->style.scroll) continue;
                const auto b = target->bounds(), p = parent->bounds();
                if (b.y < p.y) parent->scrollTo(parent->scrollOffset()+b.y-p.y);
                else if (b.y+b.height > p.y+p.height) parent->scrollTo(parent->scrollOffset()+b.y+b.height-p.y-p.height);
            }
            if (doc.m->width > 0) doc.layout(doc.m->width,doc.m->height);
        }
    }
    static void editChanged(Node& node) {
        auto& n = *node.m;
        n.text = n.edit.value; n.invalid = false; invalidate(node);
        const auto value = n.text; auto callback = n.textChanged;
        if (!n.numeric && callback) callback(value);
    }
    static void adjust(Node& node, float value) {
        const auto old = node.value(); node.setValue(value);
        value = node.value(); auto callback = node.m->numberChanged;
        if (old != value && callback) callback(value);
    }
    static void sliderAt(Node& node, float x) {
        auto& n = *node.m;
        const float t = std::clamp((x-n.box.x-10)/std::max(1.0f,n.box.width-20),0.0f,1.0f);
        float value = n.minimum+t*(n.maximum-n.minimum);
        if (t > 0 && t < 1) value = n.minimum+std::round((value-n.minimum)/n.step)*n.step;
        adjust(node,value);
    }
    static void caretAt(Node& node, float x, bool select) {
        auto& n = *node.m;
        const float local = x-n.box.x-n.style.padding.left-n.style.borderWidth+n.textScroll;
        size_t at = 0; float last = 0;
        while (at < n.text.size()) {
            const size_t next = TextEdit::next(n.text,at);
            const float end = advance(node,n.text.substr(0,next));
            if (local < (last+end)*.5f) break;
            at = next; last = end;
        }
        n.edit.move(at,select); n.owner.m->dirty = true;
    }
    static void invalidate(Node& node) {
        node.m->preparedWidth = -1;
        node.m->measuredValid = false;
        node.m->overlayDirty = true;
        node.m->owner.m->dirty = node.m->owner.m->layoutDirty = true;
        if (node.m->kind != Kind::Container) YGNodeMarkDirty(node.m->yoga);
    }
    static Rect ink(Node& node, const std::string& value) {
        auto& canvas = node.m->owner.m->measureCanvas;
        auto* text = textPaint(node.m->style,value);
        text->wrap(tvg::TextWrap::None);
        canvas->add(text);
        canvas->update();
        Rect bounds;
        text->bounds(&bounds.x,&bounds.y,&bounds.width,&bounds.height);
        canvas->remove();
        return bounds;
    }
    static float advance(Node& node, const std::string& line) {
        if (line.empty()) return 0;
        // Public ThorVG bounds describe ink, not the pen advance. A trailing
        // unkerned bar measures the advance (including spaces/right bearing).
        // Without it, auto-width labels can truncate their own final character.
        const auto suffix = ink(node,"|");
        const auto whole = ink(node,line+"|");
        return std::max(0.0f,whole.x+whole.width-suffix.x-suffix.width)+1;
    }
    static const std::string& prepare(Node& node, float width) {
        auto& n = *node.m;
        if (n.style.textWrap != TextWrap::Word || width <= 0) return n.text;
        // Keep the existing line breaks while a dock splitter is being dragged.
        // Reflow once at the final width on pointer release.
        if (n.owner.m->capture && n.owner.m->capture->m->kind == Kind::Splitter &&
            n.preparedWidth > 0) return n.preparedText;
        if (n.preparedWidth == width) return n.preparedText;
        n.preparedWidth = width; n.preparedText.clear(); n.measuredValid = false;
        std::string line;
        auto flush = [&] { n.preparedText += line; n.preparedText += '\n'; line.clear(); };
        // The pinned ThorVG word wrapper tests a font-specific glyph index for
        // spaces. Inter's index differs, so use explicit UTF-8 line breaks here
        // and let ThorVG measure and draw the exact same prepared string.
        for (size_t i = 0; i < n.text.size();) {
            if (n.text[i] == '\n') { flush(); ++i; continue; }
            if (n.text[i] == ' ' || n.text[i] == '\t' || n.text[i] == '\r') { ++i; continue; }
            const size_t begin = i;
            while (i < n.text.size() && n.text[i] != ' ' && n.text[i] != '\n' && n.text[i] != '\t' && n.text[i] != '\r') ++i;
            const std::string word = n.text.substr(begin,i-begin);
            const std::string candidate = line.empty() ? word : line+" "+word;
            if (advance(node,candidate) <= width) { line = candidate; continue; }
            if (!line.empty()) flush();
            if (advance(node,word) <= width) { line = word; continue; }
            // Long unbroken names/paths fall back at UTF-8 codepoint boundaries.
            for (size_t j = 0; j < word.size();) {
                size_t end = j+1;
                while (end < word.size() && (static_cast<unsigned char>(word[end]) & 0xc0) == 0x80) ++end;
                auto next = word.substr(j,end-j);
                if (!line.empty() && advance(node,line+next) > width) flush();
                line += next; j = end;
            }
        }
        n.preparedText += line;
        return n.preparedText;
    }
    static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode,
                          float height, YGMeasureMode heightMode) {
        auto& node = *static_cast<Node*>(YGNodeGetContext(yoga));
        auto& n = *node.m;
        const auto& s = n.style;
        const auto& value = prepare(node,widthMode == YGMeasureModeUndefined ? 0 : width);
        if (!n.measuredValid || n.measuredText != value) {
            const auto bounds = ink(node,value);
            float w = 0;
            for (size_t start = 0; start < value.size();) {
                const auto end = value.find('\n',start);
                w = std::max(w,advance(node,value.substr(start,end == std::string::npos ? end : end-start)));
                if (end == std::string::npos) break;
                start = end+1;
            }
            float h = std::max(s.fontSize*1.6f,bounds.y+bounds.height);
            if (n.icon != Icon::None || !n.godotIcon.empty()) { w += n.iconSize+(value.empty()?0:4); h = std::max(h,n.iconSize); }
            if (n.kind == Kind::TextField) w = std::max(w,120.0f);
            if (n.kind == Kind::Slider) { w = 140; h = 20; }
            if (n.kind == Kind::Splitter) { w = 4; h = 4; }
            n.measuredText = value; n.measuredWidth = w; n.measuredHeight = h; n.measuredValid = true;
        }
        float w = n.measuredWidth, h = n.measuredHeight;
        if (widthMode == YGMeasureModeExactly) w = width;
        else if (widthMode == YGMeasureModeAtMost) w = std::min(w, width);
        if (heightMode == YGMeasureModeExactly) h = height;
        else if (heightMode == YGMeasureModeAtMost) h = std::min(h, height);
        return {std::ceil(w), std::ceil(h)};
    }
    static void applyStyle(Node& node) {
        auto n = node.m->yoga; const auto& s = node.m->style;
        YGNodeStyleSetPositionType(n,s.absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative);
        YGNodeStyleSetPosition(n,YGEdgeLeft,s.right && !s.stretchX?YGUndefined:s.left);
        YGNodeStyleSetPosition(n,YGEdgeTop,s.bottom && !s.stretchY?YGUndefined:s.top);
        YGNodeStyleSetPosition(n,YGEdgeRight,s.right.value_or(float(YGUndefined)));
        YGNodeStyleSetPosition(n,YGEdgeBottom,s.bottom.value_or(float(YGUndefined)));
        dimension(n,s.width,true); dimension(n,s.height,false);
        YGNodeStyleSetMinWidth(n,s.minWidth); YGNodeStyleSetMinHeight(n,s.minHeight);
        YGNodeStyleSetMaxWidth(n,s.maxWidth > 0 ? s.maxWidth : YGUndefined);
        YGNodeStyleSetMaxHeight(n,s.maxHeight > 0 ? s.maxHeight : YGUndefined);
        YGNodeStyleSetFlexGrow(n,s.grow); YGNodeStyleSetFlexShrink(n,s.shrink);
        YGNodeStyleSetFlexDirection(n,s.direction == Direction::Row ? YGFlexDirectionRow : YGFlexDirectionColumn);
        constexpr YGAlign align[] = {YGAlignFlexStart,YGAlignCenter,YGAlignFlexEnd,YGAlignStretch};
        constexpr YGJustify justify[] = {YGJustifyFlexStart,YGJustifyCenter,YGJustifyFlexEnd,YGJustifySpaceBetween};
        YGNodeStyleSetAlignItems(n,align[static_cast<int>(s.align)]);
        YGNodeStyleSetJustifyContent(n,justify[static_cast<int>(s.justify)]);
        YGNodeStyleSetFlexWrap(n,s.wrap ? YGWrapWrap : YGWrapNoWrap);
        YGNodeStyleSetOverflow(n,s.scroll ? YGOverflowScroll : YGOverflowVisible);
        YGNodeStyleSetGap(n,YGGutterAll,s.gap);
        constexpr YGEdge edges[] = {YGEdgeLeft,YGEdgeTop,YGEdgeRight,YGEdgeBottom};
        const float padding[] = {s.padding.left,s.padding.top,s.padding.right,s.padding.bottom};
        const float margin[] = {s.margin.left,s.margin.top,s.margin.right,s.margin.bottom};
        for (int i = 0; i < 4; ++i) {
            YGNodeStyleSetPadding(n,edges[i],padding[i]); YGNodeStyleSetMargin(n,edges[i],margin[i]);
        }
        YGNodeStyleSetBorder(n,YGEdgeAll,s.borderWidth);
        YGNodeStyleSetDisplay(n,node.m->visible ? YGDisplayFlex : YGDisplayNone);
        invalidate(node);
    }
    static void boxes(Node& node, float x, float y, Rect clip, bool visible, bool enabled) {
        auto& n = *node.m;
        n.box = {x+YGNodeLayoutGetLeft(n.yoga), y+YGNodeLayoutGetTop(n.yoga),
            YGNodeLayoutGetWidth(n.yoga),YGNodeLayoutGetHeight(n.yoga)};
        n.clipBox = clip;
        n.effectiveVisible = visible && n.visible;
        n.effectiveEnabled = enabled && n.enabled;
        if (n.style.clip || n.style.scroll) clip = intersect(clip,n.box);
        float bottom = 0;
        for (auto& child : n.children) if (child->m->visible)
            bottom = std::max(bottom,YGNodeLayoutGetTop(child->m->yoga)+YGNodeLayoutGetHeight(child->m->yoga));
        n.scrollMax = n.style.scroll ? std::max(0.0f,bottom+n.style.padding.bottom-n.box.height) : 0;
        n.scrollY = std::clamp(n.scrollY,0.0f,n.scrollMax);
        for (auto& child : n.children) boxes(*child,n.box.x,n.box.y-n.scrollY,clip,n.effectiveVisible,n.effectiveEnabled);
    }
    static Node* hit(Node& node, float x, float y) {
        auto& n = *node.m;
        if (!n.effectiveVisible || !n.style.hitTest || !n.clipBox.contains(x,y)) return nullptr;
        if (!n.style.clip || n.box.contains(x,y)) {
            for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
                if (auto* result = hit(**it,x,y)) return result;
        }
        return n.box.contains(x,y) ? &node : nullptr;
    }
    static Node* buttonAt(Document& doc, float x, float y) {
        auto* node = hit(*doc.m->root,x,y);
        // Disabled controls still occlude underlying controls, but never activate.
        for (; node; node = node->m->parent)
            if (interactive(node->m->kind) || !node->m->tooltipTitle.empty())
                return node->m->effectiveEnabled ? node : nullptr;
        return nullptr;
    }
    static Rect scrollThumb(Node& node) {
        const auto& n = *node.m; const auto b = n.box;
        const float height = std::min(b.height,std::max(24.0f,b.height*b.height/(b.height+n.scrollMax)));
        return {b.x+b.width-7,b.y+(b.height-height)*n.scrollY/n.scrollMax,5,height};
    }
    static Node* scrollAt(Document& doc, float x, float y) {
        for (auto* node = hit(doc.root(),x,y); node; node = node->m->parent) {
            const auto& n = *node->m;
            if (n.effectiveEnabled && n.style.scroll && n.scrollMax > 0 &&
                intersect(n.box,n.clipBox).contains(x,y) && x >= n.box.x+n.box.width-10) return node;
        }
        return nullptr;
    }
    static void dragScroll(Document& doc, float y) {
        auto& node = *doc.m->scrollCapture; const auto b = node.bounds();
        const auto thumb = scrollThumb(node);
        node.scrollTo((y-b.y-doc.m->scrollGrab)/std::max(1.0f,b.height-thumb.height)*node.m->scrollMax);
        doc.layout(doc.m->width,doc.m->height);
    }
    static void focusable(Node& node, std::vector<Node*>& nodes) {
        if (!node.m->effectiveVisible || !node.m->effectiveEnabled) return;
        bool inScroll = false;
        for (auto* parent = node.m->parent; parent; parent = parent->m->parent)
            if (parent->m->style.scroll) inScroll = true;
        if (interactive(node.m->kind) && node.m->box.width > 0 && node.m->box.height > 0 &&
            (inScroll || (intersect(node.m->box,node.m->clipBox).width > 0 && intersect(node.m->box,node.m->clipBox).height > 0)))
            nodes.push_back(&node);
        for (auto& child : node.m->children) focusable(*child,nodes);
    }
    static void forget(Document& doc, Node& subtree) {
        auto inside = [&](Node* target) {
            for (; target; target = target->m->parent) if (target == &subtree) return true;
            return false;
        };
        for (Node** target : {&doc.m->hover,&doc.m->capture,&doc.m->scrollCapture,&doc.m->focus,&doc.m->keyPressed,&doc.m->popup,&doc.m->popupBar,&doc.m->popupFocus,&doc.m->tooltip,&doc.m->tooltipOwner,&doc.m->tooltipTitle,&doc.m->tooltipDescription,&doc.m->tooltipShortcut})
            if (inside(*target)) *target = nullptr;
        doc.m->dirty = true;
    }
    static void clip(tvg::Paint& paint, Rect bounds) {
        auto* mask = tvg::Shape::gen();
        mask->appendRect(bounds.x,bounds.y,bounds.width,bounds.height,0,0);
        mask->fill(255,255,255);
        paint.clip(mask);
    }
    static void paintIcon(Node& node,tvg::Scene& scene) {
        const auto& n=*node.m; const auto b=n.box; const float size=n.iconSize, unit=size/16;
        const float x=n.text.empty() ? b.x+(b.width-size)*.5f : b.x+n.style.padding.left+n.style.borderWidth;
        const float y=b.y+(b.height-size)*.5f;
        const auto tint=n.style.textColor;
        std::string godotIcon=n.godotIcon;
        switch(n.icon) {
        case Icon::Cube: godotIcon="editor/icons/BoxMesh";break;
        case Icon::Folder: godotIcon="editor/icons/Folder";break;
        case Icon::File: godotIcon="editor/icons/File";break;
        case Icon::Play: godotIcon="editor/icons/Play";break;
        case Icon::Pause: godotIcon="editor/icons/Pause";break;
        case Icon::Step: godotIcon="editor/icons/NextFrame";break;
        case Icon::Frame: godotIcon="editor/icons/CenterView";break;
        case Icon::Search: godotIcon="editor/icons/Search";break;
        case Icon::Scene: godotIcon="editor/icons/PackedScene";break;
        case Icon::Console: godotIcon="editor/icons/Terminal";break;
        case Icon::ChevronDown: godotIcon="editor/icons/ArrowDown";break;
        case Icon::ChevronRight: godotIcon="editor/icons/ArrowRight";break;
        default:break;
        }
        if(!godotIcon.empty()) {
            const bool blender=godotIcon.starts_with("blender/");
            const auto path=(std::filesystem::path(GENESIS_ROOT)/
                (blender?"assets/editor_icons/blender":"assets/editor_icons/godot")/
                ((blender?godotIcon.substr(8):godotIcon)+".svg")).string();
            if(std::filesystem::exists(path)) {
                auto* picture=tvg::Picture::gen();
                check(picture->load(path.c_str()),"UI: editor SVG load failed");
                picture->scale(blender ? unit/100.0f : unit);picture->translate(x,y);
                if(!n.effectiveEnabled)picture->opacity(100);
                clip(*picture,intersect(b,n.clipBox));scene.add(picture);return;
            }
        }
        const auto submit=[&](tvg::Shape* shape) {
            if(!n.effectiveEnabled) shape->opacity(100);
            clip(*shape,intersect(b,n.clipBox)); scene.add(shape);
        };
        struct Point { float x,y; };
        const auto path=[&](std::initializer_list<Point> points,bool closed,Color fill=Color{}) {
            auto* shape=tvg::Shape::gen(); bool first=true;
            for(auto [px,py]:points) { if(first) shape->moveTo(x+px*unit,y+py*unit); else shape->lineTo(x+px*unit,y+py*unit); first=false; }
            if(closed) shape->close();
            shape->fill(fill.r,fill.g,fill.b,fill.a);
            shape->strokeWidth(std::max(1.0f,unit)); shape->strokeFill(tint.r,tint.g,tint.b,tint.a);
            submit(shape);
        };
        const auto rect=[&](float px,float py,float w,float h,Color fill) {
            auto* shape=tvg::Shape::gen(); shape->appendRect(x+px*unit,y+py*unit,w*unit,h*unit,0,0);
            shape->fill(fill.r,fill.g,fill.b,fill.a); submit(shape);
        };
        switch(n.icon) {
        case Icon::Cube: case Icon::Scene:
            path({{8,1},{14,4.5f},{8,8},{2,4.5f}},true,{145,180,198,255});
            path({{2,4.5f},{8,8},{8,15},{2,11.5f}},true,{92,139,164,255});
            path({{8,8},{14,4.5f},{14,11.5f},{8,15}},true,{69,106,126,255}); break;
        case Icon::Folder:
            path({{1,4},{1,2},{6,2},{8,4},{15,4},{15,13},{1,13}},true,{179,179,179,255}); break;
        case Icon::File:
            path({{3,1},{10,1},{13,4},{13,15},{3,15}},true,{103,103,103,255});
            path({{10,1},{10,4},{13,4}},false); path({{5,8},{11,8}},false); path({{5,11},{11,11}},false); break;
        case Icon::Play: path({{5,2},{13,8},{5,14}},true,tint); break;
        case Icon::Pause: rect(4,3,3,10,tint); rect(10,3,3,10,tint); break;
        case Icon::Step: path({{3,3},{10,8},{3,13}},true,tint); rect(12,3,2,10,tint); break;
        case Icon::Frame:
            path({{1,6},{1,1},{6,1}},false); path({{10,1},{15,1},{15,6}},false);
            path({{15,10},{15,15},{10,15}},false); path({{6,15},{1,15},{1,10}},false); break;
        case Icon::Search: {
            auto* shape=tvg::Shape::gen(); shape->appendCircle(x+6*unit,y+6*unit,4*unit,4*unit);
            shape->strokeWidth(std::max(1.0f,unit)); shape->strokeFill(tint.r,tint.g,tint.b); submit(shape);
            path({{9,9},{14,14}},false); break;
        }
        case Icon::Console:
            path({{1,2},{15,2},{15,14},{1,14}},true,{42,42,42,255});
            path({{4,5},{7,8},{4,11}},false); path({{9,11},{12,11}},false); break;
        case Icon::ChevronDown: path({{4,6},{12,6},{8,10}},true,tint); break;
        case Icon::ChevronRight: path({{6,4},{10,8},{6,12}},true,tint); break;
        default: break;
        }
    }
    static void paint(Node& node, tvg::Scene& scene, bool recurse=true, bool background=true) {
        auto& n = *node.m; const auto& s = n.style; const auto b = n.box;
        if (!n.effectiveVisible || b.width <= 0 || b.height <= 0) return;
        const Rect visible = intersect(b,n.clipBox);
        if (visible.width > 0 && visible.height > 0) {
            auto color = s.background;
            if ((n.kind == Kind::Button || n.kind == Kind::CheckBox || n.kind == Kind::Splitter) && n.effectiveEnabled) {
                if (node.pressed() || (n.kind == Kind::Splitter && n.owner.m->capture == &node)) color = s.pressedBackground;
                else if (node.hovered()) color = s.hoverBackground;
            }
            const bool showFocus = node.focused() && n.kind != Kind::Splitter;
            if (background && (color.a || s.borderWidth > 0 || showFocus)) {
                auto* shape = tvg::Shape::gen();
                const float stroke = showFocus ? std::max(2.0f,s.borderWidth) : s.borderWidth;
                shape->appendRect(b.x+stroke*.5f,b.y+stroke*.5f,std::max(0.0f,b.width-stroke),
                    std::max(0.0f,b.height-stroke),s.radius,s.radius);
                shape->fill(color.r,color.g,color.b,color.a);
                if (stroke > 0) {
                    const auto c = n.invalid ? Color{245,112,94,255} : showFocus ? s.focusColor : s.borderColor;
                    shape->strokeWidth(stroke); shape->strokeFill(c.r,c.g,c.b,c.a);
                }
                if (!n.effectiveEnabled) shape->opacity(110);
                clip(*shape,n.clipBox); scene.add(shape);
            }
            if (n.kind == Kind::CheckBox) {
                auto* mark = tvg::Shape::gen();
                mark->appendRect(b.x+4,b.y+(b.height-13)*.5f,13,13,2,2);
                const auto fill = n.checked ? theme::selection : theme::input;
                mark->fill(fill.r,fill.g,fill.b);
                mark->strokeWidth(1); mark->strokeFill(26,26,26);
                if (!n.effectiveEnabled) mark->opacity(110);
                clip(*mark,n.clipBox); scene.add(mark);
                if (n.checked) {
                    auto* tick = tvg::Shape::gen(); const float y = b.y+b.height*.5f;
                    tick->moveTo(b.x+6,y); tick->lineTo(b.x+9,y+3); tick->lineTo(b.x+15,y-3);
                    tick->strokeWidth(1.5f); tick->strokeFill(210,210,210);
                    if (!n.effectiveEnabled) tick->opacity(110);
                    clip(*tick,n.clipBox); scene.add(tick);
                }
            }
            if (n.kind == Kind::Slider) {
                const float t = (n.value-n.minimum)/(n.maximum-n.minimum);
                auto* track = tvg::Shape::gen();
                track->appendRect(b.x+10,b.y+b.height*.5f-2,b.width-20,4,2,2); track->fill(54,65,86);
                clip(*track,n.clipBox); scene.add(track);
                auto* fill = tvg::Shape::gen();
                fill->appendRect(b.x+10,b.y+b.height*.5f-2,std::max(1.0f,(b.width-20)*t),4,2,2);
                fill->fill(112,145,250); clip(*fill,n.clipBox); scene.add(fill);
                auto* knob = tvg::Shape::gen();
                knob->appendCircle(b.x+10+(b.width-20)*t,b.y+b.height*.5f,7,7);
                knob->fill(181,203,255); if (!n.effectiveEnabled) knob->opacity(110);
                clip(*knob,n.clipBox); scene.add(knob);
            }
            if(n.image) {
                const auto& bitmap=*n.image;
                const float width=b.width-s.padding.left-s.padding.right-2*s.borderWidth;
                const float height=b.height-s.padding.top-s.padding.bottom-2*s.borderWidth;
                if(width>0 && height>0) {
                    auto* picture=tvg::Picture::gen();
                    check(picture->load(bitmap.pixels.data(),bitmap.width,bitmap.height,tvg::ColorSpace::ARGB8888S,false),"UI: bitmap load failed");
                    const float scale=std::min(width/bitmap.width,height/bitmap.height);
                    picture->scale(scale);picture->translate(b.x+s.padding.left+s.borderWidth+(width-bitmap.width*scale)*.5f,
                        b.y+s.padding.top+s.borderWidth+(height-bitmap.height*scale)*.5f);
                    if(!n.effectiveEnabled)picture->opacity(110);
                    clip(*picture,intersect(b,n.clipBox));scene.add(picture);
                }
            } else if (n.icon != Icon::None || !n.godotIcon.empty()) paintIcon(node,scene);
            if (n.kind != Kind::Container && n.kind != Kind::Slider && (!n.text.empty() || n.kind == Kind::TextField)) {
                const float left = s.padding.left+s.borderWidth+((n.icon != Icon::None || !n.godotIcon.empty()) ? n.iconSize+4 : 0), top = s.padding.top+s.borderWidth;
                const float w = b.width-left-s.padding.right-s.borderWidth;
                const float h = b.height-top-s.padding.bottom-s.borderWidth;
                if (w > 0 && h > 0) {
                    const Rect textClip = intersect(visible,{b.x+left,b.y+top,w,h});
                    if (n.kind == Kind::TextField && node.focused()) {
                        const float caret = advance(node,n.text.substr(0,n.edit.cursor));
                        n.textScroll = std::max(0.0f,std::clamp(n.textScroll,caret-std::max(2.0f,w)+2,caret));
                        if (n.edit.begin() != n.edit.end()) {
                            const float a = advance(node,n.text.substr(0,n.edit.begin()));
                            const float z = advance(node,n.text.substr(0,n.edit.end()));
                            auto* highlight = tvg::Shape::gen();
                            highlight->appendRect(b.x+left+a-n.textScroll,b.y+top,z-a,h,0,0);
                            highlight->fill(69,95,155); clip(*highlight,textClip); scene.add(highlight);
                        }
                        auto* cursor = tvg::Shape::gen();
                        cursor->appendRect(b.x+left+caret-n.textScroll,b.y+top,1,h,0,0);
                        cursor->fill(220,233,255); clip(*cursor,textClip); scene.add(cursor);
                    }
                    const bool placeholder = n.kind == Kind::TextField && n.text.empty();
                    auto textStyle = s; if (placeholder) textStyle.textColor = theme::muted;
                    auto* text = textPaint(textStyle,placeholder ? n.placeholder : prepare(node,w));
                    if (n.kind == Kind::TextField) text->wrap(tvg::TextWrap::None);
                    if(s.verticalText) {
                        text->layout(h,w);
                        text->align(.5f,.5f);
                        text->rotate(90);
                        text->translate(b.x+left+w,b.y+top);
                    } else {
                        text->layout(w,h);
                        text->align(s.textAlign,n.kind == Kind::Button ? .5f : 0);
                        text->translate(b.x+left-(n.kind == Kind::TextField && !placeholder ? n.textScroll : 0),b.y+top);
                    }
                    if (!n.effectiveEnabled) text->opacity(uint8_t(s.textColor.a*110/255));
                    clip(*text,textClip); scene.add(text);
                }
            }
        }
        if(recurse)for (auto& child : n.children) paint(*child,scene);
        if (background && s.frameColor.a && s.radius > 0 && visible.width > 0 && visible.height > 0) {
            // Even-odd: the box minus its rounded rect is the corner cover.
            auto* corners = tvg::Shape::gen();
            corners->appendRect(b.x,b.y,b.width,b.height,0,0);
            corners->appendRect(b.x,b.y,b.width,b.height,s.radius,s.radius);
            corners->fillRule(tvg::FillRule::EvenOdd);
            corners->fill(s.frameColor.r,s.frameColor.g,s.frameColor.b,s.frameColor.a);
            clip(*corners,n.clipBox); scene.add(corners);
            if (s.borderWidth > 0) {
                auto* frame = tvg::Shape::gen(); const float stroke = s.borderWidth;
                frame->appendRect(b.x+stroke*.5f,b.y+stroke*.5f,std::max(0.0f,b.width-stroke),
                    std::max(0.0f,b.height-stroke),s.radius,s.radius);
                frame->strokeWidth(stroke);
                frame->strokeFill(s.borderColor.r,s.borderColor.g,s.borderColor.b,s.borderColor.a);
                clip(*frame,n.clipBox); scene.add(frame);
            }
        }
        if (n.style.scroll && n.scrollMax > 0 && visible.width > 0 && visible.height > 0) {
            const auto box = scrollThumb(node);
            auto* thumb = tvg::Shape::gen();
            thumb->appendRect(box.x,box.y,box.width,box.height,2.5f,2.5f);
            const auto color = n.owner.m->scrollCapture == &node ? theme::accent : theme::muted;
            thumb->fill(color.r,color.g,color.b,n.effectiveEnabled ? 210 : 90);
            clip(*thumb,visible); scene.add(thumb);
        }
    }
    static void gpuCommands(Node& node,GpuDrawList& list,
        std::vector<std::pair<size_t,std::vector<uint32_t>>>& overlays) {
        auto& n=*node.m;const auto& s=n.style;const auto b=n.box;
        if(!n.effectiveVisible || b.width<=0 || b.height<=0)return;
        const Rect visible=intersect(b,n.clipBox);
        if(visible.width>0 && visible.height>0){
            Color color=s.background;
            if((n.kind==Kind::Button || n.kind==Kind::CheckBox || n.kind==Kind::Splitter) && n.effectiveEnabled){
                if(node.pressed() || (n.kind==Kind::Splitter && n.owner.m->capture==&node))color=s.pressedBackground;
                else if(node.hovered())color=s.hoverBackground;
            }
            const bool showFocus=node.focused() && n.kind!=Kind::Splitter;
            if(color.a || s.borderWidth>0 || showFocus){
                const float stroke=showFocus?std::max(2.0f,s.borderWidth):s.borderWidth;
                if(!n.effectiveEnabled)color.a=uint8_t(color.a*110/255);
                Color border=n.invalid?Color{245,112,94,255}:showFocus?s.focusColor:s.borderColor;
                if(!n.effectiveEnabled)border.a=uint8_t(border.a*110/255);
                GpuDrawCommand command;
                command.bounds=b;
                command.clip=n.clipBox;command.fill=color;command.border=border;
                command.radius=s.radius;command.borderWidth=stroke;
                list.commands.push_back(command);
            }
            const bool hasOverlay=n.kind==Kind::CheckBox || n.kind==Kind::Slider || n.image ||
                n.icon!=Icon::None || !n.godotIcon.empty() || n.style.scroll && n.scrollMax>0 ||
                n.kind!=Kind::Container && n.kind!=Kind::Slider && (!n.text.empty() || n.kind==Kind::TextField);
            if(hasOverlay){
                const bool freezeTile=n.owner.m->capture && n.owner.m->capture->m->kind==Kind::Splitter &&
                    (n.kind==Kind::Label || n.kind==Kind::Button) && !n.image && !n.overlayDirty &&
                    n.overlayWidth && n.overlayHeight && n.overlayEnabled==n.effectiveEnabled;
                const uint32_t w=freezeTile?n.overlayWidth:uint32_t(std::ceil(b.width));
                const uint32_t h=freezeTile?n.overlayHeight:uint32_t(std::ceil(b.height));
                if(w && h){
                    if(w>4096 || h>4096)throw std::runtime_error("UI: GPU tile exceeded 4096 pixels");
                    const bool cacheable=(n.kind==Kind::Label || n.kind==Kind::Button) && !s.scroll;
                    const Rect localClip{visible.x-b.x,visible.y-b.y,visible.width,visible.height};
                    const auto sameClip=[&]{return n.overlayClip.x==localClip.x && n.overlayClip.y==localClip.y &&
                        n.overlayClip.width==localClip.width && n.overlayClip.height==localClip.height;};
                    std::vector<uint32_t> uncachedPixels;
                    if(!freezeTile && (!cacheable || n.overlayDirty || n.overlayWidth!=w || n.overlayHeight!=h ||
                        n.overlayEnabled!=n.effectiveEnabled || !sameClip())) {
                        std::vector<uint32_t> pixels(size_t(w)*h);
                        std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
                        check(canvas->target(pixels.data(),w,w,h,tvg::ColorSpace::ARGB8888S),"UI: atlas target failed");
                        auto* scene=tvg::Scene::gen();paint(node,*scene,false,false);
                        scene->translate(-b.x,-b.y);
                        check(canvas->add(scene),"UI: atlas submission failed");
                        check(canvas->draw(true),"UI: atlas rasterization failed");
                        check(canvas->sync(),"UI: atlas sync failed");
                        ++list.rasterizedTiles;
                        if(cacheable) {
                            n.overlayPixels=std::move(pixels);
                            n.overlayWidth=w;n.overlayHeight=h;n.overlayClip=localClip;
                            n.overlayEnabled=n.effectiveEnabled;n.overlayDirty=false;
                        } else uncachedPixels=std::move(pixels);
                    }
                    GpuDrawCommand command;command.type=GpuDrawCommand::Type::AtlasImage;
                    command.bounds={b.x,b.y,float(w),float(h)};command.clip=freezeTile?visible:n.clipBox;
                    list.commands.push_back(command);
                    if(cacheable)overlays.emplace_back(list.commands.size()-1,n.overlayPixels);
                    else overlays.emplace_back(list.commands.size()-1,std::move(uncachedPixels));
                }
            }
        }
        for(auto& child:n.children)gpuCommands(*child,list,overlays);
        if(s.frameColor.a && s.radius>0 && visible.width>0 && visible.height>0){
            GpuDrawCommand command;command.type=GpuDrawCommand::Type::RoundedFrame;
            command.bounds=b;command.clip=n.clipBox;command.fill=s.frameColor;
            command.border=s.borderColor;command.radius=s.radius;command.borderWidth=s.borderWidth;
            list.commands.push_back(command);
        }
    }
};

bool Rect::contains(float px, float py) const {
    return width > 0 && height > 0 && px >= x && py >= y && px < x+width && py < y+height;
}
Node::Node(Document& owner, Kind kind) : m(std::make_unique<Impl>(owner,kind)) {
    m->identity = ++owner.m->nextIdentity;
    YGNodeSetContext(m->yoga,this);
    if (kind != Kind::Container) YGNodeSetMeasureFunc(m->yoga,ToolkitAccess::measure);
    if (kind == Kind::Button) {
        m->style = theme::button();
        m->style.height = {}; m->style.minHeight = 36; m->style.padding = Insets(12,7);
    }
    if (kind == Kind::CheckBox) {
        m->style.padding = Insets(5,2); m->style.padding.left = 22;
        m->style.minHeight = 20; m->style.radius = 2;
        m->style.fontSize = 10; m->style.textColor = theme::text; m->style.focusColor = theme::accent;
        m->style.hoverBackground = theme::hover; m->style.pressedBackground = theme::pressed;
    }
    if (kind == Kind::Slider) { m->style.minHeight = 30; m->style.minWidth = 40; }
    if (kind == Kind::TextField) {
        m->style = theme::field();
    }
    ToolkitAccess::applyStyle(*this);
}
Node::~Node() = default;
Node& Node::add(Kind kind, std::string text) {
    if (m->kind != Kind::Container) throw std::logic_error("UI: only containers can own children");
    auto child = std::unique_ptr<Node>(new Node(m->owner,kind));
    child->m->parent = this; child->m->text = std::move(text);
    YGNodeInsertChild(m->yoga,child->m->yoga,m->children.size());
    auto& result = *child; m->children.push_back(std::move(child));
    ++m->owner.m->treeRevision;
    ToolkitAccess::invalidate(*this); return result;
}
Node& Node::column() { return add(Kind::Container,{}); }
Node& Node::row() {
    auto& result = add(Kind::Container,{}); auto style = result.style(); style.direction = Direction::Row;
    return result.setStyle(style);
}
Node& Node::splitter(Direction axis, std::function<void(float)> dragged) {
    auto& result = add(Kind::Splitter,{}); result.m->dragAxis = axis; result.m->dragged = std::move(dragged);
    Style style; style.shrink = 0; style.background = theme::canvas;
    style.hoverBackground = theme::border; style.pressedBackground = theme::accent; style.focusColor = theme::accent;
    if (axis == Direction::Row) style.width = 4; else style.height = 4;
    return result.setStyle(style);
}
Node& Node::label(std::string text) { return add(Kind::Label,std::move(text)); }
Node& Node::button(std::string text, std::function<void()> callback) {
    return add(Kind::Button,std::move(text)).onClick(std::move(callback));
}
Node& Node::checkBox(std::string text, bool checked, std::function<void(bool)> changed) {
    auto& result = add(Kind::CheckBox,std::move(text));
    result.m->checked = checked; result.m->toggle = std::move(changed); return result;
}
Node& Node::slider(float value, float minimum, float maximum, float step, std::function<void(float)> changed) {
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) ||
        minimum >= maximum || step <= 0) throw std::invalid_argument("UI: invalid numeric range");
    auto& result = add(Kind::Slider,{});
    result.m->minimum = minimum; result.m->maximum = maximum; result.m->step = step;
    result.m->numberChanged = std::move(changed); return result.setValue(value);
}
Node& Node::textField(std::string value, std::function<void(const std::string&)> changed) {
    auto& result = add(Kind::TextField,{}); result.m->textChanged = std::move(changed);
    return result.setText(std::move(value));
}
Node& Node::numberField(float value, float minimum, float maximum, float step, std::function<void(float)> changed) {
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) ||
        minimum >= maximum || step <= 0) throw std::invalid_argument("UI: invalid numeric range");
    auto& result = add(Kind::TextField,{}); result.m->numeric = true;
    result.m->minimum = minimum; result.m->maximum = maximum; result.m->step = step;
    result.m->numberChanged = std::move(changed); return result.setValue(value);
}
Node& Node::setChecked(bool checked) {
    if (m->checked != checked) { m->checked = checked; m->owner.m->dirty = true; }
    return *this;
}
bool Node::checked() const { return m->checked; }
Node& Node::setValue(float value) {
    if (!std::isfinite(value)) throw std::invalid_argument("UI: nonfinite value");
    m->value = std::clamp(value,m->minimum,m->maximum); m->invalid = false;
    if (m->numeric) {
        char text[40];
        if(m->numberDecimals>=0)std::snprintf(text,sizeof(text),"%.*f",m->numberDecimals,m->value);
        else std::snprintf(text,sizeof(text),"%.6g",m->value);
        setText(text);
        m->edit.set(m->text);
    }
    m->owner.m->dirty = true; return *this;
}
float Node::value() const { return m->value; }
Node& Node::setNumberDecimals(int decimals) {
    if(decimals < -1 || decimals > 9 || !m->numeric)
        throw std::invalid_argument("UI: invalid numeric decimal format");
    m->numberDecimals=decimals;
    return setValue(m->value);
}
Node& Node::setDragAdjustable(bool enabled) {
    if (!m->numeric) throw std::invalid_argument("UI: drag adjustment requires a numeric field");
    m->dragAdjustable=enabled;
    return *this;
}
void Node::scrollTo(float y) {
    if (!std::isfinite(y)) return;
    m->scrollY = std::clamp(y,0.0f,m->scrollMax); ToolkitAccess::invalidate(*this);
}
float Node::scrollOffset() const { return m->scrollY; }
void Node::clear() {
    for (auto& child : m->children) ToolkitAccess::forget(m->owner,*child);
    YGNodeRemoveAllChildren(m->yoga); m->children.clear(); ++m->owner.m->treeRevision;
    ToolkitAccess::invalidate(*this);
}
void Node::reparent(Node& parent) {
    if (&parent.m->owner != &m->owner || parent.m->kind != Kind::Container || !m->parent)
        throw std::invalid_argument("UI: invalid reparent destination");
    for (auto* ancestor=&parent; ancestor; ancestor=ancestor->m->parent)
        if (ancestor==this) throw std::invalid_argument("UI: cannot reparent into a descendant");
    if (m->parent==&parent) return;
    auto* previous=m->parent;
    auto& siblings=previous->m->children;
    const auto found=std::find_if(siblings.begin(),siblings.end(),[this](const auto& child){return child.get()==this;});
    parent.m->children.reserve(parent.m->children.size()+1);
    YGNodeRemoveChild(previous->m->yoga,m->yoga);
    auto owned=std::move(*found); siblings.erase(found);
    m->parent=&parent;
    YGNodeInsertChild(parent.m->yoga,m->yoga,parent.m->children.size());
    parent.m->children.push_back(std::move(owned));
    ++m->owner.m->treeRevision;
    ToolkitAccess::invalidate(*previous); ToolkitAccess::invalidate(parent);
}
Node& Node::setStyle(const Style& style) { validate(style); m->style = style; ToolkitAccess::applyStyle(*this); return *this; }
const Style& Node::style() const { return m->style; }
Node& Node::setText(std::string text) {
    if (m->kind == Kind::TextField) m->committedText = text;
    if (m->text != text) {
        m->text = std::move(text); if (m->kind == Kind::TextField) m->edit.set(m->text);
        ToolkitAccess::invalidate(*this);
    }
    return *this;
}
const std::string& Node::text() const { return m->text; }
Node& Node::setPlaceholder(std::string text) {
    m->placeholder = std::move(text); m->owner.m->dirty = true; return *this;
}
Node& Node::setIcon(Icon icon,float size) {
    if (!std::isfinite(size) || size <= 0) throw std::invalid_argument("UI: invalid icon size");
    m->icon = icon; m->godotIcon.clear(); m->iconSize = size; ToolkitAccess::invalidate(*this); return *this;
}
Node& Node::setGodotIcon(std::string name,float size) {
    if(!std::isfinite(size) || size<=0)throw std::invalid_argument("UI: invalid icon size");
    if(!name.empty()) {
        if(name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-/")!=std::string::npos ||
            name.find("//")!=std::string::npos || name.front()=='/' || name.back()=='/')
            throw std::invalid_argument("UI: invalid Godot icon name");
        if(name.find('/')==std::string::npos)name="editor/icons/"+name;
        if(!name.starts_with("editor/icons/") && !name.starts_with("modules/"))
            throw std::invalid_argument("UI: invalid Godot icon directory");
        const auto path=std::filesystem::path(GENESIS_ROOT)/"assets/editor_icons/godot"/(name+".svg");
        if(!std::filesystem::is_regular_file(path))throw std::invalid_argument("UI: unknown Godot icon");
    }
    m->icon=Icon::None;m->godotIcon=std::move(name);m->iconSize=size;
    ToolkitAccess::invalidate(*this);return *this;
}
Node& Node::setBlenderIcon(std::string name,float size) {
    if(!std::isfinite(size) || size<=0)throw std::invalid_argument("UI: invalid icon size");
    if(name.empty() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::invalid_argument("UI: invalid Blender icon name");
    const auto path=std::filesystem::path(GENESIS_ROOT)/"assets/editor_icons/blender"/(name+".svg");
    if(!std::filesystem::is_regular_file(path))throw std::invalid_argument("UI: unknown Blender icon");
    m->icon=Icon::None;m->godotIcon="blender/"+name;m->iconSize=size;
    ToolkitAccess::invalidate(*this);return *this;
}
Node& Node::setTooltip(std::string title,std::string description,std::string shortcut) {
    m->tooltipTitle=std::move(title);
    m->tooltipDescription=std::move(description);
    m->tooltipShortcut=std::move(shortcut);
    if(m->owner.m->tooltipOwner==this) {
        ToolkitAccess::hideTooltip(m->owner);
        m->owner.m->hoverSince=std::chrono::steady_clock::now();
    }
    return *this;
}
Node& Node::setImage(std::shared_ptr<const Surface> image) {
    if(image && (!image->width || !image->height || image->width>4096 || image->height>4096
        || image->pixels.size()!=size_t(image->width)*image->height))throw std::invalid_argument("UI: invalid bitmap");
    if(m->image!=image){m->image=std::move(image);ToolkitAccess::invalidate(*this);}return *this;
}
Node& Node::onTextCommitted(std::function<void(const std::string&)> callback) {
    m->textCommitted = std::move(callback); return *this;
}
Node& Node::onKeyDown(std::function<bool(Key)> callback) { m->keyDown = std::move(callback); return *this; }
Node& Node::onPointerDown(std::function<void(float,float)> callback) { m->pointerDown = std::move(callback); return *this; }
void Node::focus() {
    auto& doc = m->owner;
    if (doc.m->width <= 0) return;
    doc.layout(doc.m->width,doc.m->height);
    std::vector<Node*> nodes; ToolkitAccess::focusable(doc.root(),nodes);
    if (std::find(nodes.begin(),nodes.end(),this) != nodes.end()) ToolkitAccess::focus(doc,this);
}
Node& Node::setVisible(bool value) {
    if (m->visible != value) {
        m->visible = value;
        if (!value && m->owner.m->tooltip != this && m->parent != m->owner.m->tooltip)
            ToolkitAccess::forget(m->owner,*this);
        ToolkitAccess::applyStyle(*this);
    }
    return *this;
}
Node& Node::setEnabled(bool value) {
    if (m->enabled != value) {
        m->enabled = value; if (!value) ToolkitAccess::forget(m->owner,*this);
        ToolkitAccess::invalidate(*this);
    }
    return *this;
}
Node& Node::onClick(std::function<void()> callback) { m->click = std::move(callback); return *this; }
Node& Node::onDrag(std::function<void(float,float)> moved,std::function<void(bool)> finished) {
    m->dragMoved=std::move(moved);m->dragFinished=std::move(finished);return *this;
}
Rect Node::bounds() const { return m->box; }
bool Node::hovered() const { return m->owner.m->hover == this; }
bool Node::focused() const { return m->owner.m->focus == this; }
bool Node::pressed() const {
    auto& doc = *m->owner.m;
    return (doc.capture == this && doc.hover == this) || doc.keyPressed == this;
}
Document::Document() : m(std::make_unique<Impl>()) {
    check(m->measureCanvas->target(&m->measurePixel,1,1,1,tvg::ColorSpace::ARGB8888S),"UI: measurement target failed");
    m->root.reset(new Node(*this,Kind::Container));
}
Document::~Document() = default;
Node& Document::root() { return *m->root; }
bool Document::hasPopup() const { return m->popup != nullptr; }
bool Document::hasPointerCapture() const { return m->capture || m->scrollCapture; }
Cursor Document::cursor() {
    if(m->width>0)layout(m->width,m->height);
    if(m->scrollCapture)return Cursor::Arrow;
    const auto* node=m->capture?m->capture:m->hover;
    if(node && node->m->style.cursor)return *node->m->style.cursor;
    if(node && node->m->numeric && node->m->dragAdjustable)return Cursor::ResizeHorizontal;
    if(!node || node->m->kind!=Kind::Splitter)return Cursor::Arrow;
    return node->m->dragAxis==Direction::Row?Cursor::ResizeHorizontal:Cursor::ResizeVertical;
}
void Document::openPopup(Node& popup,Node& anchorBar) {
    if (&popup.m->owner != this || &anchorBar.m->owner != this) throw std::invalid_argument("UI: popup document mismatch");
    closePopup(); ToolkitAccess::hideTooltip(*this);
    m->popupFocus=m->focus; m->popup=&popup; m->popupBar=&anchorBar;
    popup.setVisible(true);
    if(m->width>0) {
        layout(m->width,m->height);
        std::vector<Node*> nodes; ToolkitAccess::focusable(popup,nodes);
        if(!nodes.empty()) ToolkitAccess::focus(*this,nodes.front());
    }
}
void Document::closePopup() {
    auto* popup=m->popup; auto* restore=m->popupFocus;
    m->popup=m->popupBar=m->popupFocus=nullptr;
    if(!popup) return;
    popup->setVisible(false); m->capture=m->keyPressed=nullptr;
    if(restore) restore->focus();
}
bool Document::dirty() const { return m->dirty; }
void Document::layout(float width, float height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        throw std::invalid_argument("UI: viewport must be finite and positive");
    if (m->width != width || m->height != height) {
        m->width = width; m->height = height; m->layoutDirty = m->dirty = true;
    }
    if (!m->layoutDirty) return;
    YGNodeStyleSetWidth(root().m->yoga,width); YGNodeStyleSetHeight(root().m->yoga,height);
    YGNodeCalculateLayout(root().m->yoga,width,height,YGDirectionLTR);
    ToolkitAccess::boxes(root(),0,0,{0,0,width,height},true,true);
    m->layoutDirty = false;
    m->hover = m->pointerInside ? ToolkitAccess::buttonAt(*this,m->pointerX,m->pointerY) : nullptr;
    std::vector<Node*> focusable; ToolkitAccess::focusable(root(),focusable);
    for (Node** target : {&m->focus,&m->capture,&m->keyPressed})
        if (*target && std::find(focusable.begin(),focusable.end(),*target) == focusable.end()) *target = nullptr;
    if (m->scrollCapture && (!m->scrollCapture->m->effectiveVisible || !m->scrollCapture->m->effectiveEnabled ||
        m->scrollCapture->m->scrollMax <= 0 || !m->scrollCapture->m->style.scroll)) m->scrollCapture = nullptr;
}
const Surface& Document::render(float width, float height, float scale) {
    if (!std::isfinite(scale) || scale <= 0 || width*scale > 16384 || height*scale > 16384 ||
        double(width)*height*scale*scale > 67108864)
        throw std::invalid_argument("UI: invalid or excessive surface dimensions");
    layout(width,height);
    ToolkitAccess::updateTooltip(*this);
    if(m->layoutDirty)layout(width,height);
    if (scale != m->scale) { m->scale = scale; m->dirty = true; }
    if (!m->dirty && m->surface.revision &&
        m->surface.width==uint32_t(std::ceil(width*scale)) &&
        m->surface.height==uint32_t(std::ceil(height*scale)))return m->surface;
    auto& surface = m->surface;
    surface.width = uint32_t(std::ceil(width*scale)); surface.height = uint32_t(std::ceil(height*scale));
    surface.pixels.assign(size_t(surface.width)*surface.height,0);
    std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
    check(canvas->target(surface.pixels.data(),surface.width,surface.width,surface.height,
        tvg::ColorSpace::ARGB8888S),"UI: surface target failed");
    auto* scene = tvg::Scene::gen();
    ToolkitAccess::paint(root(),*scene); scene->scale(scale);
    check(canvas->add(scene),"UI: scene submission failed");
    check(canvas->draw(true),"UI: rasterization failed");
    check(canvas->sync(),"UI: rasterization sync failed");
    m->dirty = false; ++surface.revision;
    m->gpuDrawList.width=0; // A later GPU render must rebuild after this CPU pass.
    return surface;
}
const GpuDrawList& Document::renderGpu(float width,float height) {
    layout(width,height);
    ToolkitAccess::updateTooltip(*this);
    if(m->layoutDirty)layout(width,height);
    auto& list=m->gpuDrawList;
    if(!m->dirty && list.revision && list.width==uint32_t(std::ceil(width)) &&
        list.height==uint32_t(std::ceil(height)))return list;
    list.width=uint32_t(std::ceil(width));list.height=uint32_t(std::ceil(height));
    list.commands.clear();
    m->atlasScratch.swap(list.atlasPixels);
    list.rasterizedTiles=0;
    std::vector<std::pair<size_t,std::vector<uint32_t>>> overlays;
    ToolkitAccess::gpuCommands(root(),list,overlays);
    // Pack the small, independently rasterized text/icon tiles into one GPU texture.
    list.atlasWidth=2048;uint32_t x=0,y=0,rowHeight=0;
    for(auto& [index,pixels]:overlays){
        auto& command=list.commands[index];
        const auto w=uint32_t(command.bounds.width),h=uint32_t(command.bounds.height);
        if(w>list.atlasWidth)list.atlasWidth=4096;
        if(x+w>list.atlasWidth){x=0;y+=rowHeight;rowHeight=0;}
        command.atlas={float(x),float(y),float(w),float(h)};
        x+=w;rowHeight=std::max(rowHeight,h);
    }
    list.atlasHeight=std::max(1u,y+rowHeight);
    if(list.atlasHeight>4096)throw std::runtime_error("UI: GPU atlas exceeded 4096 pixels");
    list.atlasPixels.assign(size_t(list.atlasWidth)*list.atlasHeight,0);
    for(auto& [index,pixels]:overlays){
        const auto& a=list.commands[index].atlas;
        const uint32_t w=uint32_t(a.width),h=uint32_t(a.height);
        for(uint32_t row=0;row<h;++row)std::copy_n(pixels.data()+size_t(row)*w,w,
            list.atlasPixels.data()+size_t(uint32_t(a.y)+row)*list.atlasWidth+uint32_t(a.x));
    }
    if(m->atlasScratch!=list.atlasPixels)++list.atlasRevision;
    m->dirty=false;++list.revision;
    m->surface.revision=0; // Keep the software fallback current if rendering switches.
    return list;
}
bool Document::pointerMove(float x, float y) {
    m->pointerX = x; m->pointerY = y; m->pointerInside = true;
    if (m->width > 0) layout(m->width,m->height);
    if (m->scrollCapture) { ToolkitAccess::dragScroll(*this,y); return true; }
    auto* hover = ToolkitAccess::buttonAt(*this,x,y);
    if (m->hover != hover) {
        ToolkitAccess::hideTooltip(*this);
        m->hover = hover; m->hoverSince=std::chrono::steady_clock::now(); m->dirty = true;
    }
    const bool consumed = hover || m->capture || ToolkitAccess::scrollAt(*this,x,y);
    if (m->capture && m->capture->m->numeric && m->capture->m->dragAdjustable) {
        if (!m->dragActive && std::abs(x-m->dragX)>=5) m->dragActive=true;
        if (m->dragActive)
            ToolkitAccess::adjust(*m->capture,m->dragStartValue+(x-m->dragX)*m->capture->m->step/10.0f);
    }
    else if (m->capture && m->capture->m->dragMoved) {
        if (!m->dragActive && std::hypot(x-m->dragX,y-m->dragY)>=5) m->dragActive=true;
        auto callback=m->capture->m->dragMoved;
        if (m->dragActive) callback(x,y);
    }
    else if (m->capture && m->capture->m->kind == Kind::Slider) ToolkitAccess::sliderAt(*m->capture,x);
    else if (m->capture && m->capture->m->kind == Kind::TextField) ToolkitAccess::caretAt(*m->capture,x,true);
    else if (m->capture && m->capture->m->kind == Kind::Splitter) {
        const auto delta = m->capture->m->dragAxis == Direction::Row ? x-m->dragX : y-m->dragY;
        auto callback = m->capture->m->dragged; m->dragX = x; m->dragY = y;
        if (delta != 0 && callback) callback(delta);
    }
    return consumed;
}
bool Document::pointerControl() const { return m->pointerControl; }
bool Document::pointerDown(float x, float y, bool control) {
    m->pointerControl = control;
    ToolkitAccess::hideTooltip(*this);
    if (m->popup && !m->popup->bounds().contains(x,y)) {
        const bool menu = m->popupBar && m->popupBar->bounds().contains(x,y);
        closePopup(); if (!menu) return true;
    }
    m->capture = m->scrollCapture = nullptr; m->dragActive=false; pointerMove(x,y);
    if (auto* scroll = ToolkitAccess::scrollAt(*this,x,y)) {
        const auto revision = m->treeRevision;
        const auto identity = scroll->m->identity;
        ToolkitAccess::focus(*this,nullptr);
        if (revision != m->treeRevision && !ToolkitAccess::contains(root(),scroll,identity)) return true;
        const auto thumb = ToolkitAccess::scrollThumb(*scroll);
        m->scrollCapture = scroll; m->keyPressed = nullptr;
        m->scrollGrab = y >= thumb.y && y < thumb.y+thumb.height ? y-thumb.y : thumb.height*.5f;
        ToolkitAccess::dragScroll(*this,y); return true;
    }
    ToolkitAccess::focus(*this,m->hover);
    m->capture = m->focus; m->keyPressed = nullptr; m->dirty = true;
    m->dragX = x; m->dragY = y;
    if(m->capture && m->capture->m->numeric && m->capture->m->dragAdjustable)
        m->dragStartValue=m->capture->m->value;
    const bool consumed = m->capture != nullptr;
    if(m->capture && m->capture->m->pointerDown) {
        auto callback=m->capture->m->pointerDown;callback(x,y);
    }
    if (m->capture && m->capture->m->kind == Kind::Slider) ToolkitAccess::sliderAt(*m->capture,x);
    else if (m->capture && m->capture->m->kind == Kind::TextField) {
        if(m->capture->m->numeric && m->capture->m->dragAdjustable) {
            m->capture->m->edit.all();m->dirty=true;
        } else ToolkitAccess::caretAt(*m->capture,x,false);
    }
    return consumed;
}
bool Document::pointerUp(float x, float y) {
    if (m->scrollCapture) { pointerMove(x,y); m->scrollCapture = nullptr; m->dirty = true; return true; }
    pointerMove(x,y);
    auto* captured = m->capture; m->capture = nullptr; m->dirty = true;
    if (std::exchange(m->dragActive,false) && captured) {
        if(captured->m->numeric && captured->m->dragAdjustable) {
            ToolkitAccess::focus(*this,nullptr);return true;
        }
        auto callback=captured->m->dragFinished;if(callback)callback(true);return true;
    }
    if (captured && captured == m->hover &&
        (captured->m->kind == Kind::Button || captured->m->kind == Kind::CheckBox)) ToolkitAccess::activate(*captured);
    return captured != nullptr;
}
bool Document::wheel(float x, float y, float deltaY) {
    if(m->dragActive)return true;
    if (!std::isfinite(deltaY)) return false;
    if (m->popup && !m->popup->bounds().contains(x,y)) return true;
    if (m->width > 0) layout(m->width,m->height);
    for (auto* node = ToolkitAccess::hit(root(),x,y); node; node = node->m->parent) {
        if (!node->m->effectiveEnabled || !node->m->style.scroll) continue;
        const float old = node->m->scrollY;
        node->scrollTo(old+deltaY);
        if (node->m->scrollY != old) { m->capture = m->scrollCapture = nullptr; layout(m->width,m->height); return true; }
    }
    return false;
}
void Document::pointerLeave() {
    ToolkitAccess::hideTooltip(*this);
    m->pointerInside = false; m->hover = nullptr; m->dirty = true;
}
void Document::cancelInput() {
    ToolkitAccess::hideTooltip(*this);
    const auto finished=m->capture && m->dragActive ? m->capture->m->dragFinished : std::function<void(bool)>{};
    m->dragActive=false; m->capture=nullptr;
    if(finished)finished(false);
    closePopup();
    ToolkitAccess::focus(*this,nullptr);
    m->capture = m->scrollCapture = m->hover = m->focus = m->keyPressed = nullptr;
    m->pointerInside = false; m->dirty = true;
}
bool Document::keyDown(Key key, bool shift, bool repeat) {
    if (m->width > 0) layout(m->width,m->height);
    if(m->dragActive && key!=Key::Escape)return true;
    if (key == Key::Escape && m->popup) { closePopup(); return true; }
    if (key == Key::Escape) {
        const bool handled = m->focus != nullptr;
        if (m->focus && m->focus->m->numeric) m->focus->setValue(m->focus->value());
        else if (m->focus && m->focus->m->textCommitted) m->focus->setText(m->focus->m->committedText);
        cancelInput(); return handled;
    }
    if (key == Key::Tab) {
        if (repeat) return true;
        std::vector<Node*> nodes; ToolkitAccess::focusable(m->popup ? *m->popup : root(),nodes);
        if (nodes.empty()) return false;
        auto it = std::find(nodes.begin(),nodes.end(),m->focus);
        const size_t index = it == nodes.end() ? (shift ? nodes.size()-1 : 0) :
            (size_t(it-nodes.begin())+nodes.size()+(shift ? -1 : 1))%nodes.size();
        ToolkitAccess::focus(*this,nodes[index]); m->keyPressed = nullptr; m->capture = m->scrollCapture = nullptr; m->dirty = true;
        return true;
    }
    if (m->focus && m->focus->m->keyDown) {
        auto callback = m->focus->m->keyDown;
        const auto revision = m->treeRevision;
        if (callback(key) || revision != m->treeRevision) return true;
    }
    if (m->focus && m->focus->m->kind == Kind::TextField) {
        auto& node = *m->focus; auto& edit = node.m->edit;
        if (key == Key::Enter) { ToolkitAccess::commit(node,false); return true; }
        if (key == Key::SelectAll) edit.all();
        else if (key == Key::Left) edit.move(!shift && edit.begin() != edit.end() ? edit.begin() : TextEdit::previous(edit.value,edit.cursor),shift);
        else if (key == Key::Right) edit.move(!shift && edit.begin() != edit.end() ? edit.end() : TextEdit::next(edit.value,edit.cursor),shift);
        else if (key == Key::Home) edit.move(0,shift);
        else if (key == Key::End) edit.move(edit.value.size(),shift);
        else if (key == Key::Backspace) { edit.backspace(); ToolkitAccess::editChanged(node); }
        else if (key == Key::Delete) { edit.erase(); ToolkitAccess::editChanged(node); }
        else if (node.m->numeric && (key == Key::Up || key == Key::Down))
            ToolkitAccess::adjust(node,node.value()+(key == Key::Up ? node.m->step : -node.m->step));
        m->dirty = true; return true;
    }
    if (m->focus && m->focus->m->kind == Kind::Slider) {
        auto& node = *m->focus;
        if (key == Key::Left || key == Key::Down) ToolkitAccess::adjust(node,node.value()-node.m->step);
        else if (key == Key::Right || key == Key::Up) ToolkitAccess::adjust(node,node.value()+node.m->step);
        else if (key == Key::Home) ToolkitAccess::adjust(node,node.m->minimum);
        else if (key == Key::End) ToolkitAccess::adjust(node,node.m->maximum);
        return true;
    }
    if (m->focus && m->focus->m->kind == Kind::Splitter) {
        const bool horizontal = m->focus->m->dragAxis == Direction::Row;
        const bool decrease = key == (horizontal ? Key::Left : Key::Up);
        const bool increase = key == (horizontal ? Key::Right : Key::Down);
        if (!decrease && !increase) return false;
        auto callback = m->focus->m->dragged; if (callback) callback(decrease ? -10.0f : 10.0f);
        return true;
    }
    if (key != Key::Enter && key != Key::Space) return false;
    if (m->focus && !repeat && !m->keyPressed) {
        m->keyPressed = m->focus; m->pressedKey = key; m->dirty = true;
    }
    return m->focus != nullptr;
}
bool Document::keyUp(Key key) {
    if (m->width > 0) layout(m->width,m->height);
    if (!m->keyPressed || m->pressedKey != key) return false;
    auto* pressed = m->keyPressed; m->keyPressed = nullptr; m->dirty = true;
    if (pressed == m->focus) ToolkitAccess::activate(*pressed);
    return true;
}
bool Document::wantsTextInput() const { return m->focus && m->focus->m->kind == Kind::TextField; }
bool Document::textInput(std::string text) {
    if (!wantsTextInput() || !m->focus->m->effectiveEnabled) return false;
    auto& node = *m->focus;
    if (node.m->edit.replace(std::move(text))) ToolkitAccess::editChanged(node);
    return true;
}
std::string Document::selectedText() const { return wantsTextInput() ? m->focus->m->edit.selection() : ""; }
bool Document::cutSelection() {
    if (!wantsTextInput()) return false;
    auto& node = *m->focus;
    node.m->edit.replace(""); ToolkitAccess::editChanged(node); return true;
}
}
