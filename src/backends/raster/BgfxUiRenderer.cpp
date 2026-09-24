#include "backends/raster/BgfxUiRenderer.h"
#include <compiled/vs_ui.h>
#include <compiled/fs_ui.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace genesis::raster {
namespace {
struct Vertex {
    float x,y,z;
    uint32_t fill,border;
    float u,v,bounds[4],clip[4],style[4];
};
uint32_t pack(ui::Color color){
    return uint32_t(color.r)|(uint32_t(color.g)<<8)|(uint32_t(color.b)<<16)|(uint32_t(color.a)<<24);
}
}
bool BgfxUiRenderer::initialize(){
    auto vs=bgfx::createShader(bgfx::copy(vs_ui_shader,sizeof(vs_ui_shader)));
    auto fs=bgfx::createShader(bgfx::copy(fs_ui_shader,sizeof(fs_ui_shader)));
    m_program=bgfx::createProgram(vs,fs,true);
    m_atlasSampler=bgfx::createUniform("s_uiAtlas",bgfx::UniformType::Sampler);
    m_viewport=bgfx::createUniform("u_uiViewport",bgfx::UniformType::Vec4);
    return bgfx::isValid(m_program) && bgfx::isValid(m_atlasSampler) && bgfx::isValid(m_viewport);
}
void BgfxUiRenderer::shutdown(){
    if(bgfx::isValid(m_atlas))bgfx::destroy(m_atlas);
    if(bgfx::isValid(m_program))bgfx::destroy(m_program);
    if(bgfx::isValid(m_atlasSampler))bgfx::destroy(m_atlasSampler);
    if(bgfx::isValid(m_viewport))bgfx::destroy(m_viewport);
    m_atlas=BGFX_INVALID_HANDLE;m_program=BGFX_INVALID_HANDLE;
    m_atlasSampler=m_viewport=BGFX_INVALID_HANDLE;
    m_atlasWidth=m_atlasHeight=0;m_revision=0;
}
void BgfxUiRenderer::draw(uint16_t view,const ui::GpuDrawList& list){
    if(!bgfx::isValid(m_program) || !list.width || !list.height)return;
    const uint32_t width=std::max(1u,list.atlasWidth),height=std::max(1u,list.atlasHeight);
    if(!bgfx::isValid(m_atlas) || width!=m_atlasWidth || height!=m_atlasHeight){
        if(bgfx::isValid(m_atlas))bgfx::destroy(m_atlas);
        m_atlas=bgfx::createTexture2D(uint16_t(width),uint16_t(height),false,1,
            bgfx::TextureFormat::BGRA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
                BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
        m_atlasWidth=width;m_atlasHeight=height;m_revision=0;
    }
    if(!bgfx::isValid(m_atlas))return;
    if(m_revision!=list.atlasRevision){
        bgfx::updateTexture2D(m_atlas,0,0,0,0,uint16_t(width),uint16_t(height),
            bgfx::copy(list.atlasPixels.data(),uint32_t(list.atlasPixels.size()*sizeof(uint32_t))));
        m_revision=list.atlasRevision;
    }
    bgfx::setViewName(view,"GPU editor UI");
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);
    bgfx::setViewRect(view,0,0,uint16_t(list.width),uint16_t(list.height));
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::VertexLayout layout;layout.begin()
        .add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true)
        .add(bgfx::Attrib::Color1,4,bgfx::AttribType::Uint8,true)
        .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5,4,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord6,4,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord7,4,bgfx::AttribType::Float).end();
    const float viewport[4]{float(list.width),float(list.height),0,0};
    std::vector<Vertex> vertices;vertices.reserve(std::min<size_t>(list.commands.size()*6,6144));
    auto submit=[&]{
        if(vertices.empty())return;
        const auto count=uint32_t(vertices.size());
        if(bgfx::getAvailTransientVertexBuffer(count,layout)<count){vertices.clear();return;}
        bgfx::TransientVertexBuffer buffer;bgfx::allocTransientVertexBuffer(&buffer,count,layout);
        std::memcpy(buffer.data,vertices.data(),vertices.size()*sizeof(Vertex));
        bgfx::setVertexBuffer(0,&buffer);
        bgfx::setTexture(0,m_atlasSampler,m_atlas);
        bgfx::setUniform(m_viewport,viewport);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view,m_program);vertices.clear();
    };
    for(const auto& command:list.commands){
        const auto& b=command.bounds;
        if(b.width<=0 || b.height<=0)continue;
        const bool image=command.type==ui::GpuDrawCommand::Type::AtlasImage;
        const float u0=image?command.atlas.x/width:0,v0=image?command.atlas.y/height:0;
        const float u1=image?(command.atlas.x+command.atlas.width)/width:0;
        const float v1=image?(command.atlas.y+command.atlas.height)/height:0;
        const float xy[4][2]{{b.x,b.y},{b.x+b.width,b.y},{b.x,b.y+b.height},{b.x+b.width,b.y+b.height}};
        const float uv[4][2]{{u0,v0},{u1,v0},{u0,v1},{u1,v1}};
        constexpr int order[]{0,1,2,2,1,3};
        for(int corner:order){
            Vertex vertex{};
            vertex.x=xy[corner][0];vertex.y=xy[corner][1];vertex.z=0;
            vertex.fill=pack(command.fill);vertex.border=pack(command.border);
            vertex.u=uv[corner][0];vertex.v=uv[corner][1];
            vertex.bounds[0]=b.x;vertex.bounds[1]=b.y;
            vertex.bounds[2]=b.width;vertex.bounds[3]=b.height;
            vertex.clip[0]=command.clip.x;vertex.clip[1]=command.clip.y;
            vertex.clip[2]=command.clip.x+command.clip.width;
            vertex.clip[3]=command.clip.y+command.clip.height;
            vertex.style[0]=command.radius;vertex.style[1]=command.borderWidth;
            vertex.style[2]=image?1.0f:0.0f;
            vertex.style[3]=command.type==ui::GpuDrawCommand::Type::RoundedFrame?1.0f:0.0f;
            vertices.push_back(vertex);
        }
        if(vertices.size()>=6144)submit();
    }
    submit();
}
}
