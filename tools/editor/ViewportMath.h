#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace genesis::editor {
using Vec3 = std::array<float,3>;
struct Bounds {
    Vec3 minimum{INFINITY,INFINITY,INFINITY}, maximum{-INFINITY,-INFINITY,-INFINITY};
    void include(const Vec3& point) { for (int i=0;i<3;++i) { minimum[i]=std::min(minimum[i],point[i]); maximum[i]=std::max(maximum[i],point[i]); } }
    bool valid() const { return std::isfinite(minimum[0]) && minimum[0]<=maximum[0] && minimum[1]<=maximum[1] && minimum[2]<=maximum[2]; }
    Vec3 center() const { return {(minimum[0]+maximum[0])*.5f,(minimum[1]+maximum[1])*.5f,(minimum[2]+maximum[2])*.5f}; }
    float radius() const { float sum=0; for(int i=0;i<3;++i) sum+=(maximum[i]-minimum[i])*(maximum[i]-minimum[i]); return std::sqrt(sum)*.5f; }
};
struct Ray { Vec3 origin, direction; };
inline Ray cameraRay(Vec3 position, float yaw, float pitch, float fovDegrees, float aspect, float x, float y) {
    const float sx=(2*x-1)*aspect*std::tan(fovDegrees*.00872664626f);
    const float sy=(1-2*y)*std::tan(fovDegrees*.00872664626f);
    const Vec3 forward{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
    const Vec3 right{std::cos(yaw),0,-std::sin(yaw)};
    const Vec3 up{-std::sin(yaw)*std::sin(pitch),std::cos(pitch),-std::cos(yaw)*std::sin(pitch)};
    Ray ray{position,{}}; float length=0;
    for (int i=0;i<3;++i) { ray.direction[i]=forward[i]+right[i]*sx+up[i]*sy; length+=ray.direction[i]*ray.direction[i]; }
    for (auto& v:ray.direction) v/=std::sqrt(length);
    return ray;
}
inline float intersect(const Ray& ray, const Bounds& bounds) {
    if (!bounds.valid()) return INFINITY;
    float near=0,far=INFINITY;
    for (int i=0;i<3;++i) {
        if (std::abs(ray.direction[i])<1e-7f) {
            if (ray.origin[i]<bounds.minimum[i] || ray.origin[i]>bounds.maximum[i]) return INFINITY;
        } else {
            float a=(bounds.minimum[i]-ray.origin[i])/ray.direction[i], b=(bounds.maximum[i]-ray.origin[i])/ray.direction[i];
            if (a>b) std::swap(a,b);
            near=std::max(near,a); far=std::min(far,b);
            if (near>far) return INFINITY;
        }
    }
    return near;
}
}
