#include <genesis/atmosphere/BrunetonAtmosphere.h>

#include <algorithm>
#include <cmath>

namespace genesis::atmosphere {
namespace {

constexpr float Pi = 3.14159265358979323846f;

Settings earthPreset()
{
    // Earth coefficients are expressed in inverse kilometres, matching the
    // units used by Genesis's precomputed Bruneton LUTs.
    return {
        .solarIrradiance = { 1.4740f, 1.8504f, 1.91198f },
        .rayleighScattering = { 0.00580233941f, 0.0135577619f, 0.0331000052f },
        .mieScattering = { 0.0014985f, 0.0014985f, 0.0014985f },
        .mieExtinction = { 0.003996f, 0.003996f, 0.003996f },
        .absorptionExtinction = { 0.000650f, 0.001881f, 0.000085f },
        .groundAlbedo = { 0.30f, 0.15f, 0.14f },
        .bottomRadiusKm = 6360.0f,
        .topRadiusKm = 6420.0f,
        .miePhaseG = 0.8f,
        .sunAngularDiameterDegrees = 0.5332f,
        .sunIntensity = 5.0f,
        .radiance = 1.0f,
        .dayCycleSpeed = 300.0f,
        .timeOfDay = -0.4f,
        .sunAzimuthDegrees = 0.0f,
        .sunElevationDegrees = 35.0f,
        .manualSun = false,
    };
}

Settings g_settings = earthPreset();

float radians(float degrees) { return degrees * Pi / 180.0f; }

std::array<float, 3> normalize(std::array<float, 3> value)
{
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (length > 0.0f) {
        value[0] /= length;
        value[1] /= length;
        value[2] /= length;
    }
    return value;
}

std::array<float, 3> rotateX(std::array<float, 3> v, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return { v[0], c * v[1] - s * v[2], s * v[1] + c * v[2] };
}

std::array<float, 3> rotateY(std::array<float, 3> v, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return { c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2] };
}

std::array<float, 3> rotateZ(std::array<float, 3> v, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return { c * v[0] - s * v[1], s * v[0] + c * v[1], v[2] };
}

std::array<float, 3> directSun(float azimuthDegrees, float elevationDegrees)
{
    const float azimuth = radians(azimuthDegrees);
    const float elevation = radians(elevationDegrees);
    const float horizontal = std::cos(elevation);
    return normalize({ horizontal * std::cos(azimuth), horizontal * std::sin(azimuth), std::sin(elevation) });
}

std::array<float, 3> animatedSun(float normalizedTime, float eastWestDegrees)
{
    std::array<float, 3> direction = normalize({ std::cos(normalizedTime * Pi), 0.0f, std::sin(normalizedTime * Pi) });
    direction = rotateX(direction, -0.8f);
    direction = rotateY(direction, -1.1f);
    direction = rotateZ(direction, radians(eastWestDegrees));
    return normalize(direction);
}

} // namespace

Settings& settings() { return g_settings; }

void resetEarth() { g_settings = earthPreset(); }

void setSun(float azimuthDegrees, float elevationDegrees)
{
    g_settings.sunAzimuthDegrees = std::remainder(azimuthDegrees, 360.0f);
    g_settings.sunElevationDegrees = std::clamp(elevationDegrees, -89.9f, 89.9f);
    g_settings.manualSun = true;
}

void nudgeSun(float azimuthDeltaDegrees, float elevationDeltaDegrees)
{
    if (!g_settings.manualSun) {
        // Start direct manipulation from a friendly daylight position. This
        // avoids a jump back into the animated day cycle after the first drag.
        g_settings.manualSun = true;
    }
    setSun(g_settings.sunAzimuthDegrees + azimuthDeltaDegrees,
           g_settings.sunElevationDegrees + elevationDeltaDegrees);
}

void setTimeOfDay(float normalizedTime, bool animate)
{
    g_settings.timeOfDay = std::clamp(normalizedTime, -1.0f, 1.0f);
    g_settings.manualSun = false;
    if (!animate)
        g_settings.dayCycleSpeed = 0.0f;
}

Frame evaluate(double sceneTimeSeconds, const float* normalizedTimeOverride)
{
    Frame frame{ g_settings, {} };
    if (g_settings.manualSun) {
        frame.sunDirection = directSun(g_settings.sunAzimuthDegrees, g_settings.sunElevationDegrees);
        return frame;
    }

    float time = normalizedTimeOverride ? *normalizedTimeOverride
        : static_cast<float>(std::fmod((sceneTimeSeconds * g_settings.dayCycleSpeed) / 86400.0
                                      + g_settings.timeOfDay + 1.0, 2.0) - 1.0);
    frame.sunDirection = animatedSun(time, g_settings.sunAzimuthDegrees);
    return frame;
}

} // namespace genesis::atmosphere
