#include "editor/UiWorkbench.h"
#include <algorithm>
#include <cctype>

using namespace genesis::ui;
namespace genesis::editor {
UiWorkbench::UiWorkbench() {
    Style root; root.padding = 10; root.gap = 8; root.background = {12,16,24,255}; m_document.root().setStyle(root);
    auto& toolbar = m_document.root().row(); auto row = toolbar.style(); row.gap = 12; row.align = Align::Center;
    row.height = 42; row.shrink = 0; toolbar.setStyle(row);
    Style title; title.font = Font::Semibold; title.fontSize = 12; title.grow = 1; title.textColor = {182,203,246,255};
    toolbar.label("GENESIS   /   EDITOR UI WORKBENCH").setStyle(title);
    auto& resetButton = toolbar.button("Reset preview",[this] { reset(); });
    auto button = resetButton.style(); button.minHeight = 0; button.height = 32; button.fontSize = 10;
    resetButton.setStyle(button);
    auto& workspace = m_document.root().row(); row = workspace.style(); row.grow = 1; row.gap = 8;
    workspace.setStyle(row);
    Style pane; pane.width = 244; pane.shrink = 0; pane.padding = 10; pane.gap = 8;
    pane.background = {22,29,40,255}; pane.radius = 6;
    auto& left = workspace.column().setStyle(pane);
    m_leftTabs = std::make_unique<Tabs>(left);
    auto& scene = m_leftTabs->add("Scene");
    auto sceneStyle = scene.style(); sceneStyle.gap = 8; scene.setStyle(sceneStyle);
    m_filter = &scene.textField("",[this](const std::string&) { rebuildTree(); });
    auto filterStyle = m_filter->style(); filterStyle.fontSize = 10; m_filter->setStyle(filterStyle);
    Style hint; hint.fontSize = 9; hint.textColor = {120,140,167,255}; hint.textWrap = TextWrap::Word;
    scene.label("Filter nodes above. Select a node to edit its properties.").setStyle(hint);
    m_tree = std::make_unique<TreeView>(scene,[this](const std::string& id) {
        if (id == "root") { m_inspector->viewport().setEnabled(false); m_selectedLabel->setText("Scene root / select a child to edit"); return; }
        for (size_t i = 0; i < m_items.size(); ++i) if (m_items[i].id == id) select(i);
    });
    auto& controls = m_leftTabs->add("Controls");
    auto available = controls.style(); available.gap = 10; controls.setStyle(available);
    controls.label("AVAILABLE WIDGETS").setStyle(hint);
    for (const char* text : {"Text field", "Number field", "Checkbox", "Slider", "Tree view", "Tabs", "Scroll view", "Collapsible section"})
        controls.label(text);
    Style center = pane; center.width = {}; center.grow = 1; center.shrink = 1;
    auto& middle = workspace.column().setStyle(center);
    m_centerTabs = std::make_unique<Tabs>(middle);
    auto& previewPage = m_centerTabs->add("UI Preview");
    Style previewStyle; previewStyle.grow = 1; previewStyle.align = Align::Center; previewStyle.justify = Justify::Center;
    previewStyle.gap = 24; previewStyle.background = {14,20,31,255}; previewStyle.radius = 5;
    auto& preview = previewPage.column().setStyle(previewStyle);
    m_preview[0] = &preview.button("Button",[this] { ++m_clicks; m_status->setText("Preview button clicked " + std::to_string(m_clicks) + " times."); });
    m_preview[1] = &preview.label("Label");
    m_preview[2] = &preview.checkBox("Checkbox",false,[this](bool value) {
        m_status->setText(value ? "Preview checkbox enabled." : "Preview checkbox disabled.");
    });
    auto& help = m_centerTabs->add("Usage"); auto helpStyle = help.style(); helpStyle.gap = 12; helpStyle.padding = 20; help.setStyle(helpStyle);
    for (const char* line : {
        "Select Button, Label or Checkbox in the scene tree.",
        "Inspector edits update the real controls in the preview.",
        "Text: click/drag to select, Ctrl+A/C/X/V, arrows, Home/End.",
        "Numbers: type then Enter/Tab to commit; Up/Down to step. Escape cancels an unfinished number.",
        "Sliders: drag, use arrow keys, or Home/End.",
        "Scroll inspector panels with the mouse wheel. Click section headers to collapse them.",
        "This workbench exercises the toolkit; it is not yet the 3D scene editor."
    }) { auto style = hint; style.fontSize = 12; help.label(line).setStyle(style); }
    pane.width = 300;
    auto& right = workspace.column().setStyle(pane);
    title.grow = 0;
    right.label("INSPECTOR").setStyle(title);
    m_selectedLabel = &right.label("").setStyle(hint);
    m_inspector = std::make_unique<ScrollView>(right);
    auto& content = m_inspector->content();
    auto& identity = section(content,"Node");
    m_name = &property(identity,"Text").textField("",[this](const std::string& value) {
        m_items[m_selected].text = value; updatePreview(); rebuildTree();
    });
    m_enabled = &identity.checkBox("Enabled",true,[this](bool value) { m_items[m_selected].enabled = value; updatePreview(); });
    m_visible = &identity.checkBox("Visible",true,[this](bool value) { m_items[m_selected].visible = value; updatePreview(); });
    auto& layout = section(content,"Layout");
    m_width = &property(layout,"Width (px)").numberField(260,80,460,10,[this](float value) {
        m_items[m_selected].width = value; updatePreview();
    });
    m_padding = &property(layout,"Padding (px)").numberField(12,0,40,1,[this](float value) {
        m_items[m_selected].padding = value; updatePreview();
    });
    auto& appearance = section(content,"Appearance");
    m_size = &property(appearance,"Font size (pt)").numberField(14,8,36,1,[this](float value) {
        m_items[m_selected].fontSize = value; updatePreview();
    });
    m_radius = &property(appearance,"Corner radius").slider(8,0,32,1,[this](float value) {
        m_items[m_selected].radius = value; updatePreview();
        m_status->setText("Corner radius: " + std::to_string(int(value)) + " px");
    });
    auto& notes = section(content,"Widget behavior");
    notes.label("Values are bound to the preview model. Disabled controls stop accepting input; hidden controls leave the layout.").setStyle(hint);
    notes.label("Numeric fields clamp to valid ranges and reject invalid values on commit.").setStyle(hint);
    m_status = &m_document.root().label("").setStyle(hint);
    reset();
}
void UiWorkbench::reset() {
    m_items = {{{"button","Run action",260,14,8,12,true,true},
        {"label","Edit this label in the inspector",340,14,0,10,true,true},
        {"checkbox","Enable shadows",260,13,5,10,true,true}}};
    m_clicks = 0; m_filter->setText(""); m_preview[2]->setChecked(false);
    rebuildTree(); select(0); m_status->setText("Ready. Select a node, then edit its properties. All preview controls are interactive.");
}
void UiWorkbench::rebuildTree() {
    std::string query = m_filter->text();
    std::transform(query.begin(),query.end(),query.begin(),[](unsigned char c) { return char(std::tolower(c)); });
    std::vector<TreeItem> children;
    for (const auto& item : m_items) {
        std::string search = item.id+" "+item.text;
        std::transform(search.begin(),search.end(),search.begin(),[](unsigned char c) { return char(std::tolower(c)); });
        if (query.empty() || search.find(query) != std::string::npos)
            children.push_back({item.id,item.id+"  /  "+item.text});
    }
    bool selectedVisible = false;
    for (auto& item : children) if (item.id == m_items[m_selected].id) selectedVisible = true;
    m_tree->setItems({{"root","UI Scene",std::move(children)}});
    if (selectedVisible) m_tree->select(m_items[m_selected].id);
}
void UiWorkbench::select(size_t index) {
    if (index >= m_items.size()) return;
    m_selected = index; const auto& item = m_items[index];
    m_inspector->viewport().setEnabled(true);
    m_selectedLabel->setText(item.id + "  /  UI control");
    m_name->setText(item.text); m_width->setValue(item.width); m_padding->setValue(item.padding);
    m_size->setValue(item.fontSize); m_radius->setValue(item.radius);
    m_enabled->setChecked(item.enabled); m_visible->setChecked(item.visible);
    m_radius->setEnabled(index != 1);
    updatePreview();
}
void UiWorkbench::updatePreview() {
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i]; auto& node = *m_preview[i];
        auto style = node.style(); style.width = item.width; style.fontSize = item.fontSize;
        style.radius = item.radius; style.padding = item.padding;
        if (i == 2) style.padding.left = std::max(34.0f,item.padding);
        if (i == 1) { style.textAlign = .5f; style.textWrap = TextWrap::Word; }
        node.setStyle(style).setText(item.text).setEnabled(item.enabled).setVisible(item.visible);
    }
}
const Surface& UiWorkbench::render(float width, float height, float scale) { return m_document.render(width,height,scale); }
}
