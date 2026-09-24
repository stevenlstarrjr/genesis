#include "materials/TextureMips.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

using genesis::materials::TextureColorSpace;
using genesis::materials::textureMips;

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static int expectedSrgb(double linear) {
    // Independent analytic reference; production uses a lookup for decoding.
    return int(std::round(255.0 * (linear <= .0031308 ? 12.92 * linear
        : 1.055 * std::pow(linear, 1.0 / 2.4) - .055)));
}

int main() {
    const std::vector<uint8_t> blackWhite{0,0,0,0, 255,255,255,255};
    for (const auto dimensions : {std::array<uint32_t,2>{2,1}, {1,2}}) {
        const auto srgb = textureMips(dimensions[0], dimensions[1], blackWhite, TextureColorSpace::Srgb);
        const auto linear = textureMips(dimensions[0], dimensions[1], blackWhite, TextureColorSpace::Linear);
        require(srgb.size() == 2 && srgb[0].pixels == blackWhite, "preserve source bytes");
        require(srgb[1].pixels == std::vector<uint8_t>({188,188,188,128}), "linear-light RGB, linear alpha");
        require(linear[1].pixels == std::vector<uint8_t>({128,128,128,128}), "data maps do not get gamma conversion");
    }
    for (int value = 0; value < 256; ++value) {
        const std::vector<uint8_t> pixels(8 * 4, uint8_t(value));
        const auto chain = textureMips(4, 2, pixels, TextureColorSpace::Srgb);
        require(chain.size() == 3, "rectangular tail reaches 1x1");
        for (const auto& level : chain) for (uint8_t sample : level.pixels)
            require(sample == value, "all sRGB codes and linear alpha remain constant across mips");
    }
    const std::vector<uint8_t> rgb{255,0,0,0, 0,255,0,64, 0,0,255,128, 0,0,0,255};
    const auto channels = textureMips(2, 2, rgb, TextureColorSpace::Srgb);
    require(channels.back().pixels == std::vector<uint8_t>({137,137,137,112}), "independent RGB/alpha channel order");

    for (const auto dimensions : {std::array<uint32_t,2>{7,5}, {3,3}, {1,9}, {9,1}, {2,8}, {8,2}, {1,513}}) {
        const size_t count = size_t(dimensions[0]) * dimensions[1];
        std::vector<uint8_t> pixels(count * 4, 0);
        // A white texel on the last row AND last column must never be dropped.
        std::fill_n(pixels.end() - 4, 4, uint8_t(255));
        for (const auto colorSpace : {TextureColorSpace::Srgb, TextureColorSpace::Linear}) {
            const auto chain = textureMips(dimensions[0], dimensions[1], pixels, colorSpace);
            uint32_t w = dimensions[0], h = dimensions[1];
            for (const auto& level : chain) {
                require(level.width == w && level.height == h && level.pixels.size() == size_t(w) * h * 4,
                        "all levels have initialized storage of the expected shape");
                w = std::max(w / 2, 1u); h = std::max(h / 2, 1u);
            }
            require(chain.back().width == 1 && chain.back().height == 1, "complete mip tail");
            const int expected = colorSpace == TextureColorSpace::Srgb ? expectedSrgb(1.0 / count)
                : int(std::round(255.0 / count));
            for (int c = 0; c < 3; ++c)
                require(std::abs(int(chain.back().pixels[c]) - expected) <= 1, "odd-edge energy within 8-bit quantization");
            require(std::abs(int(chain.back().pixels[3]) - int(std::round(255.0 / count))) <= 1,
                    "alpha averages linearly even in sRGB textures");
        }
    }
    const std::vector<uint8_t> pixel{1,10,11,42};
    const auto single = textureMips(1, 1, pixel, TextureColorSpace::Srgb);
    require(single.size() == 1 && single[0].pixels == pixel, "1x1 is a valid complete chain");
    require(textureMips(0, 1, {}, TextureColorSpace::Srgb).empty(), "zero dimension");
    require(textureMips(1, 1, {}, TextureColorSpace::Srgb).empty(), "missing pixels");
    require(textureMips(1, 1, blackWhite, TextureColorSpace::Srgb).empty(), "excess pixels");
    require(textureMips(0x80000000u, 0x80000000u, {}, TextureColorSpace::Srgb).empty(), "byte-size overflow");
    std::puts("PASS: sRGB/linear RGB, linear alpha, all constant codes, 1D/NPOT tails, input validation");
}
