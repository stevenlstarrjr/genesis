#pragma once
#include <bgfx/bgfx.h>

namespace genesis::raster {
inline bool fullscreenTriangle() {
    struct Vertex { float x,y,z,u,v; };
    static const auto layout = [] {
        bgfx::VertexLayout value;
        value.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        return value;
    }();
    if (bgfx::getAvailTransientVertexBuffer(3,layout) != 3) return false;
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer,3,layout);
    auto* v = reinterpret_cast<Vertex*>(buffer.data);
    v[0] = {-1,-1,0,0,1}; v[1] = {3,-1,0,2,1}; v[2] = {-1,3,0,0,-1};
    bgfx::setVertexBuffer(0,&buffer);
    return true;
}
} // namespace genesis::raster
