#pragma once
#include <array>

namespace genesis::clouds {
enum class Kind { Cumulus, Stratus, Cirrus, Storm };
struct Layer {
    Kind kind = Kind::Cumulus;
    bool enabled = false;
    float coverage = 0.45f;
    float density = 1.0f;
    float altitude = 1800.0f; // metres above ground
    float thickness = 1400.0f;
    float size = 4000.0f; // horizontal feature scale, metres
    float detail = 0.5f;
};
struct Settings {
    bool configured = false; // retain legacy sky until explicitly configured
    bool enabled = true;
    std::array<Layer, 2> layers = { Layer{}, Layer{Kind::Cirrus, false, 0.25f, 0.35f, 9000, 1200, 8000, 0.7f} };
    float windSpeed = 0.0f; // m/s; static skies require no repeated baking
    float windDirection = 45.0f; // degrees in the sky horizontal plane
    float updateHz = 2.0f;
    int steps = 24;
    int seed = 1;
};
Settings& settings();
void reset();
void advance(double seconds);
std::array<float, 2> windOffsetKm();
}
