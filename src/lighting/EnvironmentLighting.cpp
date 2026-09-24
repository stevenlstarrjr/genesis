#include "EnvironmentLighting.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace genesis::lighting {
namespace {
constexpr double pi = 3.14159265358979323846;

std::array<double, 9> shBasis(double x, double y, double z) {
    return {0.28209479177387814, 0.4886025119029199 * y, 0.4886025119029199 * z,
        0.4886025119029199 * x, 1.0925484305920792 * x * y,
        1.0925484305920792 * y * z, 0.31539156525252005 * (3 * z * z - 1),
        1.0925484305920792 * x * z, 0.5462742152960396 * (x * x - y * y)};
}

bool valid(const LinearEnvironmentImage& image) {
    return image.width != 0 && image.height != 0
        && image.pixels.size() == size_t(image.width) * image.height;
}
} // namespace

IrradianceSH projectDiffuseIrradiance(const LinearEnvironmentImage& image, float rotationRadians) {
    IrradianceSH result{};
    if (!valid(image)) return result;
    std::array<std::array<double, 3>, 9> coefficients{};
    std::vector<std::array<double, 2>> longitude(image.width);
    for (uint32_t x = 0; x < image.width; ++x) {
        // Transform each source direction back to world space. This is the
        // inverse of the Y rotation applied when sampling the visible sky.
        const double phi = ((double(x) + .5) / image.width - .5) * 2 * pi - rotationRadians;
        longitude[x] = {std::cos(phi), std::sin(phi)};
    }
    for (uint32_t y = 0; y < image.height; ++y) {
        const double theta = (double(y) + .5) * pi / image.height;
        const double solidAngle = (2 * pi / image.width)
            * (std::cos(double(y) * pi / image.height) - std::cos(double(y + 1) * pi / image.height));
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto basis = shBasis(std::sin(theta) * longitude[x][0], std::cos(theta),
                std::sin(theta) * longitude[x][1]);
            const auto& pixel = image.pixels[size_t(y) * image.width + x];
            for (size_t i = 0; i < basis.size(); ++i)
                for (size_t channel = 0; channel < 3; ++channel)
                    coefficients[i][channel] += pixel[channel] * basis[i] * solidAngle;
        }
    }
    // Ramamoorthi/Hanrahan: convolution by max(N.L,0) is diagonal in SH.
    // Three bands retain the low-frequency diffuse response, not a GGX lobe.
    for (size_t i = 0; i < result.size(); ++i) {
        const double kernel = i == 0 ? pi : i < 4 ? 2 * pi / 3 : pi / 4;
        for (size_t channel = 0; channel < 3; ++channel)
            result[i][channel] = float(coefficients[i][channel] * kernel);
    }
    return result;
}

std::array<float, 3> evaluateDiffuseIrradiance(const IrradianceSH& coefficients,
    const std::array<float, 3>& unitNormal) {
    const auto basis = shBasis(unitNormal[0], unitNormal[1], unitNormal[2]);
    std::array<float, 3> result{};
    for (size_t i = 0; i < basis.size(); ++i)
        for (size_t channel = 0; channel < 3; ++channel)
            result[channel] += coefficients[i][channel] * float(basis[i]);
    for (float& channel : result) channel = std::max(channel, 0.0f);
    return result;
}

std::vector<LinearEnvironmentImage> buildEnvironmentMips(LinearEnvironmentImage image) {
    if (!valid(image)) return {};
    std::vector<LinearEnvironmentImage> chain;
    chain.push_back(std::move(image));
    while (chain.back().width > 1 || chain.back().height > 1) {
        const auto& source = chain.back();
        LinearEnvironmentImage mip{std::max(source.width / 2, 1u), std::max(source.height / 2, 1u), {}};
        mip.pixels.resize(size_t(mip.width) * mip.height);
        for (uint32_t y = 0; y < mip.height; ++y) {
            const double y0 = double(y) * source.height / mip.height;
            const double y1 = double(y + 1) * source.height / mip.height;
            for (uint32_t x = 0; x < mip.width; ++x) {
                const double x0 = double(x) * source.width / mip.width;
                const double x1 = double(x + 1) * source.width / mip.width;
                std::array<double, 3> sum{};
                double total = 0;
                for (uint32_t sy = uint32_t(y0); sy < uint32_t(std::ceil(y1)); ++sy) {
                    const double rowWeight = std::cos(std::max(y0, double(sy)) * pi / source.height)
                        - std::cos(std::min(y1, double(sy + 1)) * pi / source.height);
                    for (uint32_t sx = uint32_t(x0); sx < uint32_t(std::ceil(x1)); ++sx) {
                        const double weight = rowWeight * (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)));
                        const auto& pixel = source.pixels[size_t(sy) * source.width + sx];
                        for (size_t channel = 0; channel < 3; ++channel) sum[channel] += pixel[channel] * weight;
                        total += weight;
                    }
                }
                auto& pixel = mip.pixels[size_t(y) * mip.width + x];
                for (size_t channel = 0; channel < 3; ++channel) pixel[channel] = float(sum[channel] / total);
                pixel[3] = 1.0f;
            }
        }
        chain.push_back(std::move(mip));
    }
    return chain;
}

} // namespace genesis::lighting
