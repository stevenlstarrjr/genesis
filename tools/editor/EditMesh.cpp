#include "editor/EditMesh.h"
#include <cgltf.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <numeric>
#include <limits>
#include <unordered_set>

namespace genesis::editor {
namespace {
using namespace gizmoMath;
Vec3 transform(const std::array<float,16>& matrix,Vec3 point,float w) {
    return {matrix[0]*point[0]+matrix[4]*point[1]+matrix[8]*point[2]+matrix[12]*w,
        matrix[1]*point[0]+matrix[5]*point[1]+matrix[9]*point[2]+matrix[13]*w,
        matrix[2]*point[0]+matrix[6]*point[1]+matrix[10]*point[2]+matrix[14]*w};
}
Vec3 inverseLinear(const std::array<float,16>& m,Vec3 value) {
    const Vec3 a{m[0],m[1],m[2]},b{m[4],m[5],m[6]},c{m[8],m[9],m[10]};
    const auto bc=cross(b,c),ca=cross(c,a),ab=cross(a,b);
    const float determinant=dot(a,bc);
    if(std::abs(determinant)<1e-8f)return {};
    return {dot(value,bc)/determinant,dot(value,ca)/determinant,dot(value,ab)/determinant};
}
}
bool EditMesh::load(const std::filesystem::path& path,std::string& error) {
    vertices.clear();faces.clear();edges.clear();
    cgltf_options options{};cgltf_data* raw=nullptr;
    if(cgltf_parse_file(&options,path.string().c_str(),&raw)!=cgltf_result_success || !raw) {
        error="Cannot parse mesh for Edit Mode";return false;
    }
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    if(cgltf_load_buffers(&options,raw,path.string().c_str())!=cgltf_result_success ||
        cgltf_validate(raw)!=cgltf_result_success) {error="Cannot read mesh buffers for Edit Mode";return false;}
    std::set<std::array<uint32_t,2>> uniqueEdges;
    for(size_t nodeIndex=0;nodeIndex<raw->nodes_count;++nodeIndex) {
        const auto& node=raw->nodes[nodeIndex];if(!node.mesh)continue;
        std::array<float,16> nodeMatrix{};cgltf_node_transform_world(&node,nodeMatrix.data());
        for(size_t primitiveIndex=0;primitiveIndex<node.mesh->primitives_count;++primitiveIndex) {
            const auto& primitive=node.mesh->primitives[primitiveIndex];
            if(primitive.type!=cgltf_primitive_type_triangles)continue;
            const cgltf_accessor* positions=nullptr;
            for(size_t i=0;i<primitive.attributes_count;++i)
                if(primitive.attributes[i].type==cgltf_attribute_type_position && primitive.attributes[i].index==0)
                    positions=primitive.attributes[i].data;
            if(!positions || !positions->count)continue;
            if(vertices.size()+positions->count>200000 || faces.size()+positions->count/3>400000) {
                error="Mesh exceeds Edit Mode topology limit";vertices.clear();faces.clear();return false;
            }
            const uint32_t first=uint32_t(vertices.size());
            for(size_t i=0;i<positions->count;++i) {
                Vertex vertex;vertex.node=nodeMatrix;
                if(!cgltf_accessor_read_float(positions,i,vertex.base.data(),3)) {
                    error="Invalid mesh vertex";vertices.clear();faces.clear();return false;
                }
                vertex.position=vertex.base;vertices.push_back(vertex);
            }
            const size_t count=primitive.indices?primitive.indices->count:positions->count;
            for(size_t i=0;i+2<count;i+=3) {
                std::array<uint32_t,3> face{};bool valid=true;
                for(int corner=0;corner<3;++corner) {
                    const size_t index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i+corner):i+corner;
                    if(index>=positions->count){valid=false;break;}
                    face[corner]=first+uint32_t(index);
                }
                if(!valid)continue;
                faces.push_back(face);
                for(int corner=0;corner<3;++corner) {
                    auto a=face[corner],b=face[(corner+1)%3];if(a>b)std::swap(a,b);
                    uniqueEdges.insert({a,b});
                }
            }
        }
    }
    edges.assign(uniqueEdges.begin(),uniqueEdges.end());
    if(vertices.empty() || faces.empty()){error="Mesh has no editable triangles";return false;}
    return true;
}
void EditMesh::apply(const std::vector<gameplay::Renderable::MeshVertexEdit>& edits) {
    for(auto& vertex:vertices)vertex.position=vertex.base;
    for(const auto& edit:edits)if(edit.index<vertices.size())vertices[edit.index].position=edit.position;
}
Vec3 EditMesh::nodePosition(uint32_t index) const {return transform(vertices.at(index).node,vertices[index].position,1);}
Vec3 EditMesh::worldPosition(uint32_t index,const gameplay::Transform& t) const {
    const auto point=nodePosition(index);
    const auto axes=basis(t.rotation);
    auto world=Vec3(t.position);
    for(int axis=0;axis<3;++axis)world=add(world,mul(axes[axis],point[axis]*t.scale[axis]));
    return world;
}
Vec3 EditMesh::sourceDelta(uint32_t index,const gameplay::Transform& t,Vec3 worldDelta) const {
    const auto axes=basis(t.rotation);
    Vec3 entityDelta{};
    for(int axis=0;axis<3;++axis)entityDelta[axis]=dot(worldDelta,axes[axis])/t.scale[axis];
    return inverseLinear(vertices.at(index).node,entityDelta);
}
std::vector<uint32_t> EditMesh::welded(const std::vector<uint32_t>& selected) const {
    struct Key {int64_t x,y,z;bool operator==(const Key&)const=default;};
    struct Hash {size_t operator()(const Key& key) const {
        return (uint64_t(key.x)*73856093ull)^(uint64_t(key.y)*19349663ull)^(uint64_t(key.z)*83492791ull);
    }};
    const auto key=[&](uint32_t index) {
        const auto point=nodePosition(index);
        return Key{int64_t(std::llround(point[0]*100000.0)),int64_t(std::llround(point[1]*100000.0)),
            int64_t(std::llround(point[2]*100000.0))};
    };
    std::unordered_set<Key,Hash> locations;
    for(uint32_t index:selected)if(index<vertices.size())locations.insert(key(index));
    std::vector<uint32_t> result;
    for(uint32_t index=0;index<vertices.size();++index)if(locations.contains(key(index)))result.push_back(index);
    return result;
}
void MeshSurfaceIndex::rebuild(const std::vector<Vec3>& positions,
    const std::vector<std::array<uint32_t,3>>& faces) {
    m_triangles.clear();m_order.clear();m_nodes.clear();m_triangles.reserve(faces.size());
    for(size_t i=0;i<faces.size();++i) {
        const auto& face=faces[i];
        if(face[0]>=positions.size() || face[1]>=positions.size() || face[2]>=positions.size())continue;
        Triangle triangle;triangle.a=positions[face[0]];triangle.b=positions[face[1]];
        triangle.c=positions[face[2]];triangle.face=i;
        for(int axis=0;axis<3;++axis) {
            triangle.minimum[axis]=std::min({triangle.a[axis],triangle.b[axis],triangle.c[axis]});
            triangle.maximum[axis]=std::max({triangle.a[axis],triangle.b[axis],triangle.c[axis]});
            triangle.center[axis]=(triangle.a[axis]+triangle.b[axis]+triangle.c[axis])/3;
        }
        m_triangles.push_back(triangle);
    }
    m_order.resize(m_triangles.size());std::iota(m_order.begin(),m_order.end(),0);
    if(!m_order.empty())build(0,uint32_t(m_order.size()));
}
int MeshSurfaceIndex::build(uint32_t first,uint32_t count) {
    Node node;node.first=first;node.count=count;
    node.minimum={INFINITY,INFINITY,INFINITY};node.maximum={-INFINITY,-INFINITY,-INFINITY};
    Vec3 centerMin=node.minimum,centerMax=node.maximum;
    for(uint32_t i=first;i<first+count;++i) {
        const auto& triangle=m_triangles[m_order[i]];
        for(int axis=0;axis<3;++axis) {
            node.minimum[axis]=std::min(node.minimum[axis],triangle.minimum[axis]);
            node.maximum[axis]=std::max(node.maximum[axis],triangle.maximum[axis]);
            centerMin[axis]=std::min(centerMin[axis],triangle.center[axis]);
            centerMax[axis]=std::max(centerMax[axis],triangle.center[axis]);
        }
    }
    const int index=int(m_nodes.size());m_nodes.push_back(node);
    if(count<=8)return index;
    int axis=0;
    for(int i=1;i<3;++i)if(centerMax[i]-centerMin[i]>centerMax[axis]-centerMin[axis])axis=i;
    const uint32_t middle=first+count/2;
    std::nth_element(m_order.begin()+first,m_order.begin()+middle,m_order.begin()+first+count,
        [&](uint32_t a,uint32_t b){return m_triangles[a].center[axis]<m_triangles[b].center[axis];});
    const int left=build(first,middle-first),right=build(middle,first+count-middle);
    m_nodes[index].left=left;m_nodes[index].right=right;m_nodes[index].count=0;
    return index;
}
std::optional<MeshSurfaceIndex::Hit> MeshSurfaceIndex::nearest(const Ray& ray) const {
    if(m_nodes.empty())return {};
    Hit nearest{INFINITY,0};
    std::vector<int> stack{0};
    while(!stack.empty()) {
        const int index=stack.back();stack.pop_back();const auto& node=m_nodes[index];
        float near=0,far=nearest.distance;
        for(int axis=0;axis<3;++axis) {
            if(std::abs(ray.direction[axis])<1e-9f) {
                if(ray.origin[axis]<node.minimum[axis] || ray.origin[axis]>node.maximum[axis]){near=INFINITY;break;}
            } else {
                float a=(node.minimum[axis]-ray.origin[axis])/ray.direction[axis];
                float b=(node.maximum[axis]-ray.origin[axis])/ray.direction[axis];
                if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);
            }
        }
        if(near>far)continue;
        if(node.left>=0){stack.push_back(node.left);stack.push_back(node.right);continue;}
        for(uint32_t i=node.first;i<node.first+node.count;++i) {
            const auto& triangle=m_triangles[m_order[i]];
            const auto ab=gizmoMath::sub(triangle.b,triangle.a),ac=gizmoMath::sub(triangle.c,triangle.a);
            const auto p=gizmoMath::cross(ray.direction,ac);const float determinant=gizmoMath::dot(ab,p);
            if(std::abs(determinant)<1e-8f)continue;
            const float inverse=1.0f/determinant;
            const auto offset=gizmoMath::sub(ray.origin,triangle.a);
            const float u=gizmoMath::dot(offset,p)*inverse;
            if(u<0 || u>1)continue;
            const auto q=gizmoMath::cross(offset,ab);
            const float v=gizmoMath::dot(ray.direction,q)*inverse;
            if(v<0 || u+v>1)continue;
            const float distance=gizmoMath::dot(ac,q)*inverse;
            if(distance>1e-5f && distance<nearest.distance)nearest={distance,triangle.face};
        }
    }
    if(!std::isfinite(nearest.distance))return {};
    return nearest;
}
}
