#pragma once
#include "GameplayWorld.h"
#include "RenderScene.h"
#include <filesystem>
#include <json/json.h>

namespace genesis {
// Shared scene persistence, independent of editor widgets and GPU backends.
// Scripts configure the world first; an authored entities array replaces the
// script-created mesh entities, without modifying the scripts or their settings.
class SceneDocument {
public:
    bool load(const std::filesystem::path& file, std::string& error);
    void fromScript(const std::filesystem::path& script);
    bool hasEntities() const { return m_json.isMember("entities"); }
    void instantiate(gameplay::GameplayWorld& world) const;
    bool save(const gameplay::GameplayWorld& world, const render::CameraSettings& camera, std::string& error);
    const std::filesystem::path& path() const { return m_path; }
    std::string name() const;
    const std::optional<render::CameraSettings>& editorCamera() const { return m_camera; }
    bool hasEditorOrbitPivot() const { return m_json["editor_camera"].isMember("orbit_pivot"); }
private:
    std::filesystem::path m_path;
    Json::Value m_json{Json::objectValue};
    std::vector<gameplay::EntitySnapshot> m_entities;
    std::optional<render::CameraSettings> m_camera;
    std::string m_diskContents; // Detect external edits before overwriting.
    bool m_new = false;
};
}
