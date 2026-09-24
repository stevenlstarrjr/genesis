#pragma once
#include "ui/Toolkit.h"
#include <array>

namespace genesis::ui {
// Composite controls own no application data. Keep them alive as long as their
// mounted nodes; do not clear the mount externally while using the control.
class ScrollView {
public:
    explicit ScrollView(Node& parent);
    ScrollView(const ScrollView&) = delete;
    ScrollView& operator=(const ScrollView&) = delete;
    Node& viewport() { return *m_viewport; }
    Node& content() { return *m_content; }
private:
    Node *m_viewport, *m_content;
};

struct TreeItem {
    std::string id, text;
    std::vector<TreeItem> children;
    bool expanded = true;
    Icon icon = Icon::None;
};
class TreeView {
public:
    TreeView(Node& parent, std::function<void(const std::string&)> selected);
    TreeView(const TreeView&) = delete;
    TreeView& operator=(const TreeView&) = delete;
    void setItems(std::vector<TreeItem> items); // IDs must be unique and nonempty.
    void select(const std::string& id); // Programmatic selection does not emit.
    void setExpanded(const std::string& id, bool expanded);
    void focusSelected();
    const std::string& selected() const { return m_selected; }
    Node& viewport() { return m_scroll.viewport(); }
private:
    ScrollView m_scroll;
    std::vector<TreeItem> m_items;
    std::string m_selected;
    std::function<void(const std::string&)> m_changed;
    struct Row { std::string id, parent; Node *button, *toggle; };
    std::vector<Row> m_rows;
    void rebuild();
    void append(TreeItem& item, int depth, const std::string& parent = {});
    bool navigate(const std::string& id, Key key);
    void choose(const std::string& id);
};

class Tabs {
public:
    explicit Tabs(Node& parent);
    Tabs(const Tabs&) = delete;
    Tabs& operator=(const Tabs&) = delete;
    Node& add(std::string title);
    void activate(size_t index);
    size_t active() const { return m_active; }
    Node& node() { return *m_root; }
    Node& header() { return *m_header; }
    void setIcon(size_t index, Icon icon);
private:
    struct Page { Node *button, *content; };
    Node *m_root, *m_header, *m_body;
    std::vector<Page> m_pages;
    size_t m_active = 0;
};

struct MenuItem {
    std::string text, shortcut;
    std::function<void()> action;
    std::function<bool()> enabled;
    std::function<bool()> checked;
};
class MenuBar {
public:
    // Construct after other document content, so the absolute popup paints last.
    MenuBar(Document& document,Node& bar);
    Node& add(std::string title,std::vector<MenuItem> items);
private:
    struct Menu { Node* button; std::vector<MenuItem> items; };
    Document& m_document;
    Node *m_bar,*m_popup;
    std::vector<Menu> m_menus;
    std::vector<Node*> m_commands;
    void open(size_t index);
};

struct DropdownChoice {
    std::string label;
    std::string blenderIcon;
    bool enabled=true;
};
class Dropdown {
public:
    // Mount after the document's normal content so the popup draws above it.
    Dropdown(Document& document,Node& parent,std::vector<DropdownChoice> choices,
        size_t selected,std::function<void(size_t)> changed);
    Dropdown(const Dropdown&) = delete;
    Dropdown& operator=(const Dropdown&) = delete;
    Node& button() { return *m_button; }
    void setButtonLabel(std::string label);
    size_t selected() const { return m_selected; }
    void select(size_t index,bool emit=false);
    void open();
private:
    Document& m_document;
    Node *m_button{},*m_popup{};
    std::vector<DropdownChoice> m_choices;
    std::string m_buttonLabel;
    std::vector<Node*> m_items;
    std::function<void(size_t)> m_changed;
    size_t m_selected=0;
};

struct ResourceItem { std::string id, name, detail; Icon icon=Icon::File; bool enabled=true; };
class ResourcePicker {
public:
    explicit ResourcePicker(Document& document); // Mount after normal content.
    void open(Node& anchor,std::string title,std::vector<ResourceItem> items,
        std::function<void(const std::string&)> chosen,bool allowPath=false);
private:
    Document& m_document;
    Node *m_popup,*m_title,*m_search,*m_pathRow,*m_path;
    std::unique_ptr<ScrollView> m_results;
    std::vector<ResourceItem> m_items;
    std::vector<Node*> m_buttons;
    std::vector<std::string> m_ids;
    std::function<void(const std::string&)> m_chosen;
    void refresh();
    void choose(std::string id);
};
class ColorPicker {
public:
    explicit ColorPicker(Document& document); // Mount after normal content.
    void open(Node& anchor,std::array<float,3> value,std::function<void(std::array<float,3>)> changed);
private:
    Document& m_document;
    Node *m_popup{},*m_preview{},*m_hex{},*m_square{},*m_hueStrip{};
    std::array<Node*,3> m_hsvFields{};
    std::array<float,3> m_value{1,1,1};
    float m_hue=0,m_saturation=0,m_brightness=1;
    std::function<void(std::array<float,3>)> m_changed;
    void setColor(std::array<float,3> value,bool emit);
    void pickSquare(float x,float y);
    void pickHue(float x,float y);
    void refresh();
};
// Linear RGB control with editable hex, precise channels, and an optional picker.
class ColorField {
public:
    ColorField(Node& parent,std::function<void(std::array<float,3>)> changed);
    void setValue(std::array<float,3> value); // Silent programmatic update.
    void setPicker(ColorPicker& picker) { m_picker=&picker; }
private:
    std::array<float,3> m_value{1,1,1};
    std::array<Node*,3> m_fields{};
    Node *m_hex{},*m_pick{};
    ColorPicker* m_picker{};
    std::function<void(std::array<float,3>)> m_changed;
};

// A collapsible inspector section; returns its content container.
Node& section(Node& parent, std::string title, bool expanded = true);
// A conventional property row with a caption and a container for a control.
Node& property(Node& parent, std::string caption);
}
