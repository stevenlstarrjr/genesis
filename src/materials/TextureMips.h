#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace genesis::materials {

struct Rgba8Mip {
    uint32_t width{}, height{};
    std::vector<uint8_t> pixels;
};

enum class TextureColorSpace { Linear, Srgb };

inline const std::array<double, 256>& srgbDecodeTable() {
    static const std::array<double, 256> table = [] {
        std::array<double, 256> values{};
        for (size_t i = 0; i < values.size(); ++i) {
            const double value = double(i) / 255.0;
            values[i] = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        }
        return values;
    }();
    return table;
}

inline uint8_t encodeSrgb(double linear) {
    const double encoded = linear <= 0.0031308 ? linear * 12.92
        : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return uint8_t(std::clamp(std::round(encoded * 255.0), 0.0, 255.0));
}

// Filter color RGB in linear light and re-encode for hardware sRGB sampling.
// Alpha and material data (AO, metallic/roughness, normals) stay linear.
// Area weights retain odd edge texels, including complete 1D mip tails.
inline std::vector<Rgba8Mip> textureMips(uint32_t width, uint32_t height,
                                       std::span<const uint8_t> pixels, TextureColorSpace colorSpace) {
    if (!width || !height || size_t(width) > std::numeric_limits<size_t>::max() / height / 4 ||
        pixels.size() != size_t(width) * height * 4) return {};
    std::vector<Rgba8Mip> result;
    result.push_back({width, height, {pixels.begin(), pixels.end()}});
    const bool srgb = colorSpace == TextureColorSpace::Srgb;
    const auto& decoded = srgbDecodeTable();
    while (result.back().width > 1 || result.back().height > 1) {
        const auto& src = result.back();
        Rgba8Mip dst{std::max(src.width / 2, 1u), std::max(src.height / 2, 1u), {}};
        dst.pixels.resize(size_t(dst.width) * dst.height * 4);
        const double sx = double(src.width) / dst.width, sy = double(src.height) / dst.height;
        for (uint32_t y = 0; y < dst.height; ++y) for (uint32_t x = 0; x < dst.width; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
            double sum[4]{};
            for (uint32_t iy = uint32_t(y0); iy < std::min(src.height, uint32_t(std::ceil(y1))); ++iy)
                for (uint32_t ix = uint32_t(x0); ix < std::min(src.width, uint32_t(std::ceil(x1))); ++ix) {
                    const double weight = (std::min(x1, double(ix + 1)) - std::max(x0, double(ix)))
                        * (std::min(y1, double(iy + 1)) - std::max(y0, double(iy)));
                    for (int c = 0; c < 4; ++c) {
                        const uint8_t value = src.pixels[(size_t(iy) * src.width + ix) * 4 + c];
                        sum[c] += (srgb && c < 3 ? decoded[value] : double(value)) * weight;
                    }
                }
            for (int c = 0; c < 4; ++c)
                dst.pixels[(size_t(y) * dst.width + x) * 4 + c] = srgb && c < 3
                    ? encodeSrgb(sum[c] / (sx * sy))
                    : uint8_t(std::clamp(std::round(sum[c] / (sx * sy)), 0.0, 255.0));
        }
        result.push_back(std::move(dst));
    }
    return result;
}

} // namespace genesis::materials
