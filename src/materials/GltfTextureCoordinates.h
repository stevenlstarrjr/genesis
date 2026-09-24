#pragma once

#include <cgltf.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace genesis::materials {

// Resolve the source accessor at import time, so each material map can use any
// TEXCOORD_n without reserving one GPU vertex attribute for every source set.
inline int textureCoordinateSet(const cgltf_texture_view& view) {
    return view.has_transform && view.transform.has_texcoord
        ? view.transform.texcoord : view.texcoord;
}

inline std::array<float, 2> transformTextureCoordinates(
    const cgltf_texture_view& view, std::array<float, 2> uv) {
    if (!view.has_transform) return uv;
    const auto& t = view.transform;
    const float x = uv[0] * t.scale[0], y = uv[1] * t.scale[1];
    const float c = std::cos(t.rotation), s = std::sin(t.rotation);
    return {t.offset[0] + c * x - s * y, t.offset[1] + s * x + c * y};
}

inline float occlusionStrength(const cgltf_texture_view& view) {
    // cgltf initializes omitted strength to 1. Explicit zero must stay zero.
    return view.texture && std::isfinite(view.scale) ? std::clamp(view.scale, 0.0f, 1.0f) : 0.0f;
}

inline float normalScale(const cgltf_texture_view& view) {
    // cgltf supplies the omitted default (1); zero explicitly disables perturbation.
    return std::isfinite(view.scale) ? view.scale : 1.0f;
}

enum class MaterialTexture : size_t { BaseColor, MetalRough, Normal, Emissive, Occlusion, Count };
inline constexpr size_t materialTextureCount = size_t(MaterialTexture::Count);
inline constexpr std::array<const char*, materialTextureCount> materialTextureNames{
    "base color", "metallic/roughness", "normal", "emissive", "AO"};

struct MaterialTextureCoordinates {
    std::array<const cgltf_texture_view*, materialTextureCount> views{};
    std::array<const cgltf_accessor*, materialTextureCount> accessors{};

    explicit MaterialTextureCoordinates(const cgltf_primitive& primitive) {
        if (!primitive.material) return;
        const auto& material = *primitive.material;
        if (material.has_pbr_metallic_roughness) {
            views[0] = &material.pbr_metallic_roughness.base_color_texture;
            views[1] = &material.pbr_metallic_roughness.metallic_roughness_texture;
        }
        views[2] = &material.normal_texture;
        views[3] = &material.emissive_texture;
        views[4] = &material.occlusion_texture;
        for (size_t slot = 0; slot < views.size(); ++slot) {
            const auto* view = views[slot];
            if (!view || !view->texture) continue;
            for (size_t i = 0; i < primitive.attributes_count; ++i) {
                const auto& attr = primitive.attributes[i];
                if (attr.type == cgltf_attribute_type_texcoord && attr.index == textureCoordinateSet(*view)) {
                    accessors[slot] = attr.data;
                    break;
                }
            }
        }
    }

    bool available(size_t slot, size_t vertexCount) const {
        const auto* accessor = accessors[slot];
        return accessor && accessor->type == cgltf_type_vec2 && accessor->count == vertexCount;
    }

    std::array<float, 2> read(size_t slot, size_t vertex) const {
        std::array<float, 2> uv{};
        if (accessors[slot] && views[slot]) {
            cgltf_accessor_read_float(accessors[slot], vertex, uv.data(), 2);
            uv = transformTextureCoordinates(*views[slot], uv);
        }
        return uv;
    }
};

} // namespace genesis::materials
