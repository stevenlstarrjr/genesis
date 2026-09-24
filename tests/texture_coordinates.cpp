#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#undef CGLTF_IMPLEMENTATION
#include "materials/GltfTextureCoordinates.h"
#include "materials/TangentFrame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace genesis::materials;
static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool near(float a, float b) { return std::abs(a-b) < 1e-5f; }

struct TestVertex {
    float position[3];
    float normal[3]{0,0,1};
    float tangent[4]{};
    float uv[2];
};

int main() {
    const char json[] = R"({"asset":{"version":"2.0"},"images":[{"uri":"map.png"}],
        "textures":[{"source":0}],"materials":[
        {"normalTexture":{"index":0}}, {"normalTexture":{"index":0,"scale":0}},
        {"normalTexture":{"index":0,"scale":0.25}},
        {"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1},
          "metallicRoughnessTexture":{"index":0,"texCoord":3}},
         "normalTexture":{"index":0,"texCoord":1,"extensions":{"KHR_texture_transform":{"texCoord":7}}},
         "emissiveTexture":{"index":0,"texCoord":3},"occlusionTexture":{"index":0,"texCoord":7}}]})";
    cgltf_options options{}; cgltf_data* data{};
    require(cgltf_parse(&options, json, std::strlen(json), &data) == cgltf_result_success, "parse fixture");
    require(normalScale(data->materials[0].normal_texture) == 1, "default normal scale");
    require(normalScale(data->materials[1].normal_texture) == 0, "explicit zero normal scale");
    require(normalScale(data->materials[2].normal_texture) == .25f, "fractional normal scale");

    float rawUv[2]{.25f,.5f};
    cgltf_buffer buffer{}; buffer.data = rawUv; buffer.size = sizeof(rawUv);
    cgltf_buffer_view bufferView{}; bufferView.buffer = &buffer; bufferView.size = sizeof(rawUv);
    cgltf_accessor accessors[3]{};
    cgltf_attribute attributes[3]{};
    const int sets[]{1,3,7};
    for (size_t i = 0; i < 3; ++i) {
        accessors[i].buffer_view = &bufferView; accessors[i].count = 1;
        accessors[i].type = cgltf_type_vec2; accessors[i].component_type = cgltf_component_type_r_32f;
        accessors[i].stride = sizeof(rawUv);
        attributes[i].type = cgltf_attribute_type_texcoord; attributes[i].index = sets[i]; attributes[i].data = &accessors[i];
    }
    cgltf_primitive primitive{};
    primitive.material = &data->materials[3]; primitive.attributes = attributes; primitive.attributes_count = 3;
    MaterialTextureCoordinates coordinates(primitive);
    const size_t expected[]{0,1,2,1,2};
    for (size_t slot = 0; slot < materialTextureCount; ++slot) {
        require(coordinates.accessors[slot] == &accessors[expected[slot]], "independent UV selection, including override and UV7");
        require(coordinates.available(slot, 1) && !coordinates.available(slot, 2), "validate accessor length");
        // Give every material slot a distinct transform and exercise real accessor reads.
        auto& view = *const_cast<cgltf_texture_view*>(coordinates.views[slot]);
        view.has_transform = true;
        view.transform.rotation = 1.57079632679f;
        view.transform.scale[0] = 2; view.transform.scale[1] = -3;
        view.transform.offset[0] = float(slot); view.transform.offset[1] = .1f;
        const auto uv = coordinates.read(slot, 0);
        require(near(uv[0], float(slot)+1.5f) && near(uv[1], .6f), "per-map scale, rotation, offset");
    }
    primitive.attributes_count = 1;
    MaterialTextureCoordinates missing(primitive);
    require(missing.available(0, 1) && !missing.available(1, 1) && !missing.available(2, 1), "missing UV disables only affected map");
    primitive.material = nullptr;
    MaterialTextureCoordinates absent(primitive);
    for (size_t slot = 0; slot < materialTextureCount; ++slot) require(!absent.available(slot,1), "no-material fallback");
    cgltf_free(data);

    const std::array<uint32_t, 6> indices{0,1,2,0,2,3};
    for (int mode = 0; mode < 6; ++mode) {
        std::array<TestVertex, 4> vertices{};
        const float uvs[4][2]{{0,0},{1,0},{1,1},{0,1}};
        for (size_t i = 0; i < vertices.size(); ++i) {
            auto& v = vertices[i];
            v.position[0] = uvs[i][0]; v.position[1] = uvs[i][1];
            const float u = uvs[i][0], t = uvs[i][1];
            v.uv[0] = mode == 1 ? -t : mode == 2 ? -u : mode == 4 ? u*1e-7f : u;
            v.uv[1] = mode == 1 ? u : mode == 3 ? -t : mode == 4 ? t*1e-7f : t;
            if (mode == 5) { v.uv[0] = v.uv[1] = 0; v.normal[0] = 1; v.normal[2] = 0; }
        }
        generateTangents(std::span<TestVertex>(vertices), std::span<const uint32_t>(indices), [](const TestVertex& v){return v.uv;});
        for (const auto& v : vertices) {
            require(near(v.tangent[0]*v.normal[0]+v.tangent[1]*v.normal[1]+v.tangent[2]*v.normal[2],0), "orthogonal even with degenerate UV");
            require(near(v.tangent[0]*v.tangent[0]+v.tangent[1]*v.tangent[1]+v.tangent[2]*v.tangent[2],1), "unit finite tangent");
            if (mode != 5) {
                require(near(v.tangent[0],mode==1?0.0f:mode==2?-1.0f:1.0f), "rotated/scaled U orientation");
                require(near(v.tangent[1],mode==1?-1.0f:0.0f), "rotated V orientation");
                require(v.tangent[3] == (mode==2 || mode==3 ? -1.0f : 1.0f), "mirrored handedness");
            }
        }
    }
    std::puts("PASS: all material UV slots, transforms/UV override, missing attributes, normal scale, rotated/mirrored/degenerate tangent frames");
}
