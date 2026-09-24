#include "ui/LauncherView.h"

namespace genesis::ui {
LauncherView::LauncherView(std::string message, std::function<void()> openFolder, std::function<void()> openFile) {
    Style root;
    root.background = {8,11,18,255}; root.align = Align::Center; root.justify = Justify::Center;
    root.padding = 24;
    auto& body = m_document.root().setStyle(root);
    Style card; card.width = Length::percent(100); card.maxWidth = 760;
    card.padding = 36; card.gap = 18; card.radius = 22;
    card.background = {22,27,40,255}; card.borderWidth = 1; card.borderColor = {43,53,75,255};
    m_card = &body.column().setStyle(card);
    Style brand; brand.font = Font::Semibold; brand.fontSize = 11;
    brand.textColor = {137,155,255,255};
    m_card->label("GENESIS  /  ENGINE").setStyle(brand);
    Style title; title.font = Font::Semibold; title.fontSize = 28;
    title.textColor = {244,246,252,255};
    m_title = &m_card->label("Your next scene starts here.").setStyle(title);
    Style description; description.fontSize = 12.5f; description.textColor = {158,172,201,255};
    description.textWrap = TextWrap::Word;
    m_card->label("Open a project to get started, or launch a scene or Python script directly.").setStyle(description);
    Style drop; drop.padding = 24; drop.gap = 7; drop.radius = 13;
    drop.background = {13,18,30,255}; drop.borderWidth = 1; drop.borderColor = {65,83,145,255};
    auto& dropArea = m_card->column().setStyle(drop);
    Style dropTitle; dropTitle.font = Font::Medium; dropTitle.fontSize = 15;
    dropTitle.textWrap = TextWrap::Word;
    dropArea.label("Drop a project, scene, or script").setStyle(dropTitle);
    description.fontSize = 10.5f;
    dropArea.label("Project folder  /  genesis.project  /  .gscene  /  .py").setStyle(description);
    m_actions = &m_card->row();
    auto actions = m_actions->style(); actions.gap = 12; m_actions->setStyle(actions);
    m_folder = &m_actions->button("Open project folder",std::move(openFolder));
    m_file = &m_actions->button("Open file...",std::move(openFile));
    auto secondary = m_file->style(); secondary.background = {37,46,65,255};
    secondary.borderWidth = 1; secondary.borderColor = {67,80,107,255}; m_file->setStyle(secondary);
    Style status; status.fontSize = 10.5f; status.textColor = {208,179,130,255}; status.textWrap = TextWrap::Word;
    m_status = &m_card->label(std::move(message)).setStyle(status);
    Style hint; hint.fontSize = 9.5f; hint.textColor = {104,119,153,255};
    hint.textWrap = TextWrap::Word;
    m_card->label("Tab to navigate  /  Enter to open  /  Escape to close").setStyle(hint);
}
void LauncherView::setMessage(std::string message) { m_status->setText(std::move(message)); }
void LauncherView::setBusy(bool busy) { m_folder->setEnabled(!busy); m_file->setEnabled(!busy); }
void LauncherView::layout(float width, float height) {
    if (width != m_width) {
        m_width = width;
        auto card = m_card->style(); card.padding = width < 600 ? 20.0f : 36.0f; m_card->setStyle(card);
        auto actions = m_actions->style(); actions.direction = width < 600 ? Direction::Column : Direction::Row;
        m_actions->setStyle(actions);
        auto title = m_title->style(); title.fontSize = width < 600 ? 20.0f : 28.0f;
        title.textWrap = TextWrap::Word; m_title->setStyle(title);
    }
    m_document.layout(width,height);
}
const Surface& LauncherView::render(float width, float height, float scale) {
    layout(width,height); return m_document.render(width,height,scale);
}
}
