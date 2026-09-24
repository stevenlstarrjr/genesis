#include "editor/GameEditor.h"
#include <json/json.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace genesis::editor {
namespace fs=std::filesystem;
namespace {
Json::Value encode(const ui::DockLayout& layout) {
    Json::Value result;
    if(layout.children.empty()) {
        result["tabs"]=Json::arrayValue;
        for(const auto& id:layout.tabs)result["tabs"].append(id);
        result["active"]=layout.active;
    } else {
        result["axis"]=layout.axis==ui::Direction::Row?"horizontal":"vertical";
        result["ratio"]=layout.ratio;
        result["first"]=encode(layout.children[0]);result["second"]=encode(layout.children[1]);
    }
    return result;
}
ui::DockLayout decode(const Json::Value& value,int depth=0) {
    if(depth>16 || !value.isObject())throw std::runtime_error("Invalid workspace tree");
    ui::DockLayout result;
    if(value.isMember("tabs")) {
        if(!value["tabs"].isArray() || value["tabs"].size()>6 || !value["active"].isString()
            || value.isMember("first") || value.isMember("second") || value.isMember("axis"))throw std::runtime_error("Invalid workspace tabs");
        for(const auto& tab:value["tabs"]){if(!tab.isString())throw std::runtime_error("Invalid panel ID");result.tabs.push_back(tab.asString());}
        result.active=value["active"].asString();
    } else {
        if(!value["axis"].isString() || (value["axis"]!="horizontal" && value["axis"]!="vertical") || !value["ratio"].isNumeric())
            throw std::runtime_error("Invalid workspace split");
        result.axis=value["axis"]=="horizontal"?ui::Direction::Row:ui::Direction::Column;
        result.ratio=value["ratio"].asFloat();
        result.children={decode(value["first"],depth+1),decode(value["second"],depth+1)};
    }
    return result;
}
bool removeLegacyGameTab(ui::DockLayout& layout) {
    if(layout.children.empty()) {
        std::erase(layout.tabs,"game");
        if(std::find(layout.tabs.begin(),layout.tabs.end(),layout.active)==layout.tabs.end())
            layout.active=layout.tabs.empty()?"":layout.tabs.front();
        return !layout.tabs.empty();
    }
    const bool first=removeLegacyGameTab(layout.children[0]);
    const bool second=removeLegacyGameTab(layout.children[1]);
    if(first && second)return true;
    if(first || second)layout=std::move(layout.children[first?0:1]);
    else layout={};
    return first || second;
}
}
GameEditor::~GameEditor() { stop();if(m_layoutPending)saveLayout(); }
void GameEditor::applyLayoutPreset(const std::string& name) {
    using ui::DockLayout;using ui::Direction;
    // Ratios are chosen at the current window size, then remain proportional.
    const float width=std::max(m_layoutWidth,1100.0f),height=std::max(m_layoutHeight,720.0f)-44;
    auto hierarchy=DockLayout::group({"hierarchy"}),scene=DockLayout::group({"scene"});
    auto inspector=DockLayout::group({"inspector"}),project=DockLayout::group({"project","console"});
    const float mainWidth=width-324,upperHeight=height-236;
    ui::DockLayout layout;
    if(name=="Tall") {
        auto center=DockLayout::split(Direction::Column,upperHeight/(height-4),std::move(scene),std::move(project));
        auto main=DockLayout::split(Direction::Row,220/(mainWidth-4),std::move(hierarchy),std::move(center));
        layout=DockLayout::split(Direction::Row,mainWidth/(width-4),std::move(main),std::move(inspector));
    } else if(name=="Wide") {
        auto right=DockLayout::split(Direction::Column,.4f,std::move(hierarchy),std::move(inspector));
        auto top=DockLayout::split(Direction::Row,mainWidth/(width-4),std::move(scene),std::move(right));
        layout=DockLayout::split(Direction::Column,upperHeight/(height-4),std::move(top),std::move(project));
    } else {
        auto top=DockLayout::split(Direction::Row,220/(mainWidth-4),std::move(hierarchy),std::move(scene));
        auto main=DockLayout::split(Direction::Column,upperHeight/(height-4),std::move(top),std::move(project));
        layout=DockLayout::split(Direction::Row,mainWidth/(width-4),std::move(main),std::move(inspector));
    }
    m_dock->setLayout(std::move(layout));this->layout(m_layoutWidth,m_layoutHeight);
}
void GameEditor::loadLayout() {
    const bool loading=m_loadingLayout;m_loadingLayout=true;
    try {
        if(m_layoutPath.empty() || !fs::exists(m_layoutPath)){applyLayoutPreset("Default");m_loadingLayout=loading;return;}
        if(fs::file_size(m_layoutPath)>65536)throw std::runtime_error("Workspace file is too large");
        std::ifstream input(m_layoutPath);Json::Value root;Json::CharReaderBuilder reader;std::string error;
        reader["rejectDupKeys"]=true;reader["failIfExtra"]=true;
        if(!input || !Json::parseFromStream(reader,input,&root,&error) || !root["version"].isInt() || root["version"].asInt()!=1)
            throw std::runtime_error("Invalid or unsupported workspace layout");
        auto saved=decode(root["workspace"]);
        const bool deliberatelyEmpty=saved.children.empty() && saved.tabs.empty();
        if((!deliberatelyEmpty && !removeLegacyGameTab(saved)) || !m_dock->setLayout(std::move(saved)))
            throw std::runtime_error("Invalid or unsupported workspace layout");
        layout(m_layoutWidth,m_layoutHeight);
    } catch(const std::exception&) {
        applyLayoutPreset("Default");status("Saved workspace could not be loaded. Using the Default layout.");
    }
    m_loadingLayout=loading;
}
bool GameEditor::saveLayout() {
    if(m_loadingLayout)return false;
    // Clear the retry flag even on failure; another edit or explicit Save retries.
    m_layoutPending=false;
    if(m_layoutPath.empty())return false;
    try {
        fs::create_directories(m_layoutPath.parent_path());
        Json::Value root;root["version"]=1;root["workspace"]=encode(m_dock->layout());
        Json::StreamWriterBuilder writer;writer["indentation"]="  ";
        const fs::path temporary=m_layoutPath.string()+".saving";
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<Json::writeString(writer,root);output.flush();
            if(!output)throw std::runtime_error("Cannot write workspace layout");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),m_layoutPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace workspace layout");
#else
        fs::rename(temporary,m_layoutPath);
#endif
        return true;
    } catch(const std::exception&) { status("Workspace layout save failed. Check the project folder is writable.");return false; }
}
}
