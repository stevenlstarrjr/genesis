#pragma once
#include "GameplayWorld.h"
#include <json/json.h>
#include <cmath>
#include <stdexcept>

namespace genesis::components {
inline float number(const Json::Value& node,const char* key,float fallback,float minimum,float maximum) {
    if(!node.isMember(key))return fallback;
    const auto& value=node[key];
    if(!value.isNumeric() || !std::isfinite(value.asFloat()) || value.asDouble()<minimum || value.asDouble()>maximum)
        throw std::runtime_error(std::string("Invalid component property: ")+key);
    return value.asFloat();
}
inline bool boolean(const Json::Value& node,const char* key,bool fallback) {
    if(!node.isMember(key))return fallback;
    if(!node[key].isBool())throw std::runtime_error(std::string(key)+" must be boolean");
    return node[key].asBool();
}
inline std::array<float,3> vector(const Json::Value& node,const char* key,std::array<float,3> fallback,float minimum,float maximum) {
    if(!node.isMember(key))return fallback;
    const auto& value=node[key];if(!value.isArray() || value.size()!=3)throw std::runtime_error(std::string(key)+" must have three numbers");
    for(int i=0;i<3;++i) {
        if(!value[i].isNumeric() || !std::isfinite(value[i].asFloat()) || value[i].asDouble()<minimum || value[i].asDouble()>maximum)
            throw std::runtime_error(std::string("Invalid ")+key);
        fallback[i]=value[i].asFloat();
    }
    return fallback;
}
inline Json::Value array(std::array<float,3> v) { Json::Value out(Json::arrayValue);for(float c:v)out.append(c);return out; }
inline Json::Value array(std::array<float,4> v) { Json::Value out(Json::arrayValue);for(float c:v)out.append(c);return out; }
inline Json::Value array(std::array<int,3> v) { Json::Value out(Json::arrayValue);for(int c:v)out.append(c);return out; }
inline std::array<float,4> color4(const Json::Value& node,const char* key,std::array<float,4> fallback) {
    if(!node.isMember(key))return fallback;
    const auto& value=node[key];if(!value.isArray() || value.size()!=4)throw std::runtime_error(std::string(key)+" must have four numbers");
    for(int i=0;i<4;++i){
        if(!value[i].isNumeric() || !std::isfinite(value[i].asFloat()) || value[i].asDouble()<0 || value[i].asDouble()>1)
            throw std::runtime_error(std::string("Invalid ")+key);
        fallback[i]=value[i].asFloat();
    }
    return fallback;
}
inline std::vector<gameplay::ParticleSystem::Layer> legacyCampfireLayers() {
    using Layer=gameplay::ParticleSystem::Layer;
    Layer flame;flame.name="Flame";flame.count=58;flame.lifetime=.85f;flame.spawnRadius=.30f;
    flame.offset={0,.05f,0};flame.velocity={0,1.7f,0};flame.radialSpeed=-.12f;flame.turbulence=.19f;
    flame.size=.13f;flame.sizeGrowth=.05f;flame.fadeIn=.125f;
    flame.startColor={1.f,.75f,.12f,.8f};flame.endColor={1.f,.48f,.04f,0.f};
    Layer smoke;smoke.name="Smoke";smoke.count=29;smoke.lifetime=2.5f;smoke.spawnRadius=.53f;
    smoke.offset={0,.8f,0};smoke.velocity={0,.84f,0};smoke.radialSpeed=.18f;smoke.turbulence=.19f;
    smoke.size=.11f;smoke.sizeGrowth=.25f;smoke.startColor={.24f,.24f,.25f,.27f};smoke.endColor={.12f,.12f,.13f,0.f};
    Layer spark;spark.name="Sparks";spark.shape=Layer::Shape::Spark;spark.count=9;spark.lifetime=1.35f;
    spark.spawnRadius=.38f;spark.offset={0,.25f,0};spark.velocity={0,1.6f,0};spark.turbulence=.19f;
    spark.size=.032f;spark.startColor={1.f,.85f,.34f,.95f};spark.endColor={1.f,.32f,.03f,0.f};
    Layer glow;glow.name="Glow";glow.count=1;glow.spawnRadius=0;glow.velocity={0,0,0};glow.turbulence=0;
    glow.size=.307f;glow.startColor={1.f,.55f,.08f,.48f};glow.endColor=glow.startColor;
    return {smoke,flame,spark,glow};
}
inline const char* typeName(gameplay::Light::Type type) {
    return type==gameplay::Light::Type::Spot?"spot":type==gameplay::Light::Type::Area?"area":"point";
}
inline Json::Value serialize(const gameplay::EntitySnapshot& entity) {
    Json::Value node(Json::objectValue);
    if(entity.renderable && entity.renderable->material) {
        const auto& m=*entity.renderable->material;auto& value=node["material"];
        value["color"]=array(m.color);value["metallic"]=m.metallic;value["roughness"]=m.roughness;
    }
    if(entity.renderable && entity.renderable->particleKinematic)
        node["particle_collider"]["moving"]=true;
    if(entity.renderable && !entity.renderable->meshVertices.empty()) {
        auto& vertices=node["mesh_vertices"];vertices=Json::arrayValue;
        for(const auto& edit:entity.renderable->meshVertices) {
            Json::Value item(Json::objectValue);item["index"]=edit.index;
            item["position"]=array(edit.position);vertices.append(item);
        }
    }
    if(entity.light) {
        const auto& l=*entity.light;auto& value=node["light"];
        value["type"]=typeName(l.type);value["enabled"]=l.enabled;value["shadows"]=l.shadows;
        value["color"]=array(l.color);value["intensity"]=l.intensity;value["range"]=l.range;value["bias"]=l.bias;
        value["inner_angle"]=l.innerAngle;value["outer_angle"]=l.outerAngle;value["width"]=l.width;value["height"]=l.height;
    }
    if(entity.probe) {
        const auto& p=*entity.probe;auto& value=node["reflection_probe"];
        value["enabled"]=p.enabled;value["size"]=array(p.size);value["blend"]=p.blend;value["priority"]=p.priority;
    }
    if(entity.particles) {
        const auto& p=*entity.particles;auto& value=node["particle_system"];
        value["mode"]=p.mode==gameplay::ParticleSystem::Mode::Fluid?"fluid":p.mode==gameplay::ParticleSystem::Mode::Cloth?"cloth":p.mode==gameplay::ParticleSystem::Mode::Granular?"granular":p.mode==gameplay::ParticleSystem::Mode::Emitter?"emitter":"explosion";
        value["enabled"]=p.enabled;value["dimensions"]=array(p.dimensions);
        value["spacing"]=p.spacing;value["mass"]=p.mass;value["friction"]=p.friction;
        value["damping"]=p.damping;value["viscosity"]=p.viscosity;value["cohesion"]=p.cohesion;value["stiffness"]=p.stiffness;
        value["pin_edge"]=p.pinEdge==gameplay::ParticleSystem::PinEdge::Left?"left":p.pinEdge==gameplay::ParticleSystem::PinEdge::Right?"right":p.pinEdge==gameplay::ParticleSystem::PinEdge::Both?"both":"none";
        value["burst_speed"]=p.burstSpeed;value["blast_radius"]=p.blastRadius;
        value["blast_impulse"]=p.blastImpulse;value["effect_duration"]=p.effectDuration;
        if(p.mode==gameplay::ParticleSystem::Mode::Emitter){
            auto& layers=value["layers"];layers=Json::arrayValue;
            for(const auto& layer:p.layers){
                Json::Value entry(Json::objectValue);entry["name"]=layer.name;
                entry["shape"]=layer.shape==gameplay::ParticleSystem::Layer::Shape::Spark?"spark":"soft";
                entry["count"]=layer.count;entry["lifetime"]=layer.lifetime;entry["spawn_radius"]=layer.spawnRadius;
                entry["offset"]=array(layer.offset);entry["velocity"]=array(layer.velocity);
                entry["acceleration"]=array(layer.acceleration);entry["radial_speed"]=layer.radialSpeed;
                entry["turbulence"]=layer.turbulence;entry["size"]=layer.size;entry["size_growth"]=layer.sizeGrowth;
                entry["fade_in"]=layer.fadeIn;
                entry["start_color"]=array(layer.startColor);entry["end_color"]=array(layer.endColor);
                layers.append(entry);
            }
        }
    }
    return node;
}
inline void deserialize(const Json::Value& node,gameplay::EntitySnapshot& entity) {
    for(const char* key:{"material","light","reflection_probe","particle_system","particle_collider"})if(node.isMember(key) && !node[key].isObject())throw std::runtime_error(std::string(key)+" must be an object");
    if(node.isMember("particle_collider")){
        if(!entity.renderable)throw std::runtime_error("Particle collider requires a mesh renderer");
        entity.renderable->particleKinematic=boolean(node["particle_collider"],"moving",false);
    }
    if(node.isMember("mesh_vertices")) {
        if(!entity.renderable || !node["mesh_vertices"].isArray() || node["mesh_vertices"].size()>100000)
            throw std::runtime_error("Invalid edited mesh vertices");
        auto& edits=entity.renderable->meshVertices;
        uint32_t previous=0;bool first=true;
        for(const auto& item:node["mesh_vertices"]) {
            if(!item.isObject() || !item["index"].isUInt() || !item.isMember("position"))
                throw std::runtime_error("Invalid edited mesh vertex entry");
            gameplay::Renderable::MeshVertexEdit edit;
            edit.index=item["index"].asUInt();
            if(!first && edit.index<=previous)throw std::runtime_error("Edited mesh vertices must be sorted and unique");
            edit.position=vector(item,"position",{},-1000000,1000000);
            edits.push_back(edit);previous=edit.index;first=false;
        }
    }
    if(node.isMember("material")) {
        if(!entity.renderable)throw std::runtime_error("Material requires a mesh renderer");
        const auto& v=node["material"];gameplay::Renderable::Material m;
        m.color=vector(v,"color",m.color,0,1);m.metallic=number(v,"metallic",m.metallic,0,1);m.roughness=number(v,"roughness",m.roughness,.04f,1);
        entity.renderable->material=m;
    }
    if(node.isMember("light")) {
        const auto& v=node["light"];gameplay::Light l;
        if(v.isMember("type") && !v["type"].isString())throw std::runtime_error("Light type must be text");
        const auto type=v.get("type","point").asString();
        if(type!="point" && type!="spot" && type!="area")throw std::runtime_error("Unknown light type");
        l.type=type=="spot"?gameplay::Light::Type::Spot:type=="area"?gameplay::Light::Type::Area:gameplay::Light::Type::Point;
        l.enabled=boolean(v,"enabled",true);l.shadows=boolean(v,"shadows",true);l.color=vector(v,"color",l.color,0,1);
        l.intensity=number(v,"intensity",l.intensity,0,100000);l.range=number(v,"range",l.range,.1f,10000);l.bias=number(v,"bias",l.bias,0,1);
        l.innerAngle=number(v,"inner_angle",l.innerAngle,0,89);l.outerAngle=number(v,"outer_angle",l.outerAngle,.1f,89);
        if(l.innerAngle>l.outerAngle)throw std::runtime_error("Inner angle must not exceed outer angle");
        l.width=number(v,"width",l.width,.01f,1000);l.height=number(v,"height",l.height,.01f,1000);entity.light=l;
    }
    if(node.isMember("reflection_probe")) {
        const auto& v=node["reflection_probe"];gameplay::ReflectionProbe p;
        p.enabled=boolean(v,"enabled",true);p.size=vector(v,"size",p.size,.1f,10000);p.blend=number(v,"blend",p.blend,0,10000);
        const auto priority=number(v,"priority",0,-1000,1000);
        if(priority!=std::floor(priority))throw std::runtime_error("Probe priority must be an integer");
        p.priority=int(priority);entity.probe=p;
    }
    if(node.isMember("particle_system")) {
        const auto& v=node["particle_system"];gameplay::ParticleSystem p;
        if(v.isMember("mode") && !v["mode"].isString())throw std::runtime_error("Particle mode must be text");
        const auto mode=v.get("mode","fluid").asString();
        if(mode!="fluid" && mode!="cloth" && mode!="granular" && mode!="explosion" && mode!="emitter" && mode!="campfire")throw std::runtime_error("Unknown particle mode");
        p.mode=mode=="cloth"?gameplay::ParticleSystem::Mode::Cloth:mode=="granular"?gameplay::ParticleSystem::Mode::Granular:mode=="explosion"?gameplay::ParticleSystem::Mode::Explosion:(mode=="emitter" || mode=="campfire")?gameplay::ParticleSystem::Mode::Emitter:gameplay::ParticleSystem::Mode::Fluid;
        if(v.isMember("pin_edge") && !v["pin_edge"].isString())throw std::runtime_error("Cloth pin edge must be text");
        const auto pin=v.get("pin_edge","none").asString();
        if(pin!="none" && pin!="left" && pin!="right" && pin!="both")throw std::runtime_error("Unknown cloth pin edge");
        p.pinEdge=pin=="left"?gameplay::ParticleSystem::PinEdge::Left:pin=="right"?gameplay::ParticleSystem::PinEdge::Right:pin=="both"?gameplay::ParticleSystem::PinEdge::Both:gameplay::ParticleSystem::PinEdge::None;
        p.enabled=boolean(v,"enabled",true);
        if(v.isMember("dimensions")) {
            const auto& d=v["dimensions"];
            if(!d.isArray() || d.size()!=3)throw std::runtime_error("Particle dimensions must have three integers");
            for(int i=0;i<3;++i){if(!d[i].isInt() || d[i].asInt()<1 || d[i].asInt()>128)throw std::runtime_error("Invalid particle dimension");p.dimensions[i]=d[i].asInt();}
        }
        if(p.mode==gameplay::ParticleSystem::Mode::Cloth && p.dimensions[1]!=1)throw std::runtime_error("Cloth must have one layer");
        if(p.mode==gameplay::ParticleSystem::Mode::Cloth && (p.dimensions[0]<2 || p.dimensions[2]<2))
            throw std::runtime_error("Cloth needs at least a 2 by 2 grid");
        if(p.dimensions[0]*p.dimensions[1]*p.dimensions[2]>65536)throw std::runtime_error("Particle count exceeds 65536");
        p.spacing=number(v,"spacing",p.spacing,.01f,2.f);p.mass=number(v,"mass",p.mass,.0001f,100.f);
        p.friction=number(v,"friction",p.friction,0,1);p.damping=number(v,"damping",p.damping,0,1);
        p.viscosity=number(v,"viscosity",p.viscosity,0,100);p.cohesion=number(v,"cohesion",p.cohesion,0,100);
        p.stiffness=number(v,"stiffness",p.stiffness,0,1000000);
        p.burstSpeed=number(v,"burst_speed",p.burstSpeed,.1f,100.f);
        p.blastRadius=number(v,"blast_radius",p.blastRadius,.1f,100.f);
        p.blastImpulse=number(v,"blast_impulse",p.blastImpulse,0.f,100.f);
        p.effectDuration=number(v,"effect_duration",p.effectDuration,.1f,30.f);
        if(p.mode==gameplay::ParticleSystem::Mode::Emitter){
            if(mode=="campfire" && !v.isMember("layers"))p.layers=legacyCampfireLayers();
            if(v.isMember("layers")){
                const auto& source=v["layers"];
                if(!source.isArray() || source.empty() || source.size()>8)throw std::runtime_error("Emitter needs 1 to 8 layers");
                p.layers.clear();int total=0;
                for(const auto& item:source){
                    if(!item.isObject())throw std::runtime_error("Emitter layer must be an object");
                    gameplay::ParticleSystem::Layer layer;
                    if(item.isMember("name") && !item["name"].isString())throw std::runtime_error("Emitter layer name must be text");
                    layer.name=item.get("name",layer.name).asString();
                    if(layer.name.empty() || layer.name.size()>64)throw std::runtime_error("Emitter layer name must be 1 to 64 characters");
                    if(item.isMember("shape") && !item["shape"].isString())throw std::runtime_error("Emitter shape must be text");
                    const auto shape=item.get("shape","soft").asString();
                    if(shape!="soft" && shape!="spark")throw std::runtime_error("Emitter shape must be soft or spark");
                    layer.shape=shape=="spark"?gameplay::ParticleSystem::Layer::Shape::Spark:gameplay::ParticleSystem::Layer::Shape::Soft;
                    if(item.isMember("count") && (!item["count"].isInt() || item["count"].asInt()<1 || item["count"].asInt()>4096))
                        throw std::runtime_error("Emitter count must be 1 to 4096");
                    layer.count=item.get("count",layer.count).asInt();total+=layer.count;
                    layer.lifetime=number(item,"lifetime",layer.lifetime,.05f,30.f);
                    layer.spawnRadius=number(item,"spawn_radius",layer.spawnRadius,0.f,10.f);
                    layer.offset=vector(item,"offset",layer.offset,-100.f,100.f);
                    layer.velocity=vector(item,"velocity",layer.velocity,-100.f,100.f);
                    layer.acceleration=vector(item,"acceleration",layer.acceleration,-100.f,100.f);
                    layer.radialSpeed=number(item,"radial_speed",layer.radialSpeed,-100.f,100.f);
                    layer.turbulence=number(item,"turbulence",layer.turbulence,0.f,100.f);
                    layer.size=number(item,"size",layer.size,.005f,10.f);
                    layer.sizeGrowth=number(item,"size_growth",layer.sizeGrowth,-10.f,10.f);
                    layer.fadeIn=number(item,"fade_in",layer.fadeIn,0.f,1.f);
                    layer.startColor=color4(item,"start_color",layer.startColor);
                    layer.endColor=color4(item,"end_color",layer.endColor);
                    p.layers.push_back(layer);
                }
                if(total>4096)throw std::runtime_error("Emitter has more than 4096 particles");
            }
        }
        entity.particles=p;
    }
}
inline bool valid(const gameplay::EntitySnapshot& entity) {
    try {auto copy=entity;deserialize(serialize(entity),copy);return true;}catch(const std::exception&){return false;}
}
}
