#pragma once

#include <bgfx/bgfx.h>
#include <cgltf.h>
#include <filesystem>
#include <array>
#include <unordered_map>
#include <vector>

namespace genesis::raster {

struct TextureSampling {
    uint64_t flags = 0;
    bool mips = true;
};

inline TextureSampling textureSampling(const cgltf_sampler* sampler, bool srgb) {
    TextureSampling result;
    result.flags = srgb ? BGFX_TEXTURE_SRGB : 0;
    if (!sampler) {
        result.flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        return result;
    }
    if (sampler->wrap_s == 33071) result.flags |= BGFX_SAMPLER_U_CLAMP;
    else if (sampler->wrap_s == 33648) result.flags |= BGFX_SAMPLER_U_MIRROR;
    if (sampler->wrap_t == 33071) result.flags |= BGFX_SAMPLER_V_CLAMP;
    else if (sampler->wrap_t == 33648) result.flags |= BGFX_SAMPLER_V_MIRROR;
    if (sampler->mag_filter == 9728) result.flags |= BGFX_SAMPLER_MAG_POINT;
    else if (sampler->mag_filter == 0) result.flags |= BGFX_SAMPLER_MAG_ANISOTROPIC;
    switch (sampler->min_filter) {
    case 9728: result.flags |= BGFX_SAMPLER_MIN_POINT; result.mips = false; break;
    case 9729: result.mips = false; break;
    case 9984: result.flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MIP_POINT; break;
    case 9985: result.flags |= BGFX_SAMPLER_MIP_POINT; break;
    case 9986: result.flags |= BGFX_SAMPLER_MIN_POINT; break;
    case 9987: break; // linear texel and mip interpolation
    default: result.flags |= BGFX_SAMPLER_MIN_ANISOTROPIC; break;
    }
    return result;
}

class GltfTextures {
public:
    ~GltfTextures() { clear(); }
    GltfTextures() = default;
    GltfTextures(const GltfTextures&) = delete;
    GltfTextures& operator=(const GltfTextures&) = delete;
    bgfx::TextureHandle load(const cgltf_texture_view& view, bool srgb,
                             const std::filesystem::path& modelPath);
    std::array<float, 3> sampleEmissive(const cgltf_texture_view& view,
        const std::filesystem::path& modelPath, std::array<float, 2> uv);
    void clearCpuImages() { m_cpuImages.clear(); }
    void clear();

private:
    struct Key {
        const cgltf_image* image{};
        uint64_t flags{};
        bool mips{};
        bool operator==(const Key& other) const {
            return image == other.image && flags == other.flags && mips == other.mips;
        }
    };
    struct Hash {
        size_t operator()(const Key& key) const {
            return std::hash<const void*>{}(key.image) ^ std::hash<uint64_t>{}(key.flags)
                ^ (size_t(key.mips) << 1);
        }
    };
    std::unordered_map<Key, bgfx::TextureHandle, Hash> m_cache;
    struct CpuImage { int width = 0, height = 0; std::vector<uint8_t> pixels; };
    std::unordered_map<const cgltf_image*, CpuImage> m_cpuImages;
};

} // namespace genesis::raster
