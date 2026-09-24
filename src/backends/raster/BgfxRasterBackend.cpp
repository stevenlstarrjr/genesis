#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <bimg/bimg.h>
#include <bimg/encode.h>

#include "backends/raster/BgfxRasterBackend.h"
#include "backends/raster/BloomRenderer.h"
#include "backends/raster/EditorOverlay.h"
#include "backends/raster/BgfxUiRenderer.h"
#include "backends/raster/FullscreenTriangle.h"
#include "backends/raster/LightSourceRenderer.h"
#include "backends/raster/LightPatternLoader.h"
#include "backends/raster/OrbitCapture.h"
#include "backends/raster/HdriTexture.h"
#include "backends/raster/GltfTextures.h"
#include "materials/GltfTextureCoordinates.h"
#include "materials/TangentFrame.h"
#include "lighting/ProbeGridAtlas.h"
#include "GameplayWorld.h"
#include "ProjectLoader.h"
#include "SceneDocument.h"
#include "AuthoredLighting.h"
#include "editor/GameEditor.h"
#include "editor/SelectionGeometry.h"
#include "editor/SdlEditorHost.h"
#include "ScriptRuntime.h"
#include "DiffuseProbeGrid.h"
#include "ui/ProjectPrompt.h"
#include "ui/RendererHud.h"
#ifdef __EMSCRIPTEN__
#include "platform/web/BrowserHost.h"
#endif
#include <genesis/atmosphere/BrunetonAtmosphere.h>
#include <genesis/color/ColorPipeline.h>

#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <compiled/fs_pbr.h>
#include <compiled/fs_viewport.h>
#include <compiled/fs_shadow.h>
#include <compiled/fs_selection_mask.h>
#include <compiled/fs_selection_outline.h>
#include <compiled/fs_sky.h>
#include <compiled/fs_environment_prefilter.h>
#include <compiled/fs_probe_prefilter.h>
#include <compiled/fs_brdf_lut.h>
#include <compiled/fs_ao.h>
#include <compiled/fs_volumetric.h>
#include <compiled/fs_tonemap.h>
#include <compiled/fs_hud.h>
#include <compiled/cs_histogram_clear.h>
#include <compiled/cs_histogram.h>
#include <compiled/cs_auto_exposure.h>
#include <compiled/vs_pbr.h>
#include <compiled/vs_shadow.h>
#include <compiled/vs_selection_mask.h>
#include <compiled/vs_fullscreen.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// bgfx processes views in ascending order unless explicitly reordered.
// Render the shadow map before the camera consumes it in the same frame.
constexpr uint8_t kMaxEnvironmentMipCount = 9;
constexpr uint8_t kLocalProbeSlotCount = 2;
constexpr uint16_t kLightPatternSlotCount = 12;
constexpr uint16_t kLightPatternBaseLayer = 6 * kLocalProbeSlotCount;
constexpr uint16_t kBrdfView = 0;
constexpr uint16_t kEnvironmentView = kBrdfView + 1;
constexpr uint16_t kEnvironmentViewCount = 6 * kMaxEnvironmentMipCount;
constexpr uint8_t kPointShadowSlotCount = 4;
constexpr uint8_t kPointShadowTextureCount = 2;
constexpr uint16_t kPointShadowView = kEnvironmentView + kEnvironmentViewCount;
constexpr uint16_t kPointShadowViewCount = 6 * kPointShadowSlotCount;
constexpr uint16_t kLocalCaptureView = kPointShadowView + kPointShadowViewCount;
constexpr uint16_t kLocalCaptureViewCount = 6 * kLocalProbeSlotCount;
constexpr uint16_t kLocalPrefilterView = kLocalCaptureView + kLocalCaptureViewCount;
constexpr uint16_t kLocalPrefilterViewCount = 6 * kMaxEnvironmentMipCount * kLocalProbeSlotCount;
constexpr uint16_t kCascadeCount = 3;
constexpr uint16_t kLocalDiffuseView = kLocalPrefilterView + kLocalPrefilterViewCount;
constexpr uint16_t kLocalDiffuseViewCount = 6 * kLocalProbeSlotCount;
constexpr uint16_t kLocalAtlasCopyView = kLocalDiffuseView + kLocalDiffuseViewCount;
constexpr uint16_t kLocalAtlasCopyViewCount = kLocalProbeSlotCount;
constexpr uint16_t kShadowView = kLocalAtlasCopyView + kLocalAtlasCopyViewCount;
constexpr uint16_t kSkyView = kShadowView + kCascadeCount;
constexpr uint16_t kMainView = kSkyView + 1;
constexpr uint16_t kAoView = kMainView + 1;
constexpr uint16_t kVolumetricView = kAoView + 1;
constexpr uint16_t kBloomPrefilterView = kVolumetricView + 1;
constexpr uint16_t kHistogramClearView = kBloomPrefilterView + genesis::raster::BloomRenderer::viewCount;
constexpr uint16_t kHistogramView = kHistogramClearView + 1;
constexpr uint16_t kAutoExposureView = kHistogramView + 1;
constexpr uint16_t kTonemapView = kAutoExposureView + 1;
constexpr uint16_t kSelectionMaskView = kTonemapView + 1;
constexpr uint16_t kSelectionOutlineView = kSelectionMaskView + 1;
constexpr uint16_t kEditorView = kSelectionOutlineView + 1;
constexpr uint16_t kSelectionGestureView = kEditorView + 1;
constexpr uint16_t kViewStride = kSelectionGestureView + 1;
constexpr uint16_t kHudView = kViewStride * 2;
static_assert(kHudView < 512);
constexpr size_t kMaxJoints = 64;
// Slot 13 belongs to material AO; the packed grid atlas only needs slot 12.
constexpr std::array<uint8_t, 8> kEnvironmentTextureStages{7, 8, 9, 10, 11, 12, 14, 15};

struct QualityProfile {
    uint16_t shadowSize;
    uint16_t pointShadowSize;
    uint16_t environmentSize;
    uint8_t environmentMipCount;
    bgfx::BackbufferRatio::Enum effectsRatio;
    float effectsScale;
    int aoSamples;
    int contactSteps;
    int volumetricSteps;
};

QualityProfile qualityProfile(const std::string& name) {
    if (name == "low")
        return {512, 128, 64, 7, bgfx::BackbufferRatio::Quarter, 4.0f, 6, 4, 8};
    if (name == "high")
        return {2048, 512, 256, 9, bgfx::BackbufferRatio::Equal, 1.0f, 16, 12, 24};
    return {1024, 256, 128, 8, bgfx::BackbufferRatio::Half, 2.0f, 12, 8, 16};
}

enum class RenderLayer : uint32_t {
    World = 1u << 0,
    Dynamic = 1u << 1,
    Effects = 1u << 2,
    Overlay = 1u << 3,
};

constexpr uint32_t layerMask(RenderLayer layer) { return uint32_t(layer); }
constexpr uint32_t kGeometryLayers = layerMask(RenderLayer::World) | layerMask(RenderLayer::Dynamic);

class GenesisBgfxCallback final : public bgfx::CallbackI {
public:
    std::atomic_uint32_t screenshotsComplete{0};

    void fatal(const char* file, uint16_t line, bgfx::Fatal::Enum, const char* message) override {
        std::fprintf(stderr, "bgfx fatal %s:%u: %s\n", file, unsigned(line), message);
        std::abort();
    }
    void traceVargs(const char*, uint16_t, const char* format, va_list args) override {
#ifdef __EMSCRIPTEN__
        std::vfprintf(stdout,format,args);
#endif
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum format, const void* data, uint32_t, bool yflip) override {
        const std::string pngPath = std::string(path) + ".png";
        bx::FileWriter writer;
        bx::Error error;
        if (bx::open(&writer, pngPath.c_str(), false, &error)) {
            bimg::imageWritePng(&writer, width, height, pitch, data,
                bimg::TextureFormat::Enum(format), yflip, &error);
            bx::close(&writer);
            std::fprintf(stdout, "Screenshot saved: %s\n", pngPath.c_str());
        } else {
            std::fprintf(stderr, "Cannot save screenshot: %s\n", pngPath.c_str());
        }
        screenshotsComplete.fetch_add(1, std::memory_order_release);
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

static fs::path timestampedScreenshotPath() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream name;
    name << "genesis-" << std::put_time(&local, "%Y%m%d-%H%M%S");
    const fs::path directory = fs::path(GENESIS_ROOT) / "artifacts" / "screenshots";
    fs::create_directories(directory);
    return directory / name.str();
}

struct Vertex {
    float position[3]{};
    float normal[3]{0.0f, 1.0f, 0.0f};
    float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
    // Import-time material slots: base color, metal/rough, normal, emission, AO.
    float materialUv[genesis::materials::materialTextureCount][2]{};
    uint8_t joints[4]{};
    float weights[4]{1.0f, 0.0f, 0.0f, 0.0f};

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout value;
        value.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord3, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
            .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
            .end();
        return value;
    }
};

struct Material {
    float base[4]{1, 1, 1, 1};
    float params[4]{0, 1, 1, 0.5f};
    float emissive[4]{0, 0, 0, 0};
    float flags[4]{0, 0, 0, 0};
    float occlusion[4]{0, 0, 0, 0}; // strength; zero is the no-texture fallback
    bool doubleSided = false;
    bool blended = false;
    bgfx::TextureHandle baseTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mrTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normalTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emissiveTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle occlusionTexture = BGFX_INVALID_HANDLE;
};

struct Primitive {
    bgfx::DynamicVertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    mutable bgfx::IndexBufferHandle wireIndices = BGFX_INVALID_HANDLE;
    size_t nodeIndex = 0;
    size_t sourcePrimitiveIndex = 0;
    size_t materialIndex = 0;
    std::array<bool, genesis::materials::materialTextureCount> hasTextureUv{};
    std::vector<uint32_t> shadowSourceIndices;
    uint32_t firstVertex = 0;
    std::vector<Vertex> baseVertices;
    std::vector<uint32_t> editIndices;
};

struct EmissiveCluster {
    size_t nodeIndex = 0;
    size_t primitiveIndex = 0;
    std::array<float, 3> center{};
    std::array<float, 3> areaNormal{};
    std::array<float, 3> radiance{};
    std::array<float, 3> weightedTexture{};
    std::array<float, 9> secondMoment{};
    std::array<float, 3> minimum{INFINITY, INFINITY, INFINITY};
    std::array<float, 3> maximum{-INFINITY, -INFINITY, -INFINITY};
    float area = 0.0f;
    bool doubleSided = false;
    std::vector<uint32_t> sourceTriangles;
};

struct EmissiveFace {
    uint32_t sourceTriangle = 0;
    std::array<float, 3> center{};
    std::array<float, 3> areaNormal{};
    std::array<float, 3> textureColor{};
    std::array<float, 9> secondMoment{};
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    float area = 0.0f;
};

class SceneModel;
struct EmissiveLightSample {
    const SceneModel* sourceModel = nullptr;
    size_t clusterIndex = 0;
    size_t primitiveIndex = 0;
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 3> tangent{};
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    float captureOffset = 0.0f;
    std::array<float, 3> radiance{};
    float area = 0.0f;
    float score = 0.0f;
    bool doubleSided = false;
};

static bgfx::ShaderHandle shader(const uint8_t* bytes, uint32_t size) {
    return bgfx::createShader(bgfx::copy(bytes, size));
}

static float radians(float degrees) { return degrees * 0.01745329251994329577f; }

using genesis::raster::fullscreenTriangle;

static void entityMatrix(float* result, const genesis::gameplay::Transform& t) {
    bx::mtxSRT(result, t.scale[0], t.scale[1], t.scale[2],
        radians(t.rotation[0]), radians(t.rotation[1]), radians(t.rotation[2]),
        t.position[0], t.position[1], t.position[2]);
}

#ifdef GENESIS_WITH_PHYSX_PBD
static bool loadColliderMesh(const genesis::gameplay::EntitySnapshot& entity,
    genesis::physics::ColliderMesh& mesh,std::string& error) {
    if(!entity.renderable || !entity.renderable->visible)return true;
    mesh.entity=entity.id;mesh.initialTransform=entity.transform;mesh.kinematic=entity.renderable->particleKinematic;
    const auto path=fs::path(entity.renderable->path);
    cgltf_options options{};cgltf_data* raw{};
    if(cgltf_parse_file(&options,path.string().c_str(),&raw)!=cgltf_result_success || !raw){
        error="Cannot parse collider model: "+path.string();return false;
    }
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,&cgltf_free);
    if(cgltf_load_buffers(&options,raw,path.string().c_str())!=cgltf_result_success || cgltf_validate(raw)!=cgltf_result_success){
        error="Cannot load collider geometry: "+path.string();return false;
    }
    float entityTransform[16];entityMatrix(entityTransform,entity.transform);
    for(size_t nodeIndex=0;nodeIndex<raw->nodes_count;++nodeIndex){
        const auto& node=raw->nodes[nodeIndex];
        // Only nodes with a stable world transform can provide static collision.
        if(!node.mesh || node.skin)continue;
        bool animated=false;
        for(const cgltf_node* ancestor=&node;ancestor && !animated;ancestor=ancestor->parent)
            for(size_t animationIndex=0;animationIndex<raw->animations_count && !animated;++animationIndex)
                for(size_t channelIndex=0;channelIndex<raw->animations[animationIndex].channels_count;++channelIndex)
                    if(raw->animations[animationIndex].channels[channelIndex].target_node==ancestor){animated=true;break;}
        if(animated)continue;
        float nodeTransform[16],world[16];cgltf_node_transform_world(&node,nodeTransform);
        bx::mtxMul(world,nodeTransform,entityTransform);
        for(size_t primitiveIndex=0;primitiveIndex<node.mesh->primitives_count;++primitiveIndex){
            const auto& primitive=node.mesh->primitives[primitiveIndex];
            if(primitive.type!=cgltf_primitive_type_triangles)continue;
            const cgltf_accessor* accessor{};
            for(size_t attribute=0;attribute<primitive.attributes_count;++attribute)
                if(primitive.attributes[attribute].type==cgltf_attribute_type_position){accessor=primitive.attributes[attribute].data;break;}
            if(!accessor || accessor->count<3)continue;
            const unsigned offset=unsigned(mesh.positions.size());
            for(size_t i=0;i<accessor->count;++i){
                float position[4]{0,0,0,1},transformed[4];
                cgltf_accessor_read_float(accessor,i,position,3);bx::vec4MulMtx(transformed,position,world);
                mesh.positions.push_back({transformed[0],transformed[1],transformed[2]});
            }
            const size_t count=primitive.indices?primitive.indices->count:accessor->count;
            for(size_t i=0;i+2<count;i+=3){
                const auto index=[&](size_t slot){return unsigned(primitive.indices?cgltf_accessor_read_index(primitive.indices,slot):slot);};
                const unsigned a=index(i),b=index(i+1),c=index(i+2);
                if(a>=accessor->count || b>=accessor->count || c>=accessor->count){
                    error="Invalid collision triangle in "+path.string();return false;
                }
                if(a!=b && b!=c && c!=a)mesh.triangles.push_back({offset+a,offset+b,offset+c});
            }
        }
    }
    return true;
}
#endif

class SceneModel {
public:
    std::optional<genesis::gameplay::Renderable::Material> materialOverride;
    std::optional<genesis::gameplay::EntityId> entityId;
    bool visible = true;
    const fs::path& path() const { return m_path; }
    const std::vector<genesis::gameplay::Renderable::MeshVertexEdit>& meshVertices() const { return m_meshVertices; }
    void updateMeshVertices(const std::vector<genesis::gameplay::Renderable::MeshVertexEdit>& edits) {
        if(edits==m_meshVertices)return;
        m_meshVertices=edits;m_localBounds={};m_pickTriangles.clear();m_probeTriangles.clear();
        for(auto& primitive:m_primitives) {
            auto vertices=primitive.baseVertices;
            const auto begin=std::lower_bound(edits.begin(),edits.end(),primitive.firstVertex,
                [](const auto& item,uint32_t index){return item.index<index;});
            for(auto it=begin;it!=edits.end() && it->index<primitive.firstVertex+vertices.size();++it)
                std::copy(it->position.begin(),it->position.end(),vertices[it->index-primitive.firstVertex].position);
            if(!edits.empty()) {
                for(auto& vertex:vertices)std::fill_n(vertex.normal,3,0.0f);
                for(size_t i=0;i+2<primitive.editIndices.size();i+=3) {
                    const auto a=primitive.editIndices[i],b=primitive.editIndices[i+1],c=primitive.editIndices[i+2];
                    if(a>=vertices.size() || b>=vertices.size() || c>=vertices.size())continue;
                    const auto& p=vertices[a].position;const auto& q=vertices[b].position;const auto& r=vertices[c].position;
                    const float u[3]{q[0]-p[0],q[1]-p[1],q[2]-p[2]};
                    const float v[3]{r[0]-p[0],r[1]-p[1],r[2]-p[2]};
                    const float n[3]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
                    for(auto index:{a,b,c})for(int axis=0;axis<3;++axis)vertices[index].normal[axis]+=n[axis];
                }
                for(auto& vertex:vertices) {
                    auto& n=vertex.normal;const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                    if(length>1e-8f)for(float& axis:n)axis/=length;
                    else n[1]=1.0f;
                }
            }
            bgfx::update(primitive.vertices,0,bgfx::copy(vertices.data(),uint32_t(vertices.size()*sizeof(Vertex))));
            const auto& node=m_data->nodes[primitive.nodeIndex];
            float nodeMatrix[16];cgltf_node_transform_world(&node,nodeMatrix);
            auto point=[&](uint32_t index) {
                float input[4]{vertices[index].position[0],vertices[index].position[1],vertices[index].position[2],1},result[4];
                bx::vec4MulMtx(result,input,nodeMatrix);
                return genesis::editor::Vec3{result[0],result[1],result[2]};
            };
            for(uint32_t i=0;i<vertices.size();++i)m_localBounds.include(point(i));
            if(node.skin)continue;
            float worldMatrix[16];bx::mtxMul(worldMatrix,nodeMatrix,m_entityMatrix.data());
            for(size_t i=0;i+2<primitive.editIndices.size();i+=3) {
                const auto a=primitive.editIndices[i],b=primitive.editIndices[i+1],c=primitive.editIndices[i+2];
                if(a>=vertices.size() || b>=vertices.size() || c>=vertices.size())continue;
                m_pickTriangles.push_back({point(a),point(b),point(c)});
                if(m_renderLayer&layerMask(RenderLayer::World)) {
                    auto worldPoint=[&](uint32_t index) {
                        float input[4]{vertices[index].position[0],vertices[index].position[1],vertices[index].position[2],1},result[4];
                        bx::vec4MulMtx(result,input,worldMatrix);
                        return genesis::probes::Vec{result[0],result[1],result[2]};
                    };
                    m_probeTriangles.push_back({worldPoint(a),worldPoint(b),worldPoint(c)});
                }
            }
        }
    }
    void setTransform(const genesis::gameplay::Transform& transform) { entityMatrix(m_entityMatrix.data(),transform); }
    genesis::editor::Bounds bounds() const {
        genesis::editor::Bounds bounds;
        if (!m_localBounds.valid()) return bounds;
        for (int corner=0;corner<8;++corner) {
            float local[4]{0,0,0,1}, world[4];
            for (int axis=0;axis<3;++axis) local[axis]=(corner&(1<<axis)) ? m_localBounds.maximum[axis] : m_localBounds.minimum[axis];
            bx::vec4MulMtx(world,local,m_entityMatrix.data()); bounds.include({world[0],world[1],world[2]});
        }
        return bounds;
    }
    float intersectRay(const genesis::editor::Ray& ray) const {
        if(!std::isfinite(genesis::editor::intersect(ray,bounds())))return INFINITY;
        if(m_pickTriangles.empty())return genesis::editor::intersect(ray,bounds());
        using namespace genesis::editor::gizmoMath;
        float inverse[16];bx::mtxInverse(inverse,m_entityMatrix.data());
        const float origin4[]{ray.origin[0],ray.origin[1],ray.origin[2],1};
        const float direction4[]{ray.direction[0],ray.direction[1],ray.direction[2],0};
        float localOrigin4[4],localDirection4[4];
        bx::vec4MulMtx(localOrigin4,origin4,inverse);bx::vec4MulMtx(localDirection4,direction4,inverse);
        const genesis::editor::Vec3 origin{localOrigin4[0],localOrigin4[1],localOrigin4[2]};
        const genesis::editor::Vec3 direction{localDirection4[0],localDirection4[1],localDirection4[2]};
        float nearest=INFINITY;
        for(const auto& triangle:m_pickTriangles) {
            const auto e1=sub(triangle[1],triangle[0]),e2=sub(triangle[2],triangle[0]);
            const auto p=cross(direction,e2);const float determinant=dot(e1,p);
            if(std::abs(determinant)<1e-8f)continue;
            const float inverseDeterminant=1.0f/determinant;
            const auto offset=sub(origin,triangle[0]);const float u=dot(offset,p)*inverseDeterminant;
            if(u<0 || u>1)continue;
            const auto q=cross(offset,e1);const float v=dot(direction,q)*inverseDeterminant;
            if(v<0 || u+v>1)continue;
            const float t=dot(e2,q)*inverseDeterminant;
            if(t>=0 && t<nearest)nearest=t;
        }
        return nearest;
    }
    bool overlapsScreenRegions(const genesis::editor::ViewportCamera& camera,const genesis::ui::Rect& viewport,
        const std::vector<genesis::editor::selection::Polygon>& regions) const {
        const auto worldBounds=bounds();
        float minX=INFINITY,minY=INFINITY,maxX=-INFINITY,maxY=-INFINITY;
        bool projectedBounds=worldBounds.valid();
        if(projectedBounds)for(int corner=0;corner<8;++corner) {
            const genesis::editor::Vec3 position{
                corner&1?worldBounds.maximum[0]:worldBounds.minimum[0],
                corner&2?worldBounds.maximum[1]:worldBounds.minimum[1],
                corner&4?worldBounds.maximum[2]:worldBounds.minimum[2]};
            const auto point=genesis::editor::gizmoMath::project(position,camera,viewport);
            if(!point){projectedBounds=false;break;}
            minX=std::min(minX,(*point)[0]);minY=std::min(minY,(*point)[1]);
            maxX=std::max(maxX,(*point)[0]);maxY=std::max(maxY,(*point)[1]);
        }
        if(projectedBounds) {
            bool candidate=false;
            for(const auto& region:regions) {
                float left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;
                for(const auto& point:region) {
                    left=std::min(left,point[0]);top=std::min(top,point[1]);
                    right=std::max(right,point[0]);bottom=std::max(bottom,point[1]);
                }
                if(right>=minX && left<=maxX && bottom>=minY && top<=maxY){candidate=true;break;}
            }
            if(!candidate)return false;
        }
        if(m_pickTriangles.empty()) {
            const auto point=genesis::editor::gizmoMath::project(bounds().center(),camera,viewport);
            if(point)for(const auto& region:regions)if(genesis::editor::selection::contains(region,*point))return true;
            return false;
        }
        for(const auto& triangle:m_pickTriangles) {
            std::array<genesis::editor::selection::Point,3> projected{};
            bool visible=true;
            size_t index=0;
            for(const auto& vertex:triangle) {
                float local[]{vertex[0],vertex[1],vertex[2],1},world[4];
                bx::vec4MulMtx(world,local,m_entityMatrix.data());
                const auto point=genesis::editor::gizmoMath::project({world[0],world[1],world[2]},camera,viewport);
                if(!point){visible=false;break;}
                projected[index++]=*point;
            }
            if(visible)for(const auto& region:regions)
                if(genesis::editor::selection::overlapsTriangle(region,projected))return true;
        }
        return false;
    }
    ~SceneModel() { release(); }
    SceneModel(const SceneModel&) = delete;
    SceneModel& operator=(const SceneModel&) = delete;
    SceneModel() = default;

    bool load(const fs::path& path, const genesis::gameplay::Transform& transform, RenderLayer layer,
        const std::vector<genesis::gameplay::Renderable::MeshVertexEdit>& meshVertices={}) {
        m_path = path;
        m_meshVertices=meshVertices;
        m_renderLayer = layerMask(layer);
        entityMatrix(m_entityMatrix.data(), transform);
        cgltf_options options{};
        if (cgltf_parse_file(&options, path.string().c_str(), &m_data) != cgltf_result_success || !m_data) {
            std::fprintf(stderr, "Cannot parse model: %s\n", path.string().c_str());
            return false;
        }
        if (cgltf_load_buffers(&options, m_data, path.string().c_str()) != cgltf_result_success ||
            cgltf_validate(m_data) != cgltf_result_success) {
            std::fprintf(stderr, "Cannot load model buffers: %s\n", path.string().c_str());
            return false;
        }

        buildMaterials();
        const bgfx::VertexLayout vertexLayout = Vertex::layout();
        uint32_t flattenedVertex=0;
        for (size_t nodeIndex = 0; nodeIndex < m_data->nodes_count; ++nodeIndex) {
            cgltf_node& node = m_data->nodes[nodeIndex];
            if (!node.mesh) continue;
            for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
                const cgltf_primitive& source = node.mesh->primitives[p];
                if (source.type != cgltf_primitive_type_triangles) continue;
                const cgltf_accessor* positions = attribute(source, cgltf_attribute_type_position, 0);
                if (!positions || positions->count == 0) continue;
                const cgltf_accessor* normals = attribute(source, cgltf_attribute_type_normal, 0);
                const cgltf_accessor* tangents = attribute(source, cgltf_attribute_type_tangent, 0);
                const genesis::materials::MaterialTextureCoordinates textureCoordinates(source);
                Primitive primitive;
                primitive.firstVertex=flattenedVertex;
                std::vector<std::array<float,3>> basePositions(positions->count);
                for (size_t slot = 0; slot < primitive.hasTextureUv.size(); ++slot) {
                    primitive.hasTextureUv[slot] = textureCoordinates.available(slot, positions->count);
                    const auto* view = textureCoordinates.views[slot];
                    if (view && view->texture && !primitive.hasTextureUv[slot])
                        std::fprintf(stderr, "Missing or invalid %s TEXCOORD_%d in %s; texture disabled for this primitive\n",
                            genesis::materials::materialTextureNames[slot], genesis::materials::textureCoordinateSet(*view),
                            path.filename().string().c_str());
                }
                const cgltf_accessor* joints = attribute(source, cgltf_attribute_type_joints, 0);
                const cgltf_accessor* weights = attribute(source, cgltf_attribute_type_weights, 0);

                std::vector<Vertex> vertices(positions->count);
                for (size_t i = 0; i < vertices.size(); ++i) {
                    cgltf_accessor_read_float(positions, i, vertices[i].position, 3);
                    std::copy_n(vertices[i].position,3,basePositions[i].begin());
                    const auto edit=std::lower_bound(meshVertices.begin(),meshVertices.end(),flattenedVertex,
                        [](const auto& item,uint32_t index){return item.index<index;});
                    if(edit!=meshVertices.end() && edit->index==flattenedVertex)
                        std::copy(edit->position.begin(),edit->position.end(),vertices[i].position);
                    ++flattenedVertex;
                    if (normals) cgltf_accessor_read_float(normals, i, vertices[i].normal, 3);
                    if (tangents) cgltf_accessor_read_float(tangents, i, vertices[i].tangent, 4);
                    for (size_t slot = 0; slot < primitive.hasTextureUv.size(); ++slot)
                        if (primitive.hasTextureUv[slot]) {
                            const auto coordinates = textureCoordinates.read(slot, i);
                            std::copy(coordinates.begin(), coordinates.end(), vertices[i].materialUv[slot]);
                        }
                    if (joints) {
                        cgltf_uint values[4]{};
                        cgltf_accessor_read_uint(joints, i, values, 4);
                        for (int j = 0; j < 4; ++j) vertices[i].joints[j] = uint8_t(std::min<cgltf_uint>(values[j], 63));
                    }
                    if (weights) {
                        cgltf_accessor_read_float(weights, i, vertices[i].weights, 4);
                        float sum = vertices[i].weights[0] + vertices[i].weights[1] + vertices[i].weights[2] + vertices[i].weights[3];
                        if (sum > 1e-5f) for (float& weight : vertices[i].weights) weight /= sum;
                    }
                }

                std::vector<uint32_t> indices;
                if (source.indices) {
                    indices.resize(source.indices->count);
                    for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint32_t(cgltf_accessor_read_index(source.indices, i));
                } else {
                    indices.resize(vertices.size());
                    for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint32_t(i);
                }
                if (!tangents && normals && primitive.hasTextureUv[size_t(genesis::materials::MaterialTexture::Normal)])
                    genesis::materials::generateTangents(std::span<Vertex>(vertices), std::span<const uint32_t>(indices),
                        [](const Vertex& vertex) { return vertex.materialUv[size_t(genesis::materials::MaterialTexture::Normal)]; });
                primitive.baseVertices=vertices;
                for(size_t i=0;i<vertices.size();++i)
                    std::copy(basePositions[i].begin(),basePositions[i].end(),primitive.baseVertices[i].position);
                primitive.editIndices=indices;
                float nodeBoundsMatrix[16]; cgltf_node_transform_world(&node,nodeBoundsMatrix);
                for (const auto& vertex:vertices) {
                    float point[4]{vertex.position[0],vertex.position[1],vertex.position[2],1}, transformed[4];
                    bx::vec4MulMtx(transformed,point,nodeBoundsMatrix);
                    m_localBounds.include({transformed[0],transformed[1],transformed[2]});
                }
                if(!meshVertices.empty()) {
                    for(auto& vertex:vertices)std::fill_n(vertex.normal,3,0.0f);
                    for(size_t i=0;i+2<indices.size();i+=3) {
                        const auto a=indices[i],b=indices[i+1],c=indices[i+2];
                        if(a>=vertices.size() || b>=vertices.size() || c>=vertices.size())continue;
                        const auto& p=vertices[a].position;const auto& q=vertices[b].position;
                        const auto& r=vertices[c].position;
                        const float u[3]{q[0]-p[0],q[1]-p[1],q[2]-p[2]};
                        const float v[3]{r[0]-p[0],r[1]-p[1],r[2]-p[2]};
                        const float n[3]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
                        for(auto index:{a,b,c})for(int axis=0;axis<3;++axis)vertices[index].normal[axis]+=n[axis];
                    }
                    for(auto& vertex:vertices) {
                        const auto& n=vertex.normal;const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                        if(length>1e-8f)for(int axis=0;axis<3;++axis)vertex.normal[axis]/=length;
                        else vertex.normal[1]=1;
                    }
                }
                if(!node.skin) {
                    auto localPoint=[&](uint32_t index) {
                        float p[]{vertices[index].position[0],vertices[index].position[1],vertices[index].position[2],1};
                        float transformed[4];bx::vec4MulMtx(transformed,p,nodeBoundsMatrix);
                        return genesis::editor::Vec3{transformed[0],transformed[1],transformed[2]};
                    };
                    for(size_t i=0;i+2<indices.size();i+=3)
                        if(indices[i]<vertices.size() && indices[i+1]<vertices.size() && indices[i+2]<vertices.size())
                            m_pickTriangles.push_back({localPoint(indices[i]),localPoint(indices[i+1]),localPoint(indices[i+2])});
                }
                if (layer == RenderLayer::World && !node.skin) {
                    float nodeMatrix[16], worldMatrix[16];
                    cgltf_node_transform_world(&node, nodeMatrix);
                    bx::mtxMul(worldMatrix, nodeMatrix, m_entityMatrix.data());
                    auto point = [&](uint32_t index) {
                        float p[4]{vertices[index].position[0], vertices[index].position[1], vertices[index].position[2], 1};
                        float w[4]; bx::vec4MulMtx(w, p, worldMatrix);
                        return genesis::probes::Vec{w[0],w[1],w[2]};
                    };
                    for (size_t i=0;i+2<indices.size();i+=3)
                        m_probeTriangles.push_back({point(indices[i]),point(indices[i+1]),point(indices[i+2])});
                }
                primitive.vertices = bgfx::createDynamicVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Vertex))), vertexLayout);
                primitive.indices = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
                primitive.nodeIndex = nodeIndex;
                primitive.sourcePrimitiveIndex = p;
                primitive.materialIndex = source.material ? size_t(source.material - m_data->materials) : m_materials.size() - 1;
                const Material& material = m_materials[primitive.materialIndex];
                const float peakEmission = std::max(material.emissive[0],
                    std::max(material.emissive[1], material.emissive[2]));
                if (!node.skin && !material.blended && material.params[3] <= 0.0f &&
                    std::isfinite(peakEmission) && peakEmission > 0.0f) {
                    const size_t emissiveSlot = size_t(genesis::materials::MaterialTexture::Emissive);
                    const cgltf_texture_view* emissiveView = source.material
                        ? &source.material->emissive_texture : nullptr;
                    const bool textured = emissiveView && emissiveView->texture &&
                        primitive.hasTextureUv[emissiveSlot] && bgfx::isValid(material.emissiveTexture);
                    std::array<std::vector<EmissiveFace>, 6> facesByDirection;
                    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() ||
                            indices[i + 2] >= vertices.size()) continue;
                        const auto& a = vertices[indices[i]].position;
                        const auto& b = vertices[indices[i + 1]].position;
                        const auto& c = vertices[indices[i + 2]].position;
                        const float ab[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                        const float ac[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                        const float cross[3]{
                            ab[1] * ac[2] - ab[2] * ac[1],
                            ab[2] * ac[0] - ab[0] * ac[2],
                            ab[0] * ac[1] - ab[1] * ac[0]};
                        const float length = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
                        if (!std::isfinite(length) || length <= 1e-8f) continue;
                        int axis = 0;
                        if (std::abs(cross[1]) > std::abs(cross[axis])) axis = 1;
                        if (std::abs(cross[2]) > std::abs(cross[axis])) axis = 2;
                        auto& faces = facesByDirection[size_t(axis * 2 + (cross[axis] < 0.0f))];
                        const float area = 0.5f * length;
                        EmissiveFace face;
                        face.sourceTriangle = uint32_t(i / 3);
                        face.area = area;
                        // Exact second moment of a uniformly emitting triangle.
                        for (int row = 0; row < 3; ++row)
                            for (int column = 0; column < 3; ++column) {
                                const float rowSum = a[row] + b[row] + c[row];
                                const float columnSum = a[column] + b[column] + c[column];
                                face.secondMoment[size_t(row * 3 + column)] =
                                    (a[row] * a[column] + b[row] * b[column] +
                                     c[row] * c[column] + rowSum * columnSum) / 12.0f;
                            }
                        std::array<float, 3> textureColor{1.0f, 1.0f, 1.0f};
                        if (textured) {
                            textureColor = {0.0f, 0.0f, 0.0f};
                            const auto& ua = vertices[indices[i]].materialUv[emissiveSlot];
                            const auto& ub = vertices[indices[i + 1]].materialUv[emissiveSlot];
                            const auto& uc = vertices[indices[i + 2]].materialUv[emissiveSlot];
                            const std::array<std::array<float, 2>, 4> samples{{
                                {(2.0f * ua[0] + ub[0] + uc[0]) * 0.25f,
                                 (2.0f * ua[1] + ub[1] + uc[1]) * 0.25f},
                                {(ua[0] + 2.0f * ub[0] + uc[0]) * 0.25f,
                                 (ua[1] + 2.0f * ub[1] + uc[1]) * 0.25f},
                                {(ua[0] + ub[0] + 2.0f * uc[0]) * 0.25f,
                                 (ua[1] + ub[1] + 2.0f * uc[1]) * 0.25f},
                                {(ua[0] + ub[0] + uc[0]) / 3.0f,
                                 (ua[1] + ub[1] + uc[1]) / 3.0f}}};
                            for (const auto& uv : samples) {
                                const auto color = m_textures.sampleEmissive(*emissiveView, m_path, uv);
                                for (int component = 0; component < 3; ++component)
                                    textureColor[component] += color[component] * 0.25f;
                            }
                        }
                        for (int component = 0; component < 3; ++component) {
                            face.center[component] = (a[component] + b[component] + c[component]) / 3.0f;
                            face.areaNormal[component] = 0.5f * cross[component];
                            face.textureColor[component] = textureColor[component];
                            face.minimum[component] = std::min({a[component], b[component], c[component]});
                            face.maximum[component] = std::max({a[component], b[component], c[component]});
                        }
                        faces.push_back(face);
                    }
                    for (const auto& faces : facesByDirection) {
                        if (faces.empty()) continue;
                        std::vector<std::vector<size_t>> partitions(1);
                        partitions[0].resize(faces.size());
                        for (size_t index = 0; index < faces.size(); ++index) partitions[0][index] = index;
                        // Split only when the spatial span is large relative to
                        // emitting area. A compact tessellated panel stays one
                        // source; long strips and separated islands get centers
                        // of their own, up to the runtime four-light budget.
                        while (partitions.size() < 4) {
                            size_t chosen = partitions.size();
                            int splitAxis = 0;
                            float worstRatio = 1.5f;
                            for (size_t part = 0; part < partitions.size(); ++part) {
                                if (partitions[part].size() < 2) continue;
                                std::array<float, 3> low{INFINITY, INFINITY, INFINITY};
                                std::array<float, 3> high{-INFINITY, -INFINITY, -INFINITY};
                                std::array<float, 3> centerLow{INFINITY, INFINITY, INFINITY};
                                std::array<float, 3> centerHigh{-INFINITY, -INFINITY, -INFINITY};
                                float totalArea = 0.0f;
                                for (size_t faceIndex : partitions[part]) {
                                    const EmissiveFace& face = faces[faceIndex];
                                    totalArea += face.area;
                                    for (int component = 0; component < 3; ++component) {
                                        low[component] = std::min(low[component], face.minimum[component]);
                                        high[component] = std::max(high[component], face.maximum[component]);
                                        centerLow[component] = std::min(centerLow[component], face.center[component]);
                                        centerHigh[component] = std::max(centerHigh[component], face.center[component]);
                                    }
                                }
                                for (int component = 0; component < 3; ++component) {
                                    const float ratio = (high[component] - low[component])
                                        / std::sqrt(std::max(totalArea, 1e-6f));
                                    if (ratio > worstRatio &&
                                        centerHigh[component] - centerLow[component] > 1e-4f) {
                                        worstRatio = ratio;
                                        chosen = part;
                                        splitAxis = component;
                                    }
                                }
                            }
                            if (chosen == partitions.size()) break;
                            auto& part = partitions[chosen];
                            std::sort(part.begin(), part.end(), [&](size_t left, size_t right) {
                                return faces[left].center[splitAxis] < faces[right].center[splitAxis];
                            });
                            // Prefer a real gap between emitting islands. A
                            // median split can join half of one panel to half
                            // of another when each has only two triangles.
                            size_t split = part.size() / 2;
                            float widestGap = -1.0f;
                            for (size_t index = 1; index < part.size(); ++index) {
                                const float gap = faces[part[index]].center[splitAxis] -
                                    faces[part[index - 1]].center[splitAxis];
                                if (gap > widestGap + 1e-4f ||
                                    (std::abs(gap - widestGap) <= 1e-4f &&
                                     std::abs(int(index * 2) - int(part.size())) <
                                     std::abs(int(split * 2) - int(part.size())))) {
                                    widestGap = gap;
                                    split = index;
                                }
                            }
                            std::vector<size_t> other(part.begin() + split, part.end());
                            part.erase(part.begin() + split, part.end());
                            partitions.push_back(std::move(other));
                        }
                        for (const auto& part : partitions) {
                            EmissiveCluster cluster;
                            cluster.nodeIndex = nodeIndex;
                            cluster.primitiveIndex = m_primitives.size();
                            cluster.radiance = {std::max(material.emissive[0], 0.0f),
                                std::max(material.emissive[1], 0.0f), std::max(material.emissive[2], 0.0f)};
                            cluster.doubleSided = material.doubleSided;
                            for (size_t faceIndex : part) {
                                const EmissiveFace& face = faces[faceIndex];
                                cluster.sourceTriangles.push_back(face.sourceTriangle);
                                cluster.area += face.area;
                                for (size_t moment = 0; moment < 9; ++moment)
                                    cluster.secondMoment[moment] += face.secondMoment[moment] * face.area;
                                for (int component = 0; component < 3; ++component) {
                                    cluster.center[component] += face.center[component] * face.area;
                                    cluster.areaNormal[component] += face.areaNormal[component];
                                    cluster.weightedTexture[component] += face.textureColor[component] * face.area;
                                    cluster.minimum[component] = std::min(cluster.minimum[component], face.minimum[component]);
                                    cluster.maximum[component] = std::max(cluster.maximum[component], face.maximum[component]);
                                }
                            }
                            for (int component = 0; component < 3; ++component) {
                                cluster.center[component] /= cluster.area;
                                cluster.radiance[component] *= cluster.weightedTexture[component] / cluster.area;
                            }
                            std::sort(cluster.sourceTriangles.begin(), cluster.sourceTriangles.end());
                            m_emissiveClusters.push_back(cluster);
                        }
                    }
                    primitive.shadowSourceIndices = std::move(indices);
                }
                m_primitives.push_back(std::move(primitive));
            }
        }
        cacheBindPose();
        m_textures.clearCpuImages();
        findAnimationDuration();
        std::printf("Loaded %s: %zu draw surfaces, %zu materials, %zu animations\n", path.filename().string().c_str(), m_primitives.size(), m_materials.size(), m_data->animations_count);
        return !m_primitives.empty();
    }

    void animate(float time) {
        if (!m_data || m_data->animations_count == 0 || m_animationDuration <= 0.0f) return;
        restoreBindPose();
        const cgltf_animation& animation = m_data->animations[0];
        const float localTime = std::fmod(time, m_animationDuration);
        for (size_t c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            if (!channel.target_node || !channel.sampler || !channel.sampler->input || !channel.sampler->output) continue;
            const cgltf_accessor* input = channel.sampler->input;
            if (input->count == 0) continue;
            size_t right = 0;
            float keyTime = 0;
            while (right + 1 < input->count) {
                cgltf_accessor_read_float(input, right + 1, &keyTime, 1);
                if (keyTime > localTime) break;
                ++right;
            }
            const size_t left = right;
            right = std::min(right + 1, input->count - 1);
            float t0 = 0, t1 = 0;
            cgltf_accessor_read_float(input, left, &t0, 1);
            cgltf_accessor_read_float(input, right, &t1, 1);
            float alpha = t1 > t0 ? (localTime - t0) / (t1 - t0) : 0.0f;
            if (channel.sampler->interpolation == cgltf_interpolation_type_step) alpha = 0.0f;
            const size_t components = channel.target_path == cgltf_animation_path_type_rotation ? 4 : 3;
            float a[4]{}, b[4]{};
            cgltf_accessor_read_float(channel.sampler->output, left, a, components);
            cgltf_accessor_read_float(channel.sampler->output, right, b, components);
            if (components == 4) {
                const float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
                if (dot < 0.0f) for (float& value : b) value = -value;
            }
            float out[4]{};
            for (size_t i = 0; i < components; ++i) out[i] = a[i] + (b[i] - a[i]) * alpha;
            if (components == 4) {
                const float length = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
                if (length > 1e-6f) for (float& value : out) value /= length;
                std::memcpy(channel.target_node->rotation, out, sizeof(out));
                channel.target_node->has_rotation = true;
            } else if (channel.target_path == cgltf_animation_path_type_translation) {
                std::memcpy(channel.target_node->translation, out, sizeof(float) * 3);
                channel.target_node->has_translation = true;
            } else if (channel.target_path == cgltf_animation_path_type_scale) {
                std::memcpy(channel.target_node->scale, out, sizeof(float) * 3);
                channel.target_node->has_scale = true;
            }
        }
    }

    void appendProbeGeometry(std::vector<genesis::probes::Triangle>& triangles) const {
        if (!visible) return;
        triangles.insert(triangles.end(), m_probeTriangles.begin(), m_probeTriangles.end());
    }

    void appendEmissiveSamples(std::vector<EmissiveLightSample>& samples,
                               const std::array<float, 3>& cameraPosition) const {
        if (!m_data || !visible) return;
        for (size_t clusterIndex = 0; clusterIndex < m_emissiveClusters.size(); ++clusterIndex) {
            const EmissiveCluster& cluster = m_emissiveClusters[clusterIndex];
            float nodeMatrix[16], worldMatrix[16];
            cgltf_node_transform_world(&m_data->nodes[cluster.nodeIndex], nodeMatrix);
            bx::mtxMul(worldMatrix, nodeMatrix, m_entityMatrix.data());
            const float localCenter[4]{cluster.center[0], cluster.center[1], cluster.center[2], 1.0f};
            float transformedCenter[4];
            bx::vec4MulMtx(transformedCenter, localCenter, worldMatrix);
            const bx::Vec3 basisX{worldMatrix[0], worldMatrix[1], worldMatrix[2]};
            const bx::Vec3 basisY{worldMatrix[4], worldMatrix[5], worldMatrix[6]};
            const bx::Vec3 basisZ{worldMatrix[8], worldMatrix[9], worldMatrix[10]};
            const bx::Vec3 areaVector = bx::add(
                bx::add(bx::mul(bx::cross(basisY, basisZ), cluster.areaNormal[0]),
                        bx::mul(bx::cross(basisZ, basisX), cluster.areaNormal[1])),
                bx::mul(bx::cross(basisX, basisY), cluster.areaNormal[2]));
            const float area = bx::length(areaVector);
            if (!std::isfinite(area) || area <= 1e-6f) continue;
            const float inverseArea = 1.0f / area;
            const float dx = transformedCenter[0] - cameraPosition[0];
            const float dy = transformedCenter[1] - cameraPosition[1];
            const float dz = transformedCenter[2] - cameraPosition[2];
            const float luminance = cluster.radiance[0] * 0.2126f +
                cluster.radiance[1] * 0.7152f + cluster.radiance[2] * 0.0722f;
            if (!std::isfinite(luminance) || luminance <= 1e-6f) continue;
            EmissiveLightSample sample;
            sample.sourceModel = this;
            sample.clusterIndex = clusterIndex;
            sample.primitiveIndex = cluster.primitiveIndex;
            sample.position = {transformedCenter[0], transformedCenter[1], transformedCenter[2]};
            sample.normal = {areaVector.x * inverseArea, areaVector.y * inverseArea, areaVector.z * inverseArea};
            const bx::Vec3 surfaceNormal{sample.normal[0], sample.normal[1], sample.normal[2]};
            const std::array<bx::Vec3, 3> worldAxes{basisX, basisY, basisZ};
            bx::Vec3 panelTangent{0.0f, 0.0f, 0.0f};
            float longestAxis = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float span = cluster.maximum[axis] - cluster.minimum[axis];
                const bx::Vec3 candidate = bx::mul(worldAxes[axis], span);
                const bx::Vec3 planar = bx::sub(candidate,
                    bx::mul(surfaceNormal, bx::dot(candidate, surfaceNormal)));
                const float lengthSquared = bx::dot(planar, planar);
                if (lengthSquared > longestAxis) {
                    longestAxis = lengthSquared;
                    panelTangent = planar;
                }
            }
            if (longestAxis <= 1e-8f)
                panelTangent = bx::cross(surfaceNormal,
                    std::abs(surfaceNormal.y) < 0.9f ? bx::Vec3{0.0f, 1.0f, 0.0f}
                                                      : bx::Vec3{1.0f, 0.0f, 0.0f});
            panelTangent = bx::mul(panelTangent, 1.0f / bx::length(panelTangent));
            const bx::Vec3 panelBitangent = bx::cross(surfaceNormal, panelTangent);
            float minimumTangentOffset = INFINITY;
            float maximumTangentOffset = -INFINITY;
            for (int corner = 0; corner < 8; ++corner) {
                const float local[4]{
                    corner & 1 ? cluster.maximum[0] : cluster.minimum[0],
                    corner & 2 ? cluster.maximum[1] : cluster.minimum[1],
                    corner & 4 ? cluster.maximum[2] : cluster.minimum[2], 1.0f};
                float world[4];
                bx::vec4MulMtx(world, local, worldMatrix);
                const bx::Vec3 offset{world[0] - sample.position[0],
                    world[1] - sample.position[1], world[2] - sample.position[2]};
                const float tangentOffset = bx::dot(offset, panelTangent);
                minimumTangentOffset = std::min(minimumTangentOffset, tangentOffset);
                maximumTangentOffset = std::max(maximumTangentOffset, tangentOffset);
                sample.halfWidth = std::max(sample.halfWidth, std::abs(tangentOffset));
                sample.halfHeight = std::max(sample.halfHeight, std::abs(bx::dot(offset, panelBitangent)));
            }
            std::array<float, 3> localTangent{};
            for (int axis = 0; axis < 3; ++axis)
                localTangent[size_t(axis)] = bx::dot(worldAxes[size_t(axis)], panelTangent);
            float tangentVariance = 0.0f;
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    tangentVariance += localTangent[size_t(row)] * localTangent[size_t(column)] *
                        (cluster.secondMoment[size_t(row * 3 + column)] / cluster.area -
                         cluster.center[size_t(row)] * cluster.center[size_t(column)]);
            // Two equal-weight points at +/- one standard deviation preserve
            // the cluster's center and tangent variance. Keep both inside its
            // projected bounds when the source is strongly asymmetric.
            sample.captureOffset = std::min(std::sqrt(std::max(tangentVariance, 0.0f)),
                0.9f * std::max(std::min(-minimumTangentOffset, maximumTangentOffset), 0.0f));
            sample.tangent = {panelTangent.x, panelTangent.y, panelTangent.z};
            sample.radiance = cluster.radiance;
            sample.area = area;
            sample.score = area * luminance / std::max(dx * dx + dy * dy + dz * dz, 1.0f);
            sample.doubleSided = cluster.doubleSided;
            samples.push_back(sample);
        }
    }

    bool shadowGeometryWithoutCluster(size_t clusterIndex, bgfx::TransientIndexBuffer& buffer) const {
        if (clusterIndex >= m_emissiveClusters.size()) return false;
        const EmissiveCluster& cluster = m_emissiveClusters[clusterIndex];
        const auto& indices = m_primitives[cluster.primitiveIndex].shadowSourceIndices;
        const size_t triangleCount = indices.size() / 3;
        std::vector<uint8_t> excluded(triangleCount, 0);
        size_t excludedCount = 0;
        const float normalLength = std::sqrt(cluster.areaNormal[0] * cluster.areaNormal[0]
            + cluster.areaNormal[1] * cluster.areaNormal[1]
            + cluster.areaNormal[2] * cluster.areaNormal[2]);
        const float inverseLength = normalLength > 1e-8f ? 1.0f / normalLength : 0.0f;
        for (const EmissiveCluster& other : m_emissiveClusters) {
            if (other.primitiveIndex != cluster.primitiveIndex) continue;
            const float otherLength = std::sqrt(other.areaNormal[0] * other.areaNormal[0]
                + other.areaNormal[1] * other.areaNormal[1]
                + other.areaNormal[2] * other.areaNormal[2]);
            const float normalDot = inverseLength / std::max(otherLength, 1e-8f)
                * (cluster.areaNormal[0] * other.areaNormal[0]
                 + cluster.areaNormal[1] * other.areaNormal[1]
                 + cluster.areaNormal[2] * other.areaNormal[2]);
            const float planeDistance = inverseLength * std::abs(
                cluster.areaNormal[0] * (other.center[0] - cluster.center[0])
              + cluster.areaNormal[1] * (other.center[1] - cluster.center[1])
              + cluster.areaNormal[2] * (other.center[2] - cluster.center[2]));
            // Splitting a long flat panel must not make its halves cast false
            // shadows from centers that lie on the same emitting plane.
            if (&other != &cluster &&
                (std::abs(normalDot) < 0.995f || planeDistance > 0.005f)) continue;
            for (uint32_t triangle : other.sourceTriangles) if (triangle < triangleCount && !excluded[triangle]) {
                excluded[triangle] = 1;
                ++excludedCount;
            }
        }
        if (excludedCount >= triangleCount) return false;
        const size_t remaining = (triangleCount - excludedCount) * 3;
        if (remaining == 0 || remaining > UINT32_MAX ||
            bgfx::getAvailTransientIndexBuffer(uint32_t(remaining), true) < remaining)
            return false;
        // Build only for the selected shadow caster. A persistent index buffer
        // per candidate cluster would multiply GPU index storage on dense meshes.
        bgfx::allocTransientIndexBuffer(&buffer, uint32_t(remaining), true);
        auto* destination = reinterpret_cast<uint32_t*>(buffer.data);
        for (size_t triangle = 0; triangle * 3 + 2 < indices.size(); ++triangle) {
            if (excluded[triangle]) continue;
            std::copy_n(indices.data() + triangle * 3, 3, destination);
            destination += 3;
        }
        return true;
    }

    void draw(uint16_t view, uint32_t visibleLayers, bgfx::ProgramHandle program, bool shadow,
              bgfx::UniformHandle uSkinning, bgfx::UniformHandle uJoints,
              const std::array<bgfx::UniformHandle, 9>& materialUniforms,
              const std::array<bgfx::UniformHandle, 5>& samplers,
              const std::array<bgfx::TextureHandle, 3>& fallback,
              const std::array<bgfx::UniformHandle, 49>& lightingUniforms,
              const std::array<const float*, 49>& lightingValues,
              const std::array<uint16_t, 49>& lightingCounts,
              const std::array<bgfx::UniformHandle, kCascadeCount>& shadowSamplers,
              const std::array<bgfx::TextureHandle, kCascadeCount>& shadowTextures,
              const std::array<bgfx::UniformHandle, 8>& environmentSamplers,
              const std::array<bgfx::TextureHandle, 8>& environmentTextures,
              size_t skipPrimitive = SIZE_MAX,
              const bgfx::TransientIndexBuffer* shadowRemainder = nullptr,
              bool selectionMask = false,
              bgfx::UniformHandle uViewportShading = BGFX_INVALID_HANDLE,
              int viewportShading = 3,
              bgfx::UniformHandle uSelectionId = BGFX_INVALID_HANDLE,
              float selectionId = 0.0f) const {
        if (!m_data || !visible || (m_renderLayer & visibleLayers) == 0) return;
        for (size_t primitiveIndex = 0; primitiveIndex < m_primitives.size(); ++primitiveIndex) {
            if (primitiveIndex == skipPrimitive && !shadowRemainder) continue;
            const Primitive& primitive = m_primitives[primitiveIndex];
            const cgltf_node& node = m_data->nodes[primitive.nodeIndex];
            float nodeWorld[16], model[16];
            cgltf_node_transform_world(&node, nodeWorld);
            bx::mtxMul(model, nodeWorld, m_entityMatrix.data());
            bgfx::setTransform(model);

            float skinning[4]{node.skin ? 1.0f : 0.0f, 0, 0, 0};
            bgfx::setUniform(uSkinning, skinning);
            if (node.skin) {
                std::array<float, kMaxJoints * 16> joints{};
                for (size_t i = 0; i < kMaxJoints; ++i) bx::mtxIdentity(joints.data() + i * 16);
                float inverseMesh[16];
                bx::mtxInverse(inverseMesh, nodeWorld);
                const size_t count = std::min<size_t>(node.skin->joints_count, kMaxJoints);
                for (size_t i = 0; i < count; ++i) {
                    float jointWorld[16], inverseBind[16], temp[16];
                    cgltf_node_transform_world(node.skin->joints[i], jointWorld);
                    bx::mtxIdentity(inverseBind);
                    if (node.skin->inverse_bind_matrices)
                        cgltf_accessor_read_float(node.skin->inverse_bind_matrices, i, inverseBind, 16);
                    bx::mtxMul(temp, inverseBind, jointWorld);
                    bx::mtxMul(joints.data() + i * 16, temp, inverseMesh);
                }
                bgfx::setUniform(uJoints, joints.data(), uint16_t(kMaxJoints));
            }

            bgfx::setVertexBuffer(0, primitive.vertices);
            if(viewportShading==0) {
                if(!bgfx::isValid(primitive.wireIndices)) {
                    const cgltf_primitive& source=node.mesh->primitives[primitive.sourcePrimitiveIndex];
                    const size_t count=source.indices ? source.indices->count :
                        source.attributes[0].data->count;
                    std::vector<uint32_t> lines;
                    lines.reserve(count*2);
                    std::unordered_set<uint64_t> seen;
                    auto indexAt=[&](size_t i){return source.indices ?
                        uint32_t(cgltf_accessor_read_index(source.indices,i)) : uint32_t(i);};
                    for(size_t i=0;i+2<count;i+=3) {
                        const uint32_t triangle[]{indexAt(i),indexAt(i+1),indexAt(i+2)};
                        for(int edge=0;edge<3;++edge) {
                            const uint32_t a=triangle[edge],b=triangle[(edge+1)%3];
                            if(a==b)continue;
                            const uint64_t key=(uint64_t(std::min(a,b))<<32)|std::max(a,b);
                            if(seen.insert(key).second){lines.push_back(a);lines.push_back(b);}
                        }
                    }
                    if(!lines.empty())primitive.wireIndices=bgfx::createIndexBuffer(
                        bgfx::copy(lines.data(),uint32_t(lines.size()*sizeof(uint32_t))),BGFX_BUFFER_INDEX32);
                }
                if(!bgfx::isValid(primitive.wireIndices))continue;
                bgfx::setIndexBuffer(primitive.wireIndices);
            } else if (primitiveIndex == skipPrimitive)
                bgfx::setIndexBuffer(shadowRemainder);
            else
                bgfx::setIndexBuffer(primitive.indices);
            if (!shadow) {
                if(bgfx::isValid(uSelectionId)) {
                    const float value[4]{selectionId,0,0,0};
                    bgfx::setUniform(uSelectionId,value);
                }
                if(bgfx::isValid(uViewportShading)) {
                    const float mode[4]{float(viewportShading),0,0,0};
                    bgfx::setUniform(uViewportShading,mode);
                }
                // bgfx clears draw state after submit. Frame lighting and the
                // shadow texture therefore have to be rebound for every
                // primitive, not merely once before traversing the scene.
                for (size_t i = 0; i < lightingUniforms.size(); ++i)
                    bgfx::setUniform(lightingUniforms[i], lightingValues[i], lightingCounts[i]);
                for (uint8_t cascade = 0; cascade < kCascadeCount; ++cascade)
                    bgfx::setTexture(uint8_t(4 + cascade), shadowSamplers[cascade], shadowTextures[cascade]);
                for (uint8_t texture = 0; texture < environmentTextures.size(); ++texture)
                    bgfx::setTexture(kEnvironmentTextureStages[texture], environmentSamplers[texture], environmentTextures[texture]);
                Material material = m_materials[primitive.materialIndex];
                if(materialOverride) {
                    for(int i=0;i<3;++i)material.base[i]=materialOverride->color[i];
                    material.params[0]=materialOverride->metallic;material.params[1]=materialOverride->roughness;
                }
                bgfx::setUniform(materialUniforms[0], material.base);
                bgfx::setUniform(materialUniforms[1], material.params);
                bgfx::setUniform(materialUniforms[2], material.emissive);
                float textureFlags[4];
                for (size_t slot = 0; slot < 4; ++slot)
                    textureFlags[slot] = primitive.hasTextureUv[slot] ? material.flags[slot] : 0.0f;
                bgfx::setUniform(materialUniforms[3], textureFlags);
                const float noOcclusion[4]{};
                bgfx::setUniform(materialUniforms[8], primitive.hasTextureUv[size_t(genesis::materials::MaterialTexture::Occlusion)]
                    ? material.occlusion : noOcclusion);
                bgfx::setTexture(0, samplers[0], bgfx::isValid(material.baseTexture) ? material.baseTexture : fallback[0]);
                bgfx::setTexture(1, samplers[1], bgfx::isValid(material.mrTexture) ? material.mrTexture : fallback[0]);
                bgfx::setTexture(2, samplers[2], bgfx::isValid(material.normalTexture) ? material.normalTexture : fallback[1]);
                bgfx::setTexture(3, samplers[3], bgfx::isValid(material.emissiveTexture) ? material.emissiveTexture : fallback[2]);
                bgfx::setTexture(13, samplers[4], bgfx::isValid(material.occlusionTexture) ? material.occlusionTexture : fallback[0]);
                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                    BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | BGFX_STATE_CULL_CCW;
                // World architecture is treated as a solid shell from either
                // side. This keeps the sky and solar disc from appearing when
                // the free camera crosses behind a thin glTF wall or roof.
                if (material.doubleSided || (m_renderLayer & layerMask(RenderLayer::World)) != 0)
                    state &= ~BGFX_STATE_CULL_MASK;
                // Only HDR color uses opacity blending. Normal/AO metadata
                // must not use material AO as an accidental blend alpha.
                if (material.blended && viewportShading>=2) state |= BGFX_STATE_BLEND_ALPHA | BGFX_STATE_BLEND_INDEPENDENT;
                if(viewportShading==0) {
                    state|=BGFX_STATE_PT_LINES;
                    state&=~BGFX_STATE_CULL_MASK;
                }
                bgfx::setState(state);
            } else {
                if(selectionMask){
                    const Material& material=m_materials[primitive.materialIndex];
                    float base[4];std::copy_n(material.base,4,base);
                    float params[4];std::copy_n(material.params,4,params);
                    // Solid and wireframe display all mesh surfaces as opaque.
                    // Apply alpha clipping only in material/rendered modes so
                    // the mask depth matches the visible scene depth.
                    if(viewportShading<2){base[3]=1.0f;params[3]=0.0f;}
                    else params[3]=material.blended?0.5f:std::max(params[3],0.1f);
                    const float flags[4]{viewportShading>=2 && primitive.hasTextureUv[0]?material.flags[0]:0,0,0,0};
                    bgfx::setUniform(materialUniforms[0],base);
                    bgfx::setUniform(materialUniforms[1],params);
                    bgfx::setUniform(materialUniforms[3],flags);
                    bgfx::setTexture(0,samplers[0],bgfx::isValid(material.baseTexture)?material.baseTexture:fallback[0]);
                }
                // Shadow maps are deliberately two-sided. Architectural glTF
                // scenes commonly use thin wall shells whose back faces must
                // still block the sun and volumetric lighting.
                uint64_t state=BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS;
                if(selectionMask){
                    state|=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A;
                    const Material& material=m_materials[primitive.materialIndex];
                    if(!material.doubleSided && (m_renderLayer&layerMask(RenderLayer::World))==0)
                        state|=BGFX_STATE_CULL_CCW;
                }
                bgfx::setState(state);
            }
            bgfx::submit(view, program);
        }
    }

private:
    struct NodePose { bool ht, hr, hs, hm; float t[3], r[4], s[3], m[16]; };

    static const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index) {
        for (size_t i = 0; i < primitive.attributes_count; ++i)
            if (primitive.attributes[i].type == type && primitive.attributes[i].index == index) return primitive.attributes[i].data;
        return nullptr;
    }

    bgfx::TextureHandle loadTexture(const cgltf_texture_view& view, bool srgb) {
        return m_textures.load(view, srgb, m_path);
    }

    void buildMaterials() {
        m_materials.reserve(m_data->materials_count + 1);
        for (size_t i = 0; i < m_data->materials_count; ++i) {
            const cgltf_material& source = m_data->materials[i];
            Material material;
            material.occlusionTexture = loadTexture(source.occlusion_texture, false);
            if (bgfx::isValid(material.occlusionTexture))
                material.occlusion[0] = genesis::materials::occlusionStrength(source.occlusion_texture);
            if (source.has_pbr_metallic_roughness) {
                const auto& pbr = source.pbr_metallic_roughness;
                std::memcpy(material.base, pbr.base_color_factor, sizeof(material.base));
                material.params[0] = pbr.metallic_factor;
                material.params[1] = pbr.roughness_factor;
                material.baseTexture = loadTexture(pbr.base_color_texture, true);
                material.mrTexture = loadTexture(pbr.metallic_roughness_texture, false);
                material.flags[0] = bgfx::isValid(material.baseTexture) ? 1.0f : 0.0f;
                material.flags[1] = bgfx::isValid(material.mrTexture) ? 1.0f : 0.0f;
            }
            material.normalTexture = loadTexture(source.normal_texture, false);
            material.params[2] = genesis::materials::normalScale(source.normal_texture);
            material.flags[2] = bgfx::isValid(material.normalTexture) ? 1.0f : 0.0f;
            material.params[3] = source.alpha_mode == cgltf_alpha_mode_mask ? source.alpha_cutoff : 0.0f;
            material.doubleSided = source.double_sided != 0;
            material.blended = source.alpha_mode == cgltf_alpha_mode_blend;
            const float strength = source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;
            for (int c = 0; c < 3; ++c) material.emissive[c] = source.emissive_factor[c] * strength;
            // Sharing an image with base color is legal (including independent
            // atlas regions). Honor the authored emission instead of guessing
            // that image reuse is an exporter mistake.
            material.emissiveTexture = loadTexture(source.emissive_texture, true);
            material.flags[3] = bgfx::isValid(material.emissiveTexture) ? 1.0f : 0.0f;
            m_materials.push_back(material);
        }
        m_materials.emplace_back();
    }

    void cacheBindPose() {
        m_bindPose.resize(m_data->nodes_count);
        for (size_t i = 0; i < m_data->nodes_count; ++i) {
            const cgltf_node& n = m_data->nodes[i];
            NodePose& p = m_bindPose[i];
            p.ht = n.has_translation; p.hr = n.has_rotation; p.hs = n.has_scale; p.hm = n.has_matrix;
            std::memcpy(p.t, n.translation, sizeof(p.t)); std::memcpy(p.r, n.rotation, sizeof(p.r));
            std::memcpy(p.s, n.scale, sizeof(p.s)); std::memcpy(p.m, n.matrix, sizeof(p.m));
        }
    }

    void restoreBindPose() {
        for (size_t i = 0; i < m_data->nodes_count; ++i) {
            cgltf_node& n = m_data->nodes[i]; const NodePose& p = m_bindPose[i];
            n.has_translation = p.ht; n.has_rotation = p.hr; n.has_scale = p.hs; n.has_matrix = p.hm;
            std::memcpy(n.translation, p.t, sizeof(p.t)); std::memcpy(n.rotation, p.r, sizeof(p.r));
            std::memcpy(n.scale, p.s, sizeof(p.s)); std::memcpy(n.matrix, p.m, sizeof(p.m));
        }
    }

    void findAnimationDuration() {
        for (size_t a = 0; a < m_data->animations_count; ++a)
            for (size_t s = 0; s < m_data->animations[a].samplers_count; ++s) {
                const cgltf_accessor* input = m_data->animations[a].samplers[s].input;
                if (input && input->count) { float t = 0; cgltf_accessor_read_float(input, input->count - 1, &t, 1); m_animationDuration = std::max(m_animationDuration, t); }
            }
    }

    void release() {
        for (const Primitive& p : m_primitives) { if (bgfx::isValid(p.vertices)) bgfx::destroy(p.vertices); if (bgfx::isValid(p.indices)) bgfx::destroy(p.indices); if(bgfx::isValid(p.wireIndices))bgfx::destroy(p.wireIndices); }
        m_textures.clear();
        if (m_data) cgltf_free(m_data);
        m_data = nullptr;
    }

    fs::path m_path;
    std::vector<genesis::gameplay::Renderable::MeshVertexEdit> m_meshVertices;
    genesis::editor::Bounds m_localBounds;
    cgltf_data* m_data{};
    std::array<float, 16> m_entityMatrix{};
    std::vector<Primitive> m_primitives;
    std::vector<EmissiveCluster> m_emissiveClusters;
    std::vector<genesis::probes::Triangle> m_probeTriangles;
    std::vector<std::array<genesis::editor::Vec3,3>> m_pickTriangles;
    std::vector<Material> m_materials;
    genesis::raster::GltfTextures m_textures;
    std::vector<NodePose> m_bindPose;
    float m_animationDuration{};
    uint32_t m_renderLayer = layerMask(RenderLayer::World);
};

static bgfx::TextureHandle solidTexture(uint32_t abgr) {
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&abgr, sizeof(abgr)));
}

static bgfx::TextureHandle loadBrunetonTexture(const fs::path& path, bool texture3d) {
    std::ifstream file(path, std::ios::binary);
    char magic[8]{};
    uint32_t dimensions[3]{};
    if (!file.read(magic, sizeof(magic)) || std::memcmp(magic, "BRUNRGB1", 8) != 0 ||
        !file.read(reinterpret_cast<char*>(dimensions), sizeof(dimensions))) {
        std::fprintf(stderr, "Cannot load Bruneton LUT: %s\n", path.string().c_str());
        return BGFX_INVALID_HANDLE;
    }
    const size_t bytes = size_t(dimensions[0]) * dimensions[1] * dimensions[2] * 4 * sizeof(float);
    std::vector<uint8_t> pixels(bytes);
    if (!file.read(reinterpret_cast<char*>(pixels.data()), std::streamsize(bytes))) {
        std::fprintf(stderr, "Truncated Bruneton LUT: %s\n", path.string().c_str());
        return BGFX_INVALID_HANDLE;
    }
    const uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    const bgfx::Memory* memory = bgfx::copy(pixels.data(), uint32_t(pixels.size()));
    if (texture3d) return bgfx::createTexture3D(uint16_t(dimensions[0]), uint16_t(dimensions[1]), uint16_t(dimensions[2]), false, bgfx::TextureFormat::RGBA32F, flags, memory);
    return bgfx::createTexture2D(uint16_t(dimensions[0]), uint16_t(dimensions[1]), false, 1, bgfx::TextureFormat::RGBA32F, flags, memory);
}

} // namespace

int genesis::raster::run(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    genesis::web::initialize();
#endif
    fs::path launchInput;
    fs::path startupCapture;
    OrbitCapture orbitCapture;
    bool overrideSun = false;
    float overrideSunAzimuth = 0.0f;
    float overrideSunElevation = 35.0f;
    std::string qualityOverride;
    std::string cameraOverride;
    std::string debugViewOverride;
    std::string captureTool;
    std::string captureShading;
    bool validateOnly = false;
    bool editorMode = false;
#ifdef GENESIS_WITH_PHYSX_PBD
    // The GPU build opens a working editor sample when launched from Explorer.
    if(argc==1)editorMode=true;
#endif
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--script") == 0 ||
             std::strcmp(argv[i], "--project") == 0 ||
             std::strcmp(argv[i], "--scene") == 0) && i + 1 < argc) launchInput = argv[++i];
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) startupCapture = argv[++i];
        else if (std::strcmp(argv[i], "--capture-tool") == 0 && i + 1 < argc) captureTool = argv[++i];
        else if (std::strcmp(argv[i], "--capture-shading") == 0 && i + 1 < argc) captureShading = argv[++i];
        else if (std::strcmp(argv[i], "--capture-orbit") == 0 && i + 1 < argc) {
            char* end = nullptr;
            const long count = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || count < 2 || count > 360) {
                std::fprintf(stderr, "--capture-orbit requires a count from 2 to 360\n");
                return 2;
            }
            orbitCapture.count = uint32_t(count);
        }
        else if (std::strcmp(argv[i], "--sun") == 0 && i + 2 < argc) {
            overrideSunAzimuth = std::strtof(argv[++i], nullptr);
            overrideSunElevation = std::strtof(argv[++i], nullptr);
            overrideSun = true;
        }
        else if (std::strcmp(argv[i], "--quality") == 0 && i + 1 < argc)
            qualityOverride = argv[++i];
        else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc)
            cameraOverride = argv[++i];
        else if (std::strcmp(argv[i], "--debug-view") == 0 && i + 1 < argc)
            debugViewOverride = argv[++i];
        else if (std::strcmp(argv[i], "--validate-script") == 0) validateOnly = true;
        else if (std::strcmp(argv[i], "--editor") == 0) editorMode = true;
        else if (argv[i][0] != '-' && launchInput.empty()) launchInput = argv[i];
    }
    if (orbitCapture.count > 1 && startupCapture.empty()) {
        std::fprintf(stderr, "--capture-orbit requires --screenshot PATH\n");
        return 2;
    }
    if (editorMode && launchInput.empty()) {
#ifdef GENESIS_WITH_PHYSX_PBD
        launchInput = fs::path(GENESIS_ROOT)/"examples/physx_particles";
#else
        launchInput = fs::path(GENESIS_ROOT)/"examples/editor_project";
#endif
    }
    if(!captureTool.empty() && (startupCapture.empty() ||
        (captureTool!="move" && captureTool!="rotate" && captureTool!="scale" && captureTool!="transform"))){
        std::fprintf(stderr,"--capture-tool requires --screenshot and move, rotate, scale, or transform\n");
        return 2;
    }
    if(!captureShading.empty() && (startupCapture.empty() || !editorMode ||
        (captureShading!="wireframe" && captureShading!="solid" &&
         captureShading!="material" && captureShading!="rendered"))) {
        std::fprintf(stderr,"--capture-shading requires --editor, --screenshot, and wireframe, solid, material, or rendered\n");
        return 2;
    }
    auto launch = genesis::ProjectLoader::resolve(launchInput);
    while (launch.kind == genesis::LaunchKind::Prompt) {
        if (validateOnly || !startupCapture.empty()) {
            std::fprintf(stderr, "%s\n", launch.message.c_str());
            return 2;
        }
        const auto dropped = genesis::ui::showProjectPrompt(launch.message);
        if (!dropped) return 0;
        launch = genesis::ProjectLoader::resolve(*dropped);
    }
    genesis::scripting::ScriptRuntime runtime;
    if (!runtime.run(launch.scripts)) return 2;
    genesis::SceneDocument sceneDocument;
    if (!launch.sceneFile.empty()) {
        std::string error;
        if (!sceneDocument.load(launch.sceneFile,error)) { std::fprintf(stderr,"Scene: %s\n",error.c_str()); return 2; }
        if (sceneDocument.hasEntities()) {
            sceneDocument.instantiate(genesis::gameplay::g_world);
            genesis::scripting::g_model.clear();
        }
    } else if (editorMode) sceneDocument.fromScript(launch.scripts.front());
    const genesis::render::AuthoredLighting scriptLighting{genesis::render::g_pointLights,genesis::render::g_spotLights,genesis::render::g_areaLights,genesis::render::g_reflectionProbes};
    auto syncAuthoredLighting=[&](const std::vector<genesis::gameplay::EntitySnapshot>& entities) {
        auto state=scriptLighting.withEntities(entities);
        genesis::render::g_pointLights=std::move(state.points);genesis::render::g_spotLights=std::move(state.spots);
        genesis::render::g_areaLights=std::move(state.areas);genesis::render::g_reflectionProbes=std::move(state.probes);
        if(state.omitted)std::fprintf(stderr,"Light capacity reached: %zu authored lights omitted (4 point, 4 spot, 2 area slots).\n",state.omitted);
    };
    syncAuthoredLighting(genesis::gameplay::g_world.snapshots());
    const auto runtimeCamera=genesis::scripting::g_camera;
    if (editorMode) {
        if (!genesis::scripting::g_model.empty()) {
            const auto id=genesis::gameplay::g_world.create(genesis::scripting::g_model.stem().string());
            genesis::gameplay::g_world.setRenderable(id,genesis::scripting::g_model.string());
            genesis::scripting::g_model.clear();
        }
        if (sceneDocument.editorCamera()) genesis::scripting::g_camera=*sceneDocument.editorCamera();
        genesis::scripting::g_window.title="Genesis - Scene Editor";
        genesis::scripting::g_window.width=std::max(genesis::scripting::g_window.width,1280);
        genesis::scripting::g_window.height=std::max(genesis::scripting::g_window.height,800);
    }
    if (genesis::scripting::g_window.title == "Genesis" && launch.projectName != "Genesis")
        genesis::scripting::g_window.title = launch.projectName + " - Genesis";
    std::printf("Genesis scene: %s (%zu Python script%s)\n",
        launch.sceneFile.empty() ? "direct script" : launch.sceneFile.string().c_str(),
        launch.scripts.size(), launch.scripts.size() == 1 ? "" : "s");
    if (!qualityOverride.empty()) {
        if (qualityOverride != "low" && qualityOverride != "medium" && qualityOverride != "high") {
            std::fprintf(stderr, "--quality must be low, medium, or high\n");
            return 2;
        }
        genesis::scripting::g_renderer.quality = qualityOverride;
    }
    if (!debugViewOverride.empty()) {
        if (debugViewOverride != "none" && debugViewOverride != "ao" && debugViewOverride != "contact"
            && debugViewOverride != "invalid" && debugViewOverride != "color-chart" && debugViewOverride != "material-ao"
            && debugViewOverride != "bloom") {
            std::fprintf(stderr, "--debug-view must be none, ao, contact, invalid, color-chart, material-ao, or bloom\n");
            return 2;
        }
        genesis::scripting::g_renderer.debugView = debugViewOverride;
    }
    if (!cameraOverride.empty()) {
        const auto found = std::find_if(genesis::scripting::g_cameraViews.begin(),
            genesis::scripting::g_cameraViews.end(), [&](const auto& view) { return view.name == cameraOverride; });
        if (found == genesis::scripting::g_cameraViews.end()) {
            std::fprintf(stderr, "Unknown camera view '%s'. Available:", cameraOverride.c_str());
            for (const auto& view : genesis::scripting::g_cameraViews)
                std::fprintf(stderr, " %s", view.name.c_str());
            std::fprintf(stderr, "\n");
            return 2;
        }
        genesis::scripting::g_camera.position = found->position;
        genesis::scripting::g_camera.target = found->target;
        genesis::scripting::g_camera.up = found->up;
        genesis::scripting::g_camera.fovDegrees = found->fovDegrees;
    }
    if (overrideSun) genesis::atmosphere::setSun(overrideSunAzimuth, overrideSunElevation);
    const float initialSunAzimuth = genesis::atmosphere::settings().sunAzimuthDegrees;
    const float initialSunElevation = genesis::atmosphere::settings().sunElevationDegrees;
    if (validateOnly) return 0;

    SDL_SetAppMetadata("Genesis", "0.2.0", "com.genesis.renderer");
#ifdef __EMSCRIPTEN__
    genesis::web::prepareWindow();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 3; }
    const SDL_WindowFlags windowFlags = SDL_WindowFlags(SDL_WINDOW_RESIZABLE |
        (startupCapture.empty() ? 0 : SDL_WINDOW_HIDDEN));
    SDL_Window* window = SDL_CreateWindow(genesis::scripting::g_window.title.c_str(),
        genesis::scripting::g_window.width, genesis::scripting::g_window.height, windowFlags);
    if (!window) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
    // SDL may adopt the browser canvas's CSS size instead of the requested
    // desktop dimensions. Rendering and input must share those actual units.
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
#ifndef __EMSCRIPTEN__
    if (editorMode) SDL_SetWindowMinimumSize(window,1100,720);
#endif

    GenesisBgfxCallback callback;
    bgfx::Init init;
#ifdef __EMSCRIPTEN__
    init.type = bgfx::RendererType::WebGPU;
    init.fallback = false; // WGSL shaders must never be handed to a GLES fallback.
    init.swapChain.nwh = const_cast<char*>("#canvas");
#else
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
    init.vendorId = BGFX_PCI_ID_NONE;
    init.platformData.type = bgfx::NativeWindowHandleType::Default;
    init.swapChain.width = uint32_t(width);
    init.swapChain.height = uint32_t(height);
    // A queued swap chain makes editor controls visibly follow the cursor.
    // Keep only one presented frame in flight for interactive editing.
    if(editorMode)init.swapChain.maxFrameLatency=1;
    // Desktop editor input should present as soon as it is rendered. V-sync
    // adds a full refresh interval to divider and gizmo feedback.
    init.reset = genesis::scripting::g_window.vsync && !editorMode ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.callback = &callback;
    if (!bgfx::init(init)) { std::fprintf(stderr, "bgfx initialization failed\n"); SDL_DestroyWindow(window); SDL_Quit(); return 4; }
    std::printf("Genesis renderer: %s\n",bgfx::getRendererName(bgfx::getRendererType()));
    bgfx::setDebug(genesis::scripting::g_renderer.gpuTimings ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
    genesis::raster::LightSourceRenderer lightSourceRenderer;
    if (!lightSourceRenderer.initialize()) {
        std::fprintf(stderr, "Cannot initialize emissive light-source renderer\n");
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }
    QualityProfile quality = qualityProfile(genesis::scripting::g_renderer.quality);
    std::unordered_map<std::string, uint16_t> lightPatternLayers;
    std::vector<std::string> lightPatternPaths;
    auto registerLightPattern = [&](const std::string& path) {
        if (path.empty() || lightPatternLayers.find(path) != lightPatternLayers.end()) return;
        if (lightPatternPaths.size() >= kLightPatternSlotCount) {
            std::fprintf(stderr, "At most %u unique IES/cookie patterns are supported; ignoring %s\n",
                unsigned(kLightPatternSlotCount), path.c_str());
            return;
        }
        const uint16_t layer = uint16_t(kLightPatternBaseLayer + lightPatternPaths.size());
        lightPatternLayers.emplace(path, layer);
        lightPatternPaths.push_back(path);
    };
    for (const auto& light : genesis::scripting::g_pointLights) registerLightPattern(light.iesProfile);
    for (const auto& light : genesis::scripting::g_spotLights) {
        registerLightPattern(light.cookieTexture);
        registerLightPattern(light.iesProfile);
    }

    bgfx::ProgramHandle pbrProgram = bgfx::createProgram(shader(vs_pbr_shader, sizeof(vs_pbr_shader)), shader(fs_pbr_shader, sizeof(fs_pbr_shader)), true);
    bgfx::ProgramHandle viewportProgram = bgfx::createProgram(shader(vs_pbr_shader, sizeof(vs_pbr_shader)), shader(fs_viewport_shader, sizeof(fs_viewport_shader)), true);
    bgfx::ProgramHandle shadowProgram = bgfx::createProgram(shader(vs_shadow_shader, sizeof(vs_shadow_shader)), shader(fs_shadow_shader, sizeof(fs_shadow_shader)), true);
    bgfx::ProgramHandle skyProgram = bgfx::createProgram(shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)), shader(fs_sky_shader, sizeof(fs_sky_shader)), true);
    bgfx::ProgramHandle environmentPrefilterProgram = bgfx::createProgram(
        shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)),
        shader(fs_environment_prefilter_shader, sizeof(fs_environment_prefilter_shader)), true);
    bgfx::ProgramHandle localPrefilterProgram = bgfx::createProgram(
        shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)),
        shader(fs_probe_prefilter_shader, sizeof(fs_probe_prefilter_shader)), true);
    bgfx::ProgramHandle brdfLutProgram = bgfx::createProgram(
        shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)),
        shader(fs_brdf_lut_shader, sizeof(fs_brdf_lut_shader)), true);
    bgfx::ProgramHandle aoProgram = bgfx::createProgram(shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)), shader(fs_ao_shader, sizeof(fs_ao_shader)), true);
    bgfx::ProgramHandle volumetricProgram = bgfx::createProgram(shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)), shader(fs_volumetric_shader, sizeof(fs_volumetric_shader)), true);
    genesis::raster::BloomRenderer bloom;
    if (!bloom.initialize()) {
        std::fprintf(stderr,"Cannot initialize bloom programs\n");
        bloom.shutdown();
        return 1;
    }
    bgfx::ProgramHandle tonemapProgram = bgfx::createProgram(shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)), shader(fs_tonemap_shader, sizeof(fs_tonemap_shader)), true);
    bgfx::ProgramHandle hudProgram = bgfx::createProgram(
        shader(vs_fullscreen_shader, sizeof(vs_fullscreen_shader)),
        shader(fs_hud_shader, sizeof(fs_hud_shader)), true);
    bgfx::ProgramHandle selectionMaskProgram = bgfx::createProgram(
        shader(vs_selection_mask_shader,sizeof(vs_selection_mask_shader)),
        shader(fs_selection_mask_shader,sizeof(fs_selection_mask_shader)),true);
    bgfx::ProgramHandle selectionOutlineProgram = bgfx::createProgram(
        shader(vs_fullscreen_shader,sizeof(vs_fullscreen_shader)),
        shader(fs_selection_outline_shader,sizeof(fs_selection_outline_shader)),true);
    bgfx::ProgramHandle histogramClearProgram = bgfx::createProgram(shader(cs_histogram_clear_shader, sizeof(cs_histogram_clear_shader)), true);
    bgfx::ProgramHandle histogramProgram = bgfx::createProgram(shader(cs_histogram_shader, sizeof(cs_histogram_shader)), true);
    bgfx::ProgramHandle autoExposureProgram = bgfx::createProgram(shader(cs_auto_exposure_shader, sizeof(cs_auto_exposure_shader)), true);
    std::array<bgfx::UniformHandle, kCascadeCount> uLightMatrices{
        bgfx::createUniform("u_lightMtx0", bgfx::UniformType::Mat4),
        bgfx::createUniform("u_lightMtx1", bgfx::UniformType::Mat4),
        bgfx::createUniform("u_lightMtx2", bgfx::UniformType::Mat4)};
    bgfx::UniformHandle uCascadeSplits = bgfx::createUniform("u_cascadeSplits", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uJoints = bgfx::createUniform("u_joints", bgfx::UniformType::Mat4, uint16_t(kMaxJoints));
    bgfx::UniformHandle uSkinning = bgfx::createUniform("u_skinning", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uCamera = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uViewportShading = bgfx::createUniform("u_viewportShading", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uLightDir = bgfx::createUniform("u_lightDirIntensity", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uLightColor = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uAmbientSky = bgfx::createUniform("u_ambientSky", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uAmbientGround = bgfx::createUniform("u_ambientGround", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uSkyRight = bgfx::createUniform("u_skyRight", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uSkyUp = bgfx::createUniform("u_skyUp", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uSkyForward = bgfx::createUniform("u_skyForward", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uEditorBackground = bgfx::createUniform("u_editorBackground", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uGridOrigin = bgfx::createUniform("u_gridOrigin", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uGridSpacing = bgfx::createUniform("u_gridSpacing", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uGridCounts = bgfx::createUniform("u_gridCounts", bgfx::UniformType::Vec4);
    std::array<bgfx::UniformHandle, 4> uPointLightPositionRadius{
        bgfx::createUniform("u_pointLightPositionRadius0", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightPositionRadius1", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightPositionRadius2", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightPositionRadius3", bgfx::UniformType::Vec4)};
    std::array<bgfx::UniformHandle, 4> uPointLightColorIntensity{
        bgfx::createUniform("u_pointLightColorIntensity0", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightColorIntensity1", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightColorIntensity2", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_pointLightColorIntensity3", bgfx::UniformType::Vec4)};
    bgfx::UniformHandle uPointLightShadow = bgfx::createUniform(
        "u_pointLightShadow", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uPointLightPattern = bgfx::createUniform(
        "u_pointLightPattern", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightPositionRadius = bgfx::createUniform(
        "u_spotLightPositionRadius", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightDirectionOuter = bgfx::createUniform(
        "u_spotLightDirectionOuter", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightColorIntensity = bgfx::createUniform(
        "u_spotLightColorIntensity", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightParams = bgfx::createUniform(
        "u_spotLightParams", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightShadow = bgfx::createUniform(
        "u_spotLightShadow", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uSpotLightUpPattern = bgfx::createUniform(
        "u_spotLightUpPattern", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uAreaLightPositionRadius = bgfx::createUniform(
        "u_areaLightPositionRadius", bgfx::UniformType::Vec4, 2);
    bgfx::UniformHandle uAreaLightDirectionWidth = bgfx::createUniform(
        "u_areaLightDirectionWidth", bgfx::UniformType::Vec4, 2);
    bgfx::UniformHandle uAreaLightUpHeight = bgfx::createUniform(
        "u_areaLightUpHeight", bgfx::UniformType::Vec4, 2);
    bgfx::UniformHandle uAreaLightColorIntensity = bgfx::createUniform(
        "u_areaLightColorIntensity", bgfx::UniformType::Vec4, 2);
    bgfx::UniformHandle uAreaLightShadow = bgfx::createUniform(
        "u_areaLightShadow", bgfx::UniformType::Vec4, 2);
    bgfx::UniformHandle uEmissiveLightPositionRadius = bgfx::createUniform(
        "u_emissiveLightPositionRadius", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightDirectionArea = bgfx::createUniform(
        "u_emissiveLightDirectionArea", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightRadiance = bgfx::createUniform(
        "u_emissiveLightRadiance", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightShadow = bgfx::createUniform(
        "u_emissiveLightShadow", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightShadowSecondary = bgfx::createUniform(
        "u_emissiveLightShadowSecondary", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightShadowOrigin = bgfx::createUniform(
        "u_emissiveLightShadowOrigin", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uEmissiveLightTangentWidth = bgfx::createUniform(
        "u_emissiveLightTangentWidth", bgfx::UniformType::Vec4, 4);
    bgfx::UniformHandle uPointShadowAtlasParams = bgfx::createUniform(
        "u_pointShadowAtlasParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uSunDirection = bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uAtmosphereParams = bgfx::createUniform("u_atmosphereParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uEnvironmentParams = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uHdriIrradiance = bgfx::createUniform("u_hdriIrradiance", bgfx::UniformType::Vec4, 9);
    bgfx::UniformHandle uHdriParams = bgfx::createUniform("u_hdriParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uProbeSun = bgfx::createUniform("u_probeSun", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uProbeParams = bgfx::createUniform("u_probeParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uProbeGround = bgfx::createUniform("u_probeGround", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uProbeFace = bgfx::createUniform("u_probeFace", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uLocalPrefilterParams = bgfx::createUniform("u_localPrefilterParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uReflectionProbePosition = bgfx::createUniform(
        "u_reflectionProbePosition", bgfx::UniformType::Vec4, kLocalProbeSlotCount);
    bgfx::UniformHandle uReflectionProbeMin = bgfx::createUniform(
        "u_reflectionProbeMin", bgfx::UniformType::Vec4, kLocalProbeSlotCount);
    bgfx::UniformHandle uReflectionProbeMax = bgfx::createUniform(
        "u_reflectionProbeMax", bgfx::UniformType::Vec4, kLocalProbeSlotCount);
    bgfx::UniformHandle uFogParams = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uDepthParams = bgfx::createUniform("u_depthParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uAoParams = bgfx::createUniform("u_aoParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uQualityParams = bgfx::createUniform("u_qualityParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uColorParams = bgfx::createUniform("u_colorParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uUpsampleParams = bgfx::createUniform("u_upsampleParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uDebugParams = bgfx::createUniform("u_debugParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uHistogramParams = bgfx::createUniform("u_histogramParams", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uExposureParams0 = bgfx::createUniform("u_exposureParams0", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uExposureParams1 = bgfx::createUniform("u_exposureParams1", bgfx::UniformType::Vec4);
    bgfx::UniformHandle uExposureParams2 = bgfx::createUniform("u_exposureParams2", bgfx::UniformType::Vec4);
    std::array<bgfx::UniformHandle, 9> materialUniforms{
        bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_textureFlags", bgfx::UniformType::Vec4),
        bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4),
        uCamera, uLightDir, uLightColor,
        bgfx::createUniform("u_materialOcclusion", bgfx::UniformType::Vec4)
    };
    std::array<bgfx::UniformHandle, 5> samplers{
        bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_metalRough", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_normal", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler)
    };
    std::array<bgfx::UniformHandle, kCascadeCount> sShadows{
        bgfx::createUniform("s_shadow0", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_shadow1", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_shadow2", bgfx::UniformType::Sampler)};
    bgfx::UniformHandle sHdr = bgfx::createUniform("s_hdr", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sFog = bgfx::createUniform("s_fog", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sSceneDepth = bgfx::createUniform("s_sceneDepth", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sSceneDepthTonemap = bgfx::createUniform("s_sceneDepthTonemap", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sFogDepth = bgfx::createUniform("s_fogDepth", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAoDepth = bgfx::createUniform("s_aoDepth", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAoNormal = bgfx::createUniform("s_aoNormal", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sMaterialAo = bgfx::createUniform("s_materialAo", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAo = bgfx::createUniform("s_ao", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAoDepthUpsample = bgfx::createUniform("s_aoDepthUpsample", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sBloom = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sHistogramHdr = bgfx::createUniform("s_histogramHdr", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sHistogramFog = bgfx::createUniform("s_histogramFog", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sHistogramAo = bgfx::createUniform("s_histogramAo", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sPreviousExposure = bgfx::createUniform("s_previousExposure", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sExposure = bgfx::createUniform("s_exposure", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sHud = bgfx::createUniform("s_hud", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sSelectionMask=bgfx::createUniform("s_selectionMask",bgfx::UniformType::Sampler);
    bgfx::UniformHandle sSceneSelection=bgfx::createUniform("s_sceneSelection",bgfx::UniformType::Sampler);
    bgfx::UniformHandle uSelectionId=bgfx::createUniform("u_selectionId",bgfx::UniformType::Vec4);
    bgfx::UniformHandle uSelectionOutline=bgfx::createUniform("u_selectionOutline",bgfx::UniformType::Vec4);
    std::array<bgfx::UniformHandle, kCascadeCount> sVolumeShadows{
        bgfx::createUniform("s_volumeShadow0", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_volumeShadow1", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_volumeShadow2", bgfx::UniformType::Sampler)};
    bgfx::UniformHandle sAtmosphereTransmittance = bgfx::createUniform("s_atmosphereTransmittance", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAtmosphereScattering = bgfx::createUniform("s_atmosphereScattering", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sAtmosphereSingleMie = bgfx::createUniform("s_atmosphereSingleMie", bgfx::UniformType::Sampler);
    std::array<bgfx::UniformHandle, 8> sEnvironment{
        bgfx::createUniform("s_environmentIrradiance", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_environmentSpecular", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_brdfLut", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_localReflectionProbe", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_localDiffuseProbe", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_gridAtlas", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_pointShadow0", bgfx::UniformType::Sampler),
        bgfx::createUniform("s_pointShadow1", bgfx::UniformType::Sampler)};
    bgfx::UniformHandle sProbeScattering = bgfx::createUniform("s_probeScattering", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sProbeSingleMie = bgfx::createUniform("s_probeSingleMie", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sProbeSource = bgfx::createUniform("s_probeSource", bgfx::UniformType::Sampler);
    bgfx::UniformHandle sHdri = bgfx::createUniform("s_hdri", bgfx::UniformType::Sampler);

    const fs::path atmosphereDir = fs::path(GENESIS_ROOT) / "assets" / "atmosphere" / "bruneton";
    bgfx::TextureHandle atmosphereTransmittance = loadBrunetonTexture(atmosphereDir / "transmittance.rgb32f", false);
    bgfx::TextureHandle atmosphereScattering = loadBrunetonTexture(atmosphereDir / "scattering.rgb32f", true);
    bgfx::TextureHandle atmosphereSingleMie = loadBrunetonTexture(atmosphereDir / "single_mie.rgb32f", true);
    bgfx::TextureHandle atmosphereIrradiance = loadBrunetonTexture(atmosphereDir / "irradiance.rgb32f", false);
    const auto hdriLighting = loadHdriTexture(genesis::scripting::g_environment.hdriPath,
        radians(genesis::scripting::g_environment.rotationDegrees));
    bgfx::TextureHandle hdriTexture = hdriLighting.texture;
    const bool hdriEnabled = bgfx::isValid(hdriTexture);
    if (!genesis::scripting::g_environment.hdriPath.empty()) {
        if (hdriEnabled)
            std::printf("Loaded HDRI: %s\n", genesis::scripting::g_environment.hdriPath.c_str());
        else
            std::fprintf(stderr, "Cannot decode HDRI: %s\n", genesis::scripting::g_environment.hdriPath.c_str());
    }
    if (!hdriEnabled) hdriTexture = solidTexture(0xff000000);
    const uint64_t environmentFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    if (!bgfx::isTextureValid(0, true, 1, bgfx::TextureFormat::RGBA16F, environmentFlags)) {
        std::fprintf(stderr, "RGBA16F render-target cubemaps are not supported\n");
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 5;
    }
    bgfx::TextureHandle environmentSpecular = bgfx::createTextureCube(quality.environmentSize, true, 1,
        bgfx::TextureFormat::RGBA16F, environmentFlags);
    std::array<bgfx::FrameBufferHandle, kEnvironmentViewCount> environmentBuffers;
    for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip) {
        for (uint8_t face = 0; face < 6; ++face) {
            bgfx::Attachment attachment;
            attachment.init(environmentSpecular, bgfx::Access::Write, face, 1, mip, BGFX_RESOLVE_NONE);
            environmentBuffers[size_t(mip) * 6 + face] = bgfx::createFrameBuffer(1, &attachment, false);
        }
    }
    bgfx::TextureHandle brdfLut = bgfx::createTexture2D(256, 256, false, 1,
        bgfx::TextureFormat::RG16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::FrameBufferHandle brdfLutBuffer = bgfx::createFrameBuffer(1, &brdfLut, false);

    std::array<bgfx::TextureHandle, kLocalProbeSlotCount> localProbeRaw;
    std::array<bgfx::TextureHandle, kLocalProbeSlotCount> localProbeFiltered;
    std::array<bgfx::TextureHandle, kLocalProbeSlotCount> localDiffuseProbe;
    for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) {
        localProbeRaw[slot] = bgfx::createTextureCube(quality.environmentSize, false, 1,
            bgfx::TextureFormat::RGBA16F, environmentFlags);
        localProbeFiltered[slot] = bgfx::createTextureCube(quality.environmentSize, true, 1,
            bgfx::TextureFormat::RGBA16F, environmentFlags);
        localDiffuseProbe[slot] = bgfx::createTextureCube(16, false, 1,
            bgfx::TextureFormat::RGBA16F, environmentFlags);
    }
    const uint64_t atlasFlags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    constexpr uint16_t localProbeLayers = kLightPatternBaseLayer + kLightPatternSlotCount;
    bgfx::TextureHandle localReflectionAtlas = bgfx::createTexture2D(
        quality.environmentSize, quality.environmentSize, true, localProbeLayers,
        bgfx::TextureFormat::RGBA16F, atlasFlags);
    bgfx::TextureHandle localDiffuseAtlas = bgfx::createTexture2D(
        16, 16, false, kLightPatternBaseLayer, bgfx::TextureFormat::RGBA16F, atlasFlags);
    for (size_t patternIndex = 0; patternIndex < lightPatternPaths.size(); ++patternIndex) {
        const fs::path path = fs::u8path(lightPatternPaths[patternIndex]);
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        std::vector<uint16_t> pixels = extension == ".ies"
            ? genesis::raster::loadIesPattern(path, quality.environmentSize)
            : genesis::raster::loadCookiePattern(path, quality.environmentSize);
        if (pixels.empty()) {
            std::fprintf(stderr, "Cannot decode light pattern: %s\n", path.string().c_str());
            pixels.resize(size_t(quality.environmentSize) * quality.environmentSize * 4,
                bx::halfFromFloat(1.0f));
        }
        bgfx::updateTexture2D(localReflectionAtlas,
            uint16_t(kLightPatternBaseLayer + patternIndex), 0, 0, 0,
            quality.environmentSize, quality.environmentSize,
            bgfx::copy(pixels.data(), uint32_t(pixels.size() * sizeof(uint16_t))));
    }
    std::array<bgfx::TextureHandle, kLocalCaptureViewCount> localProbeDepth;
    std::array<bgfx::FrameBufferHandle, kLocalCaptureViewCount> localCaptureBuffers;
    for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) for (uint8_t face = 0; face < 6; ++face) {
        const size_t index = size_t(slot) * 6 + face;
        localProbeDepth[index] = bgfx::createTexture2D(quality.environmentSize, quality.environmentSize, false, 1,
            bgfx::TextureFormat::D16, BGFX_TEXTURE_RT);
        bgfx::Attachment attachments[2];
        attachments[0].init(localProbeRaw[slot], bgfx::Access::Write, face, 1, 0, BGFX_RESOLVE_NONE);
        attachments[1].init(localProbeDepth[index], bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        localCaptureBuffers[index] = bgfx::createFrameBuffer(2, attachments, false);
    }
    std::array<bgfx::FrameBufferHandle, kLocalPrefilterViewCount> localPrefilterBuffers;
    for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip) {
        for (uint8_t face = 0; face < 6; ++face) {
            const size_t index = (size_t(slot) * quality.environmentMipCount + mip) * 6 + face;
            bgfx::Attachment attachment;
            attachment.init(localProbeFiltered[slot], bgfx::Access::Write, face, 1, mip, BGFX_RESOLVE_NONE);
            localPrefilterBuffers[index] = bgfx::createFrameBuffer(1, &attachment, false);
        }
    }
    std::array<bgfx::FrameBufferHandle, kLocalDiffuseViewCount> localDiffuseBuffers;
    for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) for (uint8_t face = 0; face < 6; ++face) {
        const size_t index = size_t(slot) * 6 + face;
        bgfx::Attachment attachment;
        attachment.init(localDiffuseProbe[slot], bgfx::Access::Write, face, 1, 0, BGFX_RESOLVE_NONE);
        localDiffuseBuffers[index] = bgfx::createFrameBuffer(1, &attachment, false);
    }
    const uint64_t pointShadowFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    // Slot zero remains a cubemap. The other three slots share eighteen layers
    // of one 2D array, so PBR stays within its sixteen texture stages.
    std::array<bgfx::TextureHandle, kPointShadowTextureCount> pointShadowTextures;
    std::array<bgfx::FrameBufferHandle, kPointShadowViewCount> pointShadowBuffers;
    pointShadowTextures[0] = bgfx::createTextureCube(quality.pointShadowSize, false, 1,
        bgfx::TextureFormat::D16, pointShadowFlags);
    pointShadowTextures[1] = bgfx::createTexture2D(quality.pointShadowSize, quality.pointShadowSize,
        false, 6 * (kPointShadowSlotCount - 1), bgfx::TextureFormat::D16, pointShadowFlags);
    for (uint8_t slot = 0; slot < kPointShadowSlotCount; ++slot) {
        for (uint8_t face = 0; face < 6; ++face) {
            bgfx::Attachment attachment;
            attachment.init(pointShadowTextures[slot == 0 ? 0 : 1], bgfx::Access::Write,
                slot == 0 ? face : uint16_t((slot - 1) * 6 + face), 1, 0, BGFX_RESOLVE_NONE);
            pointShadowBuffers[size_t(slot) * 6 + face] = bgfx::createFrameBuffer(1, &attachment, false);
        }
    }
    std::array<bgfx::TextureHandle, 8> environmentTextures{
        atmosphereIrradiance, environmentSpecular, brdfLut,
        localReflectionAtlas, localDiffuseAtlas, brdfLut,
        pointShadowTextures[0], pointShadowTextures[1]};

    std::array<bgfx::TextureHandle, kCascadeCount> shadowTextures;
    std::array<bgfx::FrameBufferHandle, kCascadeCount> shadowBuffers;
    for (uint16_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        shadowTextures[cascade] = bgfx::createTexture2D(quality.shadowSize, quality.shadowSize, false, 1, bgfx::TextureFormat::D16,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        shadowBuffers[cascade] = bgfx::createFrameBuffer(1, &shadowTextures[cascade], false);
    }
    bgfx::TextureHandle hdrColor = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1,
        bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle hdrDepth = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1,
        bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
    bgfx::TextureHandle sceneNormal = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle sceneSelection = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    const bgfx::TextureHandle hdrAttachments[]{hdrColor, sceneNormal, sceneSelection, hdrDepth};
    bgfx::FrameBufferHandle hdrBuffer = bgfx::createFrameBuffer(4, hdrAttachments, false);
    bgfx::TextureHandle selectionColor=bgfx::createTexture2D(bgfx::BackbufferRatio::Equal,false,1,
        bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle selectionDepth=bgfx::createTexture2D(bgfx::BackbufferRatio::Equal,false,1,
        bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
            BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    const bgfx::TextureHandle selectionAttachments[]{selectionColor,selectionDepth};
    bgfx::FrameBufferHandle selectionBuffer=bgfx::createFrameBuffer(2,selectionAttachments,false);
    bgfx::TextureHandle aoTexture = bgfx::createTexture2D(quality.effectsRatio, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle aoDepth = bgfx::createTexture2D(quality.effectsRatio, false, 1,
        bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    const bgfx::TextureHandle aoAttachments[]{aoTexture, aoDepth};
    bgfx::FrameBufferHandle aoBuffer = bgfx::createFrameBuffer(2, aoAttachments, false);
    bgfx::TextureHandle fogTexture = bgfx::createTexture2D(quality.effectsRatio, false, 1,
        bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle fogDepth = bgfx::createTexture2D(quality.effectsRatio, false, 1,
        bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    const bgfx::TextureHandle fogAttachments[]{fogTexture, fogDepth};
    bgfx::FrameBufferHandle fogBuffer = bgfx::createFrameBuffer(2, fogAttachments, false);
    bgfx::DynamicIndexBufferHandle luminanceHistogram = bgfx::createDynamicIndexBuffer(256,
        BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    std::array<bgfx::TextureHandle, 4> exposureTextures;
    const uint64_t exposureFlags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    for (bgfx::TextureHandle& texture : exposureTextures) {
        float initialExposure[4]{genesis::color::settings().exposure, 0.0f, 0.0f, 1.0f};
        texture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
            exposureFlags, bgfx::copy(initialExposure, sizeof(initialExposure)));
    }
    std::array<bgfx::TextureHandle, 3> fallback{solidTexture(0xffffffff), solidTexture(0xffff8080), solidTexture(0xff000000)};

    std::vector<std::unique_ptr<SceneModel>> models;
    if (!genesis::scripting::g_model.empty()) {
        auto model = std::make_unique<SceneModel>();
        if (model->load(genesis::scripting::g_model, {}, RenderLayer::World)) models.push_back(std::move(model));
    }
    for (const auto& entity : genesis::gameplay::g_world.snapshots()) {
        if (!entity.renderable) continue;
        auto model = std::make_unique<SceneModel>();
        model->entityId=entity.id; model->visible=entity.renderable->visible;model->materialOverride=entity.renderable->material;
        if (model->load(entity.renderable->path, entity.transform, editorMode ? RenderLayer::World : RenderLayer::Dynamic,
            entity.renderable->meshVertices)) models.push_back(std::move(model));
    }

    struct DiffuseGridResources {
        std::array<float, 4> origin{}, spacing{}, counts{};
        bgfx::TextureHandle atlas = BGFX_INVALID_HANDLE;
    };
    std::vector<DiffuseGridResources> diffuseGrids;
    if (!genesis::scripting::g_reflectionProbes.empty()) {
        std::vector<genesis::probes::Triangle> triangles;
        for (const auto& model : models) model->appendProbeGeometry(triangles);
        genesis::probes::Geometry geometry(std::move(triangles));
        diffuseGrids.reserve(genesis::scripting::g_reflectionProbes.size());
        const uint64_t gridFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        for (const auto& probe : scriptLighting.probes) {
            genesis::probes::Grid grid(geometry,
                {probe.boundsMin[0], probe.boundsMin[1], probe.boundsMin[2]},
                {probe.boundsMax[0], probe.boundsMax[1], probe.boundsMax[2]});
            DiffuseGridResources resources;
            resources.origin = grid.origin;
            resources.spacing = grid.spacing;
            resources.counts = grid.counts;
            resources.origin[3] = 1.0f;
            static_assert(genesis::probes::Grid::resolution == genesis::probes::GridAtlas::resolution);
            const auto atlas = genesis::probes::packGridAtlas(uint32_t(grid.size()), grid.visibility, grid.lighting);
            resources.atlas = bgfx::createTexture2D(uint16_t(atlas.width), uint16_t(atlas.height),
                false, 1, bgfx::TextureFormat::RGBA32F, gridFlags,
                bgfx::copy(atlas.rgba.data(), uint32_t(atlas.rgba.size() * sizeof(float))));
            std::printf("Diffuse probe grid '%s': %dx%dx%d (%d probes)\n", probe.name.c_str(),
                int(grid.counts[0]), int(grid.counts[1]), int(grid.counts[2]), grid.size());
            diffuseGrids.push_back(resources);
        }
    }

    constexpr uint16_t hudHeight = 118;
    genesis::ui::RendererHud rendererHud;
    genesis::raster::BgfxUiRenderer uiRenderer;
    bool gpuUi=uiRenderer.initialize();
    std::printf("Genesis UI: %s\n",gpuUi?"bgfx GPU":"ThorVG software");
    std::unique_ptr<genesis::editor::GameEditor> editor;
    std::unique_ptr<genesis::editor::SdlEditorHost> editorHost;
    genesis::raster::EditorOverlay editorOverlay;
#ifdef GENESIS_WITH_PHYSX_PBD
    std::unique_ptr<genesis::physics::PhysxParticles> particleRuntime;
    bool particleRuntimeStarted=false;
    std::vector<genesis::physics::ParticleFrame> particlePreview;
    uint64_t particlePreviewRevision=~uint64_t(0);
#endif
    if (editorMode) {
        editor=std::make_unique<genesis::editor::GameEditor>(genesis::gameplay::g_world,sceneDocument,fs::path(GENESIS_ROOT)/"assets",launch.projectRoot);
        editor->setLightCapacity({4-int(scriptLighting.points.size()),4-int(scriptLighting.spots.size()),2-int(scriptLighting.areas.size())});
        editor->setQuality(genesis::scripting::g_renderer.quality);
        if(captureTool=="rotate")editor->setTool(genesis::editor::TransformTool::Rotate);
        else if(captureTool=="scale")editor->setTool(genesis::editor::TransformTool::Scale);
        else if(captureTool=="transform")editor->setTool(genesis::editor::TransformTool::Transform);
        if(captureShading=="wireframe")editor->setViewportShading(genesis::editor::GameEditor::ViewportShading::Wireframe);
        else if(captureShading=="material")editor->setViewportShading(genesis::editor::GameEditor::ViewportShading::MaterialPreview);
        else if(captureShading=="rendered")editor->setViewportShading(genesis::editor::GameEditor::ViewportShading::Rendered);
        editor->layout(float(width),float(height));
        editorHost=std::make_unique<genesis::editor::SdlEditorHost>(*editor,window,genesis::scripting::g_camera,runtimeCamera);
#ifdef GENESIS_WITH_PHYSX_PBD
        particleRuntime=std::make_unique<genesis::physics::PhysxParticles>();
#endif
        editor->setTickHandler([&](float delta){
            editorHost->gameTick(delta);
#ifdef GENESIS_WITH_PHYSX_PBD
            std::string error;
            if(!particleRuntimeStarted){
                const auto entities=genesis::gameplay::g_world.snapshots();
                std::vector<genesis::physics::ColliderMesh> colliders;
                const bool hasParticles=std::any_of(entities.begin(),entities.end(),[](const auto& entity){
                    return entity.particles && entity.particles->enabled;
                });
                if(hasParticles)for(const auto& entity:entities)if(entity.renderable && entity.renderable->visible){
                    genesis::physics::ColliderMesh mesh;
                    if(!loadColliderMesh(entity,mesh,error)){
                        editor->stop();editor->status("PhysX: "+error);editor->showConsole();return;
                    }
                    if(!mesh.triangles.empty())colliders.push_back(std::move(mesh));
                }
                if(!particleRuntime->start(entities,colliders,error)){
                    editor->stop();editor->status("PhysX: "+error);editor->showConsole();return;
                }
                particleRuntimeStarted=true;
            }
            particleRuntime->syncTransforms(genesis::gameplay::g_world.snapshots());
            if(!particleRuntime->step(delta,error)){
                particleRuntime->stop();particleRuntimeStarted=false;
                editor->stop();editor->status("PhysX: "+error);editor->showConsole();
            }
#endif
        });
        // Older editor saves wrote a unit-length fly-camera look target. Keep
        // the same view, but recover a useful scene-depth orbit pivot once.
        if (sceneDocument.editorCamera() && !sceneDocument.hasEditorOrbitPivot()) {
            genesis::editor::Bounds bounds;
            for (const auto& model:models) if (model->visible) {
                const auto b=model->bounds(); if(b.valid()) { bounds.include(b.minimum); bounds.include(b.maximum); }
            }
            if (bounds.valid()) {
                auto& camera=editorHost->camera(); const auto eye=camera.position(), center=bounds.center();
                const auto ray=genesis::editor::cameraRay(eye,camera.yaw(),camera.pitch(),camera.fov(),1,.5f,.5f);
                float distance=0; for(int axis=0;axis<3;++axis) distance+=(center[axis]-eye[axis])*ray.direction[axis];
                if(distance>.1f) camera.setPivotDepth(distance);
            }
        }
        if (!editorOverlay.initialize()) std::fprintf(stderr,"Editor overlays unavailable\n");
    }
    uint64_t editorRevision=editor ? editor->revision() : 0;
    uint64_t hudRevision=0;
    bgfx::TextureHandle hudTexture = bgfx::createTexture2D(uint16_t(width), editorMode ? uint16_t(height) : hudHeight, false, 1,
        bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bool running = true;
    bool renderFailed = false;
    bool mouseLook = false;
    bool sunDrag = false;
    bool cookieDrag = false;
    int interactiveCookieIndex = -1;
    for (size_t index = 0; index < genesis::scripting::g_spotLights.size(); ++index) {
        if (!genesis::scripting::g_spotLights[index].cookieTexture.empty()) {
            interactiveCookieIndex = int(index);
            break;
        }
    }
    std::array<std::array<float, 3>, 4> spotDirections{};
    std::array<std::array<float, 3>, 4> spotUps{};
    for (size_t index = 0; index < genesis::scripting::g_spotLights.size(); ++index) {
        spotDirections[index] = genesis::scripting::g_spotLights[index].direction;
        spotUps[index] = genesis::scripting::g_spotLights[index].up;
    }
    const auto initialSpotDirections = spotDirections;
    const auto initialSpotUps = spotUps;
    bool brdfDirty = true;
    bool environmentDirty = true;
    std::array<bool, kLocalProbeSlotCount> localProbeDirty{};
    std::array<int, kLocalProbeSlotCount> activeReflectionProbes{-1, -1};
    std::array<float, 3> lastProbeSun{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    const std::array<float, 3> initialCameraPosition = genesis::scripting::g_camera.position;
    std::array<float, 3> cameraPosition = initialCameraPosition;
    float initialDirection[3]{
        genesis::scripting::g_camera.target[0] - cameraPosition[0],
        genesis::scripting::g_camera.target[1] - cameraPosition[1],
        genesis::scripting::g_camera.target[2] - cameraPosition[2]
    };
    const float initialLength = std::sqrt(initialDirection[0] * initialDirection[0] + initialDirection[1] * initialDirection[1] + initialDirection[2] * initialDirection[2]);
    for (float& value : initialDirection) value /= std::max(initialLength, 1e-5f);
    const float initialCameraYaw = std::atan2(initialDirection[0], initialDirection[2]);
    const float initialCameraPitch = std::asin(std::clamp(initialDirection[1], -1.0f, 1.0f));
    const float initialFlySpeed = genesis::scripting::g_camera.speed;
    float cameraYaw = initialCameraYaw;
    float cameraPitch = initialCameraPitch;
    float flySpeed = initialFlySpeed;
    const auto start = std::chrono::steady_clock::now();
    auto previousFrame = start;
    uint64_t frameIndex = 0;
    double smoothedGpuMs = -1.0;
    double smoothedShadowMs = 0.0;
    double smoothedSceneMs = 0.0;
    double smoothedAoMs = 0.0;
    double smoothedFogMs = 0.0;
    double smoothedPostMs = 0.0;
    struct PreviousShadowKey {
        int kind = -1;
        int index = -1;
        const SceneModel* sourceModel = nullptr;
        size_t clusterIndex = SIZE_MAX;
    };
    std::array<PreviousShadowKey, kPointShadowSlotCount> previousLocalShadows{};
    auto switchQuality = [&](const std::string& preset) {
        if (preset == genesis::scripting::g_renderer.quality) return;
        // Finish with all view attachments before replacing their textures.
        for (size_t index = 0; index < size_t(quality.environmentMipCount) * 6; ++index)
            bgfx::destroy(environmentBuffers[index]);
        for (auto buffer : localCaptureBuffers) bgfx::destroy(buffer);
        for (size_t index = 0; index < size_t(kLocalProbeSlotCount) * quality.environmentMipCount * 6; ++index)
            bgfx::destroy(localPrefilterBuffers[index]);
        for (auto buffer : pointShadowBuffers) bgfx::destroy(buffer);
        for (auto buffer : shadowBuffers) bgfx::destroy(buffer);
        bgfx::destroy(aoBuffer);
        bgfx::destroy(fogBuffer);
        bgfx::destroy(environmentSpecular);
        for (auto texture : localProbeDepth) bgfx::destroy(texture);
        for (auto texture : localProbeRaw) bgfx::destroy(texture);
        for (auto texture : localProbeFiltered) bgfx::destroy(texture);
        bgfx::destroy(localReflectionAtlas);
        for (auto texture : pointShadowTextures) bgfx::destroy(texture);
        for (auto texture : shadowTextures) bgfx::destroy(texture);
        bgfx::destroy(aoTexture); bgfx::destroy(aoDepth);
        bgfx::destroy(fogTexture); bgfx::destroy(fogDepth);
        // bgfx retires framebuffer handles on its render thread. Drain the
        // queued frames before allocating the replacement set; otherwise the
        // old and new probe faces exceed the configured handle capacity.
        bgfx::frame();
        bgfx::frame();

        quality = qualityProfile(preset);
        genesis::scripting::g_renderer.quality = preset;
        environmentSpecular = bgfx::createTextureCube(quality.environmentSize, true, 1,
            bgfx::TextureFormat::RGBA16F, environmentFlags);
        for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip)
            for (uint8_t face = 0; face < 6; ++face) {
                bgfx::Attachment attachment;
                attachment.init(environmentSpecular, bgfx::Access::Write, face, 1, mip, BGFX_RESOLVE_NONE);
                environmentBuffers[size_t(mip) * 6 + face] = bgfx::createFrameBuffer(1, &attachment, false);
            }
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) {
            localProbeRaw[slot] = bgfx::createTextureCube(quality.environmentSize, false, 1,
                bgfx::TextureFormat::RGBA16F, environmentFlags);
            localProbeFiltered[slot] = bgfx::createTextureCube(quality.environmentSize, true, 1,
                bgfx::TextureFormat::RGBA16F, environmentFlags);
        }
        localReflectionAtlas = bgfx::createTexture2D(
            quality.environmentSize, quality.environmentSize, true, localProbeLayers,
            bgfx::TextureFormat::RGBA16F, atlasFlags);
        for (size_t patternIndex = 0; patternIndex < lightPatternPaths.size(); ++patternIndex) {
            const fs::path path = fs::u8path(lightPatternPaths[patternIndex]);
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            std::vector<uint16_t> pixels = extension == ".ies"
                ? genesis::raster::loadIesPattern(path, quality.environmentSize)
                : genesis::raster::loadCookiePattern(path, quality.environmentSize);
            if (pixels.empty())
                pixels.resize(size_t(quality.environmentSize) * quality.environmentSize * 4,
                    bx::halfFromFloat(1.0f));
            bgfx::updateTexture2D(localReflectionAtlas,
                uint16_t(kLightPatternBaseLayer + patternIndex), 0, 0, 0,
                quality.environmentSize, quality.environmentSize,
                bgfx::copy(pixels.data(), uint32_t(pixels.size() * sizeof(uint16_t))));
        }
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot)
            for (uint8_t face = 0; face < 6; ++face) {
                const size_t index = size_t(slot) * 6 + face;
                localProbeDepth[index] = bgfx::createTexture2D(quality.environmentSize,
                    quality.environmentSize, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT);
                bgfx::Attachment attachments[2];
                attachments[0].init(localProbeRaw[slot], bgfx::Access::Write, face, 1, 0, BGFX_RESOLVE_NONE);
                attachments[1].init(localProbeDepth[index], bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
                localCaptureBuffers[index] = bgfx::createFrameBuffer(2, attachments, false);
            }
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot)
            for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip)
                for (uint8_t face = 0; face < 6; ++face) {
                    const size_t index = (size_t(slot) * quality.environmentMipCount + mip) * 6 + face;
                    bgfx::Attachment attachment;
                    attachment.init(localProbeFiltered[slot], bgfx::Access::Write, face, 1, mip, BGFX_RESOLVE_NONE);
                    localPrefilterBuffers[index] = bgfx::createFrameBuffer(1, &attachment, false);
                }
        pointShadowTextures[0] = bgfx::createTextureCube(quality.pointShadowSize, false, 1,
            bgfx::TextureFormat::D16, pointShadowFlags);
        pointShadowTextures[1] = bgfx::createTexture2D(quality.pointShadowSize, quality.pointShadowSize,
            false, 6 * (kPointShadowSlotCount - 1), bgfx::TextureFormat::D16, pointShadowFlags);
        for (uint8_t slot = 0; slot < kPointShadowSlotCount; ++slot) {
            for (uint8_t face = 0; face < 6; ++face) {
                bgfx::Attachment attachment;
                attachment.init(pointShadowTextures[slot == 0 ? 0 : 1], bgfx::Access::Write,
                    slot == 0 ? face : uint16_t((slot - 1) * 6 + face), 1, 0, BGFX_RESOLVE_NONE);
                pointShadowBuffers[size_t(slot) * 6 + face] = bgfx::createFrameBuffer(1, &attachment, false);
            }
        }
        for (uint16_t cascade = 0; cascade < kCascadeCount; ++cascade) {
            shadowTextures[cascade] = bgfx::createTexture2D(quality.shadowSize, quality.shadowSize,
                false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT |
                BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            shadowBuffers[cascade] = bgfx::createFrameBuffer(1, &shadowTextures[cascade], false);
        }
        aoTexture = bgfx::createTexture2D(quality.effectsRatio, false, 1,
            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        aoDepth = bgfx::createTexture2D(quality.effectsRatio, false, 1,
            bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        const bgfx::TextureHandle nextAoAttachments[]{aoTexture, aoDepth};
        aoBuffer = bgfx::createFrameBuffer(2, nextAoAttachments, false);
        fogTexture = bgfx::createTexture2D(quality.effectsRatio, false, 1,
            bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        fogDepth = bgfx::createTexture2D(quality.effectsRatio, false, 1,
            bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        const bgfx::TextureHandle nextFogAttachments[]{fogTexture, fogDepth};
        fogBuffer = bgfx::createFrameBuffer(2, nextFogAttachments, false);
        environmentTextures[1] = environmentSpecular;
        environmentTextures[3] = localReflectionAtlas;
        environmentTextures[6] = pointShadowTextures[0];
        environmentTextures[7] = pointShadowTextures[1];
        environmentDirty = true;
        localProbeDirty.fill(true);
        smoothedGpuMs = -1.0;
        std::printf("Genesis quality: %s\n", preset.c_str());
        std::fflush(stdout);
    };
    uint64_t liveCircleStrokeId=0;
    std::vector<genesis::gameplay::EntityId> liveCircleHits;
    while (running) {
        const auto frameTime = std::chrono::steady_clock::now();
        const float deltaTime = std::min(std::chrono::duration<float>(frameTime - previousFrame).count(), 0.1f);
        previousFrame = frameTime;
        if (editor) editor->layout(float(width),float(height));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const bool editorHandled=editorHost && editorHost->event(event);
            if(editorHandled) continue;
            if (event.type == SDL_EVENT_QUIT) {
                if (!editorHost || editorHost->canClose()) running = false;
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                mouseLook=sunDrag=cookieDrag=false; SDL_SetWindowRelativeMouseMode(window,false);
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                width = std::max(event.window.data1, 1); height = std::max(event.window.data2, 1);
                bgfx::SwapChain resized;
                resized.width = uint32_t(width);
                resized.height = uint32_t(height);
                if(editorMode)resized.maxFrameLatency=1;
                bgfx::reset(genesis::scripting::g_window.vsync && !editorMode ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &resized);
                bgfx::destroy(hudTexture);
                hudRevision=0;
                if (editor) editor->layout(float(width),float(height));
                hudTexture = bgfx::createTexture2D(uint16_t(width), editorMode ? uint16_t(height) : hudHeight, false, 1,
                    bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            }
            else if (!editorMode && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
                mouseLook = true;
                SDL_SetWindowRelativeMouseMode(window, true);
            }
            else if (editor && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT
                && editor->viewport().contains(event.button.x,event.button.y)) {
                const auto viewport=editor->viewport();
                const auto& editorCamera=editorHost->camera();
                const auto ray=genesis::editor::cameraRay(editorCamera.position(),editorCamera.yaw(),editorCamera.pitch(),editorCamera.fov(),
                    viewport.width/viewport.height,(event.button.x-viewport.x)/viewport.width,(event.button.y-viewport.y)/viewport.height);
                float nearest=INFINITY; std::optional<genesis::gameplay::EntityId> selected;
                for (const auto& model:models) if (model->visible && model->entityId) {
                    const float distance=model->intersectRay(ray);
                    if (distance<nearest) { nearest=distance; selected=model->entityId; }
                }
                for(const auto& entity:genesis::gameplay::g_world.snapshots()) if(!entity.renderable) {
                    const auto point=genesis::editor::gizmoMath::project(entity.transform.position,editorCamera,viewport);
                    if(!point || std::hypot((*point)[0]-event.button.x,(*point)[1]-event.button.y)>10.0f)continue;
                    const auto delta=genesis::editor::gizmoMath::sub(entity.transform.position,ray.origin);
                    const float distance=genesis::editor::gizmoMath::dot(delta,ray.direction);
                    if(distance>0 && distance<nearest){nearest=distance;selected=entity.id;}
                }
                editor->selectMany(selected?std::vector<genesis::gameplay::EntityId>{*selected}:
                    std::vector<genesis::gameplay::EntityId>{},
                    (SDL_GetModState() & SDL_KMOD_SHIFT)!=0,
                    (SDL_GetModState() & (SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                     !editorMode && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
                const bool forceSun = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                cookieDrag = interactiveCookieIndex >= 0 && !forceSun;
                sunDrag = !cookieDrag;
                SDL_SetWindowRelativeMouseMode(window, true);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
                mouseLook = false;
                if (!sunDrag && !cookieDrag) SDL_SetWindowRelativeMouseMode(window, false);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT &&
                     (sunDrag || cookieDrag)) {
                sunDrag = false;
                cookieDrag = false;
                if (!mouseLook) SDL_SetWindowRelativeMouseMode(window, false);
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && cookieDrag) {
                constexpr float flashlightSensitivity = 0.004f;
                auto rotateAround = [](const std::array<float, 3>& vector,
                    const std::array<float, 3>& axis, float angle) {
                    const float cosine = std::cos(angle);
                    const float sine = std::sin(angle);
                    const float axisDotVector = axis[0] * vector[0] + axis[1] * vector[1] + axis[2] * vector[2];
                    return std::array<float, 3>{
                        vector[0] * cosine + (axis[1] * vector[2] - axis[2] * vector[1]) * sine
                            + axis[0] * axisDotVector * (1.0f - cosine),
                        vector[1] * cosine + (axis[2] * vector[0] - axis[0] * vector[2]) * sine
                            + axis[1] * axisDotVector * (1.0f - cosine),
                        vector[2] * cosine + (axis[0] * vector[1] - axis[1] * vector[0]) * sine
                            + axis[2] * axisDotVector * (1.0f - cosine)};
                };
                const size_t index = size_t(interactiveCookieIndex);
                auto& direction = spotDirections[index];
                auto& up = spotUps[index];
                direction = rotateAround(direction, up, event.motion.xrel * flashlightSensitivity);
                std::array<float, 3> right{
                    direction[1] * up[2] - direction[2] * up[1],
                    direction[2] * up[0] - direction[0] * up[2],
                    direction[0] * up[1] - direction[1] * up[0]};
                const float rightLength = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
                for (float& component : right) component /= std::max(rightLength, 1e-5f);
                const float pitch = -event.motion.yrel * flashlightSensitivity;
                direction = rotateAround(direction, right, pitch);
                up = rotateAround(up, right, pitch);
                localProbeDirty.fill(true);
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && sunDrag) {
                constexpr float sunSensitivity = 0.20f;
                genesis::atmosphere::nudgeSun(event.motion.xrel * sunSensitivity, -event.motion.yrel * sunSensitivity);
                environmentDirty = true;
                localProbeDirty.fill(true);
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && mouseLook) {
                constexpr float sensitivity = 0.0025f;
                cameraYaw += event.motion.xrel * sensitivity;
                cameraPitch = std::clamp(cameraPitch - event.motion.yrel * sensitivity, -1.50f, 1.50f);
            }
            else if (!editorMode && event.type == SDL_EVENT_MOUSE_WHEEL) {
                flySpeed = std::clamp(flySpeed * std::pow(1.15f, event.wheel.y), 0.1f, 100.0f);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
                     (mouseLook || sunDrag || cookieDrag)) {
                mouseLook = false;
                sunDrag = false;
                cookieDrag = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12 && !event.key.repeat) {
                const fs::path capture = timestampedScreenshotPath();
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, capture.string().c_str());
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F6 && !event.key.repeat) {
                const std::string& current = genesis::scripting::g_renderer.quality;
                switchQuality(current == "low" ? "medium" : current == "medium" ? "high" : "low");
                if (editor) editor->setQuality(genesis::scripting::g_renderer.quality);
            }
            else if (!editorMode && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R && !event.key.repeat) {
                cameraPosition = initialCameraPosition;
                cameraYaw = initialCameraYaw;
                cameraPitch = initialCameraPitch;
                flySpeed = initialFlySpeed;
                genesis::atmosphere::setSun(initialSunAzimuth, initialSunElevation);
                spotDirections = initialSpotDirections;
                spotUps = initialSpotUps;
                environmentDirty = true;
                localProbeDirty.fill(true);
            }
        }
        if(editorHost) if(auto request=editorHost->takeSelectionRequest()) {
            if(request->mode==genesis::editor::GameEditor::SelectionMode::Circle &&
                request->strokeId!=liveCircleStrokeId) {
                liveCircleStrokeId=request->strokeId;
                liveCircleHits.clear();
            }
            const auto viewport=editor->viewport();
            const auto& camera=editorHost->camera();
            const auto pickPoint=[&](genesis::editor::gizmoMath::Vec2 point) -> std::optional<genesis::gameplay::EntityId> {
                const auto ray=genesis::editor::gizmoMath::rayAt(point,camera,viewport);
                float nearest=INFINITY;std::optional<genesis::gameplay::EntityId> hit;
                for(const auto& model:models) if(model->visible && model->entityId) {
                    const float distance=model->intersectRay(ray);
                    if(distance<nearest) {nearest=distance;hit=model->entityId;}
                }
                for(const auto& entity:genesis::gameplay::g_world.snapshots()) {
                    if(entity.renderable)continue;
                    const auto projected=genesis::editor::gizmoMath::project(entity.transform.position,camera,viewport);
                    if(!projected || std::hypot((*projected)[0]-point[0],(*projected)[1]-point[1])>10.0f)continue;
                    const auto delta=genesis::editor::gizmoMath::sub(entity.transform.position,ray.origin);
                    const float distance=genesis::editor::gizmoMath::dot(delta,ray.direction);
                    if(distance>0 && distance<nearest){nearest=distance;hit=entity.id;}
                }
                return hit;
            };
            std::vector<genesis::gameplay::EntityId> hits;
            const auto addHit=[&](genesis::gameplay::EntityId id){
                if(std::find(hits.begin(),hits.end(),id)==hits.end())hits.push_back(id);
            };
            const auto& points=request->points;
            float gestureSpan=0.0f;
            if(!points.empty())for(const auto& point:points)
                gestureSpan=std::max(gestureSpan,std::hypot(point[0]-points.front()[0],point[1]-points.front()[1]));
            const bool click=request->mode==genesis::editor::GameEditor::SelectionMode::Select ||
                request->mode==genesis::editor::GameEditor::SelectionMode::Tweak ||
                (request->mode!=genesis::editor::GameEditor::SelectionMode::Circle && gestureSpan<4.0f);
            if(click) {
                if(!points.empty())if(const auto hit=pickPoint(points.front()))addHit(*hit);
            } else {
                std::vector<genesis::editor::selection::Polygon> regions;
                if(request->mode==genesis::editor::GameEditor::SelectionMode::Box)
                    regions.push_back(genesis::editor::selection::rectangle(points.front(),points.back()));
                else if(request->mode==genesis::editor::GameEditor::SelectionMode::Circle) {
                    for(const auto& point:points)regions.push_back(genesis::editor::selection::circle(point,request->radius));
                    if(points.back()!=request->cursor)
                        regions.push_back(genesis::editor::selection::circle(request->cursor,request->radius));
                } else regions.push_back(points);
                for(const auto& model:models) if(model->visible && model->entityId) {
                    if(request->mode==genesis::editor::GameEditor::SelectionMode::Circle &&
                        std::find(liveCircleHits.begin(),liveCircleHits.end(),*model->entityId)!=liveCircleHits.end())continue;
                    if(model->overlapsScreenRegions(camera,viewport,regions))addHit(*model->entityId);
                }
                for(const auto& entity:genesis::gameplay::g_world.snapshots()) if(!entity.renderable) {
                    const auto point=genesis::editor::gizmoMath::project(entity.transform.position,camera,viewport);
                    if(point)for(const auto& region:regions)if(genesis::editor::selection::contains(region,*point)) {
                        addHit(entity.id);break;
                    }
                }
            }
            if(request->mode==genesis::editor::GameEditor::SelectionMode::Circle) {
                for(const auto id:hits)
                    if(std::find(liveCircleHits.begin(),liveCircleHits.end(),id)==liveCircleHits.end())
                        liveCircleHits.push_back(id);
                std::vector<genesis::gameplay::EntityId> selected=
                    request->extend || request->toggle?request->baseSelection:std::vector<genesis::gameplay::EntityId>{};
                for(const auto id:liveCircleHits) {
                    const auto found=std::find(selected.begin(),selected.end(),id);
                    if(request->toggle) {
                        if(found==selected.end())selected.push_back(id);else selected.erase(found);
                    } else if(found==selected.end())selected.push_back(id);
                }
                if(selected!=editor->selectedIds())editor->selectMany(selected);
            } else editor->selectMany(hits,request->extend,request->toggle);
        }
        if (editor) {
            if (const auto requested=editor->takeQualityRequest()) switchQuality(*requested);
        }

        if (editorHost) {
            editorHost->update();editor->advance(deltaTime);
#ifdef GENESIS_WITH_PHYSX_PBD
            if(!editor->playing() && particleRuntimeStarted){particleRuntime->stop();particleRuntimeStarted=false;}
#endif
            const auto& editorCamera=editorHost->camera();
            cameraPosition=editorCamera.position(); cameraYaw=editorCamera.yaw(); cameraPitch=editorCamera.pitch();
        }
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const float forwardX = std::sin(cameraYaw);
        const float forwardZ = std::cos(cameraYaw);
        const float rightX = std::cos(cameraYaw);
        const float rightZ = -std::sin(cameraYaw);
        float movementX = 0, movementY = 0, movementZ = 0;
        if (keys[SDL_SCANCODE_W]) { movementX += forwardX; movementZ += forwardZ; }
        if (keys[SDL_SCANCODE_S]) { movementX -= forwardX; movementZ -= forwardZ; }
        if (keys[SDL_SCANCODE_D]) { movementX += rightX; movementZ += rightZ; }
        if (keys[SDL_SCANCODE_A]) { movementX -= rightX; movementZ -= rightZ; }
        if (keys[SDL_SCANCODE_E]) movementY += 1.0f;
        if (keys[SDL_SCANCODE_Q]) movementY -= 1.0f;
        const float movementLength = std::sqrt(movementX * movementX + movementY * movementY + movementZ * movementZ);
        if (movementLength > 1e-5f && !editor) {
            const float step = flySpeed * deltaTime / movementLength;
            cameraPosition[0] += movementX * step;
            cameraPosition[1] += movementY * step;
            cameraPosition[2] += movementZ * step;
        }

        orbitCapture.updateCamera(frameIndex, initialCameraPosition, genesis::scripting::g_camera.target,
            cameraPosition, cameraYaw, cameraPitch);
        const float time = editor && editor->playing() ? float(editor->simulationTime())
            : editor && !editor->animate() ? 0.0f : std::chrono::duration<float>(frameTime - start).count();
        if (editor) {
            if (editorRevision!=editor->revision()) {
                const auto entities=genesis::gameplay::g_world.snapshots();
                syncAuthoredLighting(entities);
                for(size_t i=0;i<genesis::render::g_spotLights.size();++i){spotDirections[i]=genesis::render::g_spotLights[i].direction;spotUps[i]=genesis::render::g_spotLights[i].up;}
                std::erase_if(models,[&](const auto& model) {
                    return model->entityId && std::none_of(entities.begin(),entities.end(),[&](const auto& entity) {
                        return entity.id==*model->entityId && entity.renderable && fs::path(entity.renderable->path)==model->path();
                    });
                });
                for (const auto& entity:entities) if (entity.renderable) {
                    const auto found=std::find_if(models.begin(),models.end(),[&](const auto& model){return model->entityId==entity.id;});
                    if (found!=models.end()) { (*found)->setTransform(entity.transform);
                        (*found)->updateMeshVertices(entity.renderable->meshVertices);
                        (*found)->visible=entity.renderable->visible;(*found)->materialOverride=entity.renderable->material; }
                    else {
                        auto model=std::make_unique<SceneModel>(); model->entityId=entity.id; model->visible=entity.renderable->visible;model->materialOverride=entity.renderable->material;
                        if (model->load(entity.renderable->path,entity.transform,RenderLayer::World,
                            entity.renderable->meshVertices)) models.push_back(std::move(model));
                        else editor->status("Cannot load mesh: "+entity.renderable->path);
                    }
                }
                localProbeDirty.fill(true); editorRevision=editor->revision();
            }
            if (editor->takeFrameRequest()) {
                genesis::editor::Bounds bounds;
                for (const auto& model:models) if(model->visible && (!editor->selected() || model->entityId==editor->selected())) {
                    const auto b=model->bounds(); if(b.valid()) { bounds.include(b.minimum); bounds.include(b.maximum); }
                }
                if(const auto entity=editor->selectedEntity();entity && entity->probe) {
                    for(float sign:{-1.0f,1.0f}){auto point=entity->transform.position;for(int i=0;i<3;++i)point[i]+=sign*entity->probe->size[i]*std::abs(entity->transform.scale[i])*.5f;bounds.include(point);}
                }
                if(!bounds.valid())if(const auto transform=editor->selectedTransform()) {
                    for(float offset:{-.5f,.5f}) {
                        auto point=transform->position;for(auto& value:point)value+=offset;bounds.include(point);
                    }
                }
                if (bounds.valid()) {
                    const auto viewport=editor->viewport();
                    editorHost->camera().frame(bounds,std::max(viewport.width,1.0f)/std::max(viewport.height,1.0f));
                    cameraPosition=editorHost->camera().position();
                }
            }
        }
        if(editorHost)editorHost->update();
        for (auto& model : models) model->animate(time);

        // Each visible camera submits a complete ordered graph. Shared scratch
        // targets are consumed before the next camera overwrites them; exposure
        // history is separate. The UI composites after both graphs.
        for(int pass=0;pass<(editor ? 2 : 1);++pass) {
        const bool gamePass=pass==1;
        const int viewportShading=editor && !gamePass ? int(editor->viewportShading()) : 3;
        const auto viewport=editor ? (gamePass ? editor->gameViewport() : editor->viewport()) : genesis::ui::Rect{0,0,float(width),float(height)};
        if(viewport.width<=0 || viewport.height<=0)continue;
        const auto renderView=[pass](uint16_t id){return uint16_t(id+pass*kViewStride);};
        const auto& passCamera=editor ? (gamePass ? editorHost->gameCamera() : editor->camera()) : genesis::scripting::g_camera;
        const genesis::editor::ViewportCamera pose(passCamera);
        const auto passPosition=editor ? pose.position() : cameraPosition;
        const float passYaw=editor ? pose.yaw() : cameraYaw,passPitch=editor ? pose.pitch() : cameraPitch;
        const auto cameraPosition=passPosition;
        const float cameraYaw=passYaw,cameraPitch=passPitch;
        const float rightX=std::cos(cameraYaw),rightZ=-std::sin(cameraYaw);
        const auto atmosphere = genesis::atmosphere::evaluate(time);
        const float environmentRadiance = atmosphere.settings.radiance;
        // Keep the public light intensity in the same units consumed by the
        // BRDF.  The old 2x boost made bright dielectrics clip and hid the
        // metallic/roughness response under auto exposure.
        float lightDir[4]{atmosphere.sunDirection[0], atmosphere.sunDirection[1], atmosphere.sunDirection[2], atmosphere.settings.sunIntensity};
        float lightColor[4]{1.0f, 0.92f, 0.78f, 1.0f};
        // Diffuse skylight and a restrained warm bounce keep shadowed materials
        // readable without lifting the directly lit floor into clipping.
        const float daylight = std::clamp(lightDir[1] * 0.65f + 0.55f, 0.15f, 1.0f)
            * environmentRadiance;
        float ambientSky[4]{0.24f * daylight, 0.29f * daylight, 0.36f * daylight, 1.0f};
        float ambientGround[4]{0.085f * daylight, 0.075f * daylight, 0.065f * daylight, 1.0f};
        float camera[4]{cameraPosition[0], cameraPosition[1], cameraPosition[2], 1};
        if (!std::isfinite(lastProbeSun[0]) ||
            std::abs(lastProbeSun[0] - lightDir[0]) + std::abs(lastProbeSun[1] - lightDir[1]) +
                std::abs(lastProbeSun[2] - lightDir[2]) > 1e-5f) {
            environmentDirty = true;
            localProbeDirty.fill(true);
            lastProbeSun = {lightDir[0], lightDir[1], lightDir[2]};
        }

        struct ProbeCandidate { int index; bool containsCamera; int priority; float distanceSquared; };
        std::vector<ProbeCandidate> probeCandidates;
        probeCandidates.reserve(genesis::scripting::g_reflectionProbes.size());
        for (size_t probeIndex = 0; probeIndex < genesis::scripting::g_reflectionProbes.size(); ++probeIndex) {
            const auto& probe = genesis::scripting::g_reflectionProbes[probeIndex];
            bool containsCamera = true;
            float distanceSquared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                containsCamera = containsCamera && cameraPosition[axis] >= probe.boundsMin[axis] &&
                    cameraPosition[axis] <= probe.boundsMax[axis];
                const float delta = cameraPosition[axis] < probe.boundsMin[axis]
                    ? probe.boundsMin[axis] - cameraPosition[axis]
                    : cameraPosition[axis] > probe.boundsMax[axis]
                        ? cameraPosition[axis] - probe.boundsMax[axis] : 0.0f;
                distanceSquared += delta * delta;
            }
            probeCandidates.push_back({static_cast<int>(probeIndex), containsCamera,
                probe.priority, distanceSquared});
        }
        std::sort(probeCandidates.begin(), probeCandidates.end(), [](const auto& a, const auto& b) {
            if (a.containsCamera != b.containsCamera) return a.containsCamera > b.containsCamera;
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.distanceSquared < b.distanceSquared;
        });
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) {
            const int selected = slot < probeCandidates.size() ? probeCandidates[slot].index : -1;
            if (selected != activeReflectionProbes[slot]) {
                activeReflectionProbes[slot] = selected;
                localProbeDirty[slot] = selected >= 0;
            }
        }
        const float cosPitch = std::cos(cameraPitch);
        const bx::Vec3 cameraTarget{
            camera[0] + cosPitch * std::sin(cameraYaw),
            camera[1] + std::sin(cameraPitch),
            camera[2] + cosPitch * std::cos(cameraYaw)
        };
        const float viewportAspect=std::max(viewport.width,1.0f)/std::max(viewport.height,1.0f);

        float view[16], projection[16];
        bx::mtxLookAt(view,
            {camera[0], camera[1], camera[2]},
            cameraTarget,
            {passCamera.up[0], passCamera.up[1], passCamera.up[2]});
        bx::mtxProj(projection, passCamera.fovDegrees, viewportAspect, 0.05f, 1000.0f, bgfx::getCaps()->homogeneousDepth);

        const float cascadeSplits[4]{12.0f, 35.0f, 90.0f, 0.12f};
        const float yScale = bgfx::getCaps()->originBottomLeft ? 0.5f : -0.5f;
        const float zScale = bgfx::getCaps()->homogeneousDepth ? 0.5f : 1.0f;
        const float zOffset = bgfx::getCaps()->homogeneousDepth ? 0.5f : 0.0f;
        const float shadowBias[16]{
            0.5f, 0, 0, 0,
            0, yScale, 0, 0,
            0, 0, zScale, 0,
            0.5f, 0.5f, zOffset, 1.0f
        };
        std::array<std::array<float, 16>, kCascadeCount> lightViews{};
        std::array<std::array<float, 16>, kCascadeCount> lightProjections{};
        std::array<std::array<float, 16>, kCascadeCount> lightSampleMatrices{};
        const bx::Vec3 cameraForward{cosPitch * std::sin(cameraYaw), std::sin(cameraPitch),
            cosPitch * std::cos(cameraYaw)};
        const bx::Vec3 cameraRight{rightX, 0.0f, rightZ};
        const bx::Vec3 cameraUp{-std::sin(cameraYaw) * std::sin(cameraPitch), cosPitch,
            -std::cos(cameraYaw) * std::sin(cameraPitch)};
        const float aspectRatio = viewportAspect;
        const float cascadeTanHalfFov = std::tan(radians(passCamera.fovDegrees) * 0.5f);
        const float extentQuantums[kCascadeCount]{1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 8.0f};
        const bx::Vec3 shadowUp = std::abs(lightDir[1]) > 0.98f
            ? bx::Vec3{0.0f, 0.0f, 1.0f} : bx::Vec3{0.0f, 1.0f, 0.0f};
        float previousSplit = 0.05f;
        for (uint16_t cascade = 0; cascade < kCascadeCount; ++cascade) {
            const float sliceNear = cascade == 0 ? previousSplit
                : previousSplit * (1.0f - cascadeSplits[3]);
            std::array<bx::Vec3, 8> corners{
                bx::Vec3{0.0f, 0.0f, 0.0f}, bx::Vec3{0.0f, 0.0f, 0.0f},
                bx::Vec3{0.0f, 0.0f, 0.0f}, bx::Vec3{0.0f, 0.0f, 0.0f},
                bx::Vec3{0.0f, 0.0f, 0.0f}, bx::Vec3{0.0f, 0.0f, 0.0f},
                bx::Vec3{0.0f, 0.0f, 0.0f}, bx::Vec3{0.0f, 0.0f, 0.0f}};
            for (int plane = 0; plane < 2; ++plane) {
                const float distance = plane == 0 ? sliceNear : cascadeSplits[cascade];
                const float halfHeight = distance * cascadeTanHalfFov;
                const float halfWidth = halfHeight * aspectRatio;
                const bx::Vec3 planeCenter{
                    camera[0] + cameraForward.x * distance,
                    camera[1] + cameraForward.y * distance,
                    camera[2] + cameraForward.z * distance};
                for (int corner = 0; corner < 4; ++corner) {
                    const float horizontal = (corner & 1) ? halfWidth : -halfWidth;
                    const float vertical = (corner & 2) ? halfHeight : -halfHeight;
                    corners[size_t(plane * 4 + corner)] = {
                        planeCenter.x + cameraRight.x * horizontal + cameraUp.x * vertical,
                        planeCenter.y + cameraRight.y * horizontal + cameraUp.y * vertical,
                        planeCenter.z + cameraRight.z * horizontal + cameraUp.z * vertical};
                }
            }

            bx::Vec3 sliceCenter{0.0f, 0.0f, 0.0f};
            for (const auto& corner : corners) {
                sliceCenter.x += corner.x;
                sliceCenter.y += corner.y;
                sliceCenter.z += corner.z;
            }
            sliceCenter.x *= 0.125f;
            sliceCenter.y *= 0.125f;
            sliceCenter.z *= 0.125f;

            float sliceRadius = 0.0f;
            for (const auto& corner : corners) {
                const float dx = corner.x - sliceCenter.x;
                const float dy = corner.y - sliceCenter.y;
                const float dz = corner.z - sliceCenter.z;
                sliceRadius = std::max(sliceRadius, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            const float lightDistance = sliceRadius + 80.0f;
            bx::Vec3 lightEye{sliceCenter.x + lightDir[0] * lightDistance,
                sliceCenter.y + lightDir[1] * lightDistance,
                sliceCenter.z + lightDir[2] * lightDistance};
            float provisionalView[16];
            bx::mtxLookAt(provisionalView, lightEye, sliceCenter, shadowUp);

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            for (const auto& corner : corners) {
                const float world[4]{corner.x, corner.y, corner.z, 1.0f};
                float lightSpace[4];
                bx::vec4MulMtx(lightSpace, world, provisionalView);
                minX = std::min(minX, lightSpace[0]);
                minY = std::min(minY, lightSpace[1]);
                maxX = std::max(maxX, lightSpace[0]);
                maxY = std::max(maxY, lightSpace[1]);
            }

            // Keep a two-percent receiver guard band, then quantize dimensions
            // so camera rotation changes projection scale in small stable steps.
            const float quantum = extentQuantums[cascade];
            const float halfWidth = std::ceil((maxX - minX) * 0.51f / quantum) * quantum;
            const float halfHeight = std::ceil((maxY - minY) * 0.51f / quantum) * quantum;
            const float centerX = (minX + maxX) * 0.5f;
            const float centerY = (minY + maxY) * 0.5f;
            float inverseProvisionalView[16];
            bx::mtxInverse(inverseProvisionalView, provisionalView);
            const float fittedCenterLight[4]{centerX, centerY, 0.0f, 1.0f};
            float fittedCenterWorld[4];
            bx::vec4MulMtx(fittedCenterWorld, fittedCenterLight, inverseProvisionalView);

            // Snap in a light-space coordinate system anchored at the world
            // origin. Unlike snapping world XYZ, this remains stable for an
            // arbitrarily oriented sun and exactly tracks shadow-map texels.
            float lightRotation[16];
            const bx::Vec3 lightOriginEye{lightDir[0], lightDir[1], lightDir[2]};
            bx::mtxLookAt(lightRotation, lightOriginEye, {0.0f, 0.0f, 0.0f}, shadowUp);
            const float fittedWorld[4]{fittedCenterWorld[0], fittedCenterWorld[1], fittedCenterWorld[2], 1.0f};
            float anchoredCenter[4];
            bx::vec4MulMtx(anchoredCenter, fittedWorld, lightRotation);
            const float texelX = halfWidth * 2.0f / float(quality.shadowSize);
            const float texelY = halfHeight * 2.0f / float(quality.shadowSize);
            const float snappedX = std::round(anchoredCenter[0] / texelX) * texelX;
            const float snappedY = std::round(anchoredCenter[1] / texelY) * texelY;
            float inverseLightRotation[16];
            bx::mtxInverse(inverseLightRotation, lightRotation);
            const float correctionLight[4]{snappedX - anchoredCenter[0], snappedY - anchoredCenter[1], 0.0f, 0.0f};
            float correctionWorld[4];
            bx::vec4MulMtx(correctionWorld, correctionLight, inverseLightRotation);
            const bx::Vec3 focus{fittedCenterWorld[0] + correctionWorld[0],
                fittedCenterWorld[1] + correctionWorld[1], fittedCenterWorld[2] + correctionWorld[2]};
            lightEye = {focus.x + lightDir[0] * lightDistance,
                focus.y + lightDir[1] * lightDistance,
                focus.z + lightDir[2] * lightDistance};
            bx::mtxLookAt(lightViews[cascade].data(), lightEye, focus, shadowUp);

            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();
            for (const auto& corner : corners) {
                const float world[4]{corner.x, corner.y, corner.z, 1.0f};
                float lightSpace[4];
                bx::vec4MulMtx(lightSpace, world, lightViews[cascade].data());
                minZ = std::min(minZ, lightSpace[2]);
                maxZ = std::max(maxZ, lightSpace[2]);
            }
            // Extend toward the sun so off-frustum geometry can still cast onto
            // visible receivers, while keeping the receiver side reasonably tight.
            // bx::mtxLookAt and mtxOrtho default to left-handed coordinates:
            // receivers in front of the light have POSITIVE view-space Z.
            const float nearPlane = std::max(0.1f, std::floor((minZ - 60.0f) * 4.0f) * 0.25f);
            const float farPlane = std::max(nearPlane + 1.0f, std::ceil((maxZ + 12.0f) * 4.0f) * 0.25f);
            bx::mtxOrtho(lightProjections[cascade].data(), -halfWidth, halfWidth,
                -halfHeight, halfHeight, nearPlane, farPlane, 0.0f,
                bgfx::getCaps()->homogeneousDepth);
            float lightProjectionBias[16];
            bx::mtxMul(lightProjectionBias, lightProjections[cascade].data(), shadowBias);
            bx::mtxMul(lightSampleMatrices[cascade].data(), lightViews[cascade].data(), lightProjectionBias);
            previousSplit = cascadeSplits[cascade];
        }

        const float tanHalfFov = std::tan(radians(passCamera.fovDegrees) * 0.5f);
        float skyRight[4]{rightX, 0.0f, rightZ, tanHalfFov * viewportAspect};
        float skyForward[4]{cosPitch * std::sin(cameraYaw), std::sin(cameraPitch), cosPitch * std::cos(cameraYaw), 0.0f};
        float skyUp[4]{-std::sin(cameraYaw) * std::sin(cameraPitch), cosPitch,
            -std::cos(cameraYaw) * std::sin(cameraPitch), tanHalfFov};
        float sunDirection[4]{lightDir[0], lightDir[1], lightDir[2], environmentRadiance};
        float atmosphereParams[4]{6360.0f, 6420.0f, std::max(cameraPosition[1], 1.0f) * 0.001f, 24.0f};
        // The global cubemap already contains the requested sky radiance and
        // local probes contain fully shaded HDR radiance.  Applying radiance a
        // second time here made reflections scale quadratically and forced
        // metals toward white.  Diffuse irradiance is the only LUT that still
        // needs the user's radiance multiplier.
        // Keep an 8x8 directional signal at roughness=1. A 1x1-per-face cube
        // exposes its six coarse lobes on rough materials. Lower storage mips
        // are still initialized, but are not part of the roughness LOD range.
        const float specularMaxMip = float(std::max(int(quality.environmentMipCount) - 4, 1));
        float environmentParams[4]{environmentRadiance, 1.0f,
            specularMaxMip, hdriEnabled ? 1.0f : 0.0f};
        const auto& environment = genesis::scripting::g_environment;
        auto hdriIrradiance = hdriLighting.diffuse;
        for (auto& coefficient : hdriIrradiance)
            for (size_t channel = 0; channel < 3; ++channel) coefficient[channel] *= environment.intensity;
        float hdriParams[4]{hdriEnabled ? 1.0f : 0.0f, environment.intensity,
            environment.rotationDegrees * 3.14159265f / 180.0f, environment.visible ? 1.0f : 0.0f};
        float fogParams[4]{0.020f, 0.018f, 0.80f, 90.0f};
        float depthParams[4]{0.05f, 1000.0f, 0.30f, 0.0f};
        std::array<std::array<float, 4>, kLocalProbeSlotCount> reflectionProbePosition{};
        std::array<std::array<float, 4>, kLocalProbeSlotCount> reflectionProbeMin{};
        std::array<std::array<float, 4>, kLocalProbeSlotCount> reflectionProbeMax{};
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) {
            reflectionProbeMin[slot] = {-1.0f, -1.0f, -1.0f, 1.0f};
            reflectionProbeMax[slot] = {1.0f, 1.0f, 1.0f, 0.0f};
        }
        auto fillProbeUniforms = [&](uint8_t slot, float* position, float* minimum, float* maximum) {
            const int active = activeReflectionProbes[slot];
            if (active < 0) return;
            const auto& probe = genesis::scripting::g_reflectionProbes[size_t(active)];
            for (int axis = 0; axis < 3; ++axis) {
                position[axis] = probe.position[axis];
                minimum[axis] = probe.boundsMin[axis];
                maximum[axis] = probe.boundsMax[axis];
            }
            position[3] = 1.0f;
            minimum[3] = probe.blendDistance;
            maximum[3] = float(probe.priority);
        };
        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot)
            fillProbeUniforms(slot, reflectionProbePosition[slot].data(),
                reflectionProbeMin[slot].data(), reflectionProbeMax[slot].data());
        std::array<std::array<float, 4>, 4> pointLightPositionRadius{};
        std::array<std::array<float, 4>, 4> pointLightColorIntensity{};
        std::array<std::array<float, 4>, 4> pointLightPattern{};
        auto encodedPatternLayer = [&](const std::string& path) {
            const auto found = lightPatternLayers.find(path);
            return found == lightPatternLayers.end() ? 0.0f : float(found->second + 1);
        };
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_pointLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_pointLights[lightIndex];
            pointLightPositionRadius[lightIndex] = {
                light.position[0], light.position[1], light.position[2], light.radius};
            pointLightColorIntensity[lightIndex] = {
                light.color[0], light.color[1], light.color[2], light.intensity};
            pointLightPattern[lightIndex][0] = encodedPatternLayer(light.iesProfile);
        }
        std::array<std::array<float, 4>, 4> pointLightShadow{};
        std::array<std::array<float, 4>, 4> spotLightPositionRadius{};
        std::array<std::array<float, 4>, 4> spotLightDirectionOuter{};
        std::array<std::array<float, 4>, 4> spotLightColorIntensity{};
        std::array<std::array<float, 4>, 4> spotLightParams{};
        std::array<std::array<float, 4>, 4> spotLightShadow{};
        std::array<std::array<float, 4>, 4> spotLightUpPattern{};
        std::array<std::array<float, 4>, 2> areaLightPositionRadius{};
        std::array<std::array<float, 4>, 2> areaLightDirectionWidth{};
        std::array<std::array<float, 4>, 2> areaLightUpHeight{};
        std::array<std::array<float, 4>, 2> areaLightColorIntensity{};
        std::array<std::array<float, 4>, 2> areaLightShadow{};
        std::array<std::array<float, 4>, 4> emissiveLightPositionRadius{};
        std::array<std::array<float, 4>, 4> emissiveLightDirectionArea{};
        std::array<std::array<float, 4>, 4> emissiveLightRadiance{};
        std::array<std::array<float, 4>, 4> emissiveLightShadow{};
        std::array<std::array<float, 4>, 4> emissiveLightShadowSecondary{};
        std::array<std::array<float, 4>, 4> emissiveLightShadowOrigin{};
        std::array<std::array<float, 4>, 4> emissiveLightTangentWidth{};
        std::vector<EmissiveLightSample> emissiveCandidates;
        for (const auto& model : models)
            model->appendEmissiveSamples(emissiveCandidates, cameraPosition);
        std::sort(emissiveCandidates.begin(), emissiveCandidates.end(),
            [](const auto& a, const auto& b) { return a.score > b.score; });
        for (size_t index = 0; index < std::min<size_t>(4, emissiveCandidates.size()); ++index) {
            const auto& sample = emissiveCandidates[index];
            const float area = std::min(sample.area, 100.0f);
            emissiveLightPositionRadius[index] = {
                sample.position[0], sample.position[1], sample.position[2],
                std::min(40.0f, std::max(5.0f, std::sqrt(area) * 8.0f))};
            emissiveLightDirectionArea[index] = {
                sample.normal[0], sample.normal[1], sample.normal[2],
                sample.doubleSided ? -area : area};
            emissiveLightRadiance[index] = {
                std::min(sample.radiance[0], 1000.0f),
                std::min(sample.radiance[1], 1000.0f),
                std::min(sample.radiance[2], 1000.0f), sample.halfHeight};
            emissiveLightTangentWidth[index] = {
                sample.tangent[0], sample.tangent[1], sample.tangent[2], sample.halfWidth};
        }
        const float pointShadowAtlasParams[4]{
            1.0f / float(quality.pointShadowSize), 1.35f, 0.0f, 0.0f};
        std::array<uint16_t, 49> lightingCounts;
        lightingCounts.fill(1);
        lightingCounts[12] = lightingCounts[13] = lightingCounts[14] = kLocalProbeSlotCount;
        for (size_t index = 27; index <= 32; ++index) lightingCounts[index] = 4;
        for (size_t index = 34; index <= 37; ++index) lightingCounts[index] = 2;
        lightingCounts[38] = lightingCounts[39] = 4;
        lightingCounts[40] = 9;
        lightingCounts[41] = 2;
        lightingCounts[42] = lightingCounts[43] = lightingCounts[44] = 4;
        lightingCounts[45] = 4;
        lightingCounts[46] = 4;
        lightingCounts[47] = 4;
        lightingCounts[48] = 4;
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_spotLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_spotLights[lightIndex];
            spotLightPositionRadius[lightIndex] = {
                light.position[0], light.position[1], light.position[2], light.radius};
            spotLightDirectionOuter[lightIndex] = {
                spotDirections[lightIndex][0], spotDirections[lightIndex][1], spotDirections[lightIndex][2],
                std::cos(radians(light.outerAngle))};
            spotLightColorIntensity[lightIndex] = {
                light.color[0], light.color[1], light.color[2], light.intensity};
            spotLightParams[lightIndex][0] = std::cos(radians(light.innerAngle));
            spotLightParams[lightIndex][2] = encodedPatternLayer(light.iesProfile);
            spotLightUpPattern[lightIndex] = {
                spotUps[lightIndex][0], spotUps[lightIndex][1], spotUps[lightIndex][2],
                encodedPatternLayer(light.cookieTexture)};
        }
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_areaLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_areaLights[lightIndex];
            areaLightPositionRadius[lightIndex] = {
                light.position[0], light.position[1], light.position[2], light.radius};
            areaLightDirectionWidth[lightIndex] = {
                light.direction[0], light.direction[1], light.direction[2], light.width};
            areaLightUpHeight[lightIndex] = {
                light.up[0], light.up[1], light.up[2], light.height};
            areaLightColorIntensity[lightIndex] = {
                light.color[0], light.color[1], light.color[2], light.intensity};
        }
        enum class ShadowKind { Point, Spot, Area, Emissive };
        struct ShadowCandidate { float score; ShadowKind kind; int index; bool secondary = false; };
        std::array<ShadowCandidate, kPointShadowSlotCount> selectedLocalShadows{};
        for (auto& selection : selectedLocalShadows) selection.index = -1;
        std::vector<ShadowCandidate> shadowCandidates;
        std::vector<genesis::editor::Bounds> shadowReceiverBounds;
        shadowReceiverBounds.reserve(models.size());
        for (const auto& model : models) {
            if (!model->visible) continue;
            const auto bounds = model->bounds();
            if (bounds.valid()) shadowReceiverBounds.push_back(bounds);
        }
        // The shader's finite range makes a depth capture useless if the light
        // cannot reach any visible mesh. In particular, a tiny light near the
        // camera should not displace a shadowed light over the scene.
        auto reachesScene = [&](const std::array<float, 3>& position, float radius,
                                const std::array<float, 3>* direction, float coneTangent) {
            if (radius <= 0.0f) return false;
            const float radiusSquared = radius * radius;
            for (const auto& bounds : shadowReceiverBounds) {
                float distanceSquared = 0.0f;
                for (size_t axis = 0; axis < 3; ++axis) {
                    const float nearest = std::clamp(position[axis],
                        bounds.minimum[axis], bounds.maximum[axis]);
                    const float delta = position[axis] - nearest;
                    distanceSquared += delta * delta;
                }
                if (distanceSquared >= radiusSquared) continue;
                if (direction) {
                    float farthestFacing = 0.0f;
                    for (size_t axis = 0; axis < 3; ++axis) {
                        const float edge = (*direction)[axis] >= 0.0f
                            ? bounds.maximum[axis] : bounds.minimum[axis];
                        farthestFacing += (edge - position[axis]) * (*direction)[axis];
                    }
                    if (farthestFacing <= 0.0f) continue;
                    if (coneTangent > 0.0f) {
                        // Eight cells tighten the circumscribing spheres for
                        // large bounds. A sphere/cone overlap is conservative:
                        // it may keep an extra capture, but cannot reject a
                        // receiver inside the actual spotlight cone.
                        std::array<float, 3> middle{};
                        for (size_t axis = 0; axis < 3; ++axis)
                            middle[axis] = 0.5f * (bounds.minimum[axis] + bounds.maximum[axis]);
                        bool coneTouches = false;
                        for (uint8_t cell = 0; cell < 8 && !coneTouches; ++cell) {
                            float axial = 0.0f;
                            float centerDistanceSquared = 0.0f;
                            float cellRadiusSquared = 0.0f;
                            for (size_t axis = 0; axis < 3; ++axis) {
                                const float minimum = (cell & (1 << axis)) ? middle[axis] : bounds.minimum[axis];
                                const float maximum = (cell & (1 << axis)) ? bounds.maximum[axis] : middle[axis];
                                const float offset = 0.5f * (minimum + maximum) - position[axis];
                                const float halfExtent = 0.5f * (maximum - minimum);
                                axial += offset * (*direction)[axis];
                                centerDistanceSquared += offset * offset;
                                cellRadiusSquared += halfExtent * halfExtent;
                            }
                            const float cellRadius = std::sqrt(cellRadiusSquared);
                            const float farthestAxial = axial + cellRadius;
                            if (farthestAxial <= 0.0f) continue;
                            const float radialSquared = std::max(centerDistanceSquared - axial * axial, 0.0f);
                            const float coneLimit = farthestAxial * coneTangent + cellRadius;
                            coneTouches = radialSquared <= coneLimit * coneLimit;
                        }
                        if (!coneTouches) continue;
                    }
                }
                return true;
            }
            return false;
        };
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_pointLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_pointLights[lightIndex];
            if (!light.castsShadows || light.intensity <= 0.0f ||
                !reachesScene(light.position, light.radius, nullptr, 0.0f)) continue;
            const float dx = light.position[0] - cameraPosition[0];
            const float dy = light.position[1] - cameraPosition[1];
            const float dz = light.position[2] - cameraPosition[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float colorStrength = std::max(light.color[0], std::max(light.color[1], light.color[2]));
            shadowCandidates.push_back({light.intensity * colorStrength / std::max(distanceSquared, 1.0f),
                ShadowKind::Point, int(lightIndex)});
        }
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_spotLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_spotLights[lightIndex];
            if (!light.castsShadows || light.intensity <= 0.0f ||
                !reachesScene(light.position, light.radius, &spotDirections[lightIndex],
                    std::tan(radians(light.outerAngle)))) continue;
            const float dx = light.position[0] - cameraPosition[0];
            const float dy = light.position[1] - cameraPosition[1];
            const float dz = light.position[2] - cameraPosition[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float colorStrength = std::max(light.color[0], std::max(light.color[1], light.color[2]));
            shadowCandidates.push_back({light.intensity * colorStrength / std::max(distanceSquared, 1.0f),
                ShadowKind::Spot, int(lightIndex)});
        }
        for (size_t lightIndex = 0; lightIndex < genesis::scripting::g_areaLights.size(); ++lightIndex) {
            const auto& light = genesis::scripting::g_areaLights[lightIndex];
            if (!light.castsShadows || light.intensity <= 0.0f ||
                !reachesScene(light.position, light.radius, &light.direction, 0.0f)) continue;
            const float dx = light.position[0] - cameraPosition[0];
            const float dy = light.position[1] - cameraPosition[1];
            const float dz = light.position[2] - cameraPosition[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float colorStrength = std::max(light.color[0], std::max(light.color[1], light.color[2]));
            shadowCandidates.push_back({light.intensity * colorStrength / std::max(distanceSquared, 1.0f),
                ShadowKind::Area, int(lightIndex)});
        }
        for (size_t lightIndex = 0; lightIndex < std::min<size_t>(4, emissiveCandidates.size()); ++lightIndex) {
            const auto& sample = emissiveCandidates[lightIndex];
            const float dx = sample.position[0] - cameraPosition[0];
            const float dy = sample.position[1] - cameraPosition[1];
            const float dz = sample.position[2] - cameraPosition[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            const float peakRadiance = std::max(sample.radiance[0],
                std::max(sample.radiance[1], sample.radiance[2]));
            shadowCandidates.push_back({std::min(sample.area, 100.0f) * peakRadiance /
                std::max(distanceSquared, 1.0f), ShadowKind::Emissive, int(lightIndex)});
        }
        auto shadowKey = [&](const ShadowCandidate& candidate) {
            PreviousShadowKey key;
            key.kind = int(candidate.kind);
            if (candidate.kind == ShadowKind::Emissive) {
                const auto& sample = emissiveCandidates[size_t(candidate.index)];
                key.sourceModel = sample.sourceModel;
                key.clusterIndex = sample.clusterIndex;
            } else {
                key.index = candidate.index;
            }
            return key;
        };
        for (auto& candidate : shadowCandidates) {
            const auto key = shadowKey(candidate);
            for (const auto& previous : previousLocalShadows) {
                if (previous.kind == key.kind && previous.index == key.index &&
                    previous.sourceModel == key.sourceModel &&
                    previous.clusterIndex == key.clusterIndex) {
                    candidate.score *= 1.15f;
                    break;
                }
            }
        }
        std::stable_sort(shadowCandidates.begin(), shadowCandidates.end(),
            [](const auto& a, const auto& b) { return a.score > b.score; });
        // Once the capture budget is exceeded, prefer coverage of separate
        // influence regions over another nearly coincident capture. Keep the
        // score (including the previous-frame margin) as the primary signal.
        if (shadowCandidates.size() > kPointShadowSlotCount) {
            auto influence = [&](const ShadowCandidate& candidate) -> const std::array<float, 4>& {
                const size_t index = size_t(candidate.index);
                switch (candidate.kind) {
                    case ShadowKind::Point: return pointLightPositionRadius[index];
                    case ShadowKind::Spot: return spotLightPositionRadius[index];
                    case ShadowKind::Area: return areaLightPositionRadius[index];
                    case ShadowKind::Emissive: return emissiveLightPositionRadius[index];
                }
                return pointLightPositionRadius[0];
            };
            std::vector<bool> chosen(shadowCandidates.size(), false);
            std::vector<ShadowCandidate> ordered;
            ordered.reserve(shadowCandidates.size());
            for (size_t slot = 0; slot < kPointShadowSlotCount; ++slot) {
                size_t bestIndex = shadowCandidates.size();
                float bestScore = -1.0f;
                for (size_t index = 0; index < shadowCandidates.size(); ++index) {
                    if (chosen[index]) continue;
                    const auto& candidate = shadowCandidates[index];
                    const auto& center = influence(candidate);
                    float overlapPenalty = 1.0f;
                    for (const auto& selected : ordered) {
                        const auto& other = influence(selected);
                        const float dx = center[0] - other[0];
                        const float dy = center[1] - other[1];
                        const float dz = center[2] - other[2];
                        const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
                        const float sharedRadius = std::max(std::min(center[3], other[3]), 0.01f);
                        overlapPenalty += std::max(1.0f - separation / sharedRadius, 0.0f);
                    }
                    const float adjustedScore = candidate.score / overlapPenalty;
                    if (adjustedScore > bestScore) {
                        bestScore = adjustedScore;
                        bestIndex = index;
                    }
                }
                chosen[bestIndex] = true;
                ordered.push_back(shadowCandidates[bestIndex]);
            }
            for (size_t index = 0; index < shadowCandidates.size(); ++index)
                if (!chosen[index]) ordered.push_back(shadowCandidates[index]);
            shadowCandidates = std::move(ordered);
        }
        previousLocalShadows.fill({});
        for (size_t slot = 0; slot < std::min<size_t>(shadowCandidates.size(), kPointShadowSlotCount); ++slot) {
            selectedLocalShadows[slot] = shadowCandidates[slot];
            previousLocalShadows[slot] = shadowKey(shadowCandidates[slot]);
            const int lightIndex = shadowCandidates[slot].index;
            if (shadowCandidates[slot].kind == ShadowKind::Spot) {
                const auto& light = genesis::scripting::g_spotLights[size_t(lightIndex)];
                const float nearPlane = std::min(0.10f, std::max(0.01f, light.radius * 0.002f));
                spotLightShadow[size_t(lightIndex)] = {
                    float(slot + 1), light.shadowBias, nearPlane, light.radius};
            } else if (shadowCandidates[slot].kind == ShadowKind::Point) {
                const auto& light = genesis::scripting::g_pointLights[size_t(lightIndex)];
                const float nearPlane = std::min(0.10f, std::max(0.01f, light.radius * 0.002f));
                pointLightShadow[size_t(lightIndex)] = {
                    float(slot + 1), light.shadowBias, nearPlane, light.radius};
            } else if (shadowCandidates[slot].kind == ShadowKind::Area) {
                const auto& light = genesis::scripting::g_areaLights[size_t(lightIndex)];
                const float nearPlane = std::min(0.10f, std::max(0.01f, light.radius * 0.002f));
                areaLightShadow[size_t(lightIndex)] = {
                    float(slot + 1), light.shadowBias, nearPlane, light.radius};
            } else {
                const float radius = emissiveLightPositionRadius[size_t(lightIndex)][3];
                const float nearPlane = std::min(0.10f, std::max(0.01f, radius * 0.002f));
                emissiveLightShadow[size_t(lightIndex)] = {
                    float(slot + 1), 0.04f, nearPlane, radius};
                const auto& sample = emissiveCandidates[size_t(lightIndex)];
                emissiveLightShadowOrigin[size_t(lightIndex)] = {
                    sample.position[0], sample.position[1], sample.position[2], sample.captureOffset};
            }
        }
        // A wide single emitter benefits from two actual capture origins. Use
        // the otherwise idle cubemap without evicting another shadow caster.
        if (shadowCandidates.size() == 1 && selectedLocalShadows[0].kind == ShadowKind::Emissive) {
            const int lightIndex = selectedLocalShadows[0].index;
            const auto& sample = emissiveCandidates[size_t(lightIndex)];
            if (sample.halfWidth >= 1.0f && sample.captureOffset >= 0.2f) {
                selectedLocalShadows[1] = {shadowCandidates[0].score,
                    ShadowKind::Emissive, lightIndex, true};
                emissiveLightShadowSecondary[size_t(lightIndex)] = {
                    2.0f, emissiveLightShadow[size_t(lightIndex)][1],
                    emissiveLightShadow[size_t(lightIndex)][2],
                    emissiveLightShadow[size_t(lightIndex)][3]};
            }
        }
        // Adjacent coplanar clusters in one primitive can use a selected
        // cluster's capture. The capture already omits that emitting surface,
        // so nearby samples gain blocker visibility without a third sampler.
        for (size_t lightIndex = 0; lightIndex < std::min<size_t>(4, emissiveCandidates.size()); ++lightIndex) {
            if (emissiveLightShadow[lightIndex][0] > 0.5f) continue;
            const auto& sample = emissiveCandidates[lightIndex];
            float bestDistanceSquared = INFINITY;
            int nearest = -1;
            for (const auto& selected : selectedLocalShadows) {
                if (selected.index < 0 || selected.secondary || selected.kind != ShadowKind::Emissive) continue;
                const auto& source = emissiveCandidates[size_t(selected.index)];
                if (source.sourceModel != sample.sourceModel ||
                    source.primitiveIndex != sample.primitiveIndex) continue;
                const float dx = sample.position[0] - source.position[0];
                const float dy = sample.position[1] - source.position[1];
                const float dz = sample.position[2] - source.position[2];
                const float normalAlignment = sample.normal[0] * source.normal[0] +
                    sample.normal[1] * source.normal[1] + sample.normal[2] * source.normal[2];
                const float planeOffset = dx * source.normal[0] +
                    dy * source.normal[1] + dz * source.normal[2];
                const float distanceSquared = dx * dx + dy * dy + dz * dz;
                const float maxDistance = 1.5f * (sample.halfWidth + source.halfWidth) + 0.5f;
                if (normalAlignment < 0.99f || std::abs(planeOffset) > 0.03f ||
                    distanceSquared > maxDistance * maxDistance ||
                    distanceSquared >= bestDistanceSquared) continue;
                bestDistanceSquared = distanceSquared;
                nearest = selected.index;
            }
            if (nearest >= 0) {
                emissiveLightShadow[lightIndex] = emissiveLightShadow[size_t(nearest)];
                const auto& source = emissiveCandidates[size_t(nearest)];
                emissiveLightShadowOrigin[lightIndex] = {
                    source.position[0], source.position[1], source.position[2], source.captureOffset};
            }
        }

        if (brdfDirty) {
            bgfx::setViewName(renderView(kBrdfView), "Split-sum BRDF integration LUT");
            bgfx::setViewFrameBuffer(renderView(kBrdfView), brdfLutBuffer);
            bgfx::setViewRect(renderView(kBrdfView), 0, 0, 256, 256);
            bgfx::setViewClear(renderView(kBrdfView), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            fullscreenTriangle();
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(renderView(kBrdfView), brdfLutProgram);
            brdfDirty = false;
        }

        if (environmentDirty) {
            const float probeGround[4]{ambientGround[0], ambientGround[1], ambientGround[2], 0.001f};
            for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip) {
                const uint16_t mipSize = std::max<uint16_t>(uint16_t(quality.environmentSize >> mip), 1);
                const float roughness = std::min(float(mip) / specularMaxMip, 1.0f);
                // Capture a lighting-normalized sky.  The 24x atmosphere
                // display scale is for the visible backdrop, not for PBR IBL.
                const float probeParams[4]{roughness, environmentRadiance,
                    atmosphereParams[0], atmosphereParams[1]};
                for (uint8_t face = 0; face < 6; ++face) {
                    const size_t index = size_t(mip) * 6 + face;
                    const uint16_t viewId = uint16_t(renderView(kEnvironmentView) + index);
                    const std::string viewName = "Sky environment mip " + std::to_string(mip) +
                        " face " + std::to_string(face);
                    const float probeFace[4]{float(face), float(mipSize),
                        float(hdriLighting.width), float(hdriLighting.height)};
                    bgfx::setViewName(viewId, viewName.c_str());
                    bgfx::setViewFrameBuffer(viewId, environmentBuffers[index]);
                    bgfx::setViewRect(viewId, 0, 0, mipSize, mipSize);
                    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
                    bgfx::setUniform(uProbeSun, sunDirection);
                    bgfx::setUniform(uProbeParams, probeParams);
                    bgfx::setUniform(uProbeGround, probeGround);
                    bgfx::setUniform(uProbeFace, probeFace);
                    bgfx::setUniform(uHdriParams, hdriParams);
                    bgfx::setTexture(0, sProbeScattering, atmosphereScattering);
                    bgfx::setTexture(1, sProbeSingleMie, atmosphereSingleMie);
                    bgfx::setTexture(2, sHdri, hdriTexture);
                    fullscreenTriangle();
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    bgfx::submit(viewId, environmentPrefilterProgram);
                }
            }
            environmentDirty = false;
        }

        static const bx::Vec3 pointShadowDirections[6]{
            { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            { 0.0f, 1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f}};
        static const bx::Vec3 pointShadowUps[6]{
            {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f,-1.0f}, {0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        for (uint8_t slot = 0; slot < kPointShadowSlotCount; ++slot) {
            const ShadowCandidate selection = selectedLocalShadows[slot];
            if (selection.index < 0) continue;
            std::array<float, 3> shadowPosition{};
            float shadowRadius = 1.0f;
            float shadowNear = 0.01f;
            std::string shadowName;
            if (selection.kind == ShadowKind::Spot) {
                const auto& light = genesis::scripting::g_spotLights[size_t(selection.index)];
                shadowPosition = light.position;
                shadowRadius = light.radius;
                shadowNear = spotLightShadow[size_t(selection.index)][2];
                shadowName = "Spot shadow " + light.name;
            } else if (selection.kind == ShadowKind::Point) {
                const auto& light = genesis::scripting::g_pointLights[size_t(selection.index)];
                shadowPosition = light.position;
                shadowRadius = light.radius;
                shadowNear = pointLightShadow[size_t(selection.index)][2];
                shadowName = "Point shadow " + light.name;
            } else if (selection.kind == ShadowKind::Area) {
                const auto& light = genesis::scripting::g_areaLights[size_t(selection.index)];
                shadowPosition = light.position;
                shadowRadius = light.radius;
                shadowNear = areaLightShadow[size_t(selection.index)][2];
                shadowName = "Area shadow " + light.name;
            } else {
                const auto& sample = emissiveCandidates[size_t(selection.index)];
                shadowPosition = sample.position;
                if (emissiveLightShadowSecondary[size_t(selection.index)][0] > 0.5f) {
                    const float offset = sample.captureOffset * (selection.secondary ? 1.0f : -1.0f);
                    for (size_t axis = 0; axis < 3; ++axis)
                        shadowPosition[axis] += sample.tangent[axis] * offset;
                }
                shadowRadius = emissiveLightPositionRadius[size_t(selection.index)][3];
                shadowNear = emissiveLightShadow[size_t(selection.index)][2];
                shadowName = "Emissive mesh shadow " + std::to_string(selection.index)
                    + (selection.secondary ? " secondary" : " primary");
            }
            const bx::Vec3 eye{shadowPosition[0], shadowPosition[1], shadowPosition[2]};
            const EmissiveLightSample* shadowEmitter = selection.kind == ShadowKind::Emissive
                ? &emissiveCandidates[size_t(selection.index)] : nullptr;
            bgfx::TransientIndexBuffer shadowRemainder{};
            const bool hasShadowRemainder = shadowEmitter &&
                shadowEmitter->sourceModel->shadowGeometryWithoutCluster(
                    shadowEmitter->clusterIndex, shadowRemainder);
            float pointProjection[16];
            bx::mtxProj(pointProjection, 90.0f, 1.0f, shadowNear, shadowRadius,
                bgfx::getCaps()->homogeneousDepth);
            for (uint8_t face = 0; face < 6; ++face) {
                const uint16_t viewId = uint16_t(renderView(kPointShadowView) + size_t(slot) * 6 + face);
                const bx::Vec3 target{eye.x + pointShadowDirections[face].x,
                    eye.y + pointShadowDirections[face].y, eye.z + pointShadowDirections[face].z};
                float pointView[16];
                bx::mtxLookAt(pointView, eye, target, pointShadowUps[face]);
                const std::string viewName = shadowName + " face " + std::to_string(face);
                bgfx::setViewName(viewId, viewName.c_str());
                bgfx::setViewFrameBuffer(viewId, pointShadowBuffers[size_t(slot) * 6 + face]);
                bgfx::setViewRect(viewId, 0, 0, quality.pointShadowSize, quality.pointShadowSize);
                bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
                bgfx::setViewTransform(viewId, pointView, pointProjection);
                bgfx::touch(viewId);
                for (const auto& model : models)
                    model->draw(viewId, kGeometryLayers, shadowProgram, true,
                        uSkinning, uJoints, materialUniforms, samplers, fallback,
                        {}, {}, {}, sShadows, shadowTextures, sEnvironment, environmentTextures,
                        shadowEmitter && model.get() == shadowEmitter->sourceModel
                            ? shadowEmitter->primitiveIndex : SIZE_MAX,
                        shadowEmitter && model.get() == shadowEmitter->sourceModel && hasShadowRemainder
                            ? &shadowRemainder : nullptr);
            }
        }

        for (uint8_t slot = 0; slot < kLocalProbeSlotCount; ++slot) if (localProbeDirty[slot] && activeReflectionProbes[slot] >= 0) {
            const auto& probe = genesis::scripting::g_reflectionProbes[size_t(activeReflectionProbes[slot])];
            static const bx::Vec3 directions[6]{
                { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                { 0.0f, 1.0f, 0.0f}, { 0.0f,-1.0f, 0.0f},
                { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f,-1.0f}};
            static const bx::Vec3 ups[6]{
                {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f,-1.0f}, {0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
            const bx::Vec3 probeEye{probe.position[0], probe.position[1], probe.position[2]};
            float captureFar = 10.0f;
            for (int axis = 0; axis < 3; ++axis)
                captureFar = std::max(captureFar, (probe.boundsMax[axis] - probe.boundsMin[axis]) * 2.0f);
            float captureProjection[16];
            bx::mtxProj(captureProjection, 90.0f, 1.0f, 0.05f, captureFar,
                bgfx::getCaps()->homogeneousDepth);
            const float captureCamera[4]{probe.position[0], probe.position[1], probe.position[2], 1.0f};
            std::array<std::array<float, 4>, kLocalProbeSlotCount> disabledProbePosition{};
            std::array<std::array<float, 4>, kLocalProbeSlotCount> disabledProbeMin{};
            std::array<std::array<float, 4>, kLocalProbeSlotCount> disabledProbeMax{};
            for (uint8_t disabledSlot = 0; disabledSlot < kLocalProbeSlotCount; ++disabledSlot) {
                disabledProbePosition[disabledSlot] = {
                    probe.position[0], probe.position[1], probe.position[2], 0.0f};
                disabledProbeMin[disabledSlot] = {
                    probe.boundsMin[0], probe.boundsMin[1], probe.boundsMin[2], probe.blendDistance};
                disabledProbeMax[disabledSlot] = {
                    probe.boundsMax[0], probe.boundsMax[1], probe.boundsMax[2], 0.0f};
            }
            const float captureShadowParams[4]{float(quality.shadowSize), 0.0015f, 0.0f, 0.0f};
            // Camera-fitted cascades cannot cover all six probe faces. Until
            // probes have dedicated shadow coverage, capture ambient lighting
            // rather than baking unoccluded direct sunlight into indoor probes.
            const float captureLightDir[4]{lightDir[0], lightDir[1], lightDir[2], 0.0f};
            const float disabledGridOrigin[4]{0.0f, 0.0f, 0.0f, 0.0f};
            const float disabledGridSpacing[4]{1.0f, 1.0f, 1.0f, 0.0f};
            const float disabledGridCounts[4]{2.0f, 2.0f, 2.0f, 8.0f};
            const std::array<bgfx::UniformHandle, 49> captureLightingUniforms{
                uLightMatrices[0], uLightMatrices[1], uLightMatrices[2], uCascadeSplits,
                uCamera, uLightDir, uLightColor, uAmbientSky, uAmbientGround, materialUniforms[4],
                uAtmosphereParams, uEnvironmentParams, uReflectionProbePosition,
                uReflectionProbeMin, uReflectionProbeMax, uSkyForward,
                uGridOrigin, uGridSpacing, uGridCounts,
                uPointLightPositionRadius[0], uPointLightPositionRadius[1],
                uPointLightPositionRadius[2], uPointLightPositionRadius[3],
                uPointLightColorIntensity[0], uPointLightColorIntensity[1],
                uPointLightColorIntensity[2], uPointLightColorIntensity[3], uPointLightShadow,
                uSpotLightPositionRadius, uSpotLightDirectionOuter, uSpotLightColorIntensity,
                uSpotLightParams, uSpotLightShadow, uPointShadowAtlasParams,
                uAreaLightPositionRadius, uAreaLightDirectionWidth,
                uAreaLightUpHeight, uAreaLightColorIntensity,
                uPointLightPattern, uSpotLightUpPattern, uHdriIrradiance, uAreaLightShadow,
                uEmissiveLightPositionRadius, uEmissiveLightDirectionArea, uEmissiveLightRadiance,
                uEmissiveLightShadow, uEmissiveLightTangentWidth,
                uEmissiveLightShadowSecondary, uEmissiveLightShadowOrigin};
            const std::array<const float*, 49> captureLightingValues{
                lightSampleMatrices[0].data(), lightSampleMatrices[1].data(), lightSampleMatrices[2].data(), cascadeSplits,
                captureCamera, captureLightDir, lightColor, ambientSky, ambientGround, captureShadowParams,
                atmosphereParams, environmentParams, disabledProbePosition[0].data(),
                disabledProbeMin[0].data(), disabledProbeMax[0].data(),
                skyForward, disabledGridOrigin, disabledGridSpacing, disabledGridCounts,
                pointLightPositionRadius[0].data(), pointLightPositionRadius[1].data(),
                pointLightPositionRadius[2].data(), pointLightPositionRadius[3].data(),
                pointLightColorIntensity[0].data(), pointLightColorIntensity[1].data(),
                pointLightColorIntensity[2].data(), pointLightColorIntensity[3].data(),
                pointLightShadow[0].data(), spotLightPositionRadius[0].data(),
                spotLightDirectionOuter[0].data(), spotLightColorIntensity[0].data(),
                spotLightParams[0].data(), spotLightShadow[0].data(), pointShadowAtlasParams,
                areaLightPositionRadius[0].data(), areaLightDirectionWidth[0].data(),
                areaLightUpHeight[0].data(), areaLightColorIntensity[0].data(),
                pointLightPattern[0].data(), spotLightUpPattern[0].data(), hdriIrradiance[0].data(),
                areaLightShadow[0].data(), emissiveLightPositionRadius[0].data(),
                emissiveLightDirectionArea[0].data(), emissiveLightRadiance[0].data(),
                emissiveLightShadow[0].data(), emissiveLightTangentWidth[0].data(),
                emissiveLightShadowSecondary[0].data(), emissiveLightShadowOrigin[0].data()};
            const std::array<bgfx::TextureHandle, 8> captureEnvironmentTextures{
                atmosphereIrradiance, environmentSpecular, brdfLut,
                localReflectionAtlas, localDiffuseAtlas, brdfLut,
                pointShadowTextures[0], pointShadowTextures[1]};

            for (uint8_t face = 0; face < 6; ++face) {
                const size_t captureIndex = size_t(slot) * 6 + face;
                const uint16_t viewId = uint16_t(renderView(kLocalCaptureView) + captureIndex);
                const bx::Vec3 target{probeEye.x + directions[face].x,
                    probeEye.y + directions[face].y, probeEye.z + directions[face].z};
                float captureView[16];
                bx::mtxLookAt(captureView, probeEye, target, ups[face]);
                const std::string viewName = "Local reflection capture " + probe.name +
                    " face " + std::to_string(face);
                bgfx::setViewName(viewId, viewName.c_str());
                bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
                bgfx::setViewFrameBuffer(viewId, localCaptureBuffers[captureIndex]);
                bgfx::setViewRect(viewId, 0, 0, quality.environmentSize, quality.environmentSize);
                bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
                bgfx::setViewTransform(viewId, captureView, captureProjection);

                const float copyParams[4]{0.0f, float(face), 0.0f, 0.0f};
                bgfx::setUniform(uLocalPrefilterParams, copyParams);
                bgfx::setTexture(0, sProbeSource, environmentSpecular);
                fullscreenTriangle();
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(viewId, localPrefilterProgram);

                for (const auto& model : models)
                    model->draw(viewId, layerMask(RenderLayer::World), pbrProgram, false,
                        uSkinning, uJoints, materialUniforms, samplers, fallback,
                        captureLightingUniforms, captureLightingValues, lightingCounts, sShadows, shadowTextures,
                        sEnvironment, captureEnvironmentTextures);
            }

            for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip) {
                const uint16_t mipSize = std::max<uint16_t>(uint16_t(quality.environmentSize >> mip), 1);
                const float roughness = std::min(float(mip) / specularMaxMip, 1.0f);
                for (uint8_t face = 0; face < 6; ++face) {
                    const size_t index = (size_t(slot) * quality.environmentMipCount + mip) * 6 + face;
                    const uint16_t viewId = uint16_t(renderView(kLocalPrefilterView) + index);
                    const float prefilterParams[4]{roughness, float(face), 0.0f, float(slot)};
                    bgfx::setViewName(viewId, "Local reflection prefilter");
                    bgfx::setViewFrameBuffer(viewId, localPrefilterBuffers[index]);
                    bgfx::setViewRect(viewId, 0, 0, mipSize, mipSize);
                    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
                    bgfx::setUniform(uLocalPrefilterParams, prefilterParams);
                    bgfx::setTexture(0, sProbeSource, localProbeRaw[slot]);
                    fullscreenTriangle();
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    bgfx::submit(viewId, localPrefilterProgram);
                }
            }
            for (uint8_t face = 0; face < 6; ++face) {
                const size_t index = size_t(slot) * 6 + face;
                const uint16_t viewId = uint16_t(renderView(kLocalDiffuseView) + index);
                const float params[4]{0.0f, float(face), 1.0f, float(slot)};
                bgfx::setViewName(viewId, "Local diffuse irradiance convolution");
                bgfx::setViewFrameBuffer(viewId, localDiffuseBuffers[index]);
                bgfx::setViewRect(viewId, 0, 0, 16, 16);
                bgfx::setUniform(uLocalPrefilterParams, params);
                bgfx::setTexture(0, sProbeSource, localProbeRaw[slot]);
                fullscreenTriangle();
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(viewId, localPrefilterProgram);
            }
            const uint16_t copyView = uint16_t(renderView(kLocalAtlasCopyView) + slot);
            bgfx::setViewName(copyView, "Local probe atlas copy");
            for (uint8_t mip = 0; mip < quality.environmentMipCount; ++mip) {
                const uint16_t mipSize = std::max<uint16_t>(uint16_t(quality.environmentSize >> mip), 1);
                for (uint8_t face = 0; face < 6; ++face) {
                    bgfx::TextureRegion destination{
                        .handle = localReflectionAtlas, .mip = mip, .z = uint16_t(slot * 6 + face),
                        .width = mipSize, .height = mipSize, .depth = 1};
                    bgfx::TextureRegion source{
                        .handle = localProbeFiltered[slot], .mip = mip, .z = face,
                        .width = mipSize, .height = mipSize, .depth = 1};
                    bgfx::blit(copyView, destination, source);
                }
            }
            for (uint8_t face = 0; face < 6; ++face) {
                bgfx::TextureRegion destination{
                    .handle = localDiffuseAtlas, .z = uint16_t(slot * 6 + face),
                    .width = 16, .height = 16, .depth = 1};
                bgfx::TextureRegion source{
                    .handle = localDiffuseProbe[slot], .z = face,
                    .width = 16, .height = 16, .depth = 1};
                bgfx::blit(copyView, destination, source);
            }
            localProbeDirty[slot] = false;
        }

        for (uint16_t cascade = 0; cascade < kCascadeCount; ++cascade) {
            const uint16_t viewId = renderView(kShadowView) + cascade;
            const std::string viewName = "Sun shadow cascade " + std::to_string(cascade);
            bgfx::setViewName(viewId, viewName.c_str());
            bgfx::setViewFrameBuffer(viewId, shadowBuffers[cascade]);
            bgfx::setViewRect(viewId, 0, 0, quality.shadowSize, quality.shadowSize);
            bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(viewId, lightViews[cascade].data(), lightProjections[cascade].data());
            bgfx::touch(viewId);
            for (const auto& model : models)
                model->draw(viewId, kGeometryLayers, shadowProgram, true, uSkinning, uJoints, materialUniforms, samplers, fallback,
                    {}, {}, {}, sShadows, shadowTextures, sEnvironment, environmentTextures);
        }

        bgfx::setViewName(renderView(kSkyView), "HDR atmosphere");
        bgfx::setViewFrameBuffer(renderView(kSkyView), hdrBuffer);
        bgfx::setViewRect(renderView(kSkyView), 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setViewClear(renderView(kSkyView), BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        bgfx::setUniform(uSkyRight, skyRight);
        bgfx::setUniform(uSkyUp, skyUp);
        bgfx::setUniform(uSkyForward, skyForward);
        bgfx::setUniform(uSunDirection, sunDirection);
        bgfx::setUniform(uAtmosphereParams, atmosphereParams);
        bgfx::setUniform(uHdriParams, hdriParams);
        const bool authoredSky=genesis::scripting::g_environment.atmosphereConfigured ||
            (!genesis::scripting::g_environment.hdriPath.empty() && genesis::scripting::g_environment.visible);
        const float editorBackground[4]{editor && !gamePass &&
            (viewportShading!=3 || !authoredSky) ? 1.0f : 0.0f,0,0,0};
        bgfx::setUniform(uEditorBackground, editorBackground);
        bgfx::setTexture(0, sAtmosphereTransmittance, atmosphereTransmittance);
        bgfx::setTexture(1, sAtmosphereScattering, atmosphereScattering);
        bgfx::setTexture(2, sAtmosphereSingleMie, atmosphereSingleMie);
        bgfx::setTexture(3, sHdri, hdriTexture);
        fullscreenTriangle();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(renderView(kSkyView), skyProgram);

        bgfx::setViewName(renderView(kMainView), viewportShading==3 ? "PBR scene" : "Viewport shading");
        bgfx::setViewFrameBuffer(renderView(kMainView), hdrBuffer);
        bgfx::setViewRect(renderView(kMainView), 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setViewClear(renderView(kMainView), BGFX_CLEAR_NONE);
        bgfx::setViewTransform(renderView(kMainView), view, projection);
        bgfx::touch(renderView(kMainView));
        float shadowParams[4]{float(quality.shadowSize), 0.0015f, 1.0f, 0};
        float gridOrigin[4]{0.0f, 0.0f, 0.0f, 0.0f};
        float gridSpacing[4]{1.0f, 1.0f, 1.0f, 0.0f};
        float gridCounts[4]{2.0f, 2.0f, 2.0f, 8.0f};
        if (activeReflectionProbes[0] >= 0 && size_t(activeReflectionProbes[0]) < diffuseGrids.size()) {
            const auto& grid = diffuseGrids[size_t(activeReflectionProbes[0])];
            std::copy(grid.origin.begin(), grid.origin.end(), gridOrigin);
            std::copy(grid.spacing.begin(), grid.spacing.end(), gridSpacing);
            std::copy(grid.counts.begin(), grid.counts.end(), gridCounts);
            environmentTextures[5] = grid.atlas;
        }
        const std::array<bgfx::UniformHandle, 49> lightingUniforms{
            uLightMatrices[0], uLightMatrices[1], uLightMatrices[2], uCascadeSplits,
            uCamera, uLightDir, uLightColor, uAmbientSky, uAmbientGround, materialUniforms[4],
            uAtmosphereParams, uEnvironmentParams, uReflectionProbePosition,
            uReflectionProbeMin, uReflectionProbeMax, uSkyForward,
            uGridOrigin, uGridSpacing, uGridCounts,
            uPointLightPositionRadius[0], uPointLightPositionRadius[1],
            uPointLightPositionRadius[2], uPointLightPositionRadius[3],
            uPointLightColorIntensity[0], uPointLightColorIntensity[1],
            uPointLightColorIntensity[2], uPointLightColorIntensity[3], uPointLightShadow,
            uSpotLightPositionRadius, uSpotLightDirectionOuter, uSpotLightColorIntensity,
            uSpotLightParams, uSpotLightShadow, uPointShadowAtlasParams,
            uAreaLightPositionRadius, uAreaLightDirectionWidth,
            uAreaLightUpHeight, uAreaLightColorIntensity,
            uPointLightPattern, uSpotLightUpPattern, uHdriIrradiance, uAreaLightShadow,
            uEmissiveLightPositionRadius, uEmissiveLightDirectionArea, uEmissiveLightRadiance,
            uEmissiveLightShadow, uEmissiveLightTangentWidth,
            uEmissiveLightShadowSecondary, uEmissiveLightShadowOrigin
        };
        const std::array<const float*, 49> lightingValues{
            lightSampleMatrices[0].data(), lightSampleMatrices[1].data(), lightSampleMatrices[2].data(), cascadeSplits,
            camera, lightDir, lightColor, ambientSky, ambientGround, shadowParams,
            atmosphereParams, environmentParams, reflectionProbePosition[0].data(),
            reflectionProbeMin[0].data(), reflectionProbeMax[0].data(), skyForward,
            gridOrigin, gridSpacing, gridCounts,
            pointLightPositionRadius[0].data(), pointLightPositionRadius[1].data(),
            pointLightPositionRadius[2].data(), pointLightPositionRadius[3].data(),
            pointLightColorIntensity[0].data(), pointLightColorIntensity[1].data(),
            pointLightColorIntensity[2].data(), pointLightColorIntensity[3].data(),
            pointLightShadow[0].data(), spotLightPositionRadius[0].data(),
            spotLightDirectionOuter[0].data(), spotLightColorIntensity[0].data(),
            spotLightParams[0].data(), spotLightShadow[0].data(), pointShadowAtlasParams,
            areaLightPositionRadius[0].data(), areaLightDirectionWidth[0].data(),
            areaLightUpHeight[0].data(), areaLightColorIntensity[0].data(),
            pointLightPattern[0].data(), spotLightUpPattern[0].data(), hdriIrradiance[0].data(),
            areaLightShadow[0].data(), emissiveLightPositionRadius[0].data(),
            emissiveLightDirectionArea[0].data(), emissiveLightRadiance[0].data(),
            emissiveLightShadow[0].data(), emissiveLightTangentWidth[0].data(),
            emissiveLightShadowSecondary[0].data(), emissiveLightShadowOrigin[0].data()
        };
        for (const auto& model : models)
            model->draw(renderView(kMainView), kGeometryLayers, viewportShading==3?pbrProgram:viewportProgram, false, uSkinning, uJoints, materialUniforms, samplers, fallback,
                lightingUniforms, lightingValues, lightingCounts, sShadows, shadowTextures, sEnvironment, environmentTextures,
                SIZE_MAX,nullptr,false,uViewportShading,viewportShading,uSelectionId,
                editor && !gamePass && model->entityId && editor->isSelected(*model->entityId) ? 1.0f : 0.0f);
        if(viewportShading==3)lightSourceRenderer.draw(renderView(kMainView));

        float aoParams[4]{0.45f, 0.05f, 0.75f, 0.10f};
        float qualityParams[4]{float(quality.aoSamples), float(quality.contactSteps),
            float(quality.volumetricSteps), quality.effectsScale};
        const uint16_t effectsWidth = uint16_t((width + int(quality.effectsScale) - 1) / int(quality.effectsScale));
        const uint16_t effectsHeight = uint16_t((height + int(quality.effectsScale) - 1) / int(quality.effectsScale));
        bgfx::setViewName(renderView(kAoView), "Quality-scaled SSAO and contact shadows");
        bgfx::setViewFrameBuffer(renderView(kAoView), aoBuffer);
        bgfx::setViewRect(renderView(kAoView), 0, 0, effectsWidth, effectsHeight);
        bgfx::setViewClear(renderView(kAoView), BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
        bgfx::setUniform(uSkyRight, skyRight);
        bgfx::setUniform(uSkyUp, skyUp);
        bgfx::setUniform(uSkyForward, skyForward);
        bgfx::setUniform(uCamera, camera);
        bgfx::setUniform(uLightDir, lightDir);
        bgfx::setUniform(uDepthParams, depthParams);
        bgfx::setUniform(uAoParams, aoParams);
        bgfx::setUniform(uQualityParams, qualityParams);
        bgfx::setTexture(0, sAoDepth, hdrDepth);
        bgfx::setTexture(1, sAoNormal, sceneNormal);
        fullscreenTriangle();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(renderView(kAoView), aoProgram);

        bgfx::setViewName(renderView(kVolumetricView), "Quality-scaled volumetric atmosphere");
        bgfx::setViewFrameBuffer(renderView(kVolumetricView), fogBuffer);
        bgfx::setViewRect(renderView(kVolumetricView), 0, 0, effectsWidth, effectsHeight);
        bgfx::setViewClear(renderView(kVolumetricView), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        bgfx::setUniform(uSkyRight, skyRight);
        bgfx::setUniform(uSkyUp, skyUp);
        bgfx::setUniform(uSkyForward, skyForward);
        bgfx::setUniform(uCamera, camera);
        bgfx::setUniform(uLightDir, lightDir);
        bgfx::setUniform(uLightColor, lightColor);
        bgfx::setUniform(uAmbientSky, ambientSky);
        bgfx::setUniform(uFogParams, fogParams);
        bgfx::setUniform(uDepthParams, depthParams);
        bgfx::setUniform(uQualityParams, qualityParams);
        bgfx::setUniform(materialUniforms[4], shadowParams);
        bgfx::setUniform(uCascadeSplits, cascadeSplits);
        for (uint8_t cascade = 0; cascade < kCascadeCount; ++cascade)
            bgfx::setUniform(uLightMatrices[cascade], lightSampleMatrices[cascade].data());
        bgfx::setTexture(0, sSceneDepth, hdrDepth);
        for (uint8_t cascade = 0; cascade < kCascadeCount; ++cascade)
            bgfx::setTexture(uint8_t(1 + cascade), sVolumeShadows[cascade], shadowTextures[cascade]);
        fullscreenTriangle();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(renderView(kVolumetricView), volumetricProgram);

        if (!bloom.render(renderView(kBloomPrefilterView),uint16_t(width),uint16_t(height),hdrColor,
                          genesis::scripting::g_renderer.bloom)) {
            std::fprintf(stderr,"Bloom render target or transient allocation failed\n");
            renderFailed = true;
            running = false;
            break;
        }

        const genesis::color::Settings& color = genesis::color::settings();
        float histogramParams[4]{1.0f / float(width), 1.0f / float(height), -12.0f, 1.0f / 16.0f};
        bgfx::setViewName(renderView(kHistogramClearView), "Clear luminance histogram");
        bgfx::setBuffer(0, luminanceHistogram, bgfx::Access::Write);
        bgfx::dispatch(renderView(kHistogramClearView), histogramClearProgram, 4, 1, 1);

        const uint32_t sampledWidth = (uint32_t(width) + 3u) / 4u;
        const uint32_t sampledHeight = (uint32_t(height) + 3u) / 4u;
        bgfx::setViewName(renderView(kHistogramView), "HDR luminance histogram");
        bgfx::setUniform(uHistogramParams, histogramParams);
        bgfx::setTexture(0, sHistogramHdr, hdrColor);
        bgfx::setTexture(1, sHistogramFog, fogTexture);
        bgfx::setTexture(2, sHistogramAo, aoTexture);
        bgfx::setBuffer(3, luminanceHistogram, bgfx::Access::ReadWrite);
        bgfx::dispatch(renderView(kHistogramView), histogramProgram,
            (sampledWidth + 15u) / 16u, (sampledHeight + 15u) / 16u, 1);

        const uint8_t exposureWrite = uint8_t((gamePass ? 2 : 0) + (frameIndex & 1u));
        const uint8_t exposureRead = uint8_t(exposureWrite ^ 1u);
        float exposureParams0[4]{-12.0f, 4.0f, 0.10f, 0.95f};
        float exposureParams1[4]{color.autoExposureMinEv, std::min(color.autoExposureMaxEv, 3.0f),
            deltaTime, color.exposure};
        float exposureParams2[4]{color.autoExposure ? 1.0f : 0.0f, 0.070f, 1.25f,
            frameIndex < 2 ? 1.0f : 0.0f};
        bgfx::setViewName(renderView(kAutoExposureView), "Percentile temporal auto exposure");
        bgfx::setUniform(uExposureParams0, exposureParams0);
        bgfx::setUniform(uExposureParams1, exposureParams1);
        bgfx::setUniform(uExposureParams2, exposureParams2);
        bgfx::setImage(0, exposureTextures[exposureWrite], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
        bgfx::setTexture(1, sPreviousExposure, exposureTextures[exposureRead]);
        bgfx::setBuffer(2, luminanceHistogram, bgfx::Access::Read);
        bgfx::dispatch(renderView(kAutoExposureView), autoExposureProgram, 1, 1, 1);

        float contrast = 1.0f, saturation = 1.0f;
        if (color.look == genesis::color::Look::Punchy) { contrast = 1.12f; saturation = 1.08f; }
        else if (color.look == genesis::color::Look::MediumHighContrast) contrast = 1.07f;
        else if (color.look == genesis::color::Look::Golden) { contrast = 1.04f; saturation = 1.04f; }
        float colorParams[4]{float(color.viewTransform), contrast, saturation,
            viewportShading==3 && genesis::scripting::g_renderer.bloom ? 0.28f : 0.0f};
        float upsampleParams[4]{quality.effectsScale / float(width), quality.effectsScale / float(height),
            depthParams[0], depthParams[1]};
        const float debugMode = genesis::scripting::g_renderer.debugView == "ao" ? 1.0f
            : genesis::scripting::g_renderer.debugView == "contact" ? 2.0f
            : genesis::scripting::g_renderer.debugView == "invalid" ? 3.0f
            : genesis::scripting::g_renderer.debugView == "color-chart" ? 4.0f
            : genesis::scripting::g_renderer.debugView == "material-ao" ? 5.0f
            : genesis::scripting::g_renderer.debugView == "bloom" ? 6.0f : 0.0f;
        float debugParams[4]{debugMode, viewportShading==3?0.0f:1.0f, 0.0f, 0.0f};
        bgfx::setViewName(renderView(kTonemapView), debugMode > 0.0f ? "Renderer diagnostic" : "Color tonemap");
        bgfx::setViewFrameBuffer(renderView(kTonemapView), BGFX_INVALID_HANDLE);
        bgfx::setViewRect(renderView(kTonemapView),uint16_t(viewport.x),uint16_t(viewport.y),uint16_t(std::max(viewport.width,1.0f)),uint16_t(std::max(viewport.height,1.0f)));
        bgfx::setViewClear(renderView(kTonemapView), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        bgfx::setTexture(0, sHdr, hdrColor);
        bgfx::setTexture(1, sFog, fogTexture);
        bgfx::setTexture(2, sSceneDepthTonemap, hdrDepth);
        bgfx::setTexture(3, sFogDepth, fogDepth);
        bgfx::setTexture(4, sExposure, exposureTextures[exposureWrite]);
        bgfx::setTexture(5, sAo, aoTexture);
        bgfx::setTexture(6, sAoDepthUpsample, aoDepth);
        bgfx::setTexture(7, sBloom, bloom.texture());
        bgfx::setTexture(8, sMaterialAo, sceneNormal);
        bgfx::setUniform(uColorParams, colorParams);
        bgfx::setUniform(uUpsampleParams, upsampleParams);
        bgfx::setUniform(uDebugParams, debugParams);
        fullscreenTriangle();
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(renderView(kTonemapView), tonemapProgram);

        if (editor && !gamePass) {
            genesis::editor::Bounds selectedBounds;
            std::vector<const SceneModel*> selectedModels;
            for (const auto& model:models) if(editor->editorMode()!=genesis::editor::GameEditor::EditorMode::Edit &&
                model->visible && model->entityId && editor->isSelected(*model->entityId)) {
                const auto bounds=model->bounds();if(bounds.valid()){
                    selectedBounds.include(bounds.minimum);selectedBounds.include(bounds.maximum);
                    if(editor->selected() && *model->entityId==*editor->selected())
                        editor->setSelectedDimensions(*model->entityId,{
                            bounds.maximum[0]-bounds.minimum[0],
                            bounds.maximum[1]-bounds.minimum[1],
                            bounds.maximum[2]-bounds.minimum[2]});
                }
                selectedModels.push_back(model.get());
            }
            if(const auto entity=editor->selectedEntity();entity && entity->probe) {
                for(float sign:{-1.0f,1.0f}){auto point=entity->transform.position;for(int i=0;i<3;++i)point[i]+=sign*entity->probe->size[i]*std::abs(entity->transform.scale[i])*.5f;selectedBounds.include(point);}
            }
            if(!selectedModels.empty() && bgfx::isValid(selectionMaskProgram) && bgfx::isValid(selectionOutlineProgram)
                && bgfx::isValid(selectionBuffer)){
                const uint16_t maskView=renderView(kSelectionMaskView);
                bgfx::setViewName(maskView,"Selected mesh mask");
                bgfx::setViewFrameBuffer(maskView,selectionBuffer);
                bgfx::setViewRect(maskView,0,0,uint16_t(width),uint16_t(height));
                bgfx::setViewClear(maskView,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,0x00000000,1.0f,0);
                bgfx::setViewTransform(maskView,view,projection);
                bgfx::touch(maskView);
                for(const auto* selectedModel:selectedModels)
                    selectedModel->draw(maskView,kGeometryLayers,selectionMaskProgram,true,uSkinning,uJoints,
                        materialUniforms,samplers,fallback,{},{},{},sShadows,shadowTextures,
                        sEnvironment,environmentTextures,SIZE_MAX,nullptr,true,BGFX_INVALID_HANDLE,viewportShading);
                const uint16_t outlineView=renderView(kSelectionOutlineView);
                bgfx::setViewName(outlineView,"Selected mesh outline");
                bgfx::setViewFrameBuffer(outlineView,BGFX_INVALID_HANDLE);
                bgfx::setViewRect(outlineView,uint16_t(viewport.x),uint16_t(viewport.y),
                    uint16_t(viewport.width),uint16_t(viewport.height));
                bgfx::setTexture(0,sSelectionMask,selectionColor);
                bgfx::setTexture(1,sSceneSelection,sceneSelection);
                const float outlineParams[4]{1.5f/width,1.5f/height,0,0};
                bgfx::setUniform(uSelectionOutline,outlineParams);
                fullscreenTriangle();
                bgfx::setScissor(uint16_t(viewport.x),uint16_t(viewport.y),
                    uint16_t(viewport.width),uint16_t(viewport.height));
                bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_ALPHA);
                bgfx::submit(outlineView,selectionOutlineProgram);
                selectedBounds={}; // Mesh selection uses its silhouette; bounds remain for non-mesh components.
            }
            editorOverlay.draw(renderView(kEditorView),viewport,view,projection,hdrDepth,editor->grid(),
                cameraPosition,pose.distance(),selectedBounds,editorHost->cursorPosition(),
                editorHost->gizmoFrame(),editorHost->gizmoHighlight());
            if(editor->editorMode()==genesis::editor::GameEditor::EditorMode::Edit) {
                const auto& mesh=editor->meshOverlay();
                editorOverlay.drawMeshEdit(renderView(kEditorView),viewport,view,projection,
                    int(editor->meshSelectionMode()),hdrDepth,
                    mesh.vertices,mesh.edges,mesh.faces,mesh.selected);
            }
            if(const auto gesture=editorHost->selectionPreview();gesture)
                editorOverlay.drawSelectionGesture(renderView(kSelectionGestureView),viewport,hdrDepth,
                    gesture->points,int(gesture->mode),gesture->radius,gesture->cursor);
        }
#ifdef GENESIS_WITH_PHYSX_PBD
        if(editor){
            if(!gamePass && !editor->playing() && particlePreviewRevision!=editor->revision()){
                particlePreview=genesis::physics::particleSeedFrames(genesis::gameplay::g_world.snapshots());
                particlePreviewRevision=editor->revision();
            }
            const auto& frames=particleRuntimeStarted?particleRuntime->frames():particlePreview;
            if(particleRuntimeStarted || (!gamePass && !editor->playing()))
                editorOverlay.drawParticles(renderView(kEditorView),viewport,view,projection,hdrDepth,frames);
        }
#endif
        }
        if(renderFailed)break;
        const auto& color=genesis::color::settings();

        const bgfx::Stats* stats = bgfx::getStats();
        double gpuMs = -1.0;
        double shadowMs = 0.0;
        double sceneMs = 0.0;
        double aoMs = 0.0;
        double fogMs = 0.0;
        double postMs = 0.0;
        double cpuMs = 0.0;
        if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd >= stats->gpuTimeBegin) {
            const double gpuToMs = 1000.0 / double(stats->gpuTimerFreq);
            gpuMs = double(stats->gpuTimeEnd - stats->gpuTimeBegin) * gpuToMs;
            for (uint16_t index = 0; index < stats->numViews; ++index) {
                const bgfx::ViewStats& viewStats = stats->viewStats[index];
                if (viewStats.gpuTimeEnd < viewStats.gpuTimeBegin) continue;
                const double elapsed = double(viewStats.gpuTimeEnd - viewStats.gpuTimeBegin) * gpuToMs;
                const uint16_t passView=viewStats.view%kViewStride;
                if (passView >= kShadowView && passView < kShadowView + kCascadeCount)
                    shadowMs += elapsed;
                else if (passView == kSkyView || passView == kMainView)
                    sceneMs += elapsed;
                else if (passView == kAoView)
                    aoMs += elapsed;
                else if (passView == kVolumetricView)
                    fogMs += elapsed;
                else if (passView >= kBloomPrefilterView && passView <= kTonemapView)
                    postMs += elapsed;
            }
            const double blend = smoothedGpuMs < 0.0 ? 1.0 : 0.15;
            auto smooth = [blend](double previous, double current) {
                return previous + (current - previous) * blend;
            };
            smoothedGpuMs = smoothedGpuMs < 0.0 ? gpuMs : smooth(smoothedGpuMs, gpuMs);
            smoothedShadowMs = smooth(smoothedShadowMs, shadowMs);
            smoothedSceneMs = smooth(smoothedSceneMs, sceneMs);
            smoothedAoMs = smooth(smoothedAoMs, aoMs);
            smoothedFogMs = smooth(smoothedFogMs, fogMs);
            smoothedPostMs = smooth(smoothedPostMs, postMs);
        }
        if (stats->cpuTimerFreq > 0)
            cpuMs = double(stats->cpuTimeFrame) * 1000.0 / double(stats->cpuTimerFreq);

        const auto& debugSun = genesis::atmosphere::settings();
        genesis::ui::RendererHudInfo hudInfo;
        hudInfo.backend = std::string(bgfx::getRendererName(bgfx::getRendererType())) + "  /  RASTER PBR";
        hudInfo.quality = genesis::scripting::g_renderer.quality;
        hudInfo.debugView = genesis::scripting::g_renderer.debugView;
        hudInfo.viewportWidth = width;
        hudInfo.viewportHeight = height;
        hudInfo.gpuMs = std::max(smoothedGpuMs, 0.0);
        hudInfo.cpuMs = cpuMs;
        hudInfo.shadowMs = smoothedShadowMs;
        hudInfo.sceneMs = smoothedSceneMs;
        hudInfo.aoMs = smoothedAoMs;
        hudInfo.fogMs = smoothedFogMs;
        hudInfo.postMs = smoothedPostMs;
        hudInfo.draws = stats->numDraw;
        std::copy(cameraPosition.begin(), cameraPosition.end(), hudInfo.camera);
        hudInfo.yawDegrees = cameraYaw * 180.0f / 3.14159265f;
        hudInfo.pitchDegrees = cameraPitch * 180.0f / 3.14159265f;
        hudInfo.sunAzimuth = debugSun.sunAzimuthDegrees;
        hudInfo.sunElevation = debugSun.sunElevationDegrees;
        hudInfo.automaticExposure = color.autoExposure;
        hudInfo.exposureEv = color.exposure;
        hudInfo.cookieControls = interactiveCookieIndex >= 0;
        // Rendering the scene can take longer than a mouse-motion interval.
        // Sample the current pointer immediately before drawing the UI so a
        // captured dock divider does not display an older queued position.
        if(editor && editor->document().hasPointerCapture() &&
            editor->document().cursor()!=genesis::ui::Cursor::Arrow) {
            SDL_PumpEvents();
            float pointerX=0,pointerY=0;
            SDL_GetMouseState(&pointerX,&pointerY);
            editor->document().pointerMove(pointerX,pointerY);
            SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
        }
        if(gpuUi){
            try{
                if(editor){
                    editor->layout(float(width),float(height));
                    uiRenderer.draw(kHudView,editor->document().renderGpu(float(width),float(height)));
                }else uiRenderer.draw(kHudView,rendererHud.renderGpu(uint16_t(width),hudInfo));
            }catch(const std::exception& error){
                std::fprintf(stderr,"GPU UI unavailable: %s\n",error.what());
                gpuUi=false;
            }
        }
        if(!gpuUi){
            const auto& hud = editor ? editor->render(float(width),float(height)) : rendererHud.render(uint16_t(width), hudInfo);
            if (hudRevision!=hud.revision) {
                bgfx::updateTexture2D(hudTexture, 0, 0, 0, 0, uint16_t(hud.width), uint16_t(hud.height),
                    bgfx::copy(hud.pixels.data(), uint32_t(hud.pixels.size() * sizeof(uint32_t))));
                hudRevision=hud.revision;
            }
            bgfx::setViewName(kHudView, "ThorVG renderer HUD");
            bgfx::setViewFrameBuffer(kHudView, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(kHudView, 0, 0, uint16_t(width), std::min<uint16_t>(hud.height, uint16_t(height)));
            bgfx::setTexture(0, sHud, hudTexture);
            fullscreenTriangle();
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
            bgfx::submit(kHudView, hudProgram);
        }
        if (!startupCapture.empty() && orbitCapture.ready(frameIndex)) {
            if (!startupCapture.parent_path().empty()) fs::create_directories(startupCapture.parent_path());
            if (genesis::scripting::g_renderer.gpuTimings)
                std::printf("GPU_TIMINGS preset=%s total_ms=%.3f shadows_ms=%.3f scene_ms=%.3f ao_ms=%.3f fog_ms=%.3f post_ms=%.3f draws=%u\n",
                    genesis::scripting::g_renderer.quality.c_str(), smoothedGpuMs,
                    smoothedShadowMs, smoothedSceneMs, smoothedAoMs, smoothedFogMs,
                    smoothedPostMs, stats->numDraw);
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, orbitCapture.nextPath(startupCapture).string().c_str());
        }
        bgfx::frame();
#ifdef __EMSCRIPTEN__
        genesis::web::frame(window);
#endif
        ++frameIndex;
        if (!startupCapture.empty() && callback.screenshotsComplete.load(std::memory_order_acquire) >= orbitCapture.count)
            running = false;
    }

    models.clear();
    uiRenderer.shutdown();
    editorOverlay.shutdown();
    editorHost.reset();
    lightSourceRenderer.shutdown();
    for (const auto& grid : diffuseGrids) {
        if (bgfx::isValid(grid.atlas)) bgfx::destroy(grid.atlas);
    }
    bgfx::destroy(fogBuffer);
    bgfx::destroy(fogTexture);
    bgfx::destroy(fogDepth);
    bloom.shutdown();
    bgfx::destroy(hudTexture);
    bgfx::destroy(hdrBuffer);
    bgfx::destroy(selectionBuffer);
    bgfx::destroy(selectionColor);
    bgfx::destroy(selectionDepth);
    bgfx::destroy(hdrColor);
    bgfx::destroy(sceneNormal);
    bgfx::destroy(sceneSelection);
    bgfx::destroy(hdrDepth);
    bgfx::destroy(aoBuffer);
    bgfx::destroy(aoTexture);
    bgfx::destroy(aoDepth);
    bgfx::destroy(luminanceHistogram);
    for (auto texture : exposureTextures) bgfx::destroy(texture);
    for (uint16_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        bgfx::destroy(shadowBuffers[cascade]);
        bgfx::destroy(shadowTextures[cascade]);
    }
    for (auto buffer : pointShadowBuffers) bgfx::destroy(buffer);
    for (auto texture : pointShadowTextures) bgfx::destroy(texture);
    for (auto texture : fallback) bgfx::destroy(texture);
    bgfx::destroy(pbrProgram); bgfx::destroy(viewportProgram); bgfx::destroy(shadowProgram); bgfx::destroy(skyProgram); bgfx::destroy(aoProgram);
    bgfx::destroy(environmentPrefilterProgram);
    bgfx::destroy(localPrefilterProgram); bgfx::destroy(brdfLutProgram);
    bgfx::destroy(volumetricProgram);
    bgfx::destroy(tonemapProgram); bgfx::destroy(hudProgram);
    bgfx::destroy(selectionMaskProgram);bgfx::destroy(selectionOutlineProgram);
    bgfx::destroy(histogramClearProgram); bgfx::destroy(histogramProgram); bgfx::destroy(autoExposureProgram);
    for (auto uniform : uLightMatrices) bgfx::destroy(uniform);
    bgfx::destroy(uCascadeSplits); bgfx::destroy(uJoints); bgfx::destroy(uSkinning); bgfx::destroy(uViewportShading);
    bgfx::destroy(uAmbientSky); bgfx::destroy(uAmbientGround);
    bgfx::destroy(uSkyRight); bgfx::destroy(uSkyUp); bgfx::destroy(uSkyForward); bgfx::destroy(uEditorBackground);
    bgfx::destroy(uGridOrigin); bgfx::destroy(uGridSpacing); bgfx::destroy(uGridCounts);
    for (auto uniform : uPointLightPositionRadius) bgfx::destroy(uniform);
    for (auto uniform : uPointLightColorIntensity) bgfx::destroy(uniform);
    bgfx::destroy(uPointLightShadow);
    bgfx::destroy(uPointLightPattern);
    bgfx::destroy(uSpotLightPositionRadius); bgfx::destroy(uSpotLightDirectionOuter);
    bgfx::destroy(uSpotLightColorIntensity); bgfx::destroy(uSpotLightParams);
    bgfx::destroy(uSpotLightShadow);
    bgfx::destroy(uSpotLightUpPattern);
    bgfx::destroy(uAreaLightPositionRadius); bgfx::destroy(uAreaLightDirectionWidth);
    bgfx::destroy(uAreaLightUpHeight); bgfx::destroy(uAreaLightColorIntensity);
    bgfx::destroy(uAreaLightShadow);
    bgfx::destroy(uEmissiveLightPositionRadius);
    bgfx::destroy(uEmissiveLightDirectionArea);
    bgfx::destroy(uEmissiveLightRadiance);
    bgfx::destroy(uEmissiveLightShadow);
    bgfx::destroy(uEmissiveLightShadowSecondary);
    bgfx::destroy(uEmissiveLightShadowOrigin);
    bgfx::destroy(uEmissiveLightTangentWidth);
    bgfx::destroy(uPointShadowAtlasParams);
    bgfx::destroy(uSunDirection); bgfx::destroy(uAtmosphereParams); bgfx::destroy(uEnvironmentParams);
    bgfx::destroy(uHdriParams);
    bgfx::destroy(uProbeSun); bgfx::destroy(uProbeParams); bgfx::destroy(uProbeGround); bgfx::destroy(uProbeFace);
    bgfx::destroy(uHdriIrradiance);
    bgfx::destroy(uLocalPrefilterParams);
    bgfx::destroy(uReflectionProbePosition); bgfx::destroy(uReflectionProbeMin); bgfx::destroy(uReflectionProbeMax);
    bgfx::destroy(uFogParams); bgfx::destroy(uDepthParams); bgfx::destroy(uAoParams); bgfx::destroy(uQualityParams);
    bgfx::destroy(uColorParams); bgfx::destroy(uUpsampleParams); bgfx::destroy(uDebugParams);
    bgfx::destroy(uHistogramParams); bgfx::destroy(uExposureParams0);
    bgfx::destroy(uExposureParams1); bgfx::destroy(uExposureParams2);
    for (auto uniform : materialUniforms) bgfx::destroy(uniform);
    for (auto sampler : samplers) bgfx::destroy(sampler);
    for (auto sampler : sShadows) bgfx::destroy(sampler);
    bgfx::destroy(sHdr);
    bgfx::destroy(sFog); bgfx::destroy(sSceneDepth); bgfx::destroy(sSceneDepthTonemap); bgfx::destroy(sFogDepth);
    bgfx::destroy(sAoDepth); bgfx::destroy(sAoNormal); bgfx::destroy(sAo); bgfx::destroy(sAoDepthUpsample);
    bgfx::destroy(sMaterialAo);
    bgfx::destroy(sBloom);
    bgfx::destroy(sHistogramHdr); bgfx::destroy(sHistogramFog); bgfx::destroy(sHistogramAo);
    bgfx::destroy(sPreviousExposure); bgfx::destroy(sExposure); bgfx::destroy(sHud);
    bgfx::destroy(sSelectionMask);bgfx::destroy(sSceneSelection);
    bgfx::destroy(uSelectionId);bgfx::destroy(uSelectionOutline);
    for (auto sampler : sVolumeShadows) bgfx::destroy(sampler);
    bgfx::destroy(sAtmosphereTransmittance); bgfx::destroy(sAtmosphereScattering); bgfx::destroy(sAtmosphereSingleMie);
    for (auto sampler : sEnvironment) bgfx::destroy(sampler);
    bgfx::destroy(sProbeScattering); bgfx::destroy(sProbeSingleMie);
    bgfx::destroy(sProbeSource);
    bgfx::destroy(sHdri);
    for (size_t index = 0; index < size_t(quality.environmentMipCount) * 6; ++index)
        bgfx::destroy(environmentBuffers[index]);
    for (auto buffer : localCaptureBuffers) bgfx::destroy(buffer);
    for (auto buffer : localDiffuseBuffers) bgfx::destroy(buffer);
    for (auto texture : localDiffuseProbe) bgfx::destroy(texture);
    bgfx::destroy(localDiffuseAtlas);
    for (size_t index = 0; index < size_t(kLocalProbeSlotCount) * quality.environmentMipCount * 6; ++index)
        bgfx::destroy(localPrefilterBuffers[index]);
    bgfx::destroy(brdfLutBuffer);
    for (auto texture : localProbeDepth) bgfx::destroy(texture);
    for (auto texture : localProbeRaw) bgfx::destroy(texture);
    for (auto texture : localProbeFiltered) bgfx::destroy(texture);
    bgfx::destroy(localReflectionAtlas);
    bgfx::destroy(brdfLut);
    bgfx::destroy(environmentSpecular);
    if (bgfx::isValid(atmosphereTransmittance)) bgfx::destroy(atmosphereTransmittance);
    if (bgfx::isValid(atmosphereScattering)) bgfx::destroy(atmosphereScattering);
    if (bgfx::isValid(atmosphereSingleMie)) bgfx::destroy(atmosphereSingleMie);
    if (bgfx::isValid(atmosphereIrradiance)) bgfx::destroy(atmosphereIrradiance);
    bgfx::destroy(hdriTexture);
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return renderFailed ? 1 : 0;
}
