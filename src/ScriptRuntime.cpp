#include "ScriptRuntime.h"
#include "SceneComponents.h"
#include "GameplayWorld.h"
#include <json/json.h>
#include <pocketpy.h>
#include <genesis/atmosphere/BrunetonAtmosphere.h>
#include <genesis/atmosphere/Clouds.h>
#include <genesis/color/ColorPipeline.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>
#include <cstdint>
#include <cctype>
#include <cstdio>

namespace genesis::scripting {
namespace {
std::filesystem::path scriptDirectory;

std::filesystem::path resolveAssetPath(const std::filesystem::path& value) {
    if (value.is_absolute()) return value;
    const auto local = scriptDirectory / value;
    if (std::filesystem::exists(local)) return local;
    return std::filesystem::path(GENESIS_ROOT) / value;
}

char* pythonImportFile(const char* name, int* dataSize) {
    const std::filesystem::path roots[] = {
        scriptDirectory, std::filesystem::path(GENESIS_ROOT) / "python",
        std::filesystem::path(GENESIS_ROOT)
    };
    for (const auto& root : roots) {
        std::ifstream input(root / std::filesystem::u8path(name), std::ios::binary | std::ios::ate);
        if (!input) continue;
        const auto length = input.tellg();
        if (length < 0 || length > std::numeric_limits<int>::max()) return nullptr;
        auto* data = static_cast<char*>(PK_MALLOC(size_t(length) + 1));
        if (!data) return nullptr;
        input.seekg(0);
        if (!input.read(data, length)) { PK_FREE(data); return nullptr; }
        data[size_t(length)] = '\0';
        if (dataSize) *dataSize = int(length);
        return data;
    }
    return nullptr;
}

bool pythonLoadModel(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    PY_CHECK_ARG_TYPE(0, tp_str);
    auto path = std::filesystem::u8path(py_tostr(py_arg(0)));
    if (path.is_relative()) path = resolveAssetPath(path);
    if (!g_model.empty()) return ValueError("only one scene model is supported per run");
    if (!std::filesystem::is_regular_file(path))
        return ValueError("model file does not exist: %s", path.string().c_str());
    g_model = std::filesystem::absolute(path);
    py_newnone(py_retval());
    return true;
}

bool readFloatArg(py_Ref argv, int index, float& value) {
    return py_castfloat32(&argv[index], &value);
}

bool readOptionalAssetPath(py_Ref argv, int index, const char* suffix,
    const char* label, std::string& result) {
    if (!py_checkstr(py_arg(index))) return TypeError("%s path must be a string", label);
    const std::string raw = py_tostr(py_arg(index));
    if (raw.empty()) { result.clear(); return true; }
    auto path = std::filesystem::u8path(raw);
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    if (suffix && extension != suffix)
        return ValueError("%s path must end in %s", label, suffix);
    if (path.is_relative()) path = resolveAssetPath(path);
    if (!std::filesystem::is_regular_file(path))
        return ValueError("%s file does not exist: %s", label, path.string().c_str());
    result = std::filesystem::absolute(path).generic_string();
    return true;
}

bool readEntityId(py_Ref argv, int index, gameplay::EntityId& value) {
    if (!py_checkint(py_arg(index))) return false;
    const auto raw = py_toint(py_arg(index));
    if (raw < 0 || static_cast<std::uint64_t>(raw) > std::numeric_limits<gameplay::EntityId>::max())
        return ValueError("entity id is out of range");
    value = static_cast<gameplay::EntityId>(raw);
    return true;
}

Json::Value entityJson(const gameplay::EntitySnapshot& entity) {
    Json::Value result;
    result["id"] = Json::UInt(entity.id);
    result["name"] = entity.name;
    auto& transform = result["transform"];
    for (const auto value : entity.transform.position) transform["position"].append(value);
    for (const auto value : entity.transform.rotation) transform["rotation"].append(value);
    for (const auto value : entity.transform.scale) transform["scale"].append(value);
    for (const auto& tag : entity.tags) result["tags"].append(tag);
    if (entity.tags.empty()) result["tags"] = Json::arrayValue;
    if (entity.renderable) result["model"] = entity.renderable->path;
    if (entity.renderable && entity.renderable->particleKinematic) result["particle_collider"]["moving"] = true;
    if(entity.particles)result["particle_system"]=components::serialize(entity)["particle_system"];
    return result;
}

std::string compactJson(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

bool pythonEntityCreate(int argc, py_Ref argv) {
    PY_CHECK_ARGC(10);
    PY_CHECK_ARG_TYPE(0, tp_str);
    gameplay::Transform transform;
    for (int i = 0; i < 3; ++i) {
        if (!readFloatArg(argv, i + 1, transform.position[i]) ||
            !readFloatArg(argv, i + 4, transform.rotation[i]) ||
            !readFloatArg(argv, i + 7, transform.scale[i])) return false;
        if (!std::isfinite(transform.position[i]) || !std::isfinite(transform.rotation[i]) ||
            !std::isfinite(transform.scale[i])) return ValueError("entity transform must be finite");
    }
    py_newint(py_retval(), gameplay::g_world.create(py_tostr(py_arg(0)), transform));
    return true;
}

bool pythonEntityDestroy(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id)) return false;
    py_newbool(py_retval(), gameplay::g_world.destroy(id));
    return true;
}

bool pythonEntityValid(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id)) return false;
    py_newbool(py_retval(), gameplay::g_world.valid(id));
    return true;
}

bool pythonEntityRename(int argc, py_Ref argv) {
    PY_CHECK_ARGC(2);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id) || !py_checkstr(py_arg(1))) return false;
    if (!gameplay::g_world.setName(id, py_tostr(py_arg(1))))
        return ValueError("entity handle is not valid");
    py_newnone(py_retval());
    return true;
}

bool pythonEntityTransform(int argc, py_Ref argv) {
    PY_CHECK_ARGC(10);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id)) return false;
    gameplay::Transform transform;
    for (int i = 0; i < 3; ++i) {
        if (!readFloatArg(argv, i + 1, transform.position[i]) ||
            !readFloatArg(argv, i + 4, transform.rotation[i]) ||
            !readFloatArg(argv, i + 7, transform.scale[i])) return false;
        if (!std::isfinite(transform.position[i]) || !std::isfinite(transform.rotation[i]) ||
            !std::isfinite(transform.scale[i])) return ValueError("entity transform must be finite");
    }
    if (!gameplay::g_world.setTransform(id, transform))
        return ValueError("entity handle is not valid");
    py_newnone(py_retval());
    return true;
}

bool pythonEntityTag(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id) || !py_checkstr(py_arg(1)) || !py_checkbool(py_arg(2))) return false;
    const std::string tag = py_tostr(py_arg(1));
    if (tag.empty()) return ValueError("entity tag must be non-empty");
    if (!gameplay::g_world.setTag(id, tag, py_tobool(py_arg(2))))
        return ValueError("entity handle is not valid");
    py_newnone(py_retval());
    return true;
}

bool pythonEntityModel(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id) || !py_checkstr(py_arg(1)) || !py_checkbool(py_arg(2))) return false;
    auto path = std::filesystem::u8path(py_tostr(py_arg(1)));
    if (path.is_relative()) path = resolveAssetPath(path);
    const auto suffix = path.extension().string();
    if (suffix != ".gltf" && suffix != ".glb")
        return ValueError("entity model must be a .gltf or .glb file");
    if (!std::filesystem::is_regular_file(path))
        return ValueError("entity model file does not exist: %s", path.string().c_str());
    if (!gameplay::g_world.setRenderable(id, std::filesystem::absolute(path).generic_string()))
        return ValueError("entity handle is not valid");
    gameplay::g_world.setParticleKinematic(id,py_tobool(py_arg(2)));
    py_newnone(py_retval());
    return true;
}

bool pythonEntityParticles(int argc,py_Ref argv) {
    PY_CHECK_ARGC(2);
    gameplay::EntityId id;
    if(!readEntityId(argv,0,id) || !py_checkstr(py_arg(1)))return false;
    gameplay::EntitySnapshot snapshot;
    if(!gameplay::g_world.snapshot(id,snapshot))return ValueError("entity handle is not valid");
    const std::string json=py_tostr(py_arg(1));
    if(json=="null"){
        gameplay::g_world.setParticleSystem(id,std::nullopt);py_newnone(py_retval());return true;
    }
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;std::string errors;
    if(!reader->parse(json.data(),json.data()+json.size(),&value,&errors) || !value.isObject())
        return ValueError("particle system must be a JSON object");
    try {
        Json::Value node;node["particle_system"]=value;
        components::deserialize(node,snapshot);
    }catch(const std::exception& exception){return ValueError("%s",exception.what());}
    gameplay::g_world.setParticleSystem(id,snapshot.particles);
    py_newnone(py_retval());return true;
}

bool pythonEntitySnapshot(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    gameplay::EntityId id;
    if (!readEntityId(argv, 0, id)) return false;
    gameplay::EntitySnapshot snapshot;
    if (!gameplay::g_world.snapshot(id, snapshot)) return ValueError("entity handle is not valid");
    const auto json = compactJson(entityJson(snapshot));
    py_newstr(py_retval(), json.c_str());
    return true;
}

bool pythonEntitiesSnapshot(int argc, py_Ref argv) {
    PY_CHECK_ARGC(0);
    Json::Value result(Json::arrayValue);
    for (const auto& entity : gameplay::g_world.snapshots()) result.append(entityJson(entity));
    const auto json = compactJson(result);
    py_newstr(py_retval(), json.c_str());
    return true;
}

bool pythonReflectionProbeAdd(int argc, py_Ref argv) {
    PY_CHECK_ARGC(12);
    PY_CHECK_ARG_TYPE(0, tp_str);
    if (!py_checkint(py_arg(11))) return TypeError("probe priority must be an integer");
    if (g_reflectionProbes.size() >= 16) return ValueError("at most 16 reflection probes are supported");
    ReflectionProbeSettings probe;
    probe.name = py_tostr(py_arg(0));
    if (probe.name.empty()) return ValueError("probe name must be non-empty");
    for (int axis = 0; axis < 3; ++axis) {
        if (!readFloatArg(argv, 1 + axis, probe.position[axis]) ||
            !readFloatArg(argv, 4 + axis, probe.boundsMin[axis]) ||
            !readFloatArg(argv, 7 + axis, probe.boundsMax[axis])) return false;
        if (!std::isfinite(probe.position[axis]) || !std::isfinite(probe.boundsMin[axis]) ||
            !std::isfinite(probe.boundsMax[axis])) return ValueError("probe values must be finite");
        if (probe.boundsMin[axis] >= probe.boundsMax[axis])
            return ValueError("probe bounds_min must be less than bounds_max on every axis");
        if (probe.position[axis] < probe.boundsMin[axis] || probe.position[axis] > probe.boundsMax[axis])
            return ValueError("probe position must be inside its bounds");
    }
    if (!readFloatArg(argv, 10, probe.blendDistance)) return false;
    if (!std::isfinite(probe.blendDistance) || probe.blendDistance <= 0.0f)
        return ValueError("probe blend_distance must be finite and greater than zero");
    float smallestHalfExtent = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
        smallestHalfExtent = std::min(smallestHalfExtent,
            (probe.boundsMax[axis] - probe.boundsMin[axis]) * 0.5f);
    if (probe.blendDistance > smallestHalfExtent)
        return ValueError("probe blend_distance cannot exceed the smallest half extent");
    probe.priority = static_cast<int>(py_toint(py_arg(11)));
    g_reflectionProbes.push_back(probe);
    py_newint(py_retval(), static_cast<py_i64>(g_reflectionProbes.size() - 1));
    return true;
}

bool pythonReflectionProbesClear(int argc, py_Ref argv) {
    PY_CHECK_ARGC(0);
    g_reflectionProbes.clear();
    py_newnone(py_retval());
    return true;
}

bool pythonPointLightAdd(int argc, py_Ref argv) {
    PY_CHECK_ARGC(12);
    PY_CHECK_ARG_TYPE(0, tp_str);
    if (g_pointLights.size() >= 4) return ValueError("at most 4 point lights are supported");
    PointLightSettings light;
    light.name = py_tostr(py_arg(0));
    if (light.name.empty()) return ValueError("point light name must be non-empty");
    for (const auto& existing : g_pointLights)
        if (existing.name == light.name) return ValueError("point light name must be unique");
    for (int axis = 0; axis < 3; ++axis) {
        if (!readFloatArg(argv, 1 + axis, light.position[axis]) ||
            !readFloatArg(argv, 4 + axis, light.color[axis])) return false;
        if (!std::isfinite(light.position[axis]) || !std::isfinite(light.color[axis]) ||
            light.color[axis] < 0.0f) return ValueError("point light position and color must be finite; color cannot be negative");
    }
    if (!readFloatArg(argv, 7, light.intensity) || !readFloatArg(argv, 8, light.radius) ||
        !readFloatArg(argv, 9, light.shadowBias)) return false;
    if (!std::isfinite(light.intensity) || light.intensity < 0.0f)
        return ValueError("point light intensity must be finite and non-negative");
    if (!std::isfinite(light.radius) || light.radius <= 0.0f)
        return ValueError("point light radius must be finite and greater than zero");
    if (!std::isfinite(light.shadowBias) || light.shadowBias < 0.0f || light.shadowBias > 1.0f)
        return ValueError("point light shadow_bias must be finite and between zero and one");
    if (!py_checkbool(py_arg(10))) return TypeError("point light casts_shadows must be a bool");
    light.castsShadows = py_tobool(py_arg(10));
    if (!readOptionalAssetPath(argv, 11, ".ies", "point light IES", light.iesProfile)) return false;
    g_pointLights.push_back(light);
    py_newint(py_retval(), static_cast<py_i64>(g_pointLights.size() - 1));
    return true;
}

bool pythonPointLightsClear(int argc, py_Ref argv) {
    PY_CHECK_ARGC(0);
    g_pointLights.clear();
    g_spotLights.clear();
    g_areaLights.clear();
    g_lightSources = {};
    py_newnone(py_retval());
    return true;
}

bool pythonLightSourcesConfigure(int argc, py_Ref argv) {
    PY_CHECK_ARGC(5);
    if (!py_checkbool(py_arg(0))) return TypeError("light source enabled must be a bool");
    LightSourceSettings next;
    next.enabled = py_tobool(py_arg(0));
    if (!readFloatArg(argv, 1, next.intensity) || !readFloatArg(argv, 2, next.pointSize) ||
        !readFloatArg(argv, 3, next.spotSize) || !readFloatArg(argv, 4, next.areaScale)) return false;
    if (!std::isfinite(next.intensity) || next.intensity < 0.0f || next.intensity > 1000000.0f)
        return ValueError("light source intensity must be finite and between zero and 1000000");
    if (!std::isfinite(next.pointSize) || next.pointSize <= 0.0f || next.pointSize > 100000.0f ||
        !std::isfinite(next.spotSize) || next.spotSize <= 0.0f || next.spotSize > 100000.0f ||
        !std::isfinite(next.areaScale) || next.areaScale <= 0.0f || next.areaScale > 1000.0f)
        return ValueError("light source sizes must be finite and greater than zero");
    g_lightSources = next;
    py_newnone(py_retval());
    return true;
}

bool pythonAreaLightAdd(int argc, py_Ref argv) {
    PY_CHECK_ARGC(19);
    PY_CHECK_ARG_TYPE(0, tp_str);
    if (g_areaLights.size() >= 2) return ValueError("at most 2 area lights are supported");
    AreaLightSettings light;
    light.name = py_tostr(py_arg(0));
    if (light.name.empty()) return ValueError("area light name must be non-empty");
    for (const auto& existing : g_areaLights)
        if (existing.name == light.name) return ValueError("area light name must be unique");
    float directionLengthSquared = 0.0f;
    float upLengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (!readFloatArg(argv, 1 + axis, light.position[axis]) ||
            !readFloatArg(argv, 4 + axis, light.direction[axis]) ||
            !readFloatArg(argv, 7 + axis, light.up[axis]) ||
            !readFloatArg(argv, 10 + axis, light.color[axis])) return false;
        if (!std::isfinite(light.position[axis]) || !std::isfinite(light.direction[axis]) ||
            !std::isfinite(light.up[axis]) || !std::isfinite(light.color[axis]) || light.color[axis] < 0.0f)
            return ValueError("area light vectors must be finite; color cannot be negative");
        directionLengthSquared += light.direction[axis] * light.direction[axis];
        upLengthSquared += light.up[axis] * light.up[axis];
    }
    if (directionLengthSquared <= 1e-8f) return ValueError("area light direction must be non-zero");
    if (upLengthSquared <= 1e-8f) return ValueError("area light up must be non-zero");
    const float inverseDirectionLength = 1.0f / std::sqrt(directionLengthSquared);
    for (float& component : light.direction) component *= inverseDirectionLength;
    float upProjection = 0.0f;
    for (int axis = 0; axis < 3; ++axis) upProjection += light.up[axis] * light.direction[axis];
    float orthogonalUpLengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        light.up[axis] -= light.direction[axis] * upProjection;
        orthogonalUpLengthSquared += light.up[axis] * light.up[axis];
    }
    if (orthogonalUpLengthSquared <= 1e-8f)
        return ValueError("area light up must not be parallel to direction");
    const float inverseUpLength = 1.0f / std::sqrt(orthogonalUpLengthSquared);
    for (float& component : light.up) component *= inverseUpLength;
    if (!readFloatArg(argv, 13, light.intensity) || !readFloatArg(argv, 14, light.radius) ||
        !readFloatArg(argv, 15, light.width) || !readFloatArg(argv, 16, light.height) ||
        !readFloatArg(argv, 17, light.shadowBias)) return false;
    PY_CHECK_ARG_TYPE(18, tp_bool);
    light.castsShadows = py_tobool(py_arg(18));
    if (!std::isfinite(light.intensity) || light.intensity < 0.0f)
        return ValueError("area light intensity must be finite and non-negative");
    if (!std::isfinite(light.radius) || light.radius <= 0.0f)
        return ValueError("area light radius must be finite and greater than zero");
    if (!std::isfinite(light.width) || light.width <= 0.0f ||
        !std::isfinite(light.height) || light.height <= 0.0f)
        return ValueError("area light width and height must be finite and greater than zero");
    if (!std::isfinite(light.shadowBias) || light.shadowBias < 0.0f)
        return ValueError("area light shadow bias must be finite and non-negative");
    g_areaLights.push_back(light);
    py_newint(py_retval(), static_cast<py_i64>(g_areaLights.size() - 1));
    return true;
}

bool pythonSpotLightAdd(int argc, py_Ref argv) {
    PY_CHECK_ARGC(21);
    PY_CHECK_ARG_TYPE(0, tp_str);
    if (g_spotLights.size() >= 4) return ValueError("at most 4 spot lights are supported");
    SpotLightSettings light;
    light.name = py_tostr(py_arg(0));
    if (light.name.empty()) return ValueError("spot light name must be non-empty");
    for (const auto& existing : g_spotLights)
        if (existing.name == light.name) return ValueError("spot light name must be unique");
    float directionLengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (!readFloatArg(argv, 1 + axis, light.position[axis]) ||
            !readFloatArg(argv, 4 + axis, light.direction[axis]) ||
            !readFloatArg(argv, 7 + axis, light.color[axis])) return false;
        if (!std::isfinite(light.position[axis]) || !std::isfinite(light.direction[axis]) ||
            !std::isfinite(light.color[axis]) || light.color[axis] < 0.0f)
            return ValueError("spot light vectors must be finite; color cannot be negative");
        directionLengthSquared += light.direction[axis] * light.direction[axis];
    }
    if (directionLengthSquared <= 1e-8f) return ValueError("spot light direction must be non-zero");
    const float inverseDirectionLength = 1.0f / std::sqrt(directionLengthSquared);
    for (float& component : light.direction) component *= inverseDirectionLength;
    if (!readFloatArg(argv, 10, light.intensity) || !readFloatArg(argv, 11, light.radius) ||
        !readFloatArg(argv, 12, light.innerAngle) || !readFloatArg(argv, 13, light.outerAngle) ||
        !readFloatArg(argv, 14, light.shadowBias)) return false;
    if (!std::isfinite(light.intensity) || light.intensity < 0.0f)
        return ValueError("spot light intensity must be finite and non-negative");
    if (!std::isfinite(light.radius) || light.radius <= 0.0f)
        return ValueError("spot light radius must be finite and greater than zero");
    if (!std::isfinite(light.innerAngle) || !std::isfinite(light.outerAngle) ||
        light.innerAngle <= 0.0f || light.innerAngle > light.outerAngle || light.outerAngle >= 90.0f)
        return ValueError("spot light angles must satisfy 0 < inner_angle <= outer_angle < 90");
    if (!std::isfinite(light.shadowBias) || light.shadowBias < 0.0f || light.shadowBias > 1.0f)
        return ValueError("spot light shadow_bias must be finite and between zero and one");
    if (!py_checkbool(py_arg(15))) return TypeError("spot light casts_shadows must be a bool");
    light.castsShadows = py_tobool(py_arg(15));
    float upProjection = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (!readFloatArg(argv, 16 + axis, light.up[axis]) || !std::isfinite(light.up[axis]))
            return ValueError("spot light up must be finite");
        upProjection += light.up[axis] * light.direction[axis];
    }
    float upLengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        light.up[axis] -= light.direction[axis] * upProjection;
        upLengthSquared += light.up[axis] * light.up[axis];
    }
    if (upLengthSquared <= 1e-8f) return ValueError("spot light up must not be parallel to direction");
    const float inverseUpLength = 1.0f / std::sqrt(upLengthSquared);
    for (float& component : light.up) component *= inverseUpLength;
    if (!readOptionalAssetPath(argv, 19, nullptr, "spot light cookie", light.cookieTexture) ||
        !readOptionalAssetPath(argv, 20, ".ies", "spot light IES", light.iesProfile)) return false;
    g_spotLights.push_back(light);
    py_newint(py_retval(), static_cast<py_i64>(g_spotLights.size() - 1));
    return true;
}

bool pythonAtmosphereEarth(int argc, py_Ref argv) {
    PY_CHECK_ARGC(0);
    genesis::atmosphere::resetEarth();
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereSun(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    float azimuth, elevation, intensity;
    if (!readFloatArg(argv, 0, azimuth) || !readFloatArg(argv, 1, elevation) || !readFloatArg(argv, 2, intensity)) return false;
    genesis::atmosphere::setSun(azimuth, elevation);
    genesis::atmosphere::settings().sunIntensity = std::max(0.0f, intensity);
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonSkyHdri(int argc, py_Ref argv) {
    PY_CHECK_ARGC(4);
    std::string path;
    if (!readOptionalAssetPath(argv, 0, nullptr, "HDRI", path)) return false;
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".exr" && extension != ".hdr")
        return ValueError("HDRI path must end in .exr or .hdr");
    float intensity, rotation;
    if (!readFloatArg(argv, 1, intensity) || !readFloatArg(argv, 2, rotation) ||
        !py_checkbool(py_arg(3))) return false;
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 10000.0f)
        return ValueError("HDRI intensity must be finite and in [0, 10000]");
    if (!std::isfinite(rotation) || rotation < -360.0f || rotation > 360.0f)
        return ValueError("HDRI rotation must be finite and in [-360, 360]");
    g_environment.hdriPath = std::move(path);
    g_environment.intensity = intensity;
    g_environment.rotationDegrees = rotation;
    g_environment.visible = py_tobool(py_arg(3));
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereTimeOfDay(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    float time, speed;
    if (!readFloatArg(argv, 0, time) || !py_checkbool(py_arg(1)) || !readFloatArg(argv, 2, speed)) return false;
    genesis::atmosphere::settings().dayCycleSpeed = std::max(0.0f, speed);
    genesis::atmosphere::setTimeOfDay(time, py_tobool(py_arg(1)));
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereExposure(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    float radiance;
    if (!readFloatArg(argv, 0, radiance)) return false;
    genesis::atmosphere::settings().radiance = std::max(0.0f, radiance);
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereGroundAlbedo(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    auto& value = genesis::atmosphere::settings().groundAlbedo;
    if (!readFloatArg(argv, 0, value.r) || !readFloatArg(argv, 1, value.g) || !readFloatArg(argv, 2, value.b)) return false;
    value.r = std::clamp(value.r, 0.0f, 1.0f);
    value.g = std::clamp(value.g, 0.0f, 1.0f);
    value.b = std::clamp(value.b, 0.0f, 1.0f);
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereRayleigh(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    auto& value = genesis::atmosphere::settings().rayleighScattering;
    if (!readFloatArg(argv, 0, value.r) || !readFloatArg(argv, 1, value.g) || !readFloatArg(argv, 2, value.b)) return false;
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereAerosols(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    float scattering, extinction, anisotropy;
    if (!readFloatArg(argv, 0, scattering) || !readFloatArg(argv, 1, extinction) || !readFloatArg(argv, 2, anisotropy)) return false;
    auto& state = genesis::atmosphere::settings();
    state.mieScattering = { scattering, scattering, scattering };
    state.mieExtinction = { extinction, extinction, extinction };
    state.miePhaseG = std::clamp(anisotropy, -0.999f, 0.999f);
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereOzone(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    auto& value = genesis::atmosphere::settings().absorptionExtinction;
    if (!readFloatArg(argv, 0, value.r) || !readFloatArg(argv, 1, value.g) || !readFloatArg(argv, 2, value.b)) return false;
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonAtmosphereRadii(int argc, py_Ref argv) {
    PY_CHECK_ARGC(2);
    auto& state = genesis::atmosphere::settings();
    if (!readFloatArg(argv, 0, state.bottomRadiusKm) || !readFloatArg(argv, 1, state.topRadiusKm)) return false;
    if (state.bottomRadiusKm <= 0.0f || state.topRadiusKm <= state.bottomRadiusKm)
        return ValueError("top radius must be greater than the positive bottom radius");
    g_environment.atmosphereConfigured = true;
    py_newnone(py_retval());
    return true;
}

bool pythonColorAgX(int argc, py_Ref argv) {
    PY_CHECK_ARGC(2);
    if (!py_checkint(py_arg(0))) return false;
    float exposure;
    if (!readFloatArg(argv, 1, exposure)) return false;
    const auto look = py_toint(py_arg(0));
    if (look < 0 || look > 3) return ValueError("invalid AgX look");
    auto& state = genesis::color::settings();
    state.viewTransform = genesis::color::ViewTransform::AgX;
    state.look = static_cast<genesis::color::Look>(look);
    state.exposure = std::clamp(exposure, -12.0f, 12.0f);
    py_newnone(py_retval());
    return true;
}

bool pythonColorAces(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    float exposure;
    if (!readFloatArg(argv, 0, exposure)) return false;
    auto& state = genesis::color::settings();
    state.viewTransform = genesis::color::ViewTransform::Aces;
    state.exposure = std::clamp(exposure, -12.0f, 12.0f);
    py_newnone(py_retval());
    return true;
}

bool pythonColorAutoExposure(int argc, py_Ref argv) {
    PY_CHECK_ARGC(3);
    if (!py_checkbool(py_arg(0))) return false;
    float minEv, maxEv;
    if (!readFloatArg(argv, 1, minEv) || !readFloatArg(argv, 2, maxEv)) return false;
    if (minEv >= maxEv) return ValueError("auto exposure min_ev must be less than max_ev");
    auto& state = genesis::color::settings();
    state.autoExposure = py_tobool(py_arg(0));
    state.autoExposureMinEv = std::clamp(minEv, -16.0f, 16.0f);
    state.autoExposureMaxEv = std::clamp(maxEv, -16.0f, 16.0f);
    py_newnone(py_retval());
    return true;
}

bool pythonRendererRealtime(int argc, py_Ref argv) {
    PY_CHECK_ARGC(11);
    float renderScale;
    if (!readFloatArg(argv, 10, renderScale)) return false;
    if (!std::isfinite(renderScale) || renderScale < 0.5f || renderScale > 1.0f)
        return ValueError("render_scale must be finite and in [0.5, 1.0]");
    if (!py_checkint(py_arg(0)) || !py_checkbool(py_arg(1)) ||
        !py_checkbool(py_arg(2)) || !py_checkbool(py_arg(3)) ||
        !py_checkbool(py_arg(4)) || !py_checkbool(py_arg(5)) ||
        !py_checkint(py_arg(6)) || !py_checkint(py_arg(7)) ||
        !py_checkint(py_arg(8)) || !py_checkint(py_arg(9))) return false;
    g_renderer.realtime = true;
    g_renderer.renderScale = renderScale;
    g_renderer.samplesPerPixel = std::clamp(static_cast<int>(py_toint(py_arg(0))), 1, 8);
    g_renderer.restirDI = py_tobool(py_arg(1));
    g_renderer.restirGI = py_tobool(py_arg(2));
    g_renderer.denoise = py_tobool(py_arg(3));
    g_renderer.taa = py_tobool(py_arg(4));
    g_renderer.bloom = py_tobool(py_arg(5));
    g_renderer.bounceCount = std::clamp(static_cast<int>(py_toint(py_arg(6))), 1, 20);
    g_renderer.diffuseBounceCount = std::clamp(static_cast<int>(py_toint(py_arg(7))), 0, g_renderer.bounceCount);
    g_renderer.historyFrames = std::clamp(static_cast<int>(py_toint(py_arg(8))), 1, 30);
    g_renderer.specularHistoryFrames = static_cast<int>(py_toint(py_arg(9)));
    py_newnone(py_retval());
    return true;
}

bool pythonRendererReference(int argc, py_Ref argv) {
    PY_CHECK_ARGC(2);
    if (!py_checkint(py_arg(0)) || !py_checkint(py_arg(1))) return false;
    g_renderer.realtime = false;
    g_renderer.renderScale = 1.0f;
    g_renderer.referenceSamples = std::clamp(static_cast<int>(py_toint(py_arg(0))), 1, 65536);
    g_renderer.bounceCount = std::clamp(static_cast<int>(py_toint(py_arg(1))), 1, 20);
    g_renderer.diffuseBounceCount = g_renderer.bounceCount;
    py_newnone(py_retval());
    return true;
}

bool pythonRendererQuality(int argc, py_Ref argv) {
    PY_CHECK_ARGC(2);
    if (!py_checkstr(py_arg(0)) || !py_checkbool(py_arg(1))) return false;
    const std::string quality = py_tostr(py_arg(0));
    if (quality != "low" && quality != "medium" && quality != "high")
        return ValueError("renderer quality must be 'low', 'medium', or 'high'");
    g_renderer.quality = quality;
    g_renderer.gpuTimings = py_tobool(py_arg(1));
    py_newnone(py_retval());
    return true;
}

bool pythonRendererDebugView(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    PY_CHECK_ARG_TYPE(0, tp_str);
    const std::string view = py_tostr(py_arg(0));
    if (view != "none" && view != "ao" && view != "contact" && view != "material-ao" && view != "bloom")
        return ValueError("renderer debug view must be 'none', 'ao', 'contact', 'material-ao', or 'bloom'");
    g_renderer.debugView = view;
    py_newnone(py_retval());
    return true;
}


bool pythonCameraLookAt(int argc, py_Ref argv) {
    PY_CHECK_ARGC(10);
    auto camera = g_camera;
    for (int i = 0; i < 3; ++i)
        if (!readFloatArg(argv, i, camera.position[i]) ||
            !readFloatArg(argv, i+3, camera.target[i]) ||
            !readFloatArg(argv, i+6, camera.up[i])) return false;
    if (!readFloatArg(argv, 9, camera.fovDegrees)) return false;
    g_camera = camera;
    py_newnone(py_retval());
    return true;
}

bool pythonCameraView(int argc, py_Ref argv) {
    PY_CHECK_ARGC(11);
    PY_CHECK_ARG_TYPE(0, tp_str);
    CameraViewSettings view;
    view.name = py_tostr(py_arg(0));
    if (view.name.empty()) return ValueError("camera view name must be non-empty");
    for (const auto& existing : g_cameraViews)
        if (existing.name == view.name) return ValueError("camera view name must be unique");
    for (int i = 0; i < 3; ++i)
        if (!readFloatArg(argv, i + 1, view.position[i]) ||
            !readFloatArg(argv, i + 4, view.target[i]) ||
            !readFloatArg(argv, i + 7, view.up[i])) return false;
    if (!readFloatArg(argv, 10, view.fovDegrees)) return false;
    g_cameraViews.push_back(view);
    py_newnone(py_retval());
    return true;
}

bool pythonCameraFly(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    float speed;
    if (!readFloatArg(argv, 0, speed)) return false;
    g_camera.speed = speed;
    py_newnone(py_retval());
    return true;
}

bool pythonWindowConfigure(int argc, py_Ref argv) {
    PY_CHECK_ARGC(4);
    if (!py_checkstr(py_arg(0)) || !py_checkint(py_arg(1)) ||
        !py_checkint(py_arg(2)) || !py_checkbool(py_arg(3))) return false;
    g_window = {py_tostr(py_arg(0)), int(py_toint(py_arg(1))),
        int(py_toint(py_arg(2))), py_tobool(py_arg(3))};
    py_newnone(py_retval());
    return true;
}

bool executeFile(const std::filesystem::path& path, py_GlobalRef module) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "Cannot read Python file: %s\n", path.string().c_str());
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(input)), {});
    py_newstr(py_r0(), path.generic_string().c_str());
    py_setdict(module ? module : py_getmodule("__main__"), py_name("__file__"), py_r0());
    if (py_exec(source.c_str(), path.string().c_str(), EXEC_MODE, module)) return true;
    char* error = py_formatexc();
    std::fprintf(stderr, "%s\n", error);
    PK_FREE(error);
    return false;
}

bool pythonCloudsConfigure(int argc, py_Ref argv) {
    PY_CHECK_ARGC(1);
    PY_CHECK_ARG_TYPE(0, tp_str);
    Json::Value json;
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream input(py_tostr(py_arg(0)));
    if (!Json::parseFromStream(reader, input, &json, &errors) || !json.isObject())
        return ValueError("invalid cloud configuration");
    auto next = genesis::clouds::settings();
    auto number = [](const Json::Value& v, float lo, float hi) {
        return v.isNumeric() && std::isfinite(v.asDouble()) && v.asDouble() >= lo && v.asDouble() <= hi;
    };
    if (!json["configured"].isBool() || !json["enabled"].isBool() ||
        !number(json["wind_speed"], 0, 150) || !number(json["wind_direction"], -360, 360) ||
        !number(json["update_hz"], 0.5f, 10) || !json["seed"].isInt() ||
        json["seed"].asInt() < 0 || json["seed"].asInt() > 65535 ||
        !json["quality"].isString() || !json["layers"].isArray() || json["layers"].size() != 2)
        return ValueError("invalid cloud settings or ranges");
    const std::string quality = json["quality"].asString();
    if (quality != "low" && quality != "balanced" && quality != "high") return ValueError("invalid cloud quality");
    next.configured = json["configured"].asBool();
    next.enabled = json["enabled"].asBool();
    next.windSpeed = json["wind_speed"].asFloat();
    next.windDirection = json["wind_direction"].asFloat();
    next.updateHz = json["update_hz"].asFloat();
    next.seed = json["seed"].asInt();
    next.steps = quality == "low" ? 16 : quality == "high" ? 40 : 24;
    const char* kinds[] = { "cumulus", "stratus", "cirrus", "storm" };
    for (int i=0; i<2; ++i) {
        const auto& l = json["layers"][i];
        if (!l.isObject() || !l["kind"].isString() || !l["enabled"].isBool() ||
            !number(l["coverage"], 0, 1) || !number(l["density"], 0, 8) ||
            !number(l["altitude"], 250, 20000) || !number(l["thickness"], 100, 10000) ||
            !number(l["size"], 500, 40000) || !number(l["detail"], 0, 1))
            return ValueError("invalid cloud layer settings or ranges");
        int kind = -1;
        for (int j=0; j<4; ++j) if (l["kind"].asString() == kinds[j]) kind = j;
        if (kind < 0) return ValueError("invalid cloud kind");
        next.layers[i] = { static_cast<genesis::clouds::Kind>(kind), l["enabled"].asBool(),
            l["coverage"].asFloat(), l["density"].asFloat(), l["altitude"].asFloat(),
            l["thickness"].asFloat(), l["size"].asFloat(), l["detail"].asFloat() };
        if (next.layers[i].altitude + next.layers[i].thickness > 25000)
            return ValueError("cloud layer top must be at or below 25000 metres");
    }
    if (next.layers[0].enabled && next.layers[1].enabled &&
        next.layers[0].altitude + next.layers[0].thickness > next.layers[1].altitude)
        return ValueError("low and high cloud layers must be ordered and non-overlapping");
    genesis::clouds::settings() = next; // commit atomically after all validation
    py_newnone(py_retval());
    return true;
}

bool pythonConfiguration(int argc, py_Ref argv) {
    PY_CHECK_ARGC(0);
    Json::Value result;
    result["scene"]["path"] = g_model.generic_string();
    auto& entities = result["entities"];
    entities["count"] = Json::UInt64(gameplay::g_world.size());
    entities["items"] = Json::arrayValue;
    for (const auto& entity : gameplay::g_world.snapshots())
        entities["items"].append(entityJson(entity));
    auto& window = result["window"];
    window["title"] = g_window.title;
    window["width"] = g_window.width;
    window["height"] = g_window.height;
    window["vsync"] = g_window.vsync;
    auto& camera = result["camera"];
    for (int i=0; i<3; ++i) {
        camera["position"].append(g_camera.position[i]);
        camera["target"].append(g_camera.target[i]);
        camera["up"].append(g_camera.up[i]);
    }
    camera["fov"] = g_camera.fovDegrees;
    camera["speed"] = g_camera.speed;
    auto& renderer = result["renderer"];
    renderer["mode"] = g_renderer.realtime ? "realtime" : "reference";
    renderer["samples"] = g_renderer.realtime ? g_renderer.samplesPerPixel : g_renderer.referenceSamples;
    renderer["restir_di"] = g_renderer.realtime && g_renderer.restirDI;
    renderer["restir_gi"] = g_renderer.realtime && g_renderer.restirGI;
    renderer["denoise"] = g_renderer.realtime && g_renderer.denoise;
    renderer["taa"] = g_renderer.realtime && g_renderer.taa;
    renderer["bloom"] = g_renderer.bloom;
    renderer["bounces"] = g_renderer.bounceCount;
    renderer["diffuse_bounces"] = g_renderer.diffuseBounceCount;
    renderer["history_frames"] = g_renderer.historyFrames;
    renderer["specular_history_frames"] = g_renderer.specularHistoryFrames;
    renderer["render_scale"] = g_renderer.renderScale;
    renderer["quality"] = g_renderer.quality;
    renderer["gpu_timings"] = g_renderer.gpuTimings;
    renderer["debug_view"] = g_renderer.debugView;
    camera["views"] = Json::arrayValue;
    for (const auto& view : g_cameraViews) camera["views"].append(view.name);
    auto& probes = result["reflection_probes"];
    probes = Json::arrayValue;
    for (const auto& probe : g_reflectionProbes) {
        Json::Value value;
        value["name"] = probe.name;
        for (float component : probe.position) value["position"].append(component);
        for (float component : probe.boundsMin) value["bounds_min"].append(component);
        for (float component : probe.boundsMax) value["bounds_max"].append(component);
        value["blend_distance"] = probe.blendDistance;
        value["priority"] = probe.priority;
        probes.append(value);
    }
    auto& pointLights = result["point_lights"];
    pointLights = Json::arrayValue;
    for (const auto& light : g_pointLights) {
        Json::Value value;
        value["name"] = light.name;
        for (float component : light.position) value["position"].append(component);
        for (float component : light.color) value["color"].append(component);
        value["intensity"] = light.intensity;
        value["radius"] = light.radius;
        value["shadow_bias"] = light.shadowBias;
        value["casts_shadows"] = light.castsShadows;
        value["ies"] = light.iesProfile;
        pointLights.append(value);
    }
    auto& spotLights = result["spot_lights"];
    spotLights = Json::arrayValue;
    for (const auto& light : g_spotLights) {
        Json::Value value;
        value["name"] = light.name;
        for (float component : light.position) value["position"].append(component);
        for (float component : light.direction) value["direction"].append(component);
        for (float component : light.up) value["up"].append(component);
        for (float component : light.color) value["color"].append(component);
        value["intensity"] = light.intensity;
        value["radius"] = light.radius;
        value["inner_angle"] = light.innerAngle;
        value["outer_angle"] = light.outerAngle;
        value["shadow_bias"] = light.shadowBias;
        value["casts_shadows"] = light.castsShadows;
        value["cookie"] = light.cookieTexture;
        value["ies"] = light.iesProfile;
        spotLights.append(value);
    }
    auto& areaLights = result["area_lights"];
    areaLights = Json::arrayValue;
    for (const auto& light : g_areaLights) {
        Json::Value value;
        value["name"] = light.name;
        for (float component : light.position) value["position"].append(component);
        for (float component : light.direction) value["direction"].append(component);
        for (float component : light.up) value["up"].append(component);
        for (float component : light.color) value["color"].append(component);
        value["intensity"] = light.intensity;
        value["radius"] = light.radius;
        value["width"] = light.width;
        value["height"] = light.height;
        value["shadow_bias"] = light.shadowBias;
        value["casts_shadows"] = light.castsShadows;
        areaLights.append(value);
    }
    auto& lightSources = result["light_sources"];
    lightSources["enabled"] = g_lightSources.enabled;
    lightSources["intensity"] = g_lightSources.intensity;
    lightSources["point_size"] = g_lightSources.pointSize;
    lightSources["spot_size"] = g_lightSources.spotSize;
    lightSources["area_scale"] = g_lightSources.areaScale;
    const auto& color = genesis::color::settings();
    result["color"]["transform"] = color.viewTransform == genesis::color::ViewTransform::AgX ? "agx" : "aces";
    const char* looks[] = {"neutral", "medium_high", "punchy", "golden"};
    result["color"]["look"] = looks[int(color.look)];
    result["color"]["exposure"] = color.exposure;
    result["color"]["auto_exposure"] = color.autoExposure;
    const auto& sky = genesis::atmosphere::settings();
    result["sky"]["azimuth"] = sky.sunAzimuthDegrees;
    result["sky"]["elevation"] = sky.sunElevationDegrees;
    result["sky"]["intensity"] = sky.sunIntensity;
    result["sky"]["radiance"] = sky.radiance;
    result["sky"]["manual_sun"] = sky.manualSun;
    result["sky"]["hdri"] = g_environment.hdriPath;
    result["sky"]["hdri_intensity"] = g_environment.intensity;
    result["sky"]["hdri_rotation"] = g_environment.rotationDegrees;
    result["sky"]["hdri_visible"] = g_environment.visible;
    const auto& clouds = genesis::clouds::settings();
    auto& cloudJson = result["clouds"];
    cloudJson["configured"] = clouds.configured;
    cloudJson["enabled"] = clouds.enabled;
    cloudJson["wind_speed"] = clouds.windSpeed;
    cloudJson["wind_direction"] = clouds.windDirection;
    cloudJson["update_hz"] = clouds.updateHz;
    cloudJson["seed"] = clouds.seed;
    cloudJson["quality"] = clouds.steps == 16 ? "low" : clouds.steps == 40 ? "high" : "balanced";
    const char* cloudKinds[] = { "cumulus", "stratus", "cirrus", "storm" };
    for (const auto& l : clouds.layers) {
        Json::Value layer;
        layer["kind"] = cloudKinds[int(l.kind)];
        layer["enabled"] = l.enabled;
        layer["coverage"] = l.coverage;
        layer["density"] = l.density;
        layer["altitude"] = l.altitude;
        layer["thickness"] = l.thickness;
        layer["size"] = l.size;
        layer["detail"] = l.detail;
        cloudJson["layers"].append(layer);
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const auto json = Json::writeString(writer, result);
    py_newstr(py_retval(), json.c_str());
    return true;
}
}

ScriptRuntime::ScriptRuntime() {
    py_initialize();
    py_callbacks()->importfile = pythonImportFile;
    py_callbacks()->print = [](const char* message) { std::fprintf(stdout, "%s\n", message); };
}
ScriptRuntime::~ScriptRuntime() { py_finalize(); }

bool ScriptRuntime::run(const std::filesystem::path& path) {
    return run(std::vector<std::filesystem::path>{path});
}

bool ScriptRuntime::run(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) return false;
    scriptDirectory = std::filesystem::absolute(paths.front()).parent_path();
    g_model.clear();
    g_renderer = {};
    g_reflectionProbes.clear();
    g_pointLights.clear();
    g_spotLights.clear();
    g_areaLights.clear();
    g_environment = {};
    g_camera = {};
    g_cameraViews.clear();
    g_window = {};
    gameplay::g_world.clear();
    genesis::atmosphere::resetEarth();
    genesis::clouds::reset();
    genesis::color::reset();
    py_GlobalRef module = py_newmodule("genesis");
    py_bindfunc(module, "_scene_load", pythonLoadModel);
    py_bindfunc(module, "_entity_create", pythonEntityCreate);
    py_bindfunc(module, "_entity_destroy", pythonEntityDestroy);
    py_bindfunc(module, "_entity_valid", pythonEntityValid);
    py_bindfunc(module, "_entity_rename", pythonEntityRename);
    py_bindfunc(module, "_entity_transform", pythonEntityTransform);
    py_bindfunc(module, "_entity_tag", pythonEntityTag);
    py_bindfunc(module, "_entity_model", pythonEntityModel);
    py_bindfunc(module, "_entity_particles", pythonEntityParticles);
    py_bindfunc(module, "_entity_snapshot_json", pythonEntitySnapshot);
    py_bindfunc(module, "_entities_snapshot_json", pythonEntitiesSnapshot);
    py_bindfunc(module, "_atmosphere_earth", pythonAtmosphereEarth);
    py_bindfunc(module, "_atmosphere_sun", pythonAtmosphereSun);
    py_bindfunc(module, "_sky_hdri", pythonSkyHdri);
    py_bindfunc(module, "_atmosphere_time_of_day", pythonAtmosphereTimeOfDay);
    py_bindfunc(module, "_atmosphere_exposure", pythonAtmosphereExposure);
    py_bindfunc(module, "_atmosphere_ground_albedo", pythonAtmosphereGroundAlbedo);
    py_bindfunc(module, "_atmosphere_rayleigh", pythonAtmosphereRayleigh);
    py_bindfunc(module, "_atmosphere_aerosols", pythonAtmosphereAerosols);
    py_bindfunc(module, "_atmosphere_ozone", pythonAtmosphereOzone);
    py_bindfunc(module, "_atmosphere_radii", pythonAtmosphereRadii);
    py_bindfunc(module, "_color_agx", pythonColorAgX);
    py_bindfunc(module, "_color_aces", pythonColorAces);
    py_bindfunc(module, "_color_auto_exposure", pythonColorAutoExposure);
    py_bindfunc(module, "_renderer_realtime", pythonRendererRealtime);
    py_bindfunc(module, "_renderer_reference", pythonRendererReference);
    py_bindfunc(module, "_renderer_quality", pythonRendererQuality);
    py_bindfunc(module, "_renderer_debug_view", pythonRendererDebugView);
    py_bindfunc(module, "_reflection_probe_add", pythonReflectionProbeAdd);
    py_bindfunc(module, "_reflection_probes_clear", pythonReflectionProbesClear);
    py_bindfunc(module, "_point_light_add", pythonPointLightAdd);
    py_bindfunc(module, "_spot_light_add", pythonSpotLightAdd);
    py_bindfunc(module, "_area_light_add", pythonAreaLightAdd);
    py_bindfunc(module, "_point_lights_clear", pythonPointLightsClear);
    py_bindfunc(module, "_light_sources_configure", pythonLightSourcesConfigure);

    py_bindfunc(module, "_camera_look_at", pythonCameraLookAt);
    py_bindfunc(module, "_camera_view", pythonCameraView);
    py_bindfunc(module, "_camera_fly", pythonCameraFly);
    py_bindfunc(module, "_window_configure", pythonWindowConfigure);
    py_bindfunc(module, "_configuration_json", pythonConfiguration);
    py_bindfunc(module, "_clouds_configure", pythonCloudsConfigure);
    if (!executeFile(std::filesystem::path(GENESIS_ROOT) / "python/genesis/__init__.py", module))
        return false;
    // Kept for existing scripts; new scripts use project-relative asset paths.
    const std::string bootstrap = std::string("GENESIS_ROOT = r\"") + GENESIS_ROOT + "\"";
    if (!py_exec(bootstrap.c_str(), "<genesis>", EXEC_MODE, nullptr)) return false;
    for (const auto& path : paths) {
        scriptDirectory = std::filesystem::absolute(path).parent_path();
        if (!executeFile(path, nullptr)) return false;
    }
    return true;
}
}
