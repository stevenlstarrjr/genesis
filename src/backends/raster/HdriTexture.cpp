#include "HdriTexture.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace genesis::raster {

HdriLighting loadHdriTexture(const std::filesystem::path& path, float rotationRadians) {
    if (path.empty()) return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto length = file.tellg();
    if (length <= 0 || uint64_t(length) > std::numeric_limits<uint32_t>::max())
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) return {};

    bx::DefaultAllocator allocator;
    bx::Error error;
    // Outdoor EXRs contain solar radiance above FP16's maximum (65,504).
    // Keep the source in FP32: converting first irreversibly turns those pixels
    // into infinities, poisoning filtered reflections and the bloom footprint.
    bimg::ImageContainer* image = bimg::imageParse(&allocator, bytes.data(), uint32_t(bytes.size()),
        bimg::TextureFormat::RGBA32F, &error);
    if (!image || image->m_cubeMap || image->m_depth > 1 || image->m_numLayers != 1
        || image->m_numMips != 1 || image->m_width == 0 || image->m_height == 0
        || image->m_width > bgfx::getCaps()->limits.maxTextureSize
        || image->m_height > bgfx::getCaps()->limits.maxTextureSize) {
        if (image) bimg::imageFree(image);
        return {};
    }
    float* radiance = static_cast<float*>(image->m_data);
    float peak = 0.0f;
    size_t invalid = 0;
    for (size_t index = 0; index < size_t(image->m_width) * image->m_height * 4; ++index) {
        if (index % 4 == 3) { radiance[index] = 1.0f; continue; }
        if (!std::isfinite(radiance[index])) ++invalid;
        else {
            radiance[index] = std::max(radiance[index], 0.0f);
            peak = std::max(peak, radiance[index]);
        }
    }
    if (invalid != 0) {
        std::fprintf(stderr, "HDRI contains %zu non-finite color samples: %s\n", invalid, path.string().c_str());
        bimg::imageFree(image);
        return {};
    }
    HdriLighting result;
    result.width = uint16_t(image->m_width);
    result.height = uint16_t(image->m_height);
    lighting::LinearEnvironmentImage source{image->m_width, image->m_height, {}};
    source.pixels.resize(size_t(source.width) * source.height);
    std::memcpy(source.pixels.data(), image->m_data, source.pixels.size() * sizeof(lighting::Rgba));
    bimg::imageFree(image);
    result.diffuse = lighting::projectDiffuseIrradiance(source, rotationRadians);
    const auto mips = lighting::buildEnvironmentMips(std::move(source));
    result.texture = bgfx::createTexture2D(result.width, result.height, true, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(result.texture)) return {};
    for (uint8_t mip = 0; mip < mips.size(); ++mip) {
        const auto& level = mips[mip];
        bgfx::updateTexture2D(result.texture, 0, mip, 0, 0, uint16_t(level.width), uint16_t(level.height),
            bgfx::copy(level.pixels.data(), uint32_t(level.pixels.size() * sizeof(lighting::Rgba))));
    }
    std::printf("HDRI_SOURCE format=RGBA32F peak=%g invalid=0 mips=%zu diffuse=SH9\n", peak, mips.size());
    std::fflush(stdout);
    return result;
}

} // namespace genesis::raster
