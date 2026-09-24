#pragma once
#include "GameplayWorld.h"
#include "RenderScene.h"
#include <cmath>

namespace genesis::render {
// Script lights retain their slots. Authored entities fill the remaining
// shader slots in entity order; disabled components consume no slot.
struct AuthoredLighting {
    std::vector<PointLightSettings> points;
    std::vector<SpotLightSettings> spots;
    std::vector<AreaLightSettings> areas;
    std::vector<ReflectionProbeSettings> probes;
    size_t omitted=0;
    AuthoredLighting withEntities(const std::vector<gameplay::EntitySnapshot>& entities) const {
        auto out=*this;out.omitted=0;
        if(out.points.size()>4)out.points.resize(4);
        if(out.spots.size()>4)out.spots.resize(4);
        if(out.areas.size()>2)out.areas.resize(2);
        for(const auto& e:entities) {
            if(e.light && e.light->enabled) {
                const auto& l=*e.light;
                constexpr float rad=.01745329251994f;
                const float sx=std::sin(e.transform.rotation[0]*rad),cx=std::cos(e.transform.rotation[0]*rad);
                const float sy=std::sin(e.transform.rotation[1]*rad),cy=std::cos(e.transform.rotation[1]*rad);
                const float sz=std::sin(e.transform.rotation[2]*rad),cz=std::cos(e.transform.rotation[2]*rad);
                const std::array<float,3> direction{-cy*sz,-cx*cz-sx*sy*sz,cz*sx-cx*sy*sz},up{-sy,cy*sx,cx*cy};
                auto common=[&](auto& value){value.name=e.name;value.position=e.transform.position;value.color=l.color;value.intensity=l.intensity;value.radius=l.range;value.castsShadows=l.shadows;value.shadowBias=l.bias;};
                if(l.type==gameplay::Light::Type::Point && out.points.size()<4) {
                    PointLightSettings v;common(v);out.points.push_back(v);
                } else if(l.type==gameplay::Light::Type::Spot && out.spots.size()<4) {
                    SpotLightSettings v;common(v);v.direction=direction;v.up=up;v.innerAngle=l.innerAngle;v.outerAngle=l.outerAngle;out.spots.push_back(v);
                } else if(l.type==gameplay::Light::Type::Area && out.areas.size()<2) {
                    AreaLightSettings v;common(v);v.direction=direction;v.up=up;v.width=l.width;v.height=l.height;out.areas.push_back(v);
                } else ++out.omitted;
            }
            if(e.probe && e.probe->enabled) {
                const auto& p=*e.probe;ReflectionProbeSettings v;v.name=e.name;v.position=e.transform.position;v.blendDistance=p.blend;v.priority=p.priority;
                for(int i=0;i<3;++i){const float half=p.size[i]*std::abs(e.transform.scale[i])*.5f;v.boundsMin[i]=v.position[i]-half;v.boundsMax[i]=v.position[i]+half;}
                out.probes.push_back(v);
            }
        }
        return out;
    }
};
}
