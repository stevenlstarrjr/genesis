#pragma once
#include <bgfx/bgfx.h>

namespace genesis::raster {

class LightSourceRenderer {
public:
    struct Mesh {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    };

    bool initialize();
    void draw(uint16_t viewId) const;
    void shutdown();

private:
    Mesh sphere_;
    Mesh disc_;
    Mesh quad_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_ = BGFX_INVALID_HANDLE;
};

}
