#pragma once
#include "GameplayWorld.h"
#include "editor/TransformGizmo.h"
#include <filesystem>
#include <string>
#include <optional>
#include <vector>

namespace genesis::editor {
// A bounded CPU copy of the source glTF topology used for edit-mode picking.
// Vertex indices match Renderable::meshVertices and the renderer's load order.
class EditMesh {
public:
    struct Vertex {
        Vec3 base{},position{};
        std::array<float,16> node{};
    };
    std::vector<Vertex> vertices;
    std::vector<std::array<uint32_t,3>> faces;
    std::vector<std::array<uint32_t,2>> edges;
    bool load(const std::filesystem::path& path,std::string& error);
    void apply(const std::vector<gameplay::Renderable::MeshVertexEdit>& edits);
    Vec3 nodePosition(uint32_t index) const;
    Vec3 worldPosition(uint32_t index,const gameplay::Transform& transform) const;
    Vec3 sourceDelta(uint32_t index,const gameplay::Transform& transform,Vec3 worldDelta) const;
    std::vector<uint32_t> welded(const std::vector<uint32_t>& selected) const;
};
class MeshSurfaceIndex {
public:
    struct Hit {float distance;size_t face;};
    void rebuild(const std::vector<Vec3>& positions,const std::vector<std::array<uint32_t,3>>& faces);
    std::optional<Hit> nearest(const Ray& ray) const;
private:
    struct Triangle {Vec3 a,b,c,minimum,maximum,center;size_t face;};
    struct Node {Vec3 minimum,maximum;uint32_t first=0,count=0;int left=-1,right=-1;};
    std::vector<Triangle> m_triangles;
    std::vector<uint32_t> m_order;
    std::vector<Node> m_nodes;
    int build(uint32_t first,uint32_t count);
};
}
