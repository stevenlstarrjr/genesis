#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genesis::raster {

// Deterministic camera-motion regression captures. Advance by rendered frame,
// never wall-clock time, so runs on different GPUs visit identical viewpoints.
class OrbitCapture {
public:
    uint32_t count = 1;
    uint32_t requested = 0;

    void updateCamera(uint64_t frame, const std::array<float, 3>& initial,
        const std::array<float, 3>& target, std::array<float, 3>& position,
        float& yaw, float& pitch) const {
        if (count <= 1 || frame < 30) return;
        const float progress = float(std::min<uint64_t>(frame - 30, (count - 1) * 4))
            / float(count * 4);
        const float angle = progress * 6.28318530718f;
        const float x = initial[0] - target[0], z = initial[2] - target[2];
        const float radius = std::sqrt(x * x + z * z);
        position = {target[0] + x * std::cos(angle) + z * std::sin(angle),
            initial[1] + radius * 0.25f * std::sin(angle * 2.0f),
            target[2] - x * std::sin(angle) + z * std::cos(angle)};
        yaw = std::atan2(target[0] - position[0], target[2] - position[2]);
        pitch = std::atan2(target[1] - position[1], std::max(radius, 1e-5f));
    }

    bool ready(uint64_t frame) const {
        return requested < count && frame >= 30 && (frame - 30) % 4 == 0;
    }

    std::filesystem::path nextPath(const std::filesystem::path& base) {
        const auto index = requested++;
        if (count <= 1) return base;
        const std::string digits = std::to_string(index);
        return base.string() + "-" + std::string(4 - digits.size(), '0') + digits;
    }
};

} // namespace genesis::raster
