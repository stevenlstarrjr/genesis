#include "backends/raster/LightSourceRenderer.h"
#include "RenderScene.h"

#include <bx/math.h>
#include <compiled/fs_light_source.h>
#include <compiled/vs_light_source.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace genesis::raster {
namespace {

struct SourceVertex {
    float position[3]{};
    float normal[3]{0.0f, 0.0f, 1.0f};

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout result;
        result.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .end();
        return result;
    }
};

bgfx::ShaderHandle shader(const uint8_t* bytes, uint32_t size) {
    return bgfx::createShader(bgfx::copy(bytes, size));
}

LightSourceRenderer::Mesh createMesh(const std::vector<SourceVertex>& vertices,
    const std::vector<uint16_t>& indices) {
    LightSourceRenderer::Mesh mesh;
    mesh.vertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(SourceVertex))),
        SourceVertex::layout());
    mesh.indices = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
    return mesh;
}

LightSourceRenderer::Mesh createSphere() {
    constexpr uint16_t rings = 8;
    constexpr uint16_t segments = 16;
    std::vector<SourceVertex> vertices;
    std::vector<uint16_t> indices;
    for (uint16_t ring = 0; ring <= rings; ++ring) {
        const float latitude = 3.14159265f * float(ring) / float(rings);
        const float y = std::cos(latitude);
        const float radial = std::sin(latitude);
        for (uint16_t segment = 0; segment <= segments; ++segment) {
            const float longitude = 6.28318531f * float(segment) / float(segments);
            SourceVertex vertex;
            vertex.position[0] = vertex.normal[0] = radial * std::cos(longitude);
            vertex.position[1] = vertex.normal[1] = y;
            vertex.position[2] = vertex.normal[2] = radial * std::sin(longitude);
            vertices.push_back(vertex);
        }
    }
    for (uint16_t ring = 0; ring < rings; ++ring) for (uint16_t segment = 0; segment < segments; ++segment) {
        const uint16_t a = uint16_t(ring * (segments + 1) + segment);
        const uint16_t b = uint16_t(a + segments + 1);
        indices.insert(indices.end(), {a, b, uint16_t(a + 1), uint16_t(a + 1), b, uint16_t(b + 1)});
    }
    return createMesh(vertices, indices);
}

LightSourceRenderer::Mesh createDisc() {
    constexpr uint16_t segments = 24;
    std::vector<SourceVertex> vertices(size_t(segments) + 1);
    std::vector<uint16_t> indices;
    for (uint16_t segment = 0; segment < segments; ++segment) {
        const float angle = 6.28318531f * float(segment) / float(segments);
        vertices[size_t(segment) + 1].position[0] = std::cos(angle);
        vertices[size_t(segment) + 1].position[1] = std::sin(angle);
        indices.insert(indices.end(), {0, uint16_t(segment + 1), uint16_t((segment + 1) % segments + 1)});
    }
    return createMesh(vertices, indices);
}

LightSourceRenderer::Mesh createQuad() {
    std::vector<SourceVertex> vertices(4);
    const float positions[4][2]{{-0.5f,-0.5f},{0.5f,-0.5f},{0.5f,0.5f},{-0.5f,0.5f}};
    for (size_t index = 0; index < vertices.size(); ++index) {
        vertices[index].position[0] = positions[index][0];
        vertices[index].position[1] = positions[index][1];
    }
    return createMesh(vertices, {0, 1, 2, 0, 2, 3});
}

void orientedTransform(float* matrix, const std::array<float, 3>& position,
    const std::array<float, 3>& direction, const std::array<float, 3>& up,
    float width, float height, float depth) {
    const std::array<float, 3> right{
        direction[1] * up[2] - direction[2] * up[1],
        direction[2] * up[0] - direction[0] * up[2],
        direction[0] * up[1] - direction[1] * up[0]};
    const float values[16]{
        right[0] * width, right[1] * width, right[2] * width, 0.0f,
        up[0] * height, up[1] * height, up[2] * height, 0.0f,
        direction[0] * depth, direction[1] * depth, direction[2] * depth, 0.0f,
        position[0], position[1], position[2], 1.0f};
    std::copy(std::begin(values), std::end(values), matrix);
}

}

bool LightSourceRenderer::initialize() {
    sphere_ = createSphere();
    disc_ = createDisc();
    quad_ = createQuad();
    program_ = bgfx::createProgram(
        shader(vs_light_source_shader, sizeof(vs_light_source_shader)),
        shader(fs_light_source_shader, sizeof(fs_light_source_shader)), true);
    color_ = bgfx::createUniform("u_sourceColor", bgfx::UniformType::Vec4);
    return bgfx::isValid(sphere_.vertices) && bgfx::isValid(disc_.vertices) &&
        bgfx::isValid(quad_.vertices) && bgfx::isValid(program_) && bgfx::isValid(color_);
}

void LightSourceRenderer::draw(uint16_t viewId) const {
    const auto& settings = render::g_lightSources;
    if (!settings.enabled || !bgfx::isValid(program_)) return;
    auto submit = [&](const Mesh& mesh, const float* transform, const std::array<float, 3>& color) {
        const float emission[4]{color[0] * settings.intensity, color[1] * settings.intensity,
            color[2] * settings.intensity, 1.0f};
        bgfx::setTransform(transform);
        bgfx::setVertexBuffer(0, mesh.vertices);
        bgfx::setIndexBuffer(mesh.indices);
        bgfx::setUniform(color_, emission);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(viewId, program_);
    };

    for (const auto& light : render::g_pointLights) {
        float transform[16];
        bx::mtxSRT(transform, settings.pointSize, settings.pointSize, settings.pointSize,
            0.0f, 0.0f, 0.0f, light.position[0], light.position[1], light.position[2]);
        submit(sphere_, transform, light.color);
    }
    for (const auto& light : render::g_spotLights) {
        float transform[16];
        orientedTransform(transform, light.position, light.direction, light.up,
            settings.spotSize, settings.spotSize, settings.spotSize);
        submit(disc_, transform, light.color);
    }
    for (const auto& light : render::g_areaLights) {
        float transform[16];
        orientedTransform(transform, light.position, light.direction, light.up,
            light.width * settings.areaScale, light.height * settings.areaScale, 1.0f);
        submit(quad_, transform, light.color);
    }
}

void LightSourceRenderer::shutdown() {
    for (Mesh* mesh : {&sphere_, &disc_, &quad_}) {
        if (bgfx::isValid(mesh->vertices)) bgfx::destroy(mesh->vertices);
        if (bgfx::isValid(mesh->indices)) bgfx::destroy(mesh->indices);
        *mesh = {};
    }
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(color_)) bgfx::destroy(color_);
    program_ = BGFX_INVALID_HANDLE;
    color_ = BGFX_INVALID_HANDLE;
}

}
