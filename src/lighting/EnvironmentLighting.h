#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace genesis::lighting {

using Rgba = std::array<float, 4>;
using IrradianceSH = std::array<Rgba, 9>;

struct LinearEnvironmentImage {
    uint32_t width = 0, height = 0;
    std::vector<Rgba> pixels;
};

// Linear radiance only: never gamma encode or clamp to half-float here.
// Equirectangular coordinates match atan2(z,x)/(2*pi)+.5, acos(y)/pi.
// Returns cosine-convolved irradiance coefficients (the Lambertian 1/pi is
// applied by the material shader). Rotation follows the sky's sampling rotation.
IrradianceSH projectDiffuseIrradiance(const LinearEnvironmentImage& image, float rotationRadians = 0);
std::array<float, 3> evaluateDiffuseIrradiance(const IrradianceSH& coefficients,
    const std::array<float, 3>& unitNormal);

// Solid-angle-weighted box mips, including NPOT dimensions. Every input texel
// contributes; tiny bright sources and the polar rows keep their integrated energy.
std::vector<LinearEnvironmentImage> buildEnvironmentMips(LinearEnvironmentImage image);

} // namespace genesis::lighting
