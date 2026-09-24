#include "editor/SdlEditorHost.h"
#include "ui/SdlUiInput.h"
#include <algorithm>
#include <cmath>
#ifdef __EMSCRIPTEN__
#include "platform/web/BrowserHost.h"
#endif

namespace genesis::editor {
namespace {
std::vector<gizmoMath::Vec2> appendCircleSamples(SdlEditorHost::SelectionGesture& gesture,gizmoMath::Vec2 point) {
    std::vector<gizmoMath::Vec2> samples;
    const auto start=gesture.points.back();
    const float distance=std::hypot(point[0]-start[0],point[1]-start[1]);
    const float spacing=std::min(12.0f,gesture.radius*.75f);
    for(float step=spacing;step<distance;step+=spacing) {
        const float t=step/distance;
        samples.push_back({start[0]+(point[0]-start[0])*t,start[1]+(point[1]-start[1])*t});
    }
    if(distance>=spacing)samples.push_back(point);
    if(!samples.empty())gesture.points.back()=samples.back();
    gesture.cursor=point;
    if(samples.empty() || samples.back()!=point)samples.push_back(point);
    return samples;
}
}
void SdlEditorHost::initializePlatform() {
    update();
#ifdef __EMSCRIPTEN__
    m_editor.setSaveHandler(web::save);
#endif
}
SdlEditorHost::~SdlEditorHost() {
    releaseGameInput();if(m_gizmo.dragging())finishTransform(false);if(m_camera.dragging())finishNavigation(false,false);
    if(m_tweakDragging)m_editor.endTransformEdit(false);
    if(m_meshPointerDragging)m_editor.endMeshDrag(false);
    if(m_tweakPending || m_selectionGesture || m_meshPointerPending)SDL_CaptureMouse(false);
}
GizmoFrame SdlEditorHost::gizmoFrame() const {
    if(!m_editor.gizmoVisible() || m_editor.editorMode()==GameEditor::EditorMode::Edit)return {};
    if(m_gizmo.dragging() && m_editor.tool()!=TransformTool::Move
        && !(m_editor.tool()==TransformTool::Transform &&
            (m_gizmo.handle()<3 || (m_gizmo.handle()>=10 && m_gizmo.handle()<=12))))return m_gizmo.frame();
    const auto transform=m_editor.selectedTransform();
    return transform?GizmoFrame::make(m_editor.tool(),*transform,m_camera,m_editor.viewport(),
        m_editor.localOrientation()):GizmoFrame{};
}
std::optional<SdlEditorHost::SelectionGesture> SdlEditorHost::selectionPreview() const {
    if(m_selectionGesture)return m_selectionGesture;
    if(m_editor.tool()!=TransformTool::Select || m_editor.selectionMode()!=GameEditor::SelectionMode::Circle ||
        m_editor.document().hasPopup() || m_editor.document().hasPointerCapture())return std::nullopt;
    float x=0,y=0;SDL_GetMouseState(&x,&y);
    if(!m_editor.viewport().contains(x,y))return std::nullopt;
    SelectionGesture preview;preview.mode=GameEditor::SelectionMode::Circle;
    preview.points.push_back({x,y});preview.cursor={x,y};preview.radius=m_circleRadius;
    return preview;
}
void SdlEditorHost::applyMeshGesture(SelectionGesture& gesture,const std::vector<gizmoMath::Vec2>& points) {
    const auto hits=m_editor.meshRegionHits(gesture.mode,points,gesture.radius,m_camera);
    if(gesture.mode==GameEditor::SelectionMode::Circle) {
        for(uint32_t index:hits)
            if(std::find(gesture.meshHits.begin(),gesture.meshHits.end(),index)==gesture.meshHits.end())
                gesture.meshHits.push_back(index);
    } else gesture.meshHits=hits;
    m_editor.setMeshSelection(gesture.baseMeshSelection,gesture.meshHits,gesture.extend,gesture.toggle);
}
void SdlEditorHost::queueCircleSelection(std::vector<gizmoMath::Vec2> samples) {
    if(m_selectionRequest && m_selectionGesture && m_selectionRequest->strokeId==m_selectionGesture->strokeId)
        samples.insert(samples.begin(),m_selectionRequest->points.begin(),m_selectionRequest->points.end());
    m_selectionRequest=m_selectionGesture;
    m_selectionRequest->points=std::move(samples);
}
void SdlEditorHost::finishTransform(bool commit) {
    m_gizmo.end();m_hoverHandle=-1;m_editor.endTransformEdit(commit);SDL_CaptureMouse(false);
}
void SdlEditorHost::finishNavigation(bool cancel,bool restorePointer) {
    if (cancel) m_camera.cancel(); else m_camera.end();
    SDL_SetWindowRelativeMouseMode(m_window,false); SDL_CaptureMouse(false);
    if (restorePointer) SDL_WarpMouseInWindow(m_window,m_dragX,m_dragY);
}
bool SdlEditorHost::event(const SDL_Event& event) {
    update();
    auto& document=m_editor.document();
    if(m_meshPointerPending) {
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE) {
            if(m_meshPointerDragging)m_editor.endMeshDrag(false);
            m_meshPointerPending=m_meshPointerDragging=false;SDL_CaptureMouse(false);return true;
        }
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_QUIT) {
            if(m_meshPointerDragging)m_editor.endMeshDrag(false);
            m_meshPointerPending=m_meshPointerDragging=false;SDL_CaptureMouse(false);
        } else if(event.type==SDL_EVENT_MOUSE_MOTION) {
            const gizmoMath::Vec2 point{event.motion.x,event.motion.y};
            if(!m_meshPointerDragging && std::hypot(point[0]-m_meshPointerStart[0],point[1]-m_meshPointerStart[1])>=4)
                m_meshPointerDragging=m_editor.beginMeshDrag();
            if(m_meshPointerDragging) {
                const auto viewport=m_editor.viewport();
                const auto normal=gizmoMath::rayAt({viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f},m_camera,viewport).direction;
                const auto start=gizmoMath::plane(gizmoMath::rayAt(m_meshPointerStart,m_camera,viewport),m_meshDragCenter,normal);
                const auto current=gizmoMath::plane(gizmoMath::rayAt(point,m_camera,viewport),m_meshDragCenter,normal);
                if(start && current)m_editor.previewMeshDrag(gizmoMath::sub(*current,*start));
            }
            return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) {
            if(m_meshPointerDragging)m_editor.endMeshDrag(true);
            m_meshPointerPending=m_meshPointerDragging=false;SDL_CaptureMouse(false);return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_WHEEL)return true;
    }
    if(m_selectionGesture) {
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE) {
            if(m_editor.editorMode()==GameEditor::EditorMode::Edit)
                m_editor.setMeshSelection({},m_selectionGesture->baseMeshSelection);
            else if(m_selectionGesture->mode==GameEditor::SelectionMode::Circle)
                m_editor.selectMany(m_selectionGesture->baseSelection);
            m_selectionRequest.reset();
            m_selectionGesture.reset();SDL_CaptureMouse(false);return true;
        }
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_QUIT) {
            m_selectionRequest.reset();
            m_selectionGesture.reset();SDL_CaptureMouse(false);
        } else if(event.type==SDL_EVENT_MOUSE_MOTION) {
            const gizmoMath::Vec2 point{event.motion.x,event.motion.y};
            m_selectionGesture->cursor=point;
            if(m_selectionGesture->mode==GameEditor::SelectionMode::Box)m_selectionGesture->points.back()=point;
            else if(m_selectionGesture->mode==GameEditor::SelectionMode::Circle) {
                auto samples=appendCircleSamples(*m_selectionGesture,point);
                if(m_editor.editorMode()==GameEditor::EditorMode::Edit)
                    applyMeshGesture(*m_selectionGesture,samples);
                else queueCircleSelection(std::move(samples));
            }
            else if(gizmoMath::segmentDistance(point,m_selectionGesture->points.back(),m_selectionGesture->points.back())>=
                3.0f)
                m_selectionGesture->points.push_back(point);
            return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) {
            const gizmoMath::Vec2 point{event.button.x,event.button.y};
            m_selectionGesture->cursor=point;
            if(m_selectionGesture->mode==GameEditor::SelectionMode::Box)m_selectionGesture->points.back()=point;
            else if(m_selectionGesture->mode==GameEditor::SelectionMode::Circle) {
                auto samples=appendCircleSamples(*m_selectionGesture,point);
                if(m_editor.editorMode()==GameEditor::EditorMode::Edit)
                    applyMeshGesture(*m_selectionGesture,samples);
                else queueCircleSelection(std::move(samples));
            }
            else m_selectionGesture->points.push_back(point);
            if(m_editor.editorMode()==GameEditor::EditorMode::Edit) {
                if(m_selectionGesture->mode!=GameEditor::SelectionMode::Circle)
                    applyMeshGesture(*m_selectionGesture,m_selectionGesture->points);
            } else if(m_selectionGesture->mode!=GameEditor::SelectionMode::Circle)
                m_selectionRequest=std::move(m_selectionGesture);
            m_selectionGesture.reset();
            SDL_CaptureMouse(false);return true;
        } else if(event.type==SDL_EVENT_MOUSE_WHEEL) {
            if(m_selectionGesture->mode==GameEditor::SelectionMode::Circle) {
                const float amount=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
                m_circleRadius=std::clamp(m_circleRadius*std::pow(1.12f,amount),8.0f,512.0f);
                m_selectionGesture->radius=m_circleRadius;
                if(m_editor.editorMode()==GameEditor::EditorMode::Edit)
                    applyMeshGesture(*m_selectionGesture,{m_selectionGesture->cursor});
                else queueCircleSelection({m_selectionGesture->cursor});
            }
            return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN)return true;
    }
    if(m_tweakPending) {
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE) {
            if(m_tweakDragging)m_editor.endTransformEdit(false);
            m_tweakPending=m_tweakDragging=false;SDL_CaptureMouse(false);return true;
        }
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_QUIT) {
            if(m_tweakDragging)m_editor.endTransformEdit(false);
            m_tweakPending=m_tweakDragging=false;SDL_CaptureMouse(false);
        } else if(event.type==SDL_EVENT_MOUSE_MOTION) {
            const gizmoMath::Vec2 point{event.motion.x,event.motion.y};
            if(!m_tweakDragging && std::hypot(point[0]-m_tweakStart[0],point[1]-m_tweakStart[1])>=5.0f) {
                if(const auto transform=m_editor.selectedTransform();transform && m_editor.beginTransformEdit()) {
                    m_tweakOriginal=*transform;m_tweakDragging=true;
                }
            }
            if(m_tweakDragging) {
                const auto viewport=m_editor.viewport();
                const auto normal=gizmoMath::rayAt({viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f},m_camera,viewport).direction;
                const auto start=gizmoMath::plane(gizmoMath::rayAt(m_tweakStart,m_camera,viewport),m_tweakOriginal.position,normal);
                const auto current=gizmoMath::plane(gizmoMath::rayAt(point,m_camera,viewport),m_tweakOriginal.position,normal);
                if(start && current) {
                    auto moved=m_tweakOriginal;
                    for(int axis=0;axis<3;++axis)moved.position[axis]+=(*current)[axis]-(*start)[axis];
                    m_editor.previewTransform(moved);
                }
            }
            return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) {
            if(m_tweakDragging)m_editor.endTransformEdit(true);
            m_tweakPending=m_tweakDragging=false;SDL_CaptureMouse(false);return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_WHEEL)return true;
    }
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_QUIT)releaseGameInput();
    if(event.type==SDL_EVENT_KEY_UP && event.key.scancode<SDL_SCANCODE_COUNT)m_gameKeys[event.key.scancode]=false;
    if(m_gizmo.dragging() && !m_editor.editingTransform())finishTransform(false);
    if(m_gizmo.dragging()) {
        if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) {
            const bool snap=m_editor.snapping() || (SDL_GetModState()&(SDL_KMOD_CTRL|SDL_KMOD_GUI));
            m_editor.previewTransform(m_gizmo.drag(event.button.x,event.button.y,snap,m_editor.snapSteps()));finishTransform(true);return true;
        }
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_RESIZED) {
            finishTransform(false);
        } else if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE) {
            finishTransform(false);return true;
        } else if(event.type==SDL_EVENT_MOUSE_MOTION) {
            const bool snap=m_editor.snapping() || (SDL_GetModState()&(SDL_KMOD_CTRL|SDL_KMOD_GUI));
            m_editor.previewTransform(m_gizmo.drag(event.motion.x,event.motion.y,snap,m_editor.snapSteps()));return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP
            || event.type==SDL_EVENT_MOUSE_WHEEL || event.type==SDL_EVENT_KEY_DOWN
            || event.type==SDL_EVENT_KEY_UP || event.type==SDL_EVENT_TEXT_INPUT || event.type==SDL_EVENT_DROP_FILE)return true;
    }
    // A UI drag owns input even when it crosses the live Scene viewport.
    if(document.hasPointerCapture()) {
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_RESIZED || event.type==SDL_EVENT_QUIT) {
            document.cancelInput();SDL_CaptureMouse(false);
        } else if(event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_UP
            || event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
            m_input.event(document,m_window,event);
            if(!document.hasPointerCapture())SDL_CaptureMouse(false);
            m_hoverHandle=-1;return true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_WHEEL)return true;
    }
    if(document.hasPopup()) {
        if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP || event.type==SDL_EVENT_MOUSE_WHEEL) {
            m_input.event(document,m_window,event);return true;
        }
        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_MIDDLE) {
            document.closePopup();return true;
        }
    }
    // Finish an active drag even when the pointer leaves the viewport. While
    // captured, navigation must not leak mouse or key events into UI widgets.
    if (m_camera.dragging()) {
        if (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_MIDDLE) {
            finishNavigation(); return true;
        }
        if (event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_QUIT) {
            finishNavigation(false,false);
        } else if (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE) {
            finishNavigation(true); return true;
        } else if (event.type==SDL_EVENT_MOUSE_MOTION) {
            m_camera.drag(event.motion.xrel,event.motion.yrel,m_editor.viewport().height); return true;
        } else if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP
            || event.type==SDL_EVENT_MOUSE_WHEEL || event.type==SDL_EVENT_KEY_DOWN
            || event.type==SDL_EVENT_KEY_UP || event.type==SDL_EVENT_TEXT_INPUT) return true;
    }
    const bool gamePointer=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && m_editor.gameViewport().contains(event.button.x,event.button.y);
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && !gamePointer)releaseGameInput();
    if(gamePointer && !document.hasPopup()) {
        document.cancelInput();m_input.sync(document,m_window);
        m_gameFocused=m_editor.playing();
        if(m_gameFocused && event.button.button==SDL_BUTTON_RIGHT) {
            m_gameLook=true;SDL_CaptureMouse(true);SDL_SetWindowRelativeMouseMode(m_window,true);
        }
        return true;
    }
    if(gameFocused()) {
        if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
            if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE){releaseGameInput();return true;}
            if(event.key.scancode<SDL_SCANCODE_COUNT)m_gameKeys[event.key.scancode]=event.type==SDL_EVENT_KEY_DOWN;
            return true;
        }
        if(event.type==SDL_EVENT_TEXT_INPUT)return true;
        if(event.type==SDL_EVENT_MOUSE_WHEEL && (m_gameLook || m_editor.gameViewport().contains(event.wheel.mouse_x,event.wheel.mouse_y)))return true;
        if(event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
            if(event.button.button==SDL_BUTTON_RIGHT){m_gameLook=false;SDL_CaptureMouse(false);SDL_SetWindowRelativeMouseMode(m_window,false);}
            return true;
        }
        if(event.type==SDL_EVENT_MOUSE_MOTION && (m_gameLook || m_editor.gameViewport().contains(event.motion.x,event.motion.y))) {
            document.pointerLeave();m_input.sync(document,m_window);
            if(m_gameLook && m_editor.playState()==GameEditor::PlayState::Playing) {
                ViewportCamera pose(m_gameCamera);
                const float yaw=pose.yaw()+event.motion.xrel*.0025f;
                const float pitch=std::clamp(pose.pitch()-event.motion.yrel*.0025f,-1.50f,1.50f);
                const Vec3 direction{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
                for(int i=0;i<3;++i)m_gameCamera.target[i]=m_gameCamera.position[i]+direction[i];
            }
            return true;
        }
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN &&
        m_editor.viewportSidebarContains(event.button.x,event.button.y)) {
        m_input.event(document,m_window,event);return true;
    }
    if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_MIDDLE) {
        if (m_editor.viewport().contains(event.button.x,event.button.y)) {
            const auto mod=SDL_GetModState();
            document.cancelInput(); m_input.sync(document,m_window);m_hoverHandle=-1;
            m_camera.begin((mod & SDL_KMOD_SHIFT) ? ViewportCamera::Gesture::Pan
                : (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) ? ViewportCamera::Gesture::Zoom : ViewportCamera::Gesture::Orbit);
            m_dragX=event.button.x; m_dragY=event.button.y;
            SDL_CaptureMouse(true); SDL_SetWindowRelativeMouseMode(m_window,true);
        }
        return true;
    }
    if (event.type==SDL_EVENT_MOUSE_WHEEL && m_editor.viewport().contains(event.wheel.mouse_x,event.wheel.mouse_y)) {
        if(m_editor.viewportSidebarContains(event.wheel.mouse_x,event.wheel.mouse_y))
            return m_input.event(document,m_window,event);
        if(m_editor.tool()==TransformTool::Select && m_editor.selectionMode()==GameEditor::SelectionMode::Circle &&
            !document.hasPopup() && !document.hasPointerCapture()) {
            const float amount=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
            m_circleRadius=std::clamp(m_circleRadius*std::pow(1.12f,amount),8.0f,512.0f);
            return true;
        }
        m_camera.wheel(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
        return true;
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_RIGHT
        && (SDL_GetModState()&SDL_KMOD_SHIFT) && !document.hasPopup()
        && m_editor.viewport().contains(event.button.x,event.button.y)) {
        if(const auto position=m_editor.placementPoint(event.button.x,event.button.y))m_editor.setViewportCursor(*position);
        return true;
    }
    if(event.type==SDL_EVENT_DROP_FILE) {
        if(event.drop.data) {
            if(m_editor.viewport().contains(event.drop.x,event.drop.y))m_editor.placeModel(event.drop.data,event.drop.x,event.drop.y);
            else if(!m_editor.gameViewport().contains(event.drop.x,event.drop.y))m_editor.addModel(event.drop.data);
        }
        return true;
    }
    if(event.type==SDL_EVENT_MOUSE_MOTION) {
        m_hoverHandle=document.hasPopup() || m_editor.viewportToolStripContains(event.motion.x,event.motion.y)
            || m_editor.viewportSidebarContains(event.motion.x,event.motion.y)
            ?-1:gizmoFrame().hit(event.motion.x,event.motion.y,m_camera,m_editor.viewport());
    } else if(event.type==SDL_EVENT_WINDOW_MOUSE_LEAVE || event.type==SDL_EVENT_WINDOW_FOCUS_LOST)m_hoverHandle=-1;
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT
        && !document.hasPopup() && m_editor.viewport().contains(event.button.x,event.button.y)) {
        if(m_editor.viewportToolStripContains(event.button.x,event.button.y)) {
            m_input.event(document,m_window,event);return true;
        }
        if(m_editor.viewportSidebarContains(event.button.x,event.button.y)) {
            m_input.event(document,m_window,event);return true;
        }
        if(m_editor.editorMode()==GameEditor::EditorMode::Edit) {
            const auto mode=m_editor.selectionMode();
            if(m_editor.tool()==TransformTool::Select &&
                (mode==GameEditor::SelectionMode::Box || mode==GameEditor::SelectionMode::Circle ||
                    mode==GameEditor::SelectionMode::Lasso)) {
                SelectionGesture gesture;gesture.mode=mode;gesture.radius=m_circleRadius;
                gesture.points.push_back({event.button.x,event.button.y});gesture.cursor=gesture.points.front();
                gesture.baseMeshSelection=m_editor.meshOverlay().selected;
                gesture.extend=(SDL_GetModState()&SDL_KMOD_SHIFT)!=0;
                gesture.toggle=(SDL_GetModState()&(SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0;
                if(mode==GameEditor::SelectionMode::Box)gesture.points.push_back(gesture.points.front());
                m_selectionGesture=std::move(gesture);SDL_CaptureMouse(true);
                if(mode==GameEditor::SelectionMode::Circle)
                    applyMeshGesture(*m_selectionGesture,{m_selectionGesture->cursor});
                return true;
            }
            const auto base=m_editor.meshOverlay().selected;
            const bool hit=m_editor.pickMeshElement(event.button.x,event.button.y,m_camera);
            const auto hits=m_editor.meshOverlay().selected;
            m_editor.setMeshSelection(base,hits,(SDL_GetModState()&SDL_KMOD_SHIFT)!=0,
                (SDL_GetModState()&(SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0);
            if(hit && m_editor.editSelectionCenter() &&
                (m_editor.tool()!=TransformTool::Select || mode==GameEditor::SelectionMode::Tweak)) {
                m_meshDragCenter=*m_editor.editSelectionCenter();
                m_meshPointerStart={event.button.x,event.button.y};
                m_meshPointerPending=true;SDL_CaptureMouse(true);
            }
            return true;
        }
        if(m_editor.cursorPlacement()) {
            if(const auto position=m_editor.placementPoint(event.button.x,event.button.y))m_editor.setViewportCursor(*position);
            return true;
        }
        if(m_editor.tool()==TransformTool::Select) {
            SelectionGesture gesture;gesture.mode=m_editor.selectionMode();
            gesture.radius=m_circleRadius;
            gesture.points.push_back({event.button.x,event.button.y});
            gesture.cursor=gesture.points.front();
            gesture.baseSelection=m_editor.selectedIds();
            gesture.strokeId=m_nextSelectionStrokeId++;
            gesture.extend=(SDL_GetModState() & SDL_KMOD_SHIFT)!=0;
            gesture.toggle=(SDL_GetModState() & (SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0;
            if(gesture.mode==GameEditor::SelectionMode::Select) m_selectionRequest=std::move(gesture);
            else if(gesture.mode==GameEditor::SelectionMode::Tweak) {
                m_selectionRequest=std::move(gesture);m_tweakStart={event.button.x,event.button.y};
                m_tweakPending=true;SDL_CaptureMouse(true);
            } else {
                if(gesture.mode==GameEditor::SelectionMode::Box)gesture.points.push_back(gesture.points.front());
                m_selectionGesture=std::move(gesture);SDL_CaptureMouse(true);
                if(m_selectionGesture->mode==GameEditor::SelectionMode::Circle)
                    queueCircleSelection({m_selectionGesture->cursor});
            }
            return true;
        }
        document.cancelInput();m_input.sync(document,m_window);
        const auto transform=m_editor.selectedTransform();
        if(transform && m_editor.gizmoVisible() && m_gizmo.begin(m_editor.tool(),*transform,m_camera,m_editor.viewport(),
            event.button.x,event.button.y,m_editor.localOrientation())) {
            if(m_editor.beginTransformEdit()){SDL_CaptureMouse(true);return true;}
            m_gizmo.end();
        }
    }
    if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const bool control=(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))!=0;
        if(control && event.key.key==SDLK_S) {
            if(document.wantsTextInput()) document.keyDown(ui::Key::Enter);
            m_editor.save(); return true;
        }
        if(!document.wantsTextInput()) {
            if(!control && event.key.key==SDLK_N && !m_editor.playing()) {
                m_editor.toggleViewportSidebar();return true;
            }
            if(!control && event.key.key==SDLK_TAB) {
                m_editor.setEditorMode(m_editor.editorMode()==GameEditor::EditorMode::Edit
                    ?GameEditor::EditorMode::Object:GameEditor::EditorMode::Edit);
                return true;
            }
            if(m_editor.editorMode()==GameEditor::EditorMode::Edit && !control) {
                if(event.key.key==SDLK_1)m_editor.setMeshSelectionMode(GameEditor::MeshSelectionMode::Vertex);
                else if(event.key.key==SDLK_2)m_editor.setMeshSelectionMode(GameEditor::MeshSelectionMode::Edge);
                else if(event.key.key==SDLK_3)m_editor.setMeshSelectionMode(GameEditor::MeshSelectionMode::Face);
                else if(event.key.key==SDLK_F || event.key.key==SDLK_KP_PERIOD) m_editor.frameSelection();
                else return true;
                return true;
            }
            if(control && event.key.key==SDLK_Z) { if(event.key.mod & SDL_KMOD_SHIFT) m_editor.redo(); else m_editor.undo(); return true; }
            if(control && event.key.key==SDLK_Y) { m_editor.redo(); return true; }
            if(control && event.key.key==SDLK_D) { m_editor.duplicate(); return true; }
            if(event.key.key==SDLK_DELETE) { m_editor.remove(); return true; }
            if(event.key.key==SDLK_F || event.key.key==SDLK_KP_PERIOD) { m_editor.frameSelection(); return true; }
            if(event.key.key==SDLK_C && (event.key.mod&SDL_KMOD_SHIFT)) {m_editor.setViewportCursor({});return true;}
            if(!control && !(event.key.mod&SDL_KMOD_ALT)) {
                if(event.key.key==SDLK_Q){m_editor.setTool(TransformTool::Select);m_hoverHandle=-1;return true;}
                if(event.key.key==SDLK_W){m_editor.setTool(TransformTool::Move);m_hoverHandle=-1;return true;}
                if(event.key.key==SDLK_E){m_editor.setTool(TransformTool::Rotate);m_hoverHandle=-1;return true;}
                if(event.key.key==SDLK_R){m_editor.setTool(TransformTool::Scale);m_hoverHandle=-1;return true;}
                if(event.key.key==SDLK_T){m_editor.setTool(TransformTool::Transform);m_hoverHandle=-1;return true;}
            }
        }
    }
    const bool consumed=m_input.event(document,m_window,event);
    if(consumed) return true;
    if((event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) && document.wantsTextInput()) return true;
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP)
        return !m_editor.viewport().contains(event.button.x,event.button.y);
    if(event.type==SDL_EVENT_MOUSE_WHEEL) {
        float x,y; SDL_GetMouseState(&x,&y); return !m_editor.viewport().contains(x,y);
    }
    return false;
}
bool SdlEditorHost::canClose() {
    m_editor.stop();update();
    if(m_editor.document().wantsTextInput()) m_editor.document().keyDown(ui::Key::Enter);
#ifdef __EMSCRIPTEN__
    // The browser's beforeunload prompt owns closing a tab.
    return !m_editor.dirty();
#else
    if(!m_editor.dirty()) return true;
    const SDL_MessageBoxButtonData buttons[]{
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"},
        {0,1,"Discard changes"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,2,"Save and close"}};
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING,m_window,"Unsaved scene",
        "Save your scene changes before closing Genesis?",3,buttons,nullptr};
    int choice=0; if(!SDL_ShowMessageBox(&box,&choice)) return false;
    return choice==1 || (choice==2 && m_editor.save());
#endif
}
void SdlEditorHost::update() {
    if(m_cameraRestoreRevision!=m_editor.cameraRestoreRevision()) {
        if(m_camera.dragging())finishNavigation(false,false);
        m_camera=ViewportCamera(m_editor.camera());m_cameraRestoreRevision=m_editor.cameraRestoreRevision();
    }
    if(m_editor.playing() && !m_wasPlaying)m_gameCamera=m_initialGameCamera;
    m_wasPlaying=m_editor.playing();
    if(!gameFocused() || m_editor.document().hasPopup() || m_editor.document().hasPointerCapture())releaseGameInput();
    auto settings=m_editor.camera();settings.position=m_camera.position();settings.target=m_camera.pivot();settings.fovDegrees=m_camera.fov();
    m_editor.setCamera(settings);
    m_input.sync(m_editor.document(),m_window);
#ifdef __EMSCRIPTEN__
    std::string imported;
    while(web::nextImport(imported)) m_editor.addModel(imported);
    if(web::takeExportRequest() && m_editor.save()) web::download(m_editor.scenePath());
    web::setDirty(m_editor.dirty());
#endif
}
void SdlEditorHost::releaseGameInput() {
    if(m_gameLook){SDL_SetWindowRelativeMouseMode(m_window,false);SDL_CaptureMouse(false);}
    m_gameLook=false;m_gameFocused=false;m_gameKeys.fill(false);
}
void SdlEditorHost::gameTick(float delta) {
    if(!gameFocused())return;
    const ViewportCamera pose(m_gameCamera);
    const float forward=float(m_gameKeys[SDL_SCANCODE_W])-float(m_gameKeys[SDL_SCANCODE_S]);
    const float right=float(m_gameKeys[SDL_SCANCODE_D])-float(m_gameKeys[SDL_SCANCODE_A]);
    const float up=float(m_gameKeys[SDL_SCANCODE_E])-float(m_gameKeys[SDL_SCANCODE_Q]);
    Vec3 movement{std::sin(pose.yaw())*forward+std::cos(pose.yaw())*right,up,std::cos(pose.yaw())*forward-std::sin(pose.yaw())*right};
    const float length=std::sqrt(movement[0]*movement[0]+movement[1]*movement[1]+movement[2]*movement[2]);
    if(length<1e-5f)return;
    const float distance=m_gameCamera.speed*delta/length*(m_gameKeys[SDL_SCANCODE_LSHIFT]?3.0f:1.0f);
    for(int i=0;i<3;++i){m_gameCamera.position[i]+=movement[i]*distance;m_gameCamera.target[i]+=movement[i]*distance;}
}
}
