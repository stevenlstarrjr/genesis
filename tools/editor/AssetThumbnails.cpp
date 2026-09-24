#include "editor/ProjectAssets.h"
#include "editor/ViewportMath.h"
#include <cgltf.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <algorithm>
#include <cmath>

namespace genesis::editor {
namespace {
constexpr int size=96;
uint32_t rgba(float r,float g,float b,uint8_t a=255) {
    const auto channel=[](float value){return uint32_t(std::clamp(value,0.0f,1.0f)*255+.5f);};
    return uint32_t(a)<<24|channel(r)<<16|channel(g)<<8|channel(b);
}
std::shared_ptr<ui::Surface> surface() {
    auto result=std::make_shared<ui::Surface>();result->width=result->height=size;
    result->pixels.resize(size*size);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)result->pixels[y*size+x]=((x/8+y/8)%2)?0xff363636:0xff3d3d3d;
    return result;
}
std::shared_ptr<const ui::Surface> imagePreview(const std::filesystem::path& path) {
    int width=0,height=0,channels=0;
    // BMP headers can report a negative height for top-down scanlines.
    if(!stbi_info(path.string().c_str(),&width,&height,&channels) || width<=0 || width>4096 || height==0 || height < -4096 || height>4096)return {};
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> data(stbi_load(path.string().c_str(),&width,&height,&channels,4),stbi_image_free);
    if(!data || width<=0 || height<=0 || width>4096 || height>4096)return {};
    auto result=surface();const float scale=std::min(float(size)/width,float(size)/height);
    const int w=std::max(1,int(width*scale)),h=std::max(1,int(height*scale));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const auto* pixel=data.get()+4*(std::min(height-1,int(y/scale))*width+std::min(width-1,int(x/scale)));
        auto& target=result->pixels[(y+(size-h)/2)*size+x+(size-w)/2];
        const float alpha=pixel[3]/255.0f;
        target=rgba((pixel[0]*alpha+((target>>16)&255)*(1-alpha))/255,
            (pixel[1]*alpha+((target>>8)&255)*(1-alpha))/255,(pixel[2]*alpha+(target&255)*(1-alpha))/255);
    }
    return result;
}
struct Triangle { std::array<Vec3,3> points;std::array<float,3> color; };
std::shared_ptr<const ui::Surface> modelPreview(const std::filesystem::path& path) {
    cgltf_options options{};cgltf_data* raw=nullptr;
    if(cgltf_parse_file(&options,path.string().c_str(),&raw)!=cgltf_result_success || !raw)return {};
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    uint64_t bytes=0;for(size_t i=0;i<raw->buffers_count;++i){bytes+=raw->buffers[i].size;if(bytes>128*1024*1024)return {};}
    if(cgltf_load_buffers(&options,raw,path.string().c_str())!=cgltf_result_success || cgltf_validate(raw)!=cgltf_result_success)return {};
    std::vector<Triangle> triangles;Bounds bounds;
    auto mesh=[&](const cgltf_mesh& mesh,const float* matrix) {
        for(size_t p=0;p<mesh.primitives_count;++p) {
            const auto& primitive=mesh.primitives[p];if(primitive.type!=cgltf_primitive_type_triangles)continue;
            const cgltf_accessor* positions=nullptr;
            for(size_t a=0;a<primitive.attributes_count;++a)if(primitive.attributes[a].type==cgltf_attribute_type_position)positions=primitive.attributes[a].data;
            if(!positions)continue;
            const size_t count=primitive.indices?primitive.indices->count:positions->count;
            if(count/3+triangles.size()>120000)return false;
            std::array<float,3> color{.58f,.64f,.71f};
            if(primitive.material && primitive.material->has_pbr_metallic_roughness) {
                const auto* factor=primitive.material->pbr_metallic_roughness.base_color_factor;
                for(int i=0;i<3;++i)color[i]=std::sqrt(std::clamp(factor[i],0.0f,1.0f));
            }
            for(size_t index=0;index+2<count;index+=3) {
                Triangle triangle;triangle.color=color;bool valid=true;
                for(int v=0;v<3;++v) {
                    const auto vertex=primitive.indices?cgltf_accessor_read_index(primitive.indices,index+v):index+v;
                    float point[3]{};
                    if(vertex>=positions->count || !cgltf_accessor_read_float(positions,vertex,point,3)){valid=false;break;}
                    for(int axis=0;axis<3;++axis) {
                        triangle.points[v][axis]=matrix[axis]*point[0]+matrix[axis+4]*point[1]+matrix[axis+8]*point[2]+matrix[axis+12];
                        if(!std::isfinite(triangle.points[v][axis]) || std::abs(triangle.points[v][axis])>1e8f)valid=false;
                    }
                }
                if(valid){for(const auto& point:triangle.points)bounds.include(point);triangles.push_back(triangle);}
            }
        }
        return true;
    };
    std::vector<const cgltf_node*> visited;
    auto node=[&](auto&& self,const cgltf_node* item,int depth)->bool {
        if(depth>64 || std::find(visited.begin(),visited.end(),item)!=visited.end())return false;
        visited.push_back(item);
        // Validate ancestry before calling cgltf's world-transform traversal.
        const auto* parent=item;size_t ancestry=0;while(parent && ancestry++<65)parent=parent->parent;if(parent)return false;
        float matrix[16];cgltf_node_transform_world(item,matrix);
        if(item->mesh && !mesh(*item->mesh,matrix))return false;
        for(size_t i=0;i<item->children_count;++i)if(!self(self,item->children[i],depth+1))return false;
        return true;
    };
    if(raw->scene){for(size_t i=0;i<raw->scene->nodes_count;++i)if(!node(node,raw->scene->nodes[i],0))return {};}
    else if(raw->nodes_count){for(size_t i=0;i<raw->nodes_count;++i)if(!raw->nodes[i].parent && !node(node,&raw->nodes[i],0))return {};}
    else {const float identity[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};for(size_t i=0;i<raw->meshes_count;++i)if(!mesh(raw->meshes[i],identity))return {};}
    if(triangles.empty() || !bounds.valid())return {};
    const auto center=bounds.center();
    auto project=[&](Vec3 p) {for(int i=0;i<3;++i)p[i]-=center[i];return Vec3{.8f*p[0]-.6f*p[2],-.24f*p[0]+.916515f*p[1]-.32f*p[2],.54991f*p[0]+.4f*p[1]+.733212f*p[2]};};
    Bounds screenBounds;for(const auto& triangle:triangles)for(auto p:triangle.points)screenBounds.include(project(p));
    const auto screenCenter=screenBounds.center();
    const float scale=82/std::max({screenBounds.maximum[0]-screenBounds.minimum[0],screenBounds.maximum[1]-screenBounds.minimum[1],.0001f});
    auto result=surface();std::vector<float> depth(size*size,-INFINITY);
    auto edge=[](Vec3 a,Vec3 b,float x,float y){return (x-a[0])*(b[1]-a[1])-(y-a[1])*(b[0]-a[0]);};
    for(const auto& triangle:triangles) {
        Vec3 a{},b{},normal{};for(int i=0;i<3;++i){a[i]=triangle.points[1][i]-triangle.points[0][i];b[i]=triangle.points[2][i]-triangle.points[0][i];}
        normal={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        const float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        const float light=.25f+.75f*std::abs((normal[0]*.4f+normal[1]*.8f+normal[2]*.447214f)/std::max(length,.000001f));
        const auto color=rgba(triangle.color[0]*light,triangle.color[1]*light,triangle.color[2]*light);
        auto points=triangle.points;
        for(auto& p:points){p=project(p);p[0]=(p[0]-screenCenter[0])*scale+48;p[1]=48-(p[1]-screenCenter[1])*scale;}
        const float area=edge(points[0],points[1],points[2][0],points[2][1]);if(std::abs(area)<.00001f)continue;
        const int x0=std::max(0,int(std::floor(std::min({points[0][0],points[1][0],points[2][0]})))),x1=std::min(size-1,int(std::ceil(std::max({points[0][0],points[1][0],points[2][0]}))));
        const int y0=std::max(0,int(std::floor(std::min({points[0][1],points[1][1],points[2][1]})))),y1=std::min(size-1,int(std::ceil(std::max({points[0][1],points[1][1],points[2][1]}))));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            const float u=edge(points[1],points[2],x+.5f,y+.5f)/area,v=edge(points[2],points[0],x+.5f,y+.5f)/area,w=1-u-v;
            if(u<0 || v<0 || w<0)continue;
            const float z=u*points[0][2]+v*points[1][2]+w*points[2][2];const int index=y*size+x;
            if(z>depth[index]){depth[index]=z;result->pixels[index]=color;}
        }
    }
    return result;
}
}
std::shared_ptr<const ui::Surface> AssetThumbnails::get(const ProjectAsset& asset) {
    if(auto found=m_cache.find(asset.path);found!=m_cache.end()){found->second.used=++m_clock;return found->second.image;}
    std::shared_ptr<const ui::Surface> image;
    if(asset.bytes<=32*1024*1024) {
        try {if(asset.model)image=modelPreview(asset.path);else if(asset.image)image=imagePreview(asset.path);}catch(const std::exception&){}
    }
    if(m_cache.size()>=128) {
        const auto oldest=std::min_element(m_cache.begin(),m_cache.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        m_cache.erase(oldest);
    }
    m_cache.emplace(asset.path,Entry{image,++m_clock});return image;
}
}
