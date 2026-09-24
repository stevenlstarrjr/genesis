#include "editor/GameEditor.h"
#include "ui/Theme.h"
#include "editor/SelectionGeometry.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace genesis::editor {
using namespace gizmoMath;
bool GameEditor::setEditorMode(EditorMode mode) {
    if(mode==m_editorMode)return true;
    if(mode==EditorMode::Object) {
        endMeshDrag(false);m_editorMode=mode;m_editMesh.reset();m_meshSurface.reset();m_editSelectedVertices.clear();
        m_meshOverlay={};m_meshModeMount->setVisible(false);
        m_modeDropdown->select(0);status("Object Mode");return true;
    }
    gameplay::EntitySnapshot entity;
    if(playing() || !m_selected || !m_world.snapshot(*m_selected,entity) || !entity.renderable) {
        m_modeDropdown->select(0);status("Select a mesh object before entering Edit Mode.");return false;
    }
    auto mesh=std::make_unique<EditMesh>();std::string error;
    if(!mesh->load(entity.renderable->path,error)) {
        m_modeDropdown->select(0);status(error);return false;
    }
    mesh->apply(entity.renderable->meshVertices);
    endTransformEdit(true);m_editMesh=std::move(mesh);m_meshSurface=std::make_unique<MeshSurfaceIndex>();m_editorMode=mode;
    m_editSelectedVertices.clear();m_meshModeMount->setVisible(true);
    m_modeDropdown->select(1);setMeshSelectionMode(MeshSelectionMode::Vertex);
    refreshMeshOverlay();status("Edit Mode | Select vertices, edges, or faces; drag to move.");return true;
}
void GameEditor::setMeshSelectionMode(MeshSelectionMode mode) {
    m_meshSelectionMode=mode;m_editSelectedVertices.clear();
    for(size_t i=0;i<m_meshModeButtons.size();++i)if(m_meshModeButtons[i]) {
        auto style=m_meshModeButtons[i]->style();
        style.background=i==size_t(mode)?ui::theme::selection:ui::Color{48,48,48,255};
        m_meshModeButtons[i]->setStyle(style);
    }
    refreshMeshOverlay();
}
void GameEditor::refreshMeshOverlay() {
    if(!m_editMesh || !m_selected){m_meshOverlay={};return;}
    gameplay::EntitySnapshot entity;if(!m_world.snapshot(*m_selected,entity))return;
    std::vector<Vec3> positions;positions.reserve(m_editMesh->vertices.size());
    for(uint32_t i=0;i<m_editMesh->vertices.size();++i)
        positions.push_back(m_editMesh->worldPosition(i,entity.transform));
    const bool geometryChanged=positions!=m_meshOverlay.vertices || m_meshOverlay.faces!=m_editMesh->faces;
    m_meshOverlay.vertices=std::move(positions);
    m_meshOverlay.edges=m_editMesh->edges;
    m_meshOverlay.faces=m_editMesh->faces;
    m_meshOverlay.selected=m_editSelectedVertices;
    if(geometryChanged && m_meshSurface)m_meshSurface->rebuild(m_meshOverlay.vertices,m_meshOverlay.faces);
}
bool GameEditor::pickMeshElement(float x,float y,const ViewportCamera& camera) {
    if(m_editorMode!=EditorMode::Edit || !m_editMesh || !viewport().contains(x,y))return false;
    const auto& points=m_meshOverlay.vertices;const auto area=viewport();
    const Vec2 mouse{x,y};float best=INFINITY;std::vector<uint32_t> picked;
    if(m_meshSelectionMode==MeshSelectionMode::Face) {
        if(const auto hit=m_meshSurface->nearest(rayAt(mouse,camera,area))) {
            const auto& face=m_meshOverlay.faces[hit->face];picked={face[0],face[1],face[2]};
        }
    } else {
        struct Candidate {float score;Vec3 world;Vec2 screen;std::array<uint32_t,2> indices;int count;};
        std::vector<Candidate> candidates;
        if(m_meshSelectionMode==MeshSelectionMode::Vertex) {
            for(uint32_t i=0;i<points.size();++i)if(const auto screen=project(points[i],camera,area)) {
                const float distance=std::hypot(mouse[0]-(*screen)[0],mouse[1]-(*screen)[1]);
                if(distance>9)continue;
                const float depth=std::sqrt(dot(sub(points[i],camera.position()),sub(points[i],camera.position())));
                candidates.push_back({distance+depth*.001f,points[i],*screen,{i,0},1});
            }
        } else {
            for(const auto& edge:m_meshOverlay.edges) {
                const auto a=project(points[edge[0]],camera,area),b=project(points[edge[1]],camera,area);
                if(!a || !b)continue;
                const float dx=(*b)[0]-(*a)[0],dy=(*b)[1]-(*a)[1];
                const float span=dx*dx+dy*dy;
                const float t=span>0?std::clamp(((x-(*a)[0])*dx+(y-(*a)[1])*dy)/span,0.0f,1.0f):0;
                const Vec2 screen{(*a)[0]+t*dx,(*a)[1]+t*dy};
                const float distance=std::hypot(x-screen[0],y-screen[1]);
                if(distance>7)continue;
                const auto world=add(mul(points[edge[0]],1-t),mul(points[edge[1]],t));
                const float depth=std::sqrt(dot(sub(world,camera.position()),sub(world,camera.position())));
                candidates.push_back({distance+depth*.001f,world,screen,edge,2});
            }
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score<b.score;});
        for(const auto& candidate:candidates) {
            const auto ray=rayAt(candidate.screen,camera,area);
            const auto surface=m_meshSurface->nearest(ray);
            const float depth=dot(sub(candidate.world,ray.origin),ray.direction);
            if(!surface || depth>surface->distance+std::max(.025f,surface->distance*.01f))continue;
            picked={candidate.indices[0]};
            if(candidate.count==2)picked.push_back(candidate.indices[1]);
            break;
        }
    }
    m_editSelectedVertices=picked.empty()?std::vector<uint32_t>{}:m_editMesh->welded(picked);
    refreshMeshOverlay();return !picked.empty();
}
std::vector<uint32_t> GameEditor::meshRegionHits(SelectionMode mode,
    const std::vector<Vec2>& points,float radius,const ViewportCamera& camera) const {
    if(m_editorMode!=EditorMode::Edit || !m_meshSurface || points.empty())return {};
    std::vector<selection::Polygon> regions;
    if(mode==SelectionMode::Box && points.size()>=2)
        regions.push_back(selection::rectangle(points.front(),points.back()));
    else if(mode==SelectionMode::Circle)
        for(const auto& point:points)regions.push_back(selection::circle(point,radius));
    else if(mode==SelectionMode::Lasso && points.size()>=3)regions.push_back(points);
    if(regions.empty())return {};
    const auto& mesh=m_meshOverlay;const auto area=viewport();
    const auto visible=[&](Vec3 world,Vec2 screen) {
        const auto ray=rayAt(screen,camera,area);
        const auto surface=m_meshSurface->nearest(ray);
        if(!surface)return false;
        const float distance=dot(sub(world,ray.origin),ray.direction);
        return distance<=surface->distance+std::max(.025f,surface->distance*.01f);
    };
    std::vector<uint32_t> hits;
    if(m_meshSelectionMode==MeshSelectionMode::Vertex) {
        for(uint32_t i=0;i<mesh.vertices.size();++i)if(const auto screen=project(mesh.vertices[i],camera,area))
            for(const auto& region:regions)if(selection::contains(region,*screen) && visible(mesh.vertices[i],*screen)) {
                hits.push_back(i);break;
            }
    } else if(m_meshSelectionMode==MeshSelectionMode::Edge) {
        for(const auto& edge:mesh.edges) {
            const auto a=project(mesh.vertices[edge[0]],camera,area);
            const auto b=project(mesh.vertices[edge[1]],camera,area);
            if(!a || !b)continue;
            const Vec2 middle{((*a)[0]+(*b)[0])*.5f,((*a)[1]+(*b)[1])*.5f};
            bool covered=false;
            for(const auto& region:regions) {
                if(selection::contains(region,middle) || selection::contains(region,*a) || selection::contains(region,*b))
                    covered=true;
                else for(size_t i=0;i<region.size();++i)
                    if(selection::segmentIntersects(*a,*b,region[i],region[(i+1)%region.size()])) {
                        covered=true;break;
                    }
                if(covered)break;
            }
            if(!covered)continue;
            const auto world=mul(add(mesh.vertices[edge[0]],mesh.vertices[edge[1]]),.5f);
            if(visible(world,middle)){hits.push_back(edge[0]);hits.push_back(edge[1]);}
        }
    } else {
        for(const auto& face:mesh.faces) {
            const auto a=project(mesh.vertices[face[0]],camera,area);
            const auto b=project(mesh.vertices[face[1]],camera,area);
            const auto c=project(mesh.vertices[face[2]],camera,area);
            if(!a || !b || !c)continue;
            const std::array<Vec2,3> triangle{*a,*b,*c};
            bool covered=false;
            for(const auto& region:regions)if(selection::overlapsTriangle(region,triangle)){covered=true;break;}
            if(!covered)continue;
            const auto world=mul(add(add(mesh.vertices[face[0]],mesh.vertices[face[1]]),mesh.vertices[face[2]]),1.0f/3);
            const Vec2 middle{((*a)[0]+(*b)[0]+(*c)[0])/3,((*a)[1]+(*b)[1]+(*c)[1])/3};
            if(visible(world,middle))hits.insert(hits.end(),face.begin(),face.end());
        }
    }
    if(hits.empty())return {};
    std::sort(hits.begin(),hits.end());hits.erase(std::unique(hits.begin(),hits.end()),hits.end());
    return m_editMesh->welded(hits);
}
void GameEditor::setMeshSelection(const std::vector<uint32_t>& base,
    const std::vector<uint32_t>& hits,bool extend,bool toggle) {
    if(m_editorMode!=EditorMode::Edit)return;
    std::vector<uint32_t> selected=extend || toggle?base:std::vector<uint32_t>{};
    for(uint32_t index:hits) {
        const auto found=std::find(selected.begin(),selected.end(),index);
        if(toggle){if(found==selected.end())selected.push_back(index);else selected.erase(found);}
        else if(found==selected.end())selected.push_back(index);
    }
    std::sort(selected.begin(),selected.end());
    m_editSelectedVertices=std::move(selected);m_meshOverlay.selected=m_editSelectedVertices;
}
std::optional<Vec3> GameEditor::editSelectionCenter() const {
    if(m_editSelectedVertices.empty() || m_meshOverlay.vertices.empty())return {};
    Vec3 center{};
    for(uint32_t index:m_editSelectedVertices)center=add(center,m_meshOverlay.vertices[index]);
    return mul(center,1.0f/float(m_editSelectedVertices.size()));
}
bool GameEditor::beginMeshDrag() {
    if(m_editorMode!=EditorMode::Edit || m_meshDragActive || !m_selected || m_editSelectedVertices.empty())return false;
    gameplay::EntitySnapshot entity;if(!m_world.snapshot(*m_selected,entity) || !entity.renderable)return false;
    m_meshDragBefore=capture();m_meshDragOriginal=entity.renderable->meshVertices;m_meshDragActive=true;return true;
}
void GameEditor::previewMeshDrag(Vec3 worldDelta) {
    if(!m_meshDragActive || !m_selected || !m_editMesh)return;
    gameplay::EntitySnapshot entity;if(!m_world.snapshot(*m_selected,entity) || !entity.renderable)return;
    std::map<uint32_t,gameplay::Renderable::MeshVertexEdit> entries;
    for(const auto& item:m_meshDragOriginal)entries[item.index]=item;
    for(uint32_t index:m_editSelectedVertices) {
        const auto found=entries.find(index);
        const auto original=found==entries.end()?m_editMesh->vertices[index].base:found->second.position;
        entries[index]={index,add(original,m_editMesh->sourceDelta(index,entity.transform,worldDelta))};
    }
    std::vector<gameplay::Renderable::MeshVertexEdit> result;result.reserve(entries.size());
    for(const auto& [index,item]:entries)result.push_back(item);
    m_world.setMeshVertices(*m_selected,result);m_editMesh->apply(result);refreshMeshOverlay();changed();
}
void GameEditor::endMeshDrag(bool commit) {
    if(!m_meshDragActive)return;
    gameplay::EntitySnapshot entity;
    const bool valid=m_selected && m_world.snapshot(*m_selected,entity) && entity.renderable;
    if(valid && (!commit || entity.renderable->meshVertices==m_meshDragOriginal)) {
        m_world.setMeshVertices(*m_selected,m_meshDragOriginal);
        if(m_editMesh)m_editMesh->apply(m_meshDragOriginal);
        refreshMeshOverlay();changed();
    } else if(valid && m_meshDragBefore) {
        m_undo.push_back(std::move(*m_meshDragBefore));
        if(m_undo.size()>100)m_undo.erase(m_undo.begin());
        m_redo.clear();status("Moved mesh selection");
    }
    m_meshDragBefore.reset();m_meshDragOriginal.clear();m_meshDragActive=false;
}
}
