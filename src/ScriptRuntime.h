#pragma once
#include <filesystem>
#include <vector>
#include "RenderScene.h"

namespace genesis::scripting {
using render::AreaLightSettings;
using render::CameraSettings;
using render::CameraViewSettings;
using render::LightSourceSettings;
using render::PointLightSettings;
using render::ReflectionProbeSettings;
using render::RendererSettings;
using render::SpotLightSettings;
using render::WindowSettings;
using render::g_areaLights;
using render::g_camera;
using render::g_cameraViews;
using render::g_environment;
using render::g_lightSources;
using render::g_model;
using render::g_pointLights;
using render::g_reflectionProbes;
using render::g_renderer;
using render::g_spotLights;
using render::g_window;

// One embedded PocketPy VM per application. Scripts configure startup;
// they do not run inside a render-thread callback.
class ScriptRuntime {
public:
    ScriptRuntime();
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    bool run(const std::filesystem::path& path);
    bool run(const std::vector<std::filesystem::path>& paths);
};
}
