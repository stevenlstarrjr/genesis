#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis::render {

struct RendererSettings {
    bool realtime = true;
    bool restirDI = true;
    bool restirGI = true;
    bool denoise = true;
    bool taa = true;
    bool bloom = true;
    int samplesPerPixel = 1;
    int bounceCount = 12;
    int diffuseBounceCount = 2;
    int historyFrames = 25;
    int specularHistoryFrames = 40;
    int referenceSamples = 1024;
    float renderScale = 1.0f;
    std::string quality = "medium";
    bool gpuTimings = true;
    std::string debugView = "none";
};

struct ReflectionProbeSettings {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    float blendDistance = 1.0f;
    int priority = 0;
};

struct PointLightSettings {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 20.0f;
    float radius = 10.0f;
    float shadowBias = 0.015f;
    bool castsShadows = true;
    std::string iesProfile;
};

struct SpotLightSettings {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> direction{0.0f, -1.0f, 0.0f};
    std::array<float, 3> up{0.0f, 0.0f, 1.0f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 20.0f;
    float radius = 10.0f;
    float innerAngle = 25.0f;
    float outerAngle = 35.0f;
    float shadowBias = 0.015f;
    bool castsShadows = true;
    std::string cookieTexture;
    std::string iesProfile;
};

struct AreaLightSettings {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> direction{0.0f, -1.0f, 0.0f};
    std::array<float, 3> up{0.0f, 0.0f, 1.0f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 20.0f;
    float radius = 10.0f;
    float width = 2.0f;
    float height = 2.0f;
    float shadowBias = 0.05f;
    bool castsShadows = true;
};

struct LightSourceSettings {
    bool enabled = false;
    float intensity = 8.0f;
    float pointSize = 0.30f;
    float spotSize = 0.45f;
    float areaScale = 1.0f;
};

struct EnvironmentSettings {
    std::string hdriPath;
    float intensity = 1.0f;
    float rotationDegrees = 0.0f;
    bool visible = true;
    bool atmosphereConfigured = false;
};

struct CameraSettings {
    std::array<float, 3> position{0, 2, 5};
    std::array<float, 3> target{0, 2, 0};
    std::array<float, 3> up{0, 1, 0};
    float fovDegrees = 60;
    float speed = 1.5f;
};

struct CameraViewSettings {
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 3> target{};
    std::array<float, 3> up{};
    float fovDegrees = 60;
};

struct WindowSettings {
    std::string title = "Genesis";
    int width = 1600;
    int height = 900;
    bool vsync = true;
};

extern RendererSettings g_renderer;
extern std::vector<ReflectionProbeSettings> g_reflectionProbes;
extern std::vector<PointLightSettings> g_pointLights;
extern std::vector<SpotLightSettings> g_spotLights;
extern std::vector<AreaLightSettings> g_areaLights;
extern LightSourceSettings g_lightSources;
extern EnvironmentSettings g_environment;
extern std::filesystem::path g_model;
extern CameraSettings g_camera;
extern std::vector<CameraViewSettings> g_cameraViews;
extern WindowSettings g_window;

}
