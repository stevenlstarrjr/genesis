#include "SceneDocument.h"
#include "SceneComponents.h"
#include <cmath>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace genesis {
namespace {
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
Json::Value vectorJson(const std::array<float,3>& value) {
    Json::Value array(Json::arrayValue); for (float v : value) array.append(v); return array;
}
void vectorValue(const Json::Value& root, const char* key, std::array<float,3>& out, bool scale = false) {
    if (!root.isMember(key)) return;
    const auto& value = root[key];
    if (!value.isArray() || value.size() != 3) throw std::runtime_error(std::string(key)+" must have three numbers");
    for (int i = 0; i < 3; ++i) {
        if (!value[i].isNumeric() || !std::isfinite(value[i].asFloat()) || std::abs(value[i].asDouble()) > 1000000
            || (scale && std::abs(value[i].asDouble()) < .001))
            throw std::runtime_error(std::string("Invalid ")+key+" component");
        out[i] = value[i].asFloat();
    }
}
std::string relativeAsset(const fs::path& path, const fs::path& base) {
    std::error_code ec; const auto relative = fs::relative(path,base,ec);
    return ec ? path.generic_string() : relative.generic_string();
}
}
bool SceneDocument::load(const fs::path& file, std::string& error) {
    try {
        SceneDocument next; next.m_path = fs::absolute(file); next.m_diskContents = read(next.m_path);
        Json::CharReaderBuilder builder; std::istringstream input(next.m_diskContents);
        if (!Json::parseFromStream(builder,input,&next.m_json,&error)) return false;
        if (!next.m_json.isObject()) throw std::runtime_error("Scene must be an object");
        if (next.m_json.isMember("name") && !next.m_json["name"].isString()) throw std::runtime_error("Scene name must be text");
        if (next.hasEntities()) {
            if (!next.m_json["entities"].isArray()) throw std::runtime_error("entities must be an array");
            for (const auto& node : next.m_json["entities"]) {
                if (!node.isObject() || !node["name"].isString()) throw std::runtime_error("Each entity needs a name");
                gameplay::EntitySnapshot entity; entity.name = node["name"].asString();
                vectorValue(node,"position",entity.transform.position);
                vectorValue(node,"rotation",entity.transform.rotation);
                vectorValue(node,"scale",entity.transform.scale,true);
                if (node.isMember("model")) {
                    if (!node["model"].isString() || node["model"].asString().empty()) throw std::runtime_error("model must be a path");
                    const auto path = fs::weakly_canonical(next.m_path.parent_path()/fs::u8path(node["model"].asString()));
                    if (!fs::is_regular_file(path)) throw std::runtime_error("Missing model: "+path.string());
                    if (node.isMember("visible") && !node["visible"].isBool()) throw std::runtime_error("visible must be boolean");
                    entity.renderable = gameplay::Renderable{path.string(),node.get("visible",true).asBool()};
                }
                components::deserialize(node,entity);
                if (node.isMember("tags")) {
                    if (!node["tags"].isArray()) throw std::runtime_error("tags must be an array");
                    for (const auto& tag : node["tags"]) {
                        if (!tag.isString()) throw std::runtime_error("tags must contain text");
                        entity.tags.push_back(tag.asString());
                    }
                }
                next.m_entities.push_back(std::move(entity));
            }
        }
        if (next.m_json.isMember("editor_camera")) {
            const auto& camera = next.m_json["editor_camera"];
            if (!camera.isObject()) throw std::runtime_error("editor_camera must be an object");
            render::CameraSettings settings;
            vectorValue(camera,"position",settings.position); vectorValue(camera,"target",settings.target);
            vectorValue(camera,"orbit_pivot",settings.target);
            if (camera.isMember("fov")) {
                if (!camera["fov"].isNumeric() || !std::isfinite(camera["fov"].asFloat())
                    || camera["fov"].asFloat()<1 || camera["fov"].asFloat()>175) throw std::runtime_error("Invalid editor camera fov");
                settings.fovDegrees=camera["fov"].asFloat();
            }
            next.m_camera = settings;
        }
        *this = std::move(next); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
void SceneDocument::fromScript(const fs::path& script) {
    *this = SceneDocument{};
    m_path = fs::absolute(script); m_path.replace_extension(".editor.gscene"); m_new = true;
    m_json["format"] = 1; m_json["name"] = script.stem().string();
    m_json["script"] = relativeAsset(fs::absolute(script),m_path.parent_path());
}
std::string SceneDocument::name() const { return m_json.get("name",m_path.stem().string()).asString(); }
void SceneDocument::instantiate(gameplay::GameplayWorld& world) const {
    if (!hasEntities()) return;
    world.clear();
    for (const auto& entity : m_entities) {
        const auto id = world.create(entity.name,entity.transform);
        for (const auto& tag : entity.tags) world.setTag(id,tag,true);
        world.copyComponents(id,entity);
    }
}
bool SceneDocument::save(const gameplay::GameplayWorld& world, const render::CameraSettings& camera, std::string& error) {
    try {
        if (m_path.empty()) throw std::runtime_error("No scene path");
        if (m_new ? fs::exists(m_path) : read(m_path) != m_diskContents)
            throw std::runtime_error("Scene changed on disk. Reopen it before saving: "+m_path.string());
        auto document = m_json;
        document["entities"] = Json::Value(Json::arrayValue);
        for (const auto& entity : world.snapshots()) {
            if(!components::valid(entity))throw std::runtime_error("Invalid components on "+entity.name);
            Json::Value node=components::serialize(entity); node["name"] = entity.name;
            node["position"] = vectorJson(entity.transform.position); node["rotation"] = vectorJson(entity.transform.rotation);
            node["scale"] = vectorJson(entity.transform.scale); node["tags"] = Json::Value(Json::arrayValue);
            for (const auto& tag : entity.tags) node["tags"].append(tag);
            if (entity.renderable) {
                node["model"] = relativeAsset(entity.renderable->path,m_path.parent_path());
                node["visible"] = entity.renderable->visible;
            }
            document["entities"].append(std::move(node));
        }
        document["editor_camera"]["position"] = vectorJson(camera.position);
        document["editor_camera"]["target"] = vectorJson(camera.target);
        document["editor_camera"]["orbit_pivot"] = vectorJson(camera.target);
        document["editor_camera"]["fov"] = camera.fovDegrees;
        Json::StreamWriterBuilder writer; writer["indentation"] = "  ";
        const auto contents = Json::writeString(writer,document)+"\n";
        // Adjacent temporary file + atomic replacement keeps the old scene on write failure.
        const fs::path temporary = m_path.string()+".saving";
        if (fs::exists(temporary)) throw std::runtime_error("A scene save is already pending: "+temporary.string());
        { std::ofstream output(temporary,std::ios::binary); output << contents; output.flush();
          if (!output) throw std::runtime_error("Cannot write scene: "+temporary.string()); }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),m_path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace scene; completed save remains at "+temporary.string());
#else
        fs::rename(temporary,m_path);
#endif
        m_json = std::move(document); m_diskContents = contents; m_new = false;
        m_entities = world.snapshots(); m_camera = camera;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
