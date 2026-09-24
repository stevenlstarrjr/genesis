#pragma once
#include "ui/Toolkit.h"
#include <bgfx/bgfx.h>

namespace genesis::raster {
class BgfxUiRenderer {
public:
    bool initialize();
    void shutdown();
    void draw(uint16_t view,const ui::GpuDrawList& list);
private:
    bgfx::ProgramHandle m_program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_atlasSampler=BGFX_INVALID_HANDLE,m_viewport=BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_atlas=BGFX_INVALID_HANDLE;
    uint32_t m_atlasWidth=0,m_atlasHeight=0;
    uint64_t m_revision=0;
};
}
