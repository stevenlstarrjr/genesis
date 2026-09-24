#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#undef CGLTF_IMPLEMENTATION
#include "materials/GltfTextureCoordinates.h"
#include "materials/TextureMips.h"
#include "lighting/ProbeGridAtlas.h"
#include "backends/raster/GltfTextures.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    const char json[] = R"({"asset":{"version":"2.0"},"images":[{"uri":"ao.png"}],
        "textures":[{"source":0}],"materials":[{},
        {"occlusionTexture":{"index":0}},
        {"occlusionTexture":{"index":0,"strength":0}},
        {"occlusionTexture":{"index":0,"strength":0.5,"texCoord":1,
         "extensions":{"KHR_texture_transform":{"texCoord":3,"offset":[0.1,0.2],
         "scale":[2,3],"rotation":1.57079632679}}}}]})";
    cgltf_options options{};
    cgltf_data* data = nullptr;
    require(cgltf_parse(&options, json, std::strlen(json), &data) == cgltf_result_success, "parse fixture");
    require(genesis::materials::occlusionStrength(data->materials[0].occlusion_texture) == 0, "absent AO");
    require(genesis::materials::occlusionStrength(data->materials[1].occlusion_texture) == 1, "default strength");
    require(genesis::materials::occlusionStrength(data->materials[2].occlusion_texture) == 0, "explicit zero strength");
    const auto& view = data->materials[3].occlusion_texture;
    require(genesis::materials::occlusionStrength(view) == .5f, "fractional strength");
    require(genesis::materials::textureCoordinateSet(view) == 3, "transform overrides TEXCOORD_1");
    const auto uv = genesis::materials::transformTextureCoordinates(view, {.25f, .5f});
    require(std::abs(uv[0] + 1.4f) < 1e-5f && std::abs(uv[1] - .7f) < 1e-5f, "scale then rotate then offset");
    cgltf_free(data);

    const std::vector<uint8_t> gradient{0,255,0,255, 64,255,0,255, 128,255,0,255, 255,255,0,255};
    for (const auto dimensions : {std::array<uint32_t, 2>{4,1}, {1,4}}) {
        const auto mips = genesis::materials::textureMips(dimensions[0], dimensions[1], gradient,
            genesis::materials::TextureColorSpace::Linear);
        require(mips.size() == 3 && mips.back().pixels == std::vector<uint8_t>({112,255,0,255}), "linear 1D mip tail, no gamma");
    }
    std::vector<uint8_t> odd(3 * 5 * 4, 0);
    odd[odd.size() - 4] = 255;
    const auto oddMips = genesis::materials::textureMips(3, 5, odd, genesis::materials::TextureColorSpace::Linear);
    require(oddMips.back().pixels[0] == 17, "odd edge survives minification");
    require(genesis::materials::textureMips(0, 1, {}, genesis::materials::TextureColorSpace::Linear).empty(), "empty mip input");

    cgltf_sampler sampler{};
    sampler.wrap_s = 33071; sampler.wrap_t = 33648; sampler.mag_filter = 9728;
    for (int filter : {9728, 9729, 9984, 9985, 9986, 9987}) {
        sampler.min_filter = filter;
        const auto sampling = genesis::raster::textureSampling(&sampler, false);
        require((sampling.flags & BGFX_TEXTURE_SRGB) == 0, "AO is linear data");
        require((sampling.flags & BGFX_SAMPLER_U_MASK) == BGFX_SAMPLER_U_CLAMP, "clamp U");
        require((sampling.flags & BGFX_SAMPLER_V_MASK) == BGFX_SAMPLER_V_MIRROR, "mirror V");
        require((sampling.flags & BGFX_SAMPLER_MAG_MASK) == BGFX_SAMPLER_MAG_POINT, "nearest magnification");
        require(sampling.mips == (filter >= 9984), "non-mip minification stays at level zero");
        require(((sampling.flags & BGFX_SAMPLER_MIN_MASK) == BGFX_SAMPLER_MIN_POINT)
            == (filter == 9728 || filter == 9984 || filter == 9986), "minification filter");
        require(((sampling.flags & BGFX_SAMPLER_MIP_MASK) == BGFX_SAMPLER_MIP_POINT)
            == (filter == 9984 || filter == 9985), "mip interpolation");
    }

    using genesis::probes::GridAtlas;
    for (uint32_t count : {1u, 8u, 125u, 405u}) {
        std::vector<float> visibility(size_t(count) * 16 * 96), lighting(size_t(count) * 6 * 4);
        for (size_t i = 0; i < visibility.size(); ++i) visibility[i] = float(i % 13001) / 137.0f;
        for (size_t i = 0; i < lighting.size(); ++i) lighting[i] = float(i % 129) / 128.0f;
        const auto atlas = genesis::probes::packGridAtlas(count, visibility, lighting);
        require(atlas.width == count * 4 && atlas.rgba.size() == size_t(atlas.width) * 102 * 4, "atlas shape");
        for (uint32_t y = 0; y < 96; ++y) for (uint32_t x = 0; x < count * 16; ++x) {
            const size_t packed = (size_t(y) * atlas.width + x / 4) * 4 + x % 4;
            require(atlas.rgba[packed] == visibility[size_t(y) * count * 16 + x], "lossless visibility RGBA unpack");
        }
        for (uint32_t face = 0; face < 6; ++face) for (uint32_t probe = 0; probe < count; ++probe)
            for (uint32_t channel = 0; channel < 4; ++channel)
                require(atlas.rgba[(size_t(GridAtlas::lightingRow + face) * atlas.width + probe) * 4 + channel]
                    == lighting[(size_t(face) * count + probe) * 4 + channel], "lossless lighting rows");
    }
    require(genesis::probes::packGridAtlas(1, {}, {}).rgba.empty(), "malformed atlas input");
    std::puts("PASS: glTF AO defaults/strength/UV transform, linear/1D/NPOT mips, sampler modes, lossless probe atlas");
}
