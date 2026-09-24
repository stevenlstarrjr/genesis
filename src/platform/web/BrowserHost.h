#pragma once
#include <filesystem>
#include <string>

struct SDL_Window;
namespace genesis::web {
// Called only by the Emscripten platform path. No browser types escape this API.
void prepareWindow();
void initialize();
void frame(SDL_Window* window);
bool save(const std::filesystem::path& scene,std::string& error);
bool nextImport(std::string& path);
bool takeExportRequest();
void download(const std::filesystem::path& scene);
void setDirty(bool dirty);
void runScene();
}
