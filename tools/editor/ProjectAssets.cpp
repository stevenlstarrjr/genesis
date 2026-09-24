#include "editor/ProjectAssets.h"
#include <algorithm>
#include <cctype>

namespace genesis::editor {
namespace fs=std::filesystem;
namespace {
std::string lower(std::string value){for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value;}
bool hidden(const fs::path& path) {
    const auto name=lower(path.filename().string());
    return name.empty() || name[0]=='.' || name=="build" || name=="node_modules" || name=="__pycache__" || name=="thirdparty";
}
}
ProjectAssets::ProjectAssets(fs::path root,fs::path builtins):m_root(std::move(root)),m_builtins(std::move(builtins)) {
    std::error_code error;
    if(!m_root.empty()){auto resolved=fs::weakly_canonical(m_root,error);if(!error)m_root=resolved;}
}
void ProjectAssets::addFile(const fs::path& path,std::string category,std::string name) {
    std::error_code error;if(!fs::is_regular_file(path,error) || error)return;
    const auto ext=lower(path.extension().string());
    if(ext==".saving" || ext==".tmp")return;
    ProjectAsset asset;asset.path=path.lexically_normal();asset.category=std::move(category);
    asset.name=name.empty()?path.filename().string():std::move(name);
    asset.model=ext==".gltf" || ext==".glb";
    asset.image=ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".tga";
    asset.prefab=ext==".gprefab" || ext==".gpreset";
    asset.icon=asset.model?ui::Icon::Cube:(ext==".gscene" || asset.prefab)?ui::Icon::Scene:ui::Icon::File;
    asset.bytes=fs::file_size(path,error);if(error)return;
    asset.modified=fs::last_write_time(path,error);if(error)return;
    m_nextFiles.push_back(std::move(asset));
}
void ProjectAssets::begin() {
    m_nextFiles.clear();m_nextFolders.clear();m_nextWarning.clear();m_visited=0;m_ready=false;
    const auto samples=m_builtins/"glTF-Sample-Models/2.0";
    addFile(samples/"Box/glTF-Binary/Box.glb","starter","Cube");
    addFile(samples/"BoxTextured/glTF-Binary/BoxTextured.glb","starter","Textured Cube");
    addFile(samples/"DamagedHelmet/glTF-Binary/DamagedHelmet.glb","starter","Helmet");
    addFile(m_builtins/"particle_prefabs/campfire.gprefab","prefabs","Campfire");
    std::error_code error;
    m_iterator=m_root.empty()?fs::recursive_directory_iterator{}:fs::recursive_directory_iterator(m_root,fs::directory_options::skip_permission_denied,error);
    if(error)m_nextWarning="Project folder is unavailable; refresh to retry.";
    m_scanning=true;
}
void ProjectAssets::scanStep(size_t budget) {
    if(!m_scanning)return;
    const fs::recursive_directory_iterator end;
    std::error_code error;
    while(budget-- && m_iterator!=end && m_visited<10000) {
        ++m_visited;const auto entry=*m_iterator;const auto path=entry.path();
        const bool skip=hidden(path) || entry.is_symlink(error);
        if(skip)m_iterator.disable_recursion_pending();
        else if(entry.is_directory(error)) {
            m_nextFolders.push_back(path.lexically_normal());
            if(m_iterator.depth()>=32){m_iterator.disable_recursion_pending();m_nextWarning="Folders deeper than 32 levels are not indexed.";}
        } else if(entry.is_regular_file(error))addFile(path,"project");
        error.clear();m_iterator.increment(error);
        if(error){m_nextWarning="Some project files could not be read; refresh to retry.";break;}
    }
    if(m_iterator!=end && m_visited<10000 && !error)return;
    if(m_visited>=10000)m_nextWarning="Showing the first 10,000 project entries. Choose a smaller project root for larger collections.";
    m_iterator=end;m_scanning=false;m_ready=true;
    std::sort(m_nextFiles.begin(),m_nextFiles.end(),[](const auto& a,const auto& b){
        const auto an=lower(a.name),bn=lower(b.name);return an==bn?a.path<b.path:an<bn;
    });
    std::sort(m_nextFolders.begin(),m_nextFolders.end());
}
bool ProjectAssets::takeChanges() {
    if(!m_ready)return false;m_ready=false;
    if(m_files==m_nextFiles && m_folders==m_nextFolders && m_warning==m_nextWarning)return false;
    m_files=std::move(m_nextFiles);m_folders=std::move(m_nextFolders);m_warning=std::move(m_nextWarning);return true;
}
}
