#include <genesis/atmosphere/Clouds.h>
#include <algorithm>
#include <cmath>

namespace genesis::clouds {
namespace { Settings g_settings; double g_time = 0.0; }
Settings& settings() { return g_settings; }
void reset() { g_settings = {}; g_time = 0.0; }
void advance(double seconds) {
    const bool visible = std::any_of(g_settings.layers.begin(), g_settings.layers.end(),
        [](const Layer& layer) { return layer.enabled && layer.coverage > 0 && layer.density > 0; });
    if (g_settings.configured && g_settings.enabled && visible &&
        g_settings.windSpeed > 0.0f && std::isfinite(seconds))
        g_time += std::clamp(seconds, 0.0, 0.25);
}
std::array<float, 2> windOffsetKm() {
    const double time = std::floor(g_time * g_settings.updateHz) / g_settings.updateHz;
    const double angle = g_settings.windDirection * 3.141592653589793 / 180.0;
    const double distance = time * g_settings.windSpeed / 1000.0;
    return { float(distance * std::cos(angle)), float(distance * std::sin(angle)) };
}
}
