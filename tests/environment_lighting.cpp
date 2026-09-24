#include "lighting/EnvironmentLighting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace genesis::lighting;
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

LinearEnvironmentImage constant(uint32_t w, uint32_t h, Rgba value) {
    return {w, h, std::vector<Rgba>(size_t(w) * h, value)};
}

std::array<double, 3> energy(const LinearEnvironmentImage& image) {
    std::array<double, 3> result{};
    for (uint32_t y = 0; y < image.height; ++y) {
        const double omega = 2 * pi / image.width
            * (std::cos(y * pi / image.height) - std::cos((y + 1) * pi / image.height));
        for (uint32_t x = 0; x < image.width; ++x)
            for (size_t c = 0; c < 3; ++c) result[c] += image.pixels[size_t(y) * image.width + x][c] * omega;
    }
    return result;
}

int main() {
    const auto sky = constant(128, 64, {.5f, 1, 2, 1});
    const auto sh = projectDiffuseIrradiance(sky);
    const std::array<std::array<float, 3>, 7> normals{{
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {.6f,.8f,0}}};
    for (const auto& n : normals) {
        const auto e = evaluateDiffuseIrradiance(sh, n);
        for (size_t c = 0; c < 3; ++c)
            require(std::abs(e[c] - sky.pixels[0][c] * pi) < .003, "constant sky must give pi*L in every direction");
    }
    auto gradient = constant(128, 64, {0, 0, 0, 1});
    for (uint32_t y = 0; y < gradient.height; ++y)
        for (uint32_t x = 0; x < gradient.width; ++x) {
            const double theta = (y + .5) * pi / gradient.height;
            const double phi = ((x + .5) / gradient.width - .5) * 2 * pi;
            const float value = float(1 + .5 * std::sin(theta) * std::cos(phi));
            gradient.pixels[size_t(y) * gradient.width + x] = {value, value * 2, value * 3, 1};
        }
    for (float rotation : {0.0f, float(pi / 2), -.73f}) {
        const auto rotated = projectDiffuseIrradiance(gradient, rotation);
        for (const auto& n : normals) {
            const auto e = evaluateDiffuseIrradiance(rotated, n);
            const double reference = pi + pi / 3 * (n[0] * std::cos(rotation) - n[2] * std::sin(rotation));
            for (size_t c = 0; c < 3; ++c)
                require(std::abs(e[c] - reference * (c + 1)) < .004, "directional irradiance / sky rotation mismatch");
        }
    }
    const auto constantMips = buildEnvironmentMips(sky);
    require(constantMips.size() == 8, "full mip chain required");
    for (const auto& mip : constantMips)
        for (const auto& pixel : mip.pixels)
            for (size_t c = 0; c < 4; ++c)
                require(std::abs(pixel[c] - sky.pixels[0][c]) < 1e-6, "constant radiance changed during mip filtering");

    for (const auto& size : {std::array<uint32_t, 2>{64,32}, {7,5}, {1,9}, {11,1}}) {
        auto source = constant(size[0], size[1], {.1f, .2f, .3f, 1});
        source.pixels.back() = {159744, 70000, 11, 1}; // Includes odd/polar texels and above-FP16 solar energy.
        const auto reference = energy(source);
        const auto mips = buildEnvironmentMips(std::move(source));
        require(mips.back().width == 1 && mips.back().height == 1, "NPOT chain must finish at 1x1");
        require(mips.front().pixels.back()[0] == 159744, "source radiance must not be clipped");
        for (const auto& mip : mips) {
            const auto integrated = energy(mip);
            for (size_t c = 0; c < 3; ++c)
                require(std::abs(integrated[c] - reference[c]) < std::max(1e-5, reference[c] * 2e-6),
                    "mips must preserve solid-angle-weighted energy, including NPOT edge texels");
            for (const auto& pixel : mip.pixels)
                for (float channel : pixel) require(std::isfinite(channel), "mip must remain finite");
        }
    }
    require(buildEnvironmentMips({}).empty(), "empty image must be rejected");
    require(projectDiffuseIrradiance({}) == IrradianceSH{}, "empty image must have zero irradiance");
    std::puts("PASS: diffuse irradiance, HDRI rotation, constant/bright/polar/NPOT mip energy");
}
