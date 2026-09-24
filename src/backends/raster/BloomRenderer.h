#pragma once

#include <bgfx/bgfx.h>
#include <array>
#include <cstdint>

namespace genesis::raster {

// Owns only raster post-processing resources. Scene HDR is never modified.
class BloomRenderer {
public:
    static constexpr uint16_t levelCount = 5;
    static constexpr uint16_t viewCount = levelCount * 2 - 1;
    bool initialize();
    bool render(uint16_t firstView, uint16_t width, uint16_t height,
                bgfx::TextureHandle hdr, bool enabled);
    bgfx::TextureHandle texture() const { return up_[0].texture; }
    void shutdown();

private:
    struct Level {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle buffer = BGFX_INVALID_HANDLE;
        uint16_t width = 0, height = 0;
    };
    bool resize(uint16_t width, uint16_t height);
    void releaseTargets();
    std::array<Level, levelCount> down_{};
    std::array<Level, levelCount - 1> up_{};
    uint16_t width_ = 0, height_ = 0;
    bgfx::ProgramHandle downProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle upProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle source_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle detail_ = BGFX_INVALID_HANDLE;
};

} // namespace genesis::raster
