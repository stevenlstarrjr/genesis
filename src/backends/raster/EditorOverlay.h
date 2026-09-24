#pragma once
#include "editor/ViewportMath.h"
#include "editor/TransformGizmo.h"
#include "ui/Toolkit.h"
#include "physics/PhysxParticles.h"
#include <bgfx/bgfx.h>

namespace genesis::raster {
class EditorOverlay {
public:
    bool initialize();
    void shutdown();
    void draw(uint16_t view, const ui::Rect& viewport, const float* cameraView, const float* projection,
        bgfx::TextureHandle sceneDepth, bool grid, const editor::Vec3& cameraPosition,
        float cameraDistance, const editor::Bounds& selection,const editor::Vec3& cursor,
        const editor::GizmoFrame& gizmo, int highlightedHandle);
    void drawParticles(uint16_t view,const ui::Rect& viewport,const float* cameraView,const float* projection,
        bgfx::TextureHandle sceneDepth,const std::vector<physics::ParticleFrame>& frames);
    void drawSelectionGesture(uint16_t view,const ui::Rect& viewport,bgfx::TextureHandle sceneDepth,
        const std::vector<editor::gizmoMath::Vec2>& points,int mode,float radius,editor::gizmoMath::Vec2 cursor);
    void drawMeshEdit(uint16_t view,const ui::Rect& viewport,const float* cameraView,
        const float* projection,int selectionMode,
        bgfx::TextureHandle sceneDepth,const std::vector<editor::Vec3>& positions,
        const std::vector<std::array<uint32_t,2>>& edges,
        const std::vector<std::array<uint32_t,3>>& faces,const std::vector<uint32_t>& selected);
private:
    bgfx::ProgramHandle m_program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_depth=BGFX_INVALID_HANDLE,m_params=BGFX_INVALID_HANDLE,
        m_gridParams=BGFX_INVALID_HANDLE;
};
}
