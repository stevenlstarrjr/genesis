#pragma once
#include "ui/EditorWidgets.h"
#include <array>

namespace genesis::editor {
// Interactive editor-widget workbench. It edits a real UI preview model, not a
// 3D scene. Editor application state stays outside the reusable widget library.
class UiWorkbench {
public:
    UiWorkbench();
    ui::Document& document() { return m_document; }
    const ui::Surface& render(float width, float height, float scale = 1);
    void select(size_t index);
    const std::string& selectedText() const { return m_items[m_selected].text; }
    float selectedWidth() const { return m_items[m_selected].width; }
private:
    struct Item {
        std::string id, text;
        float width = 260, fontSize = 14, radius = 8, padding = 12;
        bool enabled = true, visible = true;
    };
    ui::Document m_document;
    std::unique_ptr<ui::Tabs> m_leftTabs, m_centerTabs;
    std::unique_ptr<ui::TreeView> m_tree;
    std::unique_ptr<ui::ScrollView> m_inspector;
    std::array<Item,3> m_items;
    std::array<ui::Node*,3> m_preview{};
    ui::Node *m_name, *m_width, *m_size, *m_radius, *m_padding, *m_enabled, *m_visible;
    ui::Node *m_status, *m_selectedLabel, *m_filter;
    size_t m_selected = 0;
    int m_clicks = 0;
    void rebuildTree();
    void updatePreview();
    void reset();
};
int runUiWorkbench();
}
