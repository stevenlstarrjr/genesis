#include "editor/GameEditor.h"
#include "editor/ViewportCamera.h"
#include "ui/Theme.h"
#include "SceneComponents.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace genesis::editor {
namespace fs=std::filesystem;
using namespace ui;
namespace {
std::string lower(std::string value){for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value;}
std::string folderId(const fs::path& path,const fs::path& root){return "folder:"+path.lexically_relative(root).generic_string();}
bool beneath(const fs::path& path,const fs::path& folder) {
    const auto relative=path.lexically_relative(folder);
    return !relative.empty() && *relative.begin()!="..";
}
}
void GameEditor::refreshProject(bool immediate) {
    m_assetRefresh=true;m_projectAssets->begin();m_assetScanTime=std::chrono::steady_clock::now();
    if(immediate){while(m_projectAssets->scanning())m_projectAssets->scanStep(512);updateAssets();}
}
void GameEditor::updateAssets() {
    const auto now=std::chrono::steady_clock::now();
    if(m_assetWatch && !m_projectAssets->scanning() && now-m_assetScanTime>std::chrono::seconds(1)) {
        m_projectAssets->begin();m_assetScanTime=now;
    }
    m_projectAssets->scanStep();
    // Never destroy a captured asset tile or an in-progress text editor.
    if(m_document.hasPointerCapture() || m_document.hasPopup() || m_document.wantsTextInput())return;
    const bool changed=m_projectAssets->takeChanges();
    if(changed || (m_assetRefresh && !m_projectAssets->scanning())) {
        m_assetRefresh=false;m_assets=m_projectAssets->files();m_thumbnails.clear();
        if(m_selectedAsset && std::none_of(m_assets.begin(),m_assets.end(),[&](const auto& item){return item.path==*m_selectedAsset;})) {
            m_selectedAsset.reset();updateInspector();
        }
        for(auto& view:m_projects) {
            if(view->category.starts_with("folder:")) {
                const auto folder=(m_projectAssets->root()/fs::u8path(view->category.substr(7))).lexically_normal();
                if(std::find(m_projectAssets->folders().begin(),m_projectAssets->folders().end(),folder)==m_projectAssets->folders().end())view->category="project";
            }
            rebuildAssetFolders(*view);
        }
        refreshAssets();
        if(m_selectedAsset) {
            const auto found=std::find_if(m_assets.begin(),m_assets.end(),[&](const auto& item){return item.path==*m_selectedAsset;});
            if(found!=m_assets.end())inspectAsset(size_t(found-m_assets.begin()));
        }
    }
    for(auto& view:m_projects) {
        if(!m_dock->visible(view->id))continue;
        const auto viewport=view->grid->viewport().bounds();
        for(auto& tile:view->tiles)if(!tile.ready) {
            const auto b=tile.node->bounds();
            if(b.y+b.height<viewport.y || b.y>viewport.y+viewport.height)continue;
            const auto asset=std::find_if(m_assets.begin(),m_assets.end(),[&](const auto& item){return item.path==tile.path;});
            if(asset!=m_assets.end())tile.node->setImage(m_thumbnails.get(*asset));
            tile.ready=true;break; // One new preview per UI update; decoded images are cached.
        }
    }
}
void GameEditor::rebuildAssetFolders(ProjectView& view) {
    const auto& root=m_projectAssets->root();
    std::map<fs::path,std::vector<fs::path>> children;
    for(const auto& folder:m_projectAssets->folders())children[folder.parent_path()].push_back(folder);
    auto tree=[&](auto&& self,const fs::path& path)->TreeItem {
        TreeItem result{path==root?"project":folderId(path,root),path==root?"Project files":path.filename().string(),{},path==root,Icon::Folder};
        for(const auto& child:children[path])result.children.push_back(self(self,child));
        return result;
    };
    view.folders->setItems({{"all","All assets",{},true,Icon::Search},{"models","All Models",{},true,Icon::Cube},
        {"textures","Images",{},true,Icon::File},{"prefabs","Particle prefabs",{},true,Icon::Scene},
        tree(tree,root),{"starter","Starter models",{},true,Icon::Folder}});
    view.folders->select(view.category);
}
void GameEditor::browseAssets(const std::string& folder) { if(!m_projects.empty())browseAssets(*m_projects.front(),folder); }
void GameEditor::browseAssets(ProjectView& view,const std::string& folder) {
    view.category=folder;view.page=0;view.grid->viewport().scrollTo(0);view.folders->select(folder);refreshAssets(view);
}
void GameEditor::refreshAssets() { for(auto& view:m_projects)refreshAssets(*view); }
void GameEditor::refreshAssets(ProjectView& view) {
    view.tiles.clear();auto& content=view.grid->content();content.clear();
    const auto query=lower(view.filter->text());
    const auto& category=view.category;
    const auto& root=m_projectAssets->root();
    const bool folderView=category=="project" || category.starts_with("folder:");
    const auto folder=category=="project"?root:(root/fs::u8path(category.starts_with("folder:")?category.substr(7):".")).lexically_normal();
    struct Item { std::optional<size_t> asset;fs::path folder;std::string name; };
    std::vector<Item> items;
    if(folderView && query.empty())for(const auto& child:m_projectAssets->folders())if(child.parent_path()==folder)items.push_back({{},child,child.filename().string()});
    for(size_t i=0;i<m_assets.size();++i) {
        const auto& asset=m_assets[i];
        if(category=="models" && !asset.model)continue;
        if(category=="textures" && !asset.image)continue;
        if(category=="prefabs" && !asset.prefab)continue;
        if(category=="starter" && asset.category!="starter")continue;
        if(folderView && (asset.category!="project" || (query.empty()?asset.path.parent_path()!=folder:!beneath(asset.path,folder))))continue;
        if(!query.empty() && lower(asset.name+" "+asset.path.lexically_relative(root).generic_string()).find(query)==std::string::npos)continue;
        items.push_back({i,{},asset.name});
    }
    constexpr size_t pageSize=128;
    const size_t pages=std::max(size_t(1),(items.size()+pageSize-1)/pageSize);view.page=std::min(view.page,pages-1);
    for(size_t itemIndex=view.page*pageSize;itemIndex<std::min(items.size(),(view.page+1)*pageSize);++itemIndex) {
        const auto& item=items[itemIndex];
        Style tile;tile.width=82;tile.height=86;tile.shrink=0;
        auto& cell=content.column().setStyle(tile);
        const bool selected=item.asset && m_selectedAsset==m_assets[*item.asset].path;
        auto icon=theme::button();icon.width=82;icon.height=60;icon.radius=0;icon.padding=2;
        icon.background=selected?theme::selection:Color{};
        auto action=[this,&view,index=item.asset,path=item.folder]{if(index)inspectAsset(*index);else browseAssets(view,folderId(path,m_projectAssets->root()));};
        auto& picture=cell.button("",action).setStyle(icon).setIcon(item.asset?m_assets[*item.asset].icon:Icon::Folder,42);
        auto name=theme::button();name.height=26;name.fontSize=8;name.padding=Insets(2,2);name.radius=0;
        name.textWrap=TextWrap::Ellipsis;name.background=selected?theme::selection:Color{};
        auto& caption=cell.button(item.name,action).setStyle(name);
        if(item.asset) {
            const auto& asset=m_assets[*item.asset];
            if(asset.model || asset.image)view.tiles.push_back({asset.path,&picture});
            if(asset.model || asset.prefab)for(auto* node:{&picture,&caption})node->onDrag(
                [this,path=asset.path](float x,float y){dragAsset(path,x,y);},[this](bool commit){finishAssetDrag(commit);});
        }
    }
    if(items.empty())content.label(query.empty()?"This folder is empty":"No matching assets").setStyle(theme::label(9,theme::muted));
    auto count=std::to_string(items.size())+(items.size()==1?" item":" items");
    if(pages>1)count+=" | Page "+std::to_string(view.page+1)+"/"+std::to_string(pages);
    if(!m_projectAssets->warning().empty())count+=" | "+m_projectAssets->warning();
    view.count->setText(count);view.prev->setEnabled(view.page>0);view.next->setEnabled(view.page+1<pages);
    view.breadcrumb->setText(folderView?"Project / "+(folder==root?root.filename().string():folder.lexically_relative(root).generic_string()):
        category=="starter"?"Starter models":category=="models"?"All Models":category=="textures"?"Images":category=="prefabs"?"Particle prefabs":"All assets");
}
std::optional<fs::path> GameEditor::selectedAsset() const { return m_selectedAsset; }
void GameEditor::inspectAsset(size_t index) {
    if(index>=m_assets.size())return;
    const auto asset=m_assets[index];
    m_document.cancelInput();select(std::nullopt);m_selectedAsset=asset.path;
    m_assetInspector->setVisible(true);m_emptyInspector->setVisible(false);
    m_assetTitle->setText(asset.name).setIcon(asset.icon,20);
    const std::string kind=asset.model?"Model | Drag into Scene":asset.prefab?"Particle prefab | Drag into Scene":asset.image?"Image":"File";
    m_assetPath->setText(kind+" | "+std::to_string(asset.bytes/1024)+" KB\n"+asset.path.generic_string());
    const auto preview=(asset.model || asset.image)?m_thumbnails.get(asset):nullptr;
    m_assetPreview->setImage(preview).setVisible(bool(preview));m_assetAdd->setEnabled(asset.model || asset.prefab);refreshAssets();
}
std::optional<Vec3> GameEditor::placementPoint(float x,float y) const {
    const auto view=viewport();
    if(view.width<=0 || view.height<=0 || !view.contains(x,y) || m_document.hasPopup())return {};
    const ViewportCamera camera(m_camera);
    const auto ray=cameraRay(camera.position(),camera.yaw(),camera.pitch(),camera.fov(),view.width/view.height,(x-view.x)/view.width,(y-view.y)/view.height);
    float distance=std::abs(ray.direction[1])>.0001f?-ray.origin[1]/ray.direction[1]:-1;
    if(distance<.1f || distance>10000)distance=std::clamp(camera.distance(),.1f,10000.0f);
    Vec3 position{};for(int i=0;i<3;++i){position[i]=ray.origin[i]+ray.direction[i]*distance;if(m_snap)position[i]=std::round(position[i]*2)*.5f;}
    return position;
}
bool GameEditor::placeModel(const fs::path& path,float x,float y) {
    const auto point=placementPoint(x,y);
    const auto extension=lower(path.extension().string());
    return point && ((extension==".gprefab" || extension==".gpreset")?addPrefabAt(path,*point,false):addModelAt(path,*point,false));
}
bool GameEditor::addPrefab(const fs::path& path){return addPrefabAt(path,{0,0,0},true);}
bool GameEditor::addPrefabAt(const fs::path& path,const Vec3& position,bool frame){
    std::error_code ec;const auto resolved=fs::weakly_canonical(path,ec);
    const auto extension=lower(resolved.extension().string());
    if(ec || (extension!=".gprefab" && extension!=".gpreset") || !fs::is_regular_file(resolved,ec)
        || fs::file_size(resolved,ec)>65536 || ec){status("Choose a particle prefab smaller than 64 KB.");return false;}
    std::ifstream input(resolved,std::ios::binary);Json::Value node;Json::CharReaderBuilder parser;std::string error;
    if(!input || !Json::parseFromStream(parser,input,&node,&error) || !node.isObject()
        || !node["format"].isInt() || node["format"].asInt()!=1 || !node["name"].isString()
        || node["name"].asString().empty() || !node.isMember("particle_system")){
        status("Invalid particle prefab: "+path.filename().string());return false;
    }
    gameplay::EntitySnapshot entity;entity.name=node["name"].asString();entity.transform.position=position;
    try{components::deserialize(node,entity);}catch(const std::exception& e){status(std::string("Invalid particle prefab: ")+e.what());return false;}
    if(!entity.particles || !components::valid(entity)){status("Invalid particle prefab components.");return false;}
    if(entity.light && entity.light->enabled && !lightSlotAvailable(entity.light->type,false)){
        status("No available light slot for this particle prefab.");return false;
    }
    m_document.cancelInput();endTransformEdit(true);checkpoint();
    const auto id=m_world.create(entity.name,entity.transform);m_world.copyComponents(id,entity);
    changed();rebuildTree();select(id);if(frame)frameSelection();status("Added "+entity.name+" prefab");return true;
}
void GameEditor::dragAsset(const fs::path& path,float x,float y) {
    m_assetDrag=path;m_assetDragX=x;m_assetDragY=y;
    const auto point=placementPoint(x,y);std::ostringstream text;
    if(point)text<<"Place "<<path.stem().string()<<"  ("<<std::fixed<<std::setprecision(1)<<(*point)[0]<<", "<<(*point)[1]<<", "<<(*point)[2]<<")";
    else text<<"Drop "<<path.filename().string()<<" into Scene";
    auto style=m_assetDragLabel->style();style.left=std::clamp(x+14,0.0f,std::max(0.0f,m_layoutWidth-280));style.top=std::clamp(y+14,0.0f,std::max(0.0f,m_layoutHeight-40));
    style.borderColor=point?theme::accent:theme::muted;m_assetDragLabel->setStyle(style).setText(text.str()).setVisible(true);
}
void GameEditor::finishAssetDrag(bool commit) {
    const auto asset=std::exchange(m_assetDrag,std::nullopt);m_assetDragLabel->setVisible(false);
    if(commit && asset)placeModel(*asset,m_assetDragX,m_assetDragY);
}
}
