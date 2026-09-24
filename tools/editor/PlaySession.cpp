#include "editor/GameEditor.h"
#include "ui/Theme.h"
#include <cmath>

namespace genesis::editor {
void GameEditor::play() {
    if(m_playState==PlayState::Playing)return;
    if(m_editorMode==EditorMode::Edit)setEditorMode(EditorMode::Object);
#ifndef GENESIS_WITH_PHYSX_PBD
    for(const auto& entity:m_world.snapshots())if(entity.particles && entity.particles->enabled){
        status("PhysX particle playback needs a Windows CUDA/PhysX GPU build.");showConsole();return;
    }
#endif
    if(!playing()) {
        if(m_document.wantsTextInput())m_document.keyDown(ui::Key::Enter);
        m_document.cancelInput();endTransformEdit(true);
        auto session=std::make_unique<PlaySession>();
        session->dirty=dirty();session->selection=m_selected;session->asset=m_selectedAsset;session->camera=m_camera;
        // Retain the entire authored registry, including entity generations.
        // Only its clone is exposed to runtime/editor mutations during Play.
        session->authored.registry().swap(m_world.registry());
        m_world.cloneFrom(session->authored);m_session=std::move(session);
        m_ticks=0;m_frameRequested=false;
        ++m_revision;
    }
    m_accumulator=0;m_playState=PlayState::Playing;
    changed();
    if(!m_dock->visible("scene"))m_dock->show("scene");
    m_viewportHeader->setVisible(false);
    m_viewportToolStrip->setVisible(false);
    m_viewportSidebar->setVisible(false);
    m_sidebarOptionsButton->setVisible(false);
    layout(m_layoutWidth,m_layoutHeight);
    updatePlayback();status("Playing | Click viewport for input; Escape releases it. Stop restores the edit scene.");
}
void GameEditor::pause() {
    if(m_playState!=PlayState::Playing)return;
    m_playState=PlayState::Paused;m_accumulator=0;updatePlayback();status("Paused | Step advances 1/60 second. Play resumes.");
}
void GameEditor::tick() {
    const auto before=fingerprint();
    ++m_ticks;
    if(m_tickHandler)m_tickHandler(float(fixedDelta));
    // Hosts may mutate entities inside a simulation tick.
    if(before!=fingerprint()){++m_revision;rebuildTree();updateInspector();}
    updatePlayback();
}
void GameEditor::step() { if(m_playState==PlayState::Paused){m_accumulator=0;tick();} }
void GameEditor::advance(double elapsed) {
    if(m_playState!=PlayState::Playing || !std::isfinite(elapsed) || elapsed<=0)return;
    m_accumulator+=std::min(elapsed,.1);
    while(m_accumulator+1e-9>=fixedDelta && m_playState==PlayState::Playing) {
        m_accumulator-=fixedDelta;tick();
    }
}
void GameEditor::stop() {
    if(!m_session)return;
    m_document.cancelInput();endTransformEdit(false);
    m_world.registry().swap(m_session->authored.registry());
    const auto selection=m_session->selection;
    const auto asset=m_session->asset;m_camera=m_session->camera;
    m_session.reset();m_playState=PlayState::Edit;m_accumulator=0;m_ticks=0;m_frameRequested=false;
    ++m_cameraRestoreRevision;
    m_viewportHeader->setVisible(true);
    m_viewportToolStrip->setVisible(true);
    m_viewportSidebar->setVisible(m_viewportSidebarVisible);
    m_sidebarOptionsButton->setVisible(true);
    layout(m_layoutWidth,m_layoutHeight);
    changed();rebuildTree();select(selection);
    if(asset) {const auto found=std::find_if(m_assets.begin(),m_assets.end(),[&](const auto& item){return item.path==*asset;});
        if(found!=m_assets.end())inspectAsset(size_t(found-m_assets.begin()));}
    updatePlayback();status("Stopped | Edit scene restored.");
}
void GameEditor::updatePlayback() {
    for(auto [node,active]:{std::pair{m_playButton,m_playState==PlayState::Playing},std::pair{m_pauseButton,m_playState==PlayState::Paused}}) {
        auto style=node->style();style.background=active?ui::theme::selection:ui::theme::raised;
        style.textColor=active?ui::theme::accent:ui::theme::text;node->setStyle(style);
    }
    m_pauseButton->setEnabled(m_playState==PlayState::Playing);
    m_stepButton->setEnabled(m_playState==PlayState::Paused);m_stopButton->setEnabled(playing());
}
}
