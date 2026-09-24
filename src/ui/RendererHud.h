#pragma once

#include "ui/Toolkit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace genesis::ui {

struct RendererHudInfo {
    std::string backend;
    std::string quality;
    std::string debugView;
    int viewportWidth{};
    int viewportHeight{};
    double gpuMs{};
    double cpuMs{};
    double shadowMs{};
    double sceneMs{};
    double aoMs{};
    double fogMs{};
    double postMs{};
    uint32_t draws{};
    float camera[3]{};
    float yawDegrees{};
    float pitchDegrees{};
    float sunAzimuth{};
    float sunElevation{};
    bool automaticExposure{};
    float exposureEv{};
    bool cookieControls{};
};

using RendererHudSurface = Surface;

// Renderer-independent ThorVG HUD rasterizer. Backends only upload the returned
// straight-alpha pixels and composite them after tone mapping.
class RendererHud {
public:
    RendererHud();
    ~RendererHud();
    RendererHud(const RendererHud&) = delete;
    RendererHud& operator=(const RendererHud&) = delete;

    const RendererHudSurface& render(uint16_t width, const RendererHudInfo& info);
    const GpuDrawList& renderGpu(uint16_t width,const RendererHudInfo& info);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
