#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

enum class LaunchKind { Prompt, Scene };

struct LaunchTarget {
    LaunchKind kind = LaunchKind::Prompt;
    std::filesystem::path projectRoot;
    std::filesystem::path projectFile;
    std::filesystem::path sceneFile;
    std::vector<std::filesystem::path> scripts;
    std::string projectName = "Genesis";
    std::string message;
};

class ProjectLoader {
public:
    static constexpr const char* ProjectFilename = "genesis.project";
    static LaunchTarget resolve(const std::filesystem::path& input);
};

}

