#include "BloomRenderer.h"
#include "FullscreenTriangle.h"
#include <compiled/vs_fullscreen.h>
#include <compiled/fs_bloom_prefilter.h>
#include <compiled/fs_bloom_upsample.h>
#include <algorithm>
#include <cstdio>

namespace genesis::raster {
namespace {
constexpr uint32_t samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
bgfx::ShaderHandle shader(const uint8_t* data, uint32_t size) {
    return bgfx::createShader(bgfx::copy(data, size));
}
}

bool BloomRenderer::initialize() {
    downProgram_ = bgfx::createProgram(shader(vs_fullscreen_shader,sizeof(vs_fullscreen_shader)),
        shader(fs_bloom_prefilter_shader,sizeof(fs_bloom_prefilter_shader)),true);
    upProgram_ = bgfx::createProgram(shader(vs_fullscreen_shader,sizeof(vs_fullscreen_shader)),
        shader(fs_bloom_upsample_shader,sizeof(fs_bloom_upsample_shader)),true);
    params_ = bgfx::createUniform("u_bloomParams",bgfx::UniformType::Vec4);
    source_ = bgfx::createUniform("s_bloomSource",bgfx::UniformType::Sampler);
    detail_ = bgfx::createUniform("s_bloomDetail",bgfx::UniformType::Sampler);
    return bgfx::isValid(downProgram_) && bgfx::isValid(upProgram_) && bgfx::isValid(params_)
        && bgfx::isValid(source_) && bgfx::isValid(detail_);
}

void BloomRenderer::releaseTargets() {
    auto release = [](Level& level) {
        if (bgfx::isValid(level.buffer)) bgfx::destroy(level.buffer);
        if (bgfx::isValid(level.texture)) bgfx::destroy(level.texture);
        level = {};
    };
    for (auto& level : down_) release(level);
    for (auto& level : up_) release(level);
    width_ = height_ = 0;
}

bool BloomRenderer::resize(uint16_t width, uint16_t height) {
    if (width == width_ && height == height_) return true;
    releaseTargets();
    uint16_t w = width, h = height;
    auto create = [](Level& level, uint16_t x, uint16_t y) {
        level.width = x; level.height = y;
        level.texture = bgfx::createTexture2D(x,y,false,1,bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT | samplerFlags);
        if (!bgfx::isValid(level.texture)) {
            std::fprintf(stderr, "Bloom texture allocation failed at %ux%u\n", x, y);
            return false;
        }
        level.buffer = bgfx::createFrameBuffer(1,&level.texture,false);
        if (!bgfx::isValid(level.buffer))
            std::fprintf(stderr, "Bloom framebuffer allocation failed at %ux%u\n", x, y);
        return bgfx::isValid(level.buffer);
    };
    for (uint16_t i = 0; i < levelCount; ++i) {
        w = std::max<uint16_t>(1,w/2); h = std::max<uint16_t>(1,h/2);
        if (!create(down_[i],w,h) || (i < up_.size() && !create(up_[i],w,h))) {
            releaseTargets(); return false;
        }
    }
    width_ = width; height_ = height;
    return true;
}

bool BloomRenderer::render(uint16_t firstView, uint16_t width, uint16_t height,
                           bgfx::TextureHandle hdr, bool enabled) {
    if (!resize(width,height)) return false;
    auto target = [](uint16_t view, const Level& level, const char* name) {
        bgfx::setViewName(view,name);
        bgfx::setViewFrameBuffer(view,level.buffer);
        bgfx::setViewRect(view,0,0,level.width,level.height);
        bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0x000000ff,1.0f,0);
        bgfx::touch(view);
    };
    if (!enabled) {
        target(firstView,up_[0],"Bloom disabled (clear)");
        return true;
    }
    uint16_t sourceWidth = width, sourceHeight = height;
    auto input = hdr;
    for (uint16_t i = 0; i < levelCount; ++i) {
        const uint16_t view = firstView+i;
        target(view,down_[i],i == 0 ? "Bloom anti-firefly prefilter" : "Bloom downsample");
        const float params[4]{1.0f/sourceWidth,1.0f/sourceHeight,i == 0 ? 1.0f : 0.0f,16.0f};
        bgfx::setUniform(params_,params);
        bgfx::setTexture(0,source_,input,samplerFlags);
        if (!fullscreenTriangle()) return false;
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view,downProgram_);
        input = down_[i].texture; sourceWidth = down_[i].width; sourceHeight = down_[i].height;
    }
    for (int i = levelCount-2; i >= 0; --i) {
        const uint16_t view = uint16_t(firstView+levelCount+(levelCount-2-i));
        target(view,up_[i],"Bloom tent reconstruction");
        // Convex blend: adding pyramid levels must not multiply constant radiance.
        const float params[4]{1.0f/sourceWidth,1.0f/sourceHeight,1.0f/down_[i].width,1.0f/down_[i].height};
        bgfx::setUniform(params_,params);
        bgfx::setTexture(0,source_,input,samplerFlags);
        bgfx::setTexture(1,detail_,down_[i].texture,samplerFlags);
        if (!fullscreenTriangle()) return false;
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view,upProgram_);
        input = up_[i].texture; sourceWidth = up_[i].width; sourceHeight = up_[i].height;
    }
    return true;
}

void BloomRenderer::shutdown() {
    releaseTargets();
    if (bgfx::isValid(downProgram_)) bgfx::destroy(downProgram_);
    if (bgfx::isValid(upProgram_)) bgfx::destroy(upProgram_);
    if (bgfx::isValid(params_)) bgfx::destroy(params_);
    if (bgfx::isValid(source_)) bgfx::destroy(source_);
    if (bgfx::isValid(detail_)) bgfx::destroy(detail_);
    downProgram_ = upProgram_ = BGFX_INVALID_HANDLE;
    params_ = source_ = detail_ = BGFX_INVALID_HANDLE;
}

} // namespace genesis::raster
