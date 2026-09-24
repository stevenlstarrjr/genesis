#include "ui/RendererHud.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace genesis::ui {
struct RendererHud::Impl {
    Document document;
    Node *display, *camera, *controls, *exposure;
    std::array<Node*,8> metrics{}, values{};
    Impl() {
        Style root; root.padding = Insets(12,10);
        document.root().setStyle(root);
        Style panel; panel.height = 98; panel.shrink = 0; panel.padding = Insets(14,8);
        panel.gap = 7; panel.radius = 13; panel.background = {12,16,25,235};
        panel.borderWidth = 1; panel.borderColor = {62,72,96,210};
        auto& content = document.root().column().setStyle(panel);
        auto& header = content.row(); auto row = header.style(); row.gap = 16; row.align = Align::Center;
        row.height = 23; row.shrink = 0; header.setStyle(row);
        Style brand; brand.font = Font::Semibold; brand.fontSize = 12; brand.shrink = 0;
        brand.textColor = {161,179,255,255}; header.label("GENESIS").setStyle(brand);
        Style detail; detail.font = Font::Medium; detail.fontSize = 9;
        detail.textColor = {157,171,200,255}; detail.grow = 1;
        display = &header.label("").setStyle(detail);
        auto& metricRow = content.row(); row = metricRow.style(); row.gap = 7; row.height = 28;
        row.shrink = 0; metricRow.setStyle(row);
        constexpr const char* labels[] = {"GPU", "CPU", "SCENE", "SHADOW", "AO", "FOG", "POST", "DRAWS"};
        for (size_t i = 0; i < metrics.size(); ++i) {
            Style metric; metric.direction = Direction::Row; metric.width = 108; metric.shrink = 0;
            metric.align = Align::Center; metric.justify = Justify::SpaceBetween; metric.padding = Insets(9,3);
            metric.background = {25,31,45,232}; metric.borderColor = {50,59,79,180};
            metric.borderWidth = 1; metric.radius = 7;
            metrics[i] = &metricRow.row().setStyle(metric);
            Style label; label.font = Font::Medium; label.fontSize = 8;
            label.textColor = {126,138,168,255}; metrics[i]->label(labels[i]).setStyle(label);
            label.font = Font::Semibold; label.fontSize = 9;
            label.textColor = i == 0 ? Color{108,222,180,255} : Color{210,216,232,255};
            values[i] = &metrics[i]->label("0.00").setStyle(label);
        }
        auto& footer = content.row(); row = footer.style(); row.gap = 12; row.height = 16;
        row.shrink = 0; row.align = Align::Center; footer.setStyle(row);
        detail.fontSize = 8; detail.grow = 1;
        camera = &footer.label("").setStyle(detail);
        controls = &footer.label("").setStyle(detail);
        detail.grow = 0; detail.shrink = 0; detail.font = Font::Semibold;
        detail.textColor = {126,226,177,255}; exposure = &footer.label("").setStyle(detail);
    }
    void update(uint16_t width,const RendererHudInfo& info){
    char text[256];
    std::snprintf(text,sizeof(text),"%s  /  %s  /  %d x %d  /  VIEW %s",
        info.backend.c_str(),info.quality.c_str(),info.viewportWidth,info.viewportHeight,info.debugView.c_str());
    display->setText(text);
    const double timings[] = {info.gpuMs,info.cpuMs,info.sceneMs,info.shadowMs,info.aoMs,info.fogMs,info.postMs};
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (i < 7) std::snprintf(text,sizeof(text),"%.2f",timings[i]);
        else std::snprintf(text,sizeof(text),"%u",info.draws);
        values[i]->setText(text);
        metrics[i]->setVisible(54 + (i+1)*115 <= width);
    }
    std::snprintf(text,sizeof(text),"CAM %.2f  %.2f  %.2f   LOOK %.1f / %.1f   SUN %.1f / %.1f",
        info.camera[0],info.camera[1],info.camera[2],info.yawDegrees,info.pitchDegrees,info.sunAzimuth,info.sunElevation);
    camera->setText(text);
    controls->setVisible(width >= 1100).setText(info.cookieControls
        ? "RMB look  /  Ctrl+LMB light  /  WASD move  /  R reset"
        : "RMB look  /  Ctrl+LMB sun  /  WASD move  /  F12 capture");
    std::snprintf(text,sizeof(text),"%s  %.1f EV",info.automaticExposure ? "AUTO" : "MANUAL",info.exposureEv);
    exposure->setText(text);
    }
};
RendererHud::RendererHud() : m_impl(std::make_unique<Impl>()) {}
RendererHud::~RendererHud() = default;
const RendererHudSurface& RendererHud::render(uint16_t width, const RendererHudInfo& info) {
    m_impl->update(width,info);
    return m_impl->document.render(float(std::max<uint16_t>(width,1)),118);
}
const GpuDrawList& RendererHud::renderGpu(uint16_t width,const RendererHudInfo& info){
    m_impl->update(width,info);
    return m_impl->document.renderGpu(float(std::max<uint16_t>(width,1)),118);
}
}
