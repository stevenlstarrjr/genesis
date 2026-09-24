#include "backends/raster/GltfTextures.h"
#include "materials/TextureMips.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace genesis::raster {

std::array<float, 3> GltfTextures::sampleEmissive(const cgltf_texture_view& view,
    const std::filesystem::path& modelPath, std::array<float, 2> uv) {
    if (!view.texture) return {1.0f, 1.0f, 1.0f};
    const cgltf_image* image = view.texture->image;
    if (!image && view.texture->has_basisu) image = view.texture->basisu_image;
    if (!image) return {1.0f, 1.0f, 1.0f};
    auto [it, inserted] = m_cpuImages.try_emplace(image);
    CpuImage& decoded = it->second;
    if (inserted) {
        int channels = 0;
        stbi_uc* pixels = nullptr;
        if (image->buffer_view && image->buffer_view->buffer &&
            image->buffer_view->buffer->data &&
            image->buffer_view->size <= size_t(std::numeric_limits<int>::max())) {
            const auto* bytes = static_cast<const stbi_uc*>(image->buffer_view->buffer->data)
                + image->buffer_view->offset;
            pixels = stbi_load_from_memory(bytes, int(image->buffer_view->size),
                &decoded.width, &decoded.height, &channels, 4);
        } else if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
            std::vector<char> uri(image->uri, image->uri + std::strlen(image->uri) + 1);
            cgltf_decode_uri(uri.data());
            pixels = stbi_load((modelPath.parent_path() / uri.data()).string().c_str(),
                &decoded.width, &decoded.height, &channels, 4);
        }
        if (pixels && decoded.width > 0 && decoded.height > 0 &&
            size_t(decoded.width) * size_t(decoded.height) <= size_t(1) << 26)
            decoded.pixels.assign(pixels, pixels + size_t(decoded.width) * decoded.height * 4);
        if (pixels) stbi_image_free(pixels);
    }
    if (decoded.pixels.empty() || !std::isfinite(uv[0]) || !std::isfinite(uv[1]))
        return {1.0f, 1.0f, 1.0f};
    const cgltf_sampler* sampler = view.texture->sampler;
    const auto wrap = [](float coordinate, int mode) {
        if (mode == 33071) return std::clamp(coordinate, 0.0f, 1.0f);
        if (mode == 33648) return 1.0f - std::abs(std::fmod(std::abs(coordinate), 2.0f) - 1.0f);
        return coordinate - std::floor(coordinate);
    };
    const float u = wrap(uv[0], sampler ? sampler->wrap_s : 10497);
    const float v = wrap(uv[1], sampler ? sampler->wrap_t : 10497);
    const int x = std::min(int(u * decoded.width), decoded.width - 1);
    const int y = std::min(int(v * decoded.height), decoded.height - 1);
    const size_t offset = (size_t(y) * decoded.width + x) * 4;
    std::array<float, 3> color{};
    for (int channel = 0; channel < 3; ++channel) {
        const float srgb = decoded.pixels[offset + channel] / 255.0f;
        color[channel] = srgb <= 0.04045f ? srgb / 12.92f
            : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }
    return color;
}

bgfx::TextureHandle GltfTextures::load(const cgltf_texture_view& view, bool srgb,
                                      const std::filesystem::path& modelPath) {
    if (!view.texture) return BGFX_INVALID_HANDLE;
    const cgltf_image* image = view.texture->image;
    if (!image && view.texture->has_basisu) image = view.texture->basisu_image;
    if (!image) return BGFX_INVALID_HANDLE;
    const TextureSampling sampling = textureSampling(view.texture->sampler, srgb);
    const Key key{image, sampling.flags, sampling.mips};
    if (auto it = m_cache.find(key); it != m_cache.end()) return it->second;

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = nullptr;
    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data &&
        image->buffer_view->size <= size_t(std::numeric_limits<int>::max())) {
        const auto* bytes = static_cast<const stbi_uc*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        pixels = stbi_load_from_memory(bytes, int(image->buffer_view->size), &width, &height, &channels, 4);
    } else if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
        std::vector<char> uri(image->uri, image->uri + std::strlen(image->uri) + 1);
        cgltf_decode_uri(uri.data());
        pixels = stbi_load((modelPath.parent_path() / uri.data()).string().c_str(), &width, &height, &channels, 4);
    }
    if (!pixels || width <= 0 || height <= 0 ||
        width > bgfx::getCaps()->limits.maxTextureSize || height > bgfx::getCaps()->limits.maxTextureSize) {
        std::fprintf(stderr, "Cannot decode supported-size texture in %s\n", modelPath.filename().string().c_str());
        if (pixels) stbi_image_free(pixels);
        return BGFX_INVALID_HANDLE;
    }

    if (sampling.mips) {
        // One fully initialized chain for every material map. Only color RGB
        // receives sRGB conversion; alpha and data maps remain linear.
        const auto levels = materials::textureMips(uint32_t(width), uint32_t(height),
            {pixels, size_t(width) * height * 4},
            srgb ? materials::TextureColorSpace::Srgb : materials::TextureColorSpace::Linear);
        const auto handle = bgfx::createTexture2D(uint16_t(width), uint16_t(height), true, 1,
            bgfx::TextureFormat::RGBA8, sampling.flags);
        if (bgfx::isValid(handle)) for (size_t mip = 0; mip < levels.size(); ++mip) {
            const auto& level = levels[mip];
            bgfx::updateTexture2D(handle, 0, uint8_t(mip), 0, 0, uint16_t(level.width), uint16_t(level.height),
                bgfx::copy(level.pixels.data(), uint32_t(level.pixels.size())));
        }
        stbi_image_free(pixels);
        m_cache.emplace(key, handle);
        return handle;
    }

    const auto handle = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
        bgfx::TextureFormat::RGBA8, sampling.flags, bgfx::copy(pixels, uint32_t(width * height * 4)));
    stbi_image_free(pixels);
    m_cache.emplace(key, handle);
    return handle;
}

void GltfTextures::clear() {
    for (const auto& entry : m_cache) if (bgfx::isValid(entry.second)) bgfx::destroy(entry.second);
    m_cache.clear();
    m_cpuImages.clear();
}

} // namespace genesis::raster
