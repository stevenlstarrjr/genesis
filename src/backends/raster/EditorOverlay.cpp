#include "backends/raster/EditorOverlay.h"
#include <compiled/vs_editor_lines.h>
#include <compiled/fs_editor_lines.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_set>

namespace genesis::raster {
bool EditorOverlay::initialize() {
    auto vs=bgfx::createShader(bgfx::copy(vs_editor_lines_shader,sizeof(vs_editor_lines_shader)));
    auto fs=bgfx::createShader(bgfx::copy(fs_editor_lines_shader,sizeof(fs_editor_lines_shader)));
    m_program=bgfx::createProgram(vs,fs,true);
    m_depth=bgfx::createUniform("s_editorDepth",bgfx::UniformType::Sampler);
    m_params=bgfx::createUniform("u_editorParams",bgfx::UniformType::Vec4);
    m_gridParams=bgfx::createUniform("u_editorGrid",bgfx::UniformType::Vec4);
    return bgfx::isValid(m_program) && bgfx::isValid(m_depth) && bgfx::isValid(m_params)
        && bgfx::isValid(m_gridParams);
}
void EditorOverlay::shutdown() {
    if(bgfx::isValid(m_program)) bgfx::destroy(m_program);
    if(bgfx::isValid(m_depth)) bgfx::destroy(m_depth);
    if(bgfx::isValid(m_params)) bgfx::destroy(m_params);
    if(bgfx::isValid(m_gridParams)) bgfx::destroy(m_gridParams);
    m_program=BGFX_INVALID_HANDLE; m_depth=BGFX_INVALID_HANDLE;
    m_params=BGFX_INVALID_HANDLE; m_gridParams=BGFX_INVALID_HANDLE;
}
void EditorOverlay::draw(uint16_t view,const ui::Rect& viewport,const float* cameraView,const float* projection,
    bgfx::TextureHandle sceneDepth,bool grid,const editor::Vec3& cameraPosition,
    float cameraDistance,const editor::Bounds& selection,const editor::Vec3& cursor,
    const editor::GizmoFrame& gizmo,int highlightedHandle) {
    if(!bgfx::isValid(m_program) || viewport.width<=0 || viewport.height<=0) return;
    struct Vertex { editor::Vec3 position; uint32_t color; };
    bgfx::VertexLayout layout; layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    bgfx::setViewName(view,"Editor grid and selection");
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view,uint16_t(viewport.x),uint16_t(viewport.y),uint16_t(viewport.width),uint16_t(viewport.height));
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE); bgfx::setViewTransform(view,cameraView,projection);
    std::vector<Vertex> vertices;
    auto line=[&](editor::Vec3 a,editor::Vec3 b,uint32_t color) { vertices.push_back({a,color}); vertices.push_back({b,color}); };
    float gridParams[4]{cameraPosition[0],cameraPosition[2],0,0};
    auto submit=[&](bool occluded,bool lines=true,bool fadeGrid=false) {
        if(vertices.empty() || bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()),layout)<vertices.size()) return;
        bgfx::TransientVertexBuffer buffer; bgfx::allocTransientVertexBuffer(&buffer,uint32_t(vertices.size()),layout);
        std::memcpy(buffer.data,vertices.data(),vertices.size()*sizeof(Vertex));
        bgfx::setVertexBuffer(0,&buffer);
        bgfx::setTexture(0,m_depth,sceneDepth,BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        const float params[4]{occluded?1.0f:0.0f,bgfx::getCaps()->homogeneousDepth?1.0f:0.0f,bgfx::getCaps()->originBottomLeft?1.0f:0.0f,fadeGrid?1.0f:0.0f};
        bgfx::setUniform(m_params,params);
        bgfx::setUniform(m_gridParams,gridParams);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | (lines?BGFX_STATE_PT_LINES:0) | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view,m_program); vertices.clear();
    };
    if(grid) {
        // Regenerate a patch around the camera. The spacing follows a 1/2/5
        // scale, keeping cells readable as the camera zooms in and out.
        const float scale=std::clamp(cameraDistance,0.1f,10000.0f);
        const float target=scale/7.0f;
        const float decade=std::pow(10.0f,std::floor(std::log10(target)));
        const float unit=target/decade;
        const float spacing=decade*(unit<1.5f?1.0f:unit<3.5f?2.0f:5.0f);
        gridParams[2]=scale*1.7f;
        gridParams[3]=scale*6.5f;
        const double x0=std::floor(double(cameraPosition[0])/spacing);
        const double z0=std::floor(double(cameraPosition[2])/spacing);
        const int extent=int(std::ceil(gridParams[3]/spacing))+4;
        const float xMin=float((x0-extent)*spacing),xMax=float((x0+extent)*spacing);
        const float zMin=float((z0-extent)*spacing),zMax=float((z0+extent)*spacing);
        for(int i=-extent;i<=extent;++i) {
            const double xi=x0+i,zi=z0+i;
            const float x=float(xi*spacing),z=float(zi*spacing);
            // Genesis is Y-up, so the ground-plane axes are X (red) and Z (green).
            const uint32_t xColor=std::fmod(std::abs(xi),10.0)==0 ? 0xdda0a0a0 : 0xbb858585;
            const uint32_t zColor=std::fmod(std::abs(zi),10.0)==0 ? 0xdda0a0a0 : 0xbb858585;
            if(xi!=0)line({x,0,zMin},{x,0,zMax},xColor);
            if(zi!=0)line({xMin,0,z},{xMax,0,z},zColor);
        }
        submit(true,true,true);
        // The axes span the camera's working area even when the origin lies
        // outside the fine grid patch or a ground mesh covers the grid plane.
        const float axisExtent=scale*20.0f;
        gridParams[2]=scale*3.0f;gridParams[3]=scale*8.0f;
        line({cameraPosition[0]-axisExtent,0,0},{cameraPosition[0]+axisExtent,0,0},0xff5959e6);
        line({0,0,cameraPosition[2]-axisExtent},{0,0,cameraPosition[2]+axisExtent},0xff67bf63);
        submit(true,true,true);
    }
    if(selection.valid()) {
        std::array<editor::Vec3,8> corners;
        for(int i=0;i<8;++i) for(int axis=0;axis<3;++axis)
            corners[i][axis]=(i&(1<<axis))?selection.maximum[axis]:selection.minimum[axis];
        for(int i=0;i<8;++i) for(int axis=0;axis<3;++axis) if(!(i&(1<<axis))) line(corners[i],corners[i|(1<<axis)],0xff6fc7ff);
        submit(false); // Selection bounds intentionally remain visible through the object.
    }
    // Genesis is Y-up: show its ground Z handle in green and its up Y handle
    // in blue so the viewport reads like the reference X/red, ground/green, up/blue gizmo.
    const uint32_t colors[]{0xff5f4fe0,0xffe88b4b,0xff47c57b,0xffdddddd};
    // Portable screen-width ribbons: hardware line width is fixed at one pixel
    // on several backends and gets lost against textured scene objects.
    using namespace editor::gizmoMath;
    const editor::Vec3 right{cameraView[0],cameraView[4],cameraView[8]},up{cameraView[1],cameraView[5],cameraView[9]},forward{cameraView[2],cameraView[6],cameraView[10]};
    auto ribbon=[&](editor::Vec3 a,editor::Vec3 b,uint32_t color,float halfWidth){
        const float za=std::abs(dot(a,forward)+cameraView[14]),zb=std::abs(dot(b,forward)+cameraView[14]);
        if(za<.01f || zb<.01f)return;
        const float dx=((dot(b,right)+cameraView[12])/zb-(dot(a,right)+cameraView[12])/za)*projection[0]*viewport.width;
        const float dy=((dot(b,up)+cameraView[13])/zb-(dot(a,up)+cameraView[13])/za)*projection[5]*viewport.height;
        const float length=std::hypot(dx,dy);if(length<.001f)return;
        const auto offset=add(mul(right,-dy/length*halfWidth*2/(projection[0]*viewport.width)),mul(up,dx/length*halfWidth*2/(projection[5]*viewport.height)));
        const auto a0=sub(a,mul(offset,za)),a1=add(a,mul(offset,za));
        const auto b0=sub(b,mul(offset,zb)),b1=add(b,mul(offset,zb));
        for(auto p:{a0,b0,a1,a1,b0,b1})vertices.push_back({p,color});
    };
    const float cursorDepth=std::abs(dot(cursor,forward)+cameraView[14]);
    if(cursorDepth>.01f){
        const float pixelWorld=2.0f*cursorDepth/(projection[5]*viewport.height);
        const auto point=[&](float x,float y){return add(cursor,add(mul(right,x*pixelWorld),mul(up,y*pixelWorld)));};
        for(int i=0;i<32;++i){
            const float a=float(i)*2*pi/32,b=float(i+1)*2*pi/32;
            ribbon(point(std::cos(a)*8,std::sin(a)*8),point(std::cos(b)*8,std::sin(b)*8),
                i%8<4?0xffeeeeee:0xff5050ea,1.05f);
        }
        ribbon(point(-15,0),point(-4,0),0xffeeeeee,1.0f);
        ribbon(point(4,0),point(15,0),0xffeeeeee,1.0f);
        ribbon(point(0,-15),point(0,-4),0xff5050ea,1.0f);
        ribbon(point(0,4),point(0,15),0xff5050ea,1.0f);
    }
    if(gizmo.size>0 && gizmo.tool!=editor::TransformTool::Move && gizmo.tool!=editor::TransformTool::Select){
        const auto ring=[&](float radius,uint32_t color,float width){
            for(int i=0;i<96;++i){
                const float a=float(i)*2*pi/96,b=float(i+1)*2*pi/96;
                const auto point=[&](float angle){return add(gizmo.origin,mul(add(mul(right,std::cos(angle)),mul(up,std::sin(angle))),radius));};
                ribbon(point(a),point(b),color,width);
            }
        };
        ring(gizmo.size*1.08f,0xffc6c8cb,1.2f);
    }
    if(gizmo.size>0 && gizmo.tool!=editor::TransformTool::Select){
        for(int i=0;i<32;++i){
            const float a=float(i)*2*pi/32,b=float(i+1)*2*pi/32;
            const auto point=[&](float angle){return add(gizmo.origin,mul(add(mul(right,std::cos(angle)),mul(up,std::sin(angle))),gizmo.size*.105f));};
            ribbon(point(a),point(b),0xffe4e5e6,1.15f);
        }
    }
    for(const auto& segment:gizmo.lines) {
        if(segment.pickOnly)continue;
        uint32_t color=segment.handle==highlightedHandle?0xff55e8ff:
            colors[segment.handle==9?3:segment.handle>=10?segment.handle-10:segment.handle%3];
        const bool rotation=gizmo.tool==editor::TransformTool::Rotate ||
            (gizmo.tool==editor::TransformTool::Transform && segment.handle>=3 && segment.handle<=5);
        if(rotation && segment.handle!=highlightedHandle){
            const auto midpoint=mul(add(segment.a,segment.b),.5f);
            if(dot(sub(midpoint,gizmo.origin),sub(cameraPosition,gizmo.origin))<0)
                continue;
        }
        ribbon(segment.a,segment.b,color,segment.handle==highlightedHandle?1.8f:rotation?1.55f:1.35f);
    }
    for(const auto& marker:gizmo.markers){
        const uint32_t color=marker.handle==highlightedHandle?0xff55e8ff:
            colors[marker.handle==9?3:marker.handle>=10?marker.handle-10:marker.handle%3];
        if(marker.shape==editor::GizmoMarker::Shape::Cone){
            const auto direction=unit(marker.direction);
            const auto tangent=unit(cross(direction,std::abs(direction[1])<.85f?editor::Vec3{0,1,0}:editor::Vec3{1,0,0}));
            const auto bitangent=cross(direction,tangent);
            const auto base=sub(marker.center,mul(direction,gizmo.size*.18f));
            for(int i=0;i<12;++i){
                const float a=float(i)*2*pi/12,b=float(i+1)*2*pi/12;
                const auto rim=[&](float angle){return add(base,mul(add(mul(tangent,std::cos(angle)),mul(bitangent,std::sin(angle))),marker.size));};
                vertices.push_back({marker.center,color});vertices.push_back({rim(a),color});vertices.push_back({rim(b),color});
            }
        }else if(marker.shape==editor::GizmoMarker::Shape::Plane){
            const int axis=marker.handle>=10?marker.handle-10:marker.handle-3;
            const auto a=mul(gizmo.axes[(axis+1)%3],marker.size);
            const auto b=mul(gizmo.axes[(axis+2)%3],marker.size);
            const std::array<editor::Vec3,4> corners{add(add(marker.center,a),b),
                add(sub(marker.center,a),b),sub(sub(marker.center,a),b),add(sub(marker.center,b),a)};
            const uint32_t fill=(color&0x00ffffff)|0xdd000000;
            for(int index:{0,1,2,0,2,3})vertices.push_back({corners[index],fill});
            for(int i=0;i<4;++i)ribbon(corners[i],corners[(i+1)%4],color,.75f);
        }else{
            std::array<editor::Vec3,8> corners;
            for(int i=0;i<8;++i){
                corners[i]=marker.center;
                for(int axis=0;axis<3;++axis)corners[i]=add(corners[i],mul(gizmo.axes[axis],(i&(1<<axis)?1.f:-1.f)*marker.size));
            }
            for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){
                int indices[4]{},count=0;
                for(int i=0;i<8;++i)if(((i>>axis)&1)==side)indices[count++]=i;
                const int a=indices[0],b=indices[1],c=indices[2],d=indices[3];
                for(int index:{a,b,c,b,d,c})vertices.push_back({corners[index],color});
            }
        }
    }
    submit(false,false);
}

void EditorOverlay::drawMeshEdit(uint16_t view,const ui::Rect& viewport,const float* cameraView,
    const float* projection,int selectionMode,
    bgfx::TextureHandle sceneDepth,const std::vector<editor::Vec3>& positions,
    const std::vector<std::array<uint32_t,2>>& edges,
    const std::vector<std::array<uint32_t,3>>& faces,const std::vector<uint32_t>& selected) {
    if(!bgfx::isValid(m_program) || positions.empty() || viewport.width<=0 || viewport.height<=0)return;
    struct Vertex {editor::Vec3 position;uint32_t color;};
    bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    bgfx::setViewName(view,"Mesh edit topology");bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view,uint16_t(viewport.x),uint16_t(viewport.y),uint16_t(viewport.width),uint16_t(viewport.height));
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);bgfx::setViewTransform(view,cameraView,projection);
    const std::unordered_set<uint32_t> active(selected.begin(),selected.end());
    std::vector<Vertex> ordinary,highlighted,filled;
    if(selectionMode==2)for(const auto& face:faces) {
        if(face[0]>=positions.size() || face[1]>=positions.size() || face[2]>=positions.size() ||
            !active.contains(face[0]) || !active.contains(face[1]) || !active.contains(face[2]))continue;
        constexpr uint32_t orangeFill=0x77309dff;
        for(uint32_t index:face)filled.push_back({positions[index],orangeFill});
    }
    ordinary.reserve(std::min(edges.size(),size_t(40000))*2);
    const auto segment=[](std::vector<Vertex>& out,editor::Vec3 a,editor::Vec3 b,uint32_t color){
        out.push_back({a,color});out.push_back({b,color});
    };
    for(size_t i=0;i<std::min(edges.size(),size_t(40000));++i) {
        const auto [a,b]=edges[i];if(a>=positions.size() || b>=positions.size())continue;
        const bool picked=active.contains(a) && active.contains(b);
        segment(picked?highlighted:ordinary,positions[a],positions[b],picked?0xff309dff:0x99606060);
    }
    const editor::Vec3 right{cameraView[0],cameraView[4],cameraView[8]};
    const editor::Vec3 up{cameraView[1],cameraView[5],cameraView[9]};
    const editor::Vec3 forward{cameraView[2],cameraView[6],cameraView[10]};
    using namespace editor::gizmoMath;
    std::unordered_set<uint64_t> seen;
    for(uint32_t i=0;i<std::min(positions.size(),size_t(8000));++i) {
        if(selectionMode!=0 && !active.contains(i))continue;
        const auto point=positions[i];
        const auto quantized=[](float value){return int64_t(std::llround(value*100000.0f));};
        const auto key=(uint64_t(quantized(point[0]))*73856093ull)^
            (uint64_t(quantized(point[1]))*19349663ull)^
            (uint64_t(quantized(point[2]))*83492791ull);
        if(!seen.insert(key).second)continue;
        const float depth=std::abs(dot(point,forward)+cameraView[14]);
        if(depth<.01f)continue;
        const bool picked=active.contains(i);
        const float radius=(picked?2.5f:1.0f)*2*depth/(projection[5]*viewport.height);
        auto& out=picked?highlighted:ordinary;
        const auto color=picked?0xff309dff:0xdd292929;
        segment(out,sub(point,mul(right,radius)),add(point,mul(right,radius)),color);
        segment(out,sub(point,mul(up,radius)),add(point,mul(up,radius)),color);
    }
    const auto submit=[&](const std::vector<Vertex>& vertices,bool occluded,bool lines){
        if(vertices.empty() || bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()),layout)<vertices.size())return;
        bgfx::TransientVertexBuffer buffer;bgfx::allocTransientVertexBuffer(&buffer,uint32_t(vertices.size()),layout);
        std::memcpy(buffer.data,vertices.data(),vertices.size()*sizeof(Vertex));bgfx::setVertexBuffer(0,&buffer);
        bgfx::setTexture(0,m_depth,sceneDepth,BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|
            BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
        const float params[4]{occluded?1.0f:0.0f,bgfx::getCaps()->homogeneousDepth?1.0f:0.0f,
            bgfx::getCaps()->originBottomLeft?1.0f:0.0f,-1.0f};
        // The scene depth covers the full offscreen image, then tonemap fits that
        // image into the editor viewport. Clip-space UVs already match its depth.
        const float grid[4]{};
        bgfx::setUniform(m_params,params);bgfx::setUniform(m_gridParams,grid);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|
            (lines?BGFX_STATE_PT_LINES:0)|BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view,m_program);
    };
    submit(filled,true,false);submit(ordinary,true,true);submit(highlighted,true,true);
}

void EditorOverlay::drawParticles(uint16_t view,const ui::Rect& viewport,const float* cameraView,const float* projection,
    bgfx::TextureHandle sceneDepth,const std::vector<physics::ParticleFrame>& frames) {
    if(!bgfx::isValid(m_program) || viewport.width<=0 || viewport.height<=0 || frames.empty())return;
    struct Vertex { editor::Vec3 position;uint32_t color; };
    bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    bgfx::setViewName(view,"PhysX particles");bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view,uint16_t(viewport.x),uint16_t(viewport.y),uint16_t(viewport.width),uint16_t(viewport.height));
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);bgfx::setViewTransform(view,cameraView,projection);
    std::vector<Vertex> vertices;vertices.reserve(18000);
    const auto pack=[](float r,float g,float b,float a){
        const auto byte=[](float v){return uint32_t(std::clamp(v,0.f,1.f)*255.f+.5f);};
        return byte(r)|(byte(g)<<8)|(byte(b)<<16)|(byte(a)<<24);
    };
    const editor::Vec3 right{cameraView[0],cameraView[4],cameraView[8]};
    const editor::Vec3 up{cameraView[1],cameraView[5],cameraView[9]};
    auto submit=[&]{
        if(vertices.empty())return;
        if(bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()),layout)<vertices.size()){vertices.clear();return;}
        bgfx::TransientVertexBuffer buffer;bgfx::allocTransientVertexBuffer(&buffer,uint32_t(vertices.size()),layout);
        std::memcpy(buffer.data,vertices.data(),vertices.size()*sizeof(Vertex));
        bgfx::setVertexBuffer(0,&buffer);bgfx::setTexture(0,m_depth,sceneDepth,BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
        const float params[4]{1,bgfx::getCaps()->homogeneousDepth?1.f:0.f,bgfx::getCaps()->originBottomLeft?1.f:0.f,0};
        bgfx::setUniform(m_params,params);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view,m_program);vertices.clear();
    };
    auto triangle=[&](const auto& a,const auto& b,const auto& c,uint32_t ca,uint32_t cb,uint32_t cc){
        vertices.push_back({a,ca});vertices.push_back({b,cb});vertices.push_back({c,cc});
        if(vertices.size()>=15000)submit();
    };
    auto disc=[&](const editor::Vec3& center,float radius,uint32_t centerColor,uint32_t edgeColor,int segments=8){
        if(radius<=0)return;
        for(int segment=0;segment<segments;++segment){
            const float first=float(segment)*6.2831853f/float(segments);
            const float second=float(segment+1)*6.2831853f/float(segments);
            editor::Vec3 a{},b{};
            for(int axis=0;axis<3;++axis){
                a[axis]=center[axis]+radius*(right[axis]*std::cos(first)+up[axis]*std::sin(first));
                b[axis]=center[axis]+radius*(right[axis]*std::cos(second)+up[axis]*std::sin(second));
            }
            triangle(center,a,b,centerColor,edgeColor,edgeColor);
        }
    };
    for(const auto& frame:frames){
        if(frame.mode==gameplay::ParticleSystem::Mode::Emitter){
            size_t index=0;
            for(const auto& layer:frame.layers){
                for(int local=0;local<layer.count && index<frame.positions.size();++local,++index){
                    const auto& p=frame.positions[index];const float age=frame.progress[index];
                    std::array<float,4> color{};
                    for(int c=0;c<4;++c)color[c]=layer.startColor[c]+(layer.endColor[c]-layer.startColor[c])*age;
                    if(layer.fadeIn>0)color[3]*=std::min(1.f,age/layer.fadeIn);
                    const float size=std::max(.005f,layer.size+layer.sizeGrowth*age);
                    const auto center=pack(color[0],color[1],color[2],color[3]);
                    const auto edge=pack(color[0]*.65f,color[1]*.65f,color[2]*.65f,
                        layer.shape==gameplay::ParticleSystem::Layer::Shape::Spark?color[3]*.14f:0.f);
                    disc(p,size,center,edge,layer.shape==gameplay::ParticleSystem::Layer::Shape::Spark?5:8);
                }
            }
        }else if(frame.mode==gameplay::ParticleSystem::Mode::Explosion){
            const editor::Vec3 origin{frame.origin[0],frame.origin[1],frame.origin[2]};
            const float age=frame.age;
            if(frame.positions.size()==1){disc(origin,.13f,pack(1.f,.58f,.12f,.9f),pack(1.f,.24f,.02f,.2f));continue;}
            if(age>=frame.effectDuration)continue;
            const float fade=std::clamp(1.f-age/std::max(frame.effectDuration,.1f),0.f,1.f);
            if(age>.18f){
                for(size_t i=0;i<frame.positions.size();i+=5){const auto& p=frame.positions[i];
                    editor::Vec3 smoke{origin[0]+(p[0]-origin[0])*.32f,
                        origin[1]+age*.82f+(p[1]-origin[1])*.18f,
                        origin[2]+(p[2]-origin[2])*.32f};
                    const float size=.14f+age*.24f+float(i%7)*.025f;
                    const float alpha=std::min(1.f,(age-.18f)*3.f)*fade*.38f;
                    disc(smoke,size,pack(.19f,.19f,.20f,alpha),pack(.08f,.08f,.09f,0));
                }
            }
            if(age<.58f){
                const float waveRadius=.18f+age*5.f;
                const float alpha=(1.f-age/.58f)*.65f;
                constexpr int waveSegments=36;
                for(int i=0;i<waveSegments;++i){
                    const float a=float(i)*6.2831853f/waveSegments,b=float(i+1)*6.2831853f/waveSegments;
                    auto point=[&](float angle,float radius){return editor::Vec3{origin[0]+std::cos(angle)*radius,origin[1],origin[2]+std::sin(angle)*radius};};
                    const auto a0=point(a,waveRadius),a1=point(a,waveRadius+.12f);
                    const auto b0=point(b,waveRadius),b1=point(b,waveRadius+.12f);
                    const auto color=pack(1.f,.43f,.05f,alpha);
                    triangle(a0,a1,b0,color,color,color);triangle(b0,a1,b1,color,color,color);
                }
            }
            if(age<.85f){
                const float flameFade=1.f-age/.85f;
                for(size_t i=0;i<frame.positions.size();i+=2){const auto& p=frame.positions[i];
                    const editor::Vec3 position{p[0],p[1],p[2]};
                    const float size=frame.radius*(2.f+float(i%5)*.28f)+age*.12f;
                    disc(position,size,pack(1.f,.77f,.14f,.8f*flameFade),pack(1.f,.14f,.01f,.16f*flameFade),6);
                }
                disc(origin,.24f+age*1.15f,pack(1.f,.65f,.08f,.5f*flameFade),pack(1.f,.12f,0.f,0),12);
            }
            for(size_t i=0;i<frame.positions.size();i+=3){const auto& p=frame.positions[i];
                const editor::Vec3 position{p[0],p[1],p[2]};
                disc(position,frame.radius*(.55f+float(i%4)*.12f),
                    pack(1.f,.88f,.34f,.95f*fade),pack(1.f,.37f,.03f,.2f*fade),5);
            }
            if(age<.22f){
                const float flash=1.f-age/.22f;
                disc(origin,.28f+age*3.2f,pack(1.f,1.f,.87f,flash),pack(1.f,.63f,.12f,0),14);
            }
        }else if(frame.mode==gameplay::ParticleSystem::Mode::Cloth){
            for(const auto& t:frame.triangles){
                if(t[0]>=frame.positions.size() || t[1]>=frame.positions.size() || t[2]>=frame.positions.size())continue;
                const auto& a=frame.positions[t[0]];const auto& b=frame.positions[t[1]];const auto& c=frame.positions[t[2]];
                const float abx=b[0]-a[0],aby=b[1]-a[1],abz=b[2]-a[2];
                const float acx=c[0]-a[0],acy=c[1]-a[1],acz=c[2]-a[2];
                const float nx=aby*acz-abz*acy,ny=abz*acx-abx*acz,nz=abx*acy-aby*acx;
                const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
                if(length<1e-8f)continue;
                const float diffuse=std::abs((nx*.35f+ny*.82f+nz*.45f)/length);
                const float light=.28f+.72f*diffuse;
                const auto color=pack(.79f*light,.56f*light,.89f*light,.94f);
                triangle(a,b,c,color,color,color);
            }
        }else{
            const bool fluid=frame.mode==gameplay::ParticleSystem::Mode::Fluid;
            const float red=fluid?.13f:.77f,green=fluid?.54f:.58f,blue=fluid?.95f:.34f;
            const size_t stride=std::max<size_t>(1,(frame.positions.size()+3999)/4000);
            constexpr int segments=6;
            for(size_t i=0;i<frame.positions.size();i+=stride){const auto& p=frame.positions[i];
                const auto center=pack(red*1.28f,green*1.28f,blue*1.1f,fluid?.9f:.97f);
                auto rim=[&](int segment){
                    const float angle=float(segment)*6.2831853f/segments;
                    const float x=std::cos(angle),y=std::sin(angle);
                    const float shade=.45f+.28f*std::max(0.f,-.5f*x+.85f*y);
                    editor::Vec3 point{};
                    for(int axis=0;axis<3;++axis)point[axis]=p[axis]+frame.radius*(right[axis]*x+up[axis]*y);
                    return std::pair{point,pack(red*shade,green*shade,blue*shade,fluid?.78f:.95f)};
                };
                for(int segment=0;segment<segments;++segment){
                    const auto [a,ca]=rim(segment);
                    const auto [b,cb]=rim(segment+1);
                    triangle(p,a,b,center,ca,cb);
                }
            }
        }
    }
    submit();
}

void EditorOverlay::drawSelectionGesture(uint16_t view,const ui::Rect& viewport,bgfx::TextureHandle sceneDepth,
    const std::vector<editor::gizmoMath::Vec2>& points,int mode,float radius,editor::gizmoMath::Vec2 cursor) {
    if(!bgfx::isValid(m_program) || points.empty() || viewport.width<=0 || viewport.height<=0)return;
    struct Vertex { editor::Vec3 position;uint32_t color; };
    bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    std::vector<Vertex> vertices;
    const auto clip=[&](editor::gizmoMath::Vec2 point) {
        return editor::Vec3{2.0f*(point[0]-viewport.x)/viewport.width-1.0f,
            1.0f-2.0f*(point[1]-viewport.y)/viewport.height,0.0f};
    };
    const auto line=[&](editor::gizmoMath::Vec2 a,editor::gizmoMath::Vec2 b) {
        vertices.push_back({clip(a),0xff20aaff});vertices.push_back({clip(b),0xff20aaff});
    };
    if(mode==2 && points.size()>1) {
        const auto a=points.front(),b=points.back();
        const editor::gizmoMath::Vec2 corners[]{
            {a[0],a[1]},{b[0],a[1]},{b[0],b[1]},{a[0],b[1]}};
        for(int i=0;i<4;++i)line(corners[i],corners[(i+1)%4]);
    } else if(mode==3) {
        const auto center=cursor;
        for(int i=0;i<40;++i) {
            const float a=float(i)*6.283185307f/40,b=float(i+1)*6.283185307f/40;
            line({center[0]+radius*std::cos(a),center[1]+radius*std::sin(a)},
                {center[0]+radius*std::cos(b),center[1]+radius*std::sin(b)});
        }
    } else if(mode==4 && points.size()>1) {
        for(size_t i=1;i<points.size();++i)line(points[i-1],points[i]);
        line(points.back(),points.front());
    }
    if(vertices.empty() || bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()),layout)<vertices.size())return;
    bgfx::TransientVertexBuffer buffer;bgfx::allocTransientVertexBuffer(&buffer,uint32_t(vertices.size()),layout);
    std::memcpy(buffer.data,vertices.data(),vertices.size()*sizeof(Vertex));
    const float identity[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    bgfx::setViewName(view,"Selection gesture");bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view,uint16_t(viewport.x),uint16_t(viewport.y),uint16_t(viewport.width),uint16_t(viewport.height));
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);bgfx::setViewTransform(view,identity,identity);
    bgfx::setVertexBuffer(0,&buffer);
    bgfx::setTexture(0,m_depth,sceneDepth,BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    const float params[]{0,0,bgfx::getCaps()->originBottomLeft?1.0f:0.0f,0};
    const float grid[]{0,0,0,0};bgfx::setUniform(m_params,params);bgfx::setUniform(m_gridParams,grid);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_PT_LINES|BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(view,m_program);
}
}
