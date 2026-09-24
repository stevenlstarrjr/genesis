#pragma once
#include "ui/Toolkit.h"
#include <filesystem>
#include <map>

namespace genesis::editor {
struct ProjectAsset {
    std::filesystem::path path;
    std::string name,category;
    ui::Icon icon=ui::Icon::File;
    bool model=false,image=false,prefab=false;
    uintmax_t bytes=0;
    std::filesystem::file_time_type modified{};
    bool operator==(const ProjectAsset&) const = default;
};
// A bounded incremental scan. No symlink traversal or writes to project files.
// The previous complete index remains usable until takeChanges() publishes a scan.
class ProjectAssets {
public:
    ProjectAssets(std::filesystem::path root,std::filesystem::path builtins);
    const std::filesystem::path& root() const { return m_root; }
    const std::vector<ProjectAsset>& files() const { return m_files; }
    const std::vector<std::filesystem::path>& folders() const { return m_folders; }
    const std::string& warning() const { return m_warning; }
    void begin();
    bool scanning() const { return m_scanning; }
    void scanStep(size_t budget=128);
    bool takeChanges();
private:
    std::filesystem::path m_root,m_builtins;
    std::filesystem::recursive_directory_iterator m_iterator;
    std::vector<ProjectAsset> m_files,m_nextFiles;
    std::vector<std::filesystem::path> m_folders,m_nextFolders;
    size_t m_visited=0;
    bool m_scanning=false,m_ready=false;
    std::string m_warning,m_nextWarning;
    void addFile(const std::filesystem::path& path,std::string category,std::string name={});
};
class AssetThumbnails {
public:
    std::shared_ptr<const ui::Surface> get(const ProjectAsset& asset);
    void clear() { m_cache.clear(); }
private:
    struct Entry { std::shared_ptr<const ui::Surface> image; uint64_t used=0; };
    std::map<std::filesystem::path,Entry> m_cache;
    uint64_t m_clock=0;
};
}
