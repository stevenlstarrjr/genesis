#pragma once
#include "editor/GameEditor.h"
#include "ui/SdlUiInput.h"
#include "editor/ViewportCamera.h"
#include <SDL3/SDL.h>
#include <optional>
#include <utility>
#include <vector>

namespace genesis::editor {
class SdlEditorHost {
public:
    struct SelectionGesture {
        GameEditor::SelectionMode mode=GameEditor::SelectionMode::Select;
        std::vector<gizmoMath::Vec2> points;
        gizmoMath::Vec2 cursor{};
        std::vector<gameplay::EntityId> baseSelection;
        std::vector<uint32_t> baseMeshSelection,meshHits;
        uint64_t strokeId=0;
        float radius=48.0f;
        bool extend=false,toggle=false;
    };
    SdlEditorHost(GameEditor& editor, SDL_Window* window, const render::CameraSettings& camera = {}, const render::CameraSettings& gameCamera = {})
        : m_editor(editor),m_window(window),m_camera(camera),m_initialGameCamera(gameCamera),m_gameCamera(gameCamera) { initializePlatform(); }
    ~SdlEditorHost();
    bool event(const SDL_Event& event);
    bool canClose();
    void update();
    ViewportCamera& camera() { return m_camera; }
    const render::CameraSettings& gameCamera() const { return m_gameCamera; }
    bool gameFocused() const { return m_gameFocused && m_editor.playing() && m_editor.gameViewport().width>0; }
    void gameTick(float delta);
    GizmoFrame gizmoFrame() const;
    int gizmoHighlight() const { return m_gizmo.dragging()?m_gizmo.handle():m_hoverHandle; }
    const Vec3& cursorPosition() const { return m_editor.viewportCursor(); }
    std::optional<SelectionGesture> takeSelectionRequest() { return std::exchange(m_selectionRequest,std::nullopt); }
    std::optional<SelectionGesture> selectionPreview() const;
private:
    GameEditor& m_editor;
    SDL_Window* m_window;
    ui::SdlUiInput m_input;
    ViewportCamera m_camera;
    render::CameraSettings m_initialGameCamera,m_gameCamera;
    std::array<bool,SDL_SCANCODE_COUNT> m_gameKeys{};
    bool m_gameFocused=false,m_gameLook=false,m_wasPlaying=false;
    uint64_t m_cameraRestoreRevision=0;
    void releaseGameInput();
    TransformGizmo m_gizmo;
    int m_hoverHandle=-1;
    std::optional<SelectionGesture> m_selectionGesture,m_selectionRequest;
    uint64_t m_nextSelectionStrokeId=1;
    float m_circleRadius=48.0f;
    bool m_tweakPending=false,m_tweakDragging=false;
    bool m_meshPointerPending=false,m_meshPointerDragging=false;
    gizmoMath::Vec2 m_meshPointerStart{};
    Vec3 m_meshDragCenter{};
    gizmoMath::Vec2 m_tweakStart{};
    gameplay::Transform m_tweakOriginal{};
    float m_dragX=0,m_dragY=0;
    void finishNavigation(bool cancel=false,bool restorePointer=true);
    void finishTransform(bool commit);
    void queueCircleSelection(std::vector<gizmoMath::Vec2> samples);
    void applyMeshGesture(SelectionGesture& gesture,const std::vector<gizmoMath::Vec2>& points);
    void initializePlatform();
};
}
