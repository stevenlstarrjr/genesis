#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace genesis::probes {

// Lossless layout: four adjacent R32F visibility samples become one RGBA32F
// texel. Six appended rows hold the original RGBA32F lighting coefficients.
// This replaces two sampled resources without reducing visibility precision.
struct GridAtlas {
    static constexpr uint32_t resolution = 16;
    static constexpr uint32_t lightingRow = 6 * resolution;
    static constexpr uint32_t height = lightingRow + 6;
    uint32_t width = 0;
    std::vector<float> rgba;
};

inline GridAtlas packGridAtlas(uint32_t probeCount, const std::vector<float>& visibility,
                              const std::vector<float>& lighting) {
    GridAtlas atlas;
    if (probeCount == 0 || probeCount > 4096 ||
        visibility.size() != size_t(probeCount) * 6 * GridAtlas::resolution * GridAtlas::resolution ||
        lighting.size() != size_t(probeCount) * 6 * 4) return atlas;
    atlas.width = probeCount * GridAtlas::resolution / 4;
    atlas.rgba.resize(size_t(atlas.width) * GridAtlas::height * 4, 0.0f);
    std::copy(visibility.begin(), visibility.end(), atlas.rgba.begin());
    for (uint32_t face = 0; face < 6; ++face)
        std::copy_n(lighting.data() + size_t(face) * probeCount * 4, size_t(probeCount) * 4,
                    atlas.rgba.data() + size_t(GridAtlas::lightingRow + face) * atlas.width * 4);
    return atlas;
}

} // namespace genesis::probes
