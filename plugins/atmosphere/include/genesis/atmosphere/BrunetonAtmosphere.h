#pragma once

#include <array>

namespace genesis::atmosphere {

struct Spectrum3 {
    float r;
    float g;
    float b;
};

struct Settings {
    Spectrum3 solarIrradiance;
    Spectrum3 rayleighScattering;
    Spectrum3 mieScattering;
    Spectrum3 mieExtinction;
    Spectrum3 absorptionExtinction;
    Spectrum3 groundAlbedo;

    float bottomRadiusKm;
    float topRadiusKm;
    float miePhaseG;
    float sunAngularDiameterDegrees;
    float sunIntensity;
    float radiance;

    float dayCycleSpeed;
    float timeOfDay;
    float sunAzimuthDegrees;
    float sunElevationDegrees;
    bool manualSun;
};

struct Frame {
    Settings settings;
    std::array<float, 3> sunDirection;
};

// Process-wide state is intentional: Python configures the atmosphere before
// the renderer is created, and the renderer consumes the same state each frame.
Settings& settings();
void resetEarth();
void setSun(float azimuthDegrees, float elevationDegrees);
void nudgeSun(float azimuthDeltaDegrees, float elevationDeltaDegrees);
void setTimeOfDay(float normalizedTime, bool animate);
Frame evaluate(double sceneTimeSeconds, const float* normalizedTimeOverride = nullptr);

} // namespace genesis::atmosphere
