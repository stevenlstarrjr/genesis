#include "RenderScene.h"

namespace genesis::render {

RendererSettings g_renderer;
std::vector<ReflectionProbeSettings> g_reflectionProbes;
std::vector<PointLightSettings> g_pointLights;
std::vector<SpotLightSettings> g_spotLights;
std::vector<AreaLightSettings> g_areaLights;
LightSourceSettings g_lightSources;
EnvironmentSettings g_environment;
std::filesystem::path g_model;
CameraSettings g_camera;
std::vector<CameraViewSettings> g_cameraViews;
WindowSettings g_window;

}
