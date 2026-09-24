#pragma once

#include <bgfx/bgfx.h>
#include <filesystem>
#include "lighting/EnvironmentLighting.h"

namespace genesis::raster {
struct HdriLighting {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    lighting::IrradianceSH diffuse{};
    uint16_t width = 1, height = 1;
};

HdriLighting loadHdriTexture(const std::filesystem::path& path, float rotationRadians = 0);
}
