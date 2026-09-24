#include "ProjectLoader.h"

#include <json/json.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace genesis {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool readJson(const fs::path& path, Json::Value& value, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Cannot open " + path.string();
        return false;
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    if (!Json::parseFromStream(builder, input, &value, &error)) {
        error = path.string() + ": " + error;
        return false;
    }
    if (!value.isObject()) {
        error = path.string() + " must contain a JSON object";
        return false;
    }
    return true;
}

fs::path absoluteFrom(const fs::path& base, const std::string& value) {
    fs::path path = fs::u8path(value);
    if (path.is_relative()) path = base / path;
    std::error_code ec;
    const auto normalized = fs::weakly_canonical(path, ec);
    return ec ? fs::absolute(path) : normalized;
}

void collectScripts(const Json::Value& node, const fs::path& sceneDirectory,
    std::vector<fs::path>& scripts, std::unordered_set<std::string>& seen) {
    auto add = [&](const Json::Value& value) {
        if (!value.isString() || value.asString().empty()) return;
        auto path = absoluteFrom(sceneDirectory, value.asString());
        const auto key = lower(path.generic_string());
        if (seen.insert(key).second) scripts.push_back(std::move(path));
    };
    add(node["script"]);
    if (node["scripts"].isArray())
        for (const auto& script : node["scripts"]) add(script);
    if (node["nodes"].isArray())
        for (const auto& child : node["nodes"]) if (child.isObject())
            collectScripts(child, sceneDirectory, scripts, seen);
}

LaunchTarget prompt(std::string message) {
    LaunchTarget result;
    result.message = std::move(message);
    return result;
}

LaunchTarget loadScene(const fs::path& scene, fs::path projectRoot,
    fs::path projectFile, std::string projectName) {
    Json::Value document;
    std::string error;
    if (!readJson(scene, document, error)) return prompt(error);
    LaunchTarget result;
    result.kind = LaunchKind::Scene;
    result.projectRoot = projectRoot.empty() ? scene.parent_path() : std::move(projectRoot);
    result.projectFile = std::move(projectFile);
    result.sceneFile = scene;
    result.projectName = std::move(projectName);
    if (result.projectName.empty()) result.projectName = document.get("name", "Genesis").asString();
    std::unordered_set<std::string> seen;
    collectScripts(document, scene.parent_path(), result.scripts, seen);
    if (result.scripts.empty() && !document["entities"].isArray())
        return prompt(scene.string() + " needs a Python script or an entities array");
    for (const auto& script : result.scripts) {
        if (!fs::is_regular_file(script))
            return prompt("Scene script does not exist: " + script.string());
    }
    return result;
}

LaunchTarget loadProject(const fs::path& projectFile) {
    Json::Value document;
    std::string error;
    if (!readJson(projectFile, document, error)) return prompt(error);
    const auto& application = document["application"];
    const Json::Value& mainScene = application.isObject() ? application["main_scene"] : document["main_scene"];
    if (!mainScene.isString() || mainScene.asString().empty())
        return prompt(projectFile.string() + " must define application.main_scene");
    const auto scene = absoluteFrom(projectFile.parent_path(), mainScene.asString());
    if (!fs::is_regular_file(scene)) return prompt("Main scene does not exist: " + scene.string());
    return loadScene(scene, projectFile.parent_path(), projectFile,
        document.get("name", projectFile.parent_path().filename().string()).asString());
}

}

LaunchTarget ProjectLoader::resolve(const fs::path& rawInput) {
    if (rawInput.empty())
        return prompt("Open a Genesis project, scene, or Python script to begin.");
    std::error_code ec;
    fs::path input = fs::absolute(rawInput, ec);
    if (ec) input = rawInput;
    if (fs::is_directory(input)) {
        auto project = input / ProjectFilename;
        if (!fs::is_regular_file(project)) {
            const auto legacy = input / "genesis.project.json";
            if (fs::is_regular_file(legacy)) project = legacy;
            else return prompt("No genesis.project found in " + input.string());
        }
        return loadProject(project);
    }
    if (!fs::is_regular_file(input)) return prompt("Path does not exist: " + input.string());
    const auto extension = lower(input.extension().string());
    if (extension == ".py") {
        LaunchTarget result;
        result.kind = LaunchKind::Scene;
        result.projectRoot = input.parent_path();
        result.scripts.push_back(input);
        result.projectName = input.stem().string();
        return result;
    }
    if (extension == ".gscene") return loadScene(input, input.parent_path(), {}, "");
    if (input.filename() == ProjectFilename || input.filename() == "genesis.project.json")
        return loadProject(input);
    return prompt("Unsupported Genesis target: " + input.string());
}

}
