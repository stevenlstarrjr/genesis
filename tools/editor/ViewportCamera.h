#pragma once
#include "editor/ViewportMath.h"
#include "RenderScene.h"

namespace genesis::editor {
// Y-up, turntable-style editor navigation. No renderer or platform dependencies.
// The pivot is persistent: orbit/zoom preserve it, pan translates it, and frame
// selection establishes a new one. Camera motion never changes scene entities.
class ViewportCamera {
public:
    enum class Gesture { None, Orbit, Pan, Zoom };
    explicit ViewportCamera(const render::CameraSettings& settings = {}) {
        m_fov = std::clamp(settings.fovDegrees, 1.0f, 175.0f);
        m_pose.pivot = settings.target;
        Vec3 direction{}; float length = 0;
        for (int i=0;i<3;++i) { direction[i]=settings.target[i]-settings.position[i]; length+=direction[i]*direction[i]; }
        length=std::sqrt(length);
        if (length>1e-5f && std::isfinite(length)) {
            m_pose.yaw=std::atan2(direction[0],direction[2]);
            m_pose.pitch=std::clamp(std::asin(std::clamp(direction[1]/length,-1.0f,1.0f)),-pitchLimit,pitchLimit);
            m_pose.distance=std::clamp(length,minDistance,maxDistance);
        }
    }
    const Vec3& pivot() const { return m_pose.pivot; }
    float distance() const { return m_pose.distance; }
    float yaw() const { return m_pose.yaw; }
    float pitch() const { return m_pose.pitch; }
    float fov() const { return m_fov; }
    Vec3 position() const {
        const auto forward=direction(); Vec3 result{};
        for (int i=0;i<3;++i) result[i]=m_pose.pivot[i]-forward[i]*m_pose.distance;
        return result;
    }
    void begin(Gesture gesture) { m_before=m_pose; m_gesture=gesture; }
    bool dragging() const { return m_gesture!=Gesture::None; }
    void end() { m_gesture=Gesture::None; }
    void cancel() { if (dragging()) m_pose=m_before; end(); }
    void drag(float dx,float dy,float viewportHeight) {
        if (!std::isfinite(dx) || !std::isfinite(dy)) return;
        if (m_gesture==Gesture::Orbit) {
            m_pose.yaw=std::remainder(m_pose.yaw+dx*.006f,6.283185307f);
            m_pose.pitch=std::clamp(m_pose.pitch-dy*.006f,-pitchLimit,pitchLimit);
        } else if (m_gesture==Gesture::Pan) {
            const float units=2*m_pose.distance*std::tan(m_fov*.00872664626f)/std::max(viewportHeight,1.0f);
            const Vec3 right{std::cos(m_pose.yaw),0,-std::sin(m_pose.yaw)};
            const Vec3 up{-std::sin(m_pose.yaw)*std::sin(m_pose.pitch),std::cos(m_pose.pitch),-std::cos(m_pose.yaw)*std::sin(m_pose.pitch)};
            for (int i=0;i<3;++i) m_pose.pivot[i]+=(-right[i]*dx+up[i]*dy)*units;
        } else if (m_gesture==Gesture::Zoom) zoom(-dy*.01f);
    }
    // Positive wheel/zoom moves closer. Exponential scaling feels consistent at
    // every scene scale and cannot cross the pivot or produce a negative radius.
    void wheel(float steps) { zoom(steps*.15f); }
    void frame(const Bounds& bounds,float aspect) {
        if (!bounds.valid() || !std::isfinite(bounds.radius())) return;
        m_pose.pivot=bounds.center();
        const float halfFov=std::atan(std::tan(m_fov*.00872664626f)*std::clamp(aspect,.01f,1.0f));
        m_pose.distance=std::clamp(std::max(1.0f,bounds.radius()*1.25f/std::sin(halfFov)),minDistance,maxDistance);
    }
    // Migrate the former fly camera's unit-length look target without changing
    // its position or viewing direction. Use scene depth as the initial pivot.
    void setPivotDepth(float distance) {
        const auto eye=position(), forward=direction();
        m_pose.distance=std::clamp(distance,minDistance,maxDistance);
        for (int i=0;i<3;++i) m_pose.pivot[i]=eye[i]+forward[i]*m_pose.distance;
    }
private:
    static constexpr float minDistance=.1f,maxDistance=10000,pitchLimit=1.560796327f;
    struct Pose { Vec3 pivot{}; float yaw=0,pitch=0,distance=5; } m_pose,m_before;
    float m_fov=60;
    Gesture m_gesture=Gesture::None;
    Vec3 direction() const { return {std::cos(pitch())*std::sin(yaw()),std::sin(pitch()),std::cos(pitch())*std::cos(yaw())}; }
    void zoom(float amount) {
        if (std::isfinite(amount)) m_pose.distance=std::clamp(m_pose.distance*std::exp(std::clamp(-amount,-20.0f,20.0f)),minDistance,maxDistance);
    }
};
}
