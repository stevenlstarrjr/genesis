#include "ui/DockSpace.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace genesis::ui {
namespace {
bool splitAreaId(const std::string& id) {
    constexpr std::string_view prefix="__split_area_";
    return id.starts_with(prefix) && id.size()>prefix.size() &&
        std::all_of(id.begin()+prefix.size(),id.end(),[](unsigned char ch){return ch>='0' && ch<='9';});
}
DockLayout* findIn(DockLayout& layout,const std::string& panel) {
    if(std::find(layout.tabs.begin(),layout.tabs.end(),panel)!=layout.tabs.end())return &layout;
    for(auto& child:layout.children)if(auto* found=findIn(child,panel))return found;
    return nullptr;
}
void removeFrom(DockLayout& layout,const std::string& panel) {
    std::erase(layout.tabs,panel);
    if(layout.active==panel)layout.active=layout.tabs.empty()?"":layout.tabs.front();
    for(auto& child:layout.children)removeFrom(child,panel);
    std::erase_if(layout.children,[](const auto& child){return child.children.empty() && child.tabs.empty();});
    if(layout.children.size()==1){auto remaining=std::move(layout.children.front());layout=std::move(remaining);}
}
Style fill() { Style s;s.grow=1;s.height=0;return s; }
void extent(Node& node,float amount,Direction axis) {
    auto s=node.style();auto& value=axis==Direction::Row?s.width:s.height;
    if(value.unit==Length::Unit::Pixels && std::abs(value.value-amount)<.01f)return;
    value=amount;node.setStyle(s);
}
}
DockLayout DockLayout::group(std::vector<std::string> tabs) {
    DockLayout result;result.tabs=std::move(tabs);if(!result.tabs.empty())result.active=result.tabs.front();return result;
}
DockLayout DockLayout::split(Direction axis,float ratio,DockLayout first,DockLayout second) {
    DockLayout result;result.axis=axis;result.ratio=ratio;result.children={std::move(first),std::move(second)};return result;
}
DockSpace::DockSpace(Document& document,Node& parent,std::function<void()> changed)
    :m_document(document),m_changed(std::move(changed)) {
    auto s=fill();m_root=&parent.column().setStyle(s);
    m_tree=&m_root->column().setStyle(fill());
    m_parking=&m_root->column().setVisible(false);
    // Last child paints above every docked page. It never handles input.
    m_preview=&m_root->column().setVisible(false);
    Style overlay;overlay.absolute=true;overlay.right=0.0f;overlay.bottom=0.0f;
    overlay.stretchX=overlay.stretchY=true;overlay.hitTest=false;
    m_overlay=&m_root->column().setStyle(overlay).setVisible(false);
    m_editorPopup=&document.root().column();
    Style popup;popup.absolute=true;popup.width=200;popup.padding=3;popup.scroll=true;
    popup.background={48,48,48,255};popup.borderWidth=1;popup.borderColor=theme::border;
    popup.radius=3;m_editorPopup->setStyle(popup).setVisible(false);
}
Node& DockSpace::add(std::string id,std::string title,Icon icon,float minWidth,float minHeight,
    std::string blenderIcon) {
    if(id.empty() || m_panels.contains(id) || !std::isfinite(minWidth) || !std::isfinite(minHeight) || minWidth<=0 || minHeight<=0)
        throw std::invalid_argument("UI: invalid dock panel");
    auto s=fill();s.background=theme::panel;
    auto& page=m_parking->column().setStyle(s);
    if(!splitAreaId(id) && id.find('#')==std::string::npos)m_editorOrder.push_back(id);
    m_panels.emplace(std::move(id),Panel{&page,std::move(title),icon,minWidth,minHeight,
        std::move(blenderIcon)});return page;
}
bool DockSpace::setLayout(DockLayout layout) {
    const auto provision=[&](const auto& self,const DockLayout& node)->void {
        for(const auto& id:node.tabs) {
            if(splitAreaId(id))addSplitArea(id);
            else if(!m_panels.contains(id) && instanceId(id))addInstance(id);
        }
        for(const auto& child:node.children)self(self,child);
    };
    provision(provision,layout);
    std::set<std::string> seen;
    const auto valid=[&](const auto& self,const DockLayout& node,int depth)->bool {
        if(depth>16 || !std::isfinite(node.ratio) || node.ratio<=0 || node.ratio>=1)return false;
        if(!node.children.empty()) {
            return node.children.size()==2 && node.tabs.empty() && node.active.empty()
                && self(self,node.children[0],depth+1) && self(self,node.children[1],depth+1);
        }
        if(node.tabs.empty())return depth==0 && node.active.empty();
        if(std::find(node.tabs.begin(),node.tabs.end(),node.active)==node.tabs.end())return false;
        for(const auto& id:node.tabs)if(!m_panels.contains(id) || !seen.insert(id).second)return false;
        return true;
    };
    if(!valid(valid,layout,0))return false;
    m_document.cancelInput();m_layout=std::move(layout);rebuild();notify();return true;
}
DockLayout* DockSpace::find(const std::string& panel) { return findIn(m_layout,panel); }
bool DockSpace::open(const std::string& panel) const {
    return const_cast<DockSpace*>(this)->find(panel)!=nullptr;
}
bool DockSpace::visible(const std::string& panel) const {
    const auto* group=const_cast<DockSpace*>(this)->find(panel);return group && group->active==panel;
}
Rect DockSpace::panelBounds(const std::string& panel) const {
    for(const auto& group:m_groups)if(group.tabs.contains(panel))return group.node->bounds();return {};
}
Rect DockSpace::tabBounds(const std::string& panel) const {
    for(const auto& group:m_groups)if(auto found=group.tabs.find(panel);found!=group.tabs.end())return found->second->bounds();return {};
}
Rect DockSpace::editorButtonBounds(const std::string& panel) const {
    for(const auto& group:m_groups)if(group.tabs.contains(panel) &&
        !(group.layout->tabs.size()==1 && group.layout->active=="scene"))
        return group.editorButton->bounds();
    return {};
}
void DockSpace::notify() { if(m_changed)m_changed(); }
void DockSpace::activate(const std::string& panel,bool focus) {
    auto* group=find(panel);if(!group)return;
    group->active=panel;
    for(auto& mounted:m_groups)if(mounted.layout==group) {
        for(const auto& [id,button]:mounted.tabs) {
            m_panels.at(id).content->setVisible(id==panel);
            auto s=button->style();s.background=id==panel?theme::panel:theme::input;button->setStyle(s);
            if(id==panel && focus)button->focus();
        }
        const auto& chosen=m_panels.at(panel);
        if(chosen.blenderIcon.empty())mounted.editorButton->setIcon(chosen.icon,14);
        else mounted.editorButton->setBlenderIcon(chosen.blenderIcon,14);
    }
    notify();
}
bool DockSpace::selectEditor(const std::string& current,const std::string& next) {
    if(splitAreaId(next) || !m_panels.contains(next))return false;
    auto candidate=m_layout;
    auto* target=findIn(candidate,current);
    if(!target || target->active!=current)return false;
    if(current==next || editorType(current)==editorType(next))return true;
    auto* source=findIn(candidate,next);
    if(source==target){activate(next);return true;}
    auto currentAt=std::find(target->tabs.begin(),target->tabs.end(),current);
    if(currentAt==target->tabs.end())return false;
    if(source && m_builders.contains(editorType(next))) {
        // As in Blender, both areas show the editor; this one gets a copy.
        auto taken=openIds(candidate);
        const auto copy=newInstance(editorType(next),taken);
        *currentAt=copy;target->active=copy;
        return setLayout(std::move(candidate));
    }
    if(source) {
        auto nextAt=std::find(source->tabs.begin(),source->tabs.end(),next);
        if(nextAt==source->tabs.end())return false;
        *nextAt=current;
        if(source->active==next)source->active=current;
    }
    *currentAt=next;target->active=next;
    return setLayout(std::move(candidate));
}
void DockSpace::openEditorMenu(const std::string& panel,Node& anchor) {
    if(!open(panel))return;
    m_document.closePopup();m_editorPopup->clear();m_editorChoices.clear();
    const auto anchorBounds=anchor.bounds(),rootBounds=m_document.root().bounds();
    auto style=m_editorPopup->style();
    style.left=std::clamp(anchorBounds.x,0.0f,std::max(0.0f,rootBounds.width-style.width.value));
    style.top=anchorBounds.y+anchorBounds.height;
    style.maxHeight=std::max(30.0f,rootBounds.height-style.top-4);
    m_editorPopup->setStyle(style);
    for(const auto& id:m_editorOrder) {
        const auto& entry=m_panels.at(id);
        auto buttonStyle=theme::button();buttonStyle.height=24;buttonStyle.minHeight=0;
        buttonStyle.padding=Insets(7,2);buttonStyle.radius=0;buttonStyle.textAlign=0;
        buttonStyle.background=id==editorType(panel)?theme::selection:Color{};
        buttonStyle.hoverBackground=theme::selection;
        auto& choice=m_editorPopup->button(entry.title,[this,panel,id]{
            m_document.closePopup();selectEditor(panel,id);
        }).setStyle(buttonStyle);
        if(entry.blenderIcon.empty())choice.setIcon(entry.icon,14);
        else choice.setBlenderIcon(entry.blenderIcon,14);
        const size_t index=m_editorChoices.size();m_editorChoices.push_back(&choice);
        choice.onKeyDown([this,index](Key key){
            if(key!=Key::Up && key!=Key::Down && key!=Key::Home && key!=Key::End)return false;
            const size_t size=m_editorChoices.size();
            const size_t next=key==Key::Home?0:key==Key::End?size-1:
                (index+size+(key==Key::Up?-1:1))%size;
            m_editorChoices[next]->focus();return true;
        });
    }
    m_document.openPopup(*m_editorPopup,anchor);
}
void DockSpace::show(const std::string& panel) {
    if(!m_panels.contains(panel))return;
    if(open(panel)){activate(panel,true);return;}
    if(m_groups.empty())m_layout=DockLayout::group({panel});
    else { auto& group=*m_groups.front().layout;group.tabs.push_back(panel);group.active=panel; }
    rebuild();activate(panel,true);
}
void DockSpace::close(const std::string& panel) {
    if(!open(panel))return;
    const auto id=panel;m_document.cancelInput();removeFrom(m_layout,id);rebuild();notify();
}
bool DockSpace::dock(const std::string& panel,const std::string& target,DockEdge edge,size_t tabIndex) {
    if(panel==target || !m_panels.contains(panel) || !open(target))return false;
    auto candidate=m_layout;removeFrom(candidate,panel);
    auto* group=findIn(candidate,target);if(!group)return false;
    if(edge==DockEdge::Center) {
        if(splitAreaId(target))std::erase(group->tabs,target);
        group->tabs.insert(group->tabs.begin()+std::min(tabIndex,group->tabs.size()),panel);group->active=panel;
    } else {
        auto original=std::move(*group);auto added=DockLayout::group({panel});
        const bool before=edge==DockEdge::Left || edge==DockEdge::Top;
        *group=DockLayout::split(edge==DockEdge::Left || edge==DockEdge::Right?Direction::Row:Direction::Column,.5f,
            before?std::move(added):std::move(original),before?std::move(original):std::move(added));
    }
    return setLayout(std::move(candidate));
}
void DockSpace::rebuild() {
    m_document.cancelInput();
    for(auto& [id,panel]:m_panels){panel.content->reparent(*m_parking);panel.content->setVisible(false);}
    m_groups.clear();m_splits.clear();m_tree->clear();
    auto treeStyle=m_tree->style();treeStyle.background={};m_tree->setStyle(treeStyle);
    if(m_layout.tabs.empty() && m_layout.children.empty()) {
        treeStyle.background=theme::panel;m_tree->setStyle(treeStyle);
        auto s=theme::label(10,theme::muted);s.padding=20;
        m_tree->label("Workspace empty. Open a panel from the Window menu or choose a Layout preset.").setStyle(s);
    } else build(*m_tree,m_layout);
    arrange();
}
void DockSpace::build(Node& parent,DockLayout& layout) {
    if(!layout.children.empty()) {
        auto s=fill();s.direction=layout.axis;
        auto& node=parent.column().setStyle(s);
        Style firstStyle;firstStyle.shrink=0;auto& first=node.column().setStyle(firstStyle);
        node.splitter(layout.axis,[this,&layout](float delta){
            const auto found=std::find_if(m_splits.begin(),m_splits.end(),[&](const auto& split){return split.layout==&layout;});
            if(found==m_splits.end())return;
            const bool row=layout.axis==Direction::Row;
            const auto b=found->node->bounds();const float span=(row?b.width:b.height)-4;
            if(span<=0)return;
            const auto firstMin=minimum(layout.children[0]),secondMin=minimum(layout.children[1]);
            float low=row?firstMin.first:firstMin.second,high=span-(row?secondMin.first:secondMin.second);
            if(high<low){low=0;high=span;}
            // Several mouse motion events can arrive before the next layout.
            // Accumulate against the requested ratio, not the stale Yoga bounds.
            layout.ratio=std::clamp(std::clamp(layout.ratio*span+delta,low,high)/span,.001f,.999f);
            arrange();notify();
        });
        auto secondStyle=fill();if(layout.axis==Direction::Row){secondStyle.width=0;secondStyle.height={};}
        auto& second=node.column().setStyle(secondStyle);
        m_splits.push_back({&layout,&node,&first,&second});
        build(first,layout.children[0]);build(second,layout.children[1]);return;
    }
    auto area=fill();
    // Keep the scene viewport transparent so the 3D renderer shows through it.
    // The frame covers the corners and restrokes the border above the header
    // and page, which is what visually clips them to the rounded area.
    area.background={};
    area.borderWidth=1;
    area.borderColor=theme::border;
    area.radius=12;
    area.frameColor=theme::canvas;
    auto& node=parent.column().setStyle(area);
    Style strip;strip.direction=Direction::Row;strip.height=23;strip.shrink=0;strip.background=theme::input;
    // Keep header controls clear of the corner action zones.
    strip.padding=Insets(6,0);
    auto& header=node.row().setStyle(strip);
    if(layout.tabs.size()==1 && layout.tabs.front()=="scene")header.setVisible(false);
    Group group{&layout,&node,&header};
    auto selectorStyle=theme::button();selectorStyle.width=35;selectorStyle.height=23;
    selectorStyle.minHeight=0;selectorStyle.shrink=0;selectorStyle.padding=Insets(3,2);
    selectorStyle.radius=0;selectorStyle.background=theme::input;
    auto& selector=header.button("⌄",[]{ }).setStyle(selectorStyle)
        .setTooltip("Editor Type","Choose the editor shown in this area.");
    selector.onClick([this,&layout,&selector]{openEditorMenu(layout.active,selector);});
    const auto& selected=m_panels.at(layout.active);
    if(selected.blenderIcon.empty())selector.setIcon(selected.icon,14);
    else selector.setBlenderIcon(selected.blenderIcon,14);
    group.editorButton=&selector;
    for(const auto& id:layout.tabs) {
        const auto& panel=m_panels.at(id);
        auto s=theme::button();s.height=23;s.radius=0;s.padding=Insets(8,3);s.shrink=1;s.minWidth=30;
        s.background=id==layout.active?theme::panel:theme::input;
        auto& button=header.button(panel.title,[this,id]{activate(id);}).setStyle(s).setIcon(panel.icon,12);
        button.onDrag([this,id](float x,float y){drag(id,x,y);},[this](bool commit){finishDrag(commit);});
        button.onKeyDown([this,id](Key key){
            if(key!=Key::Left && key!=Key::Right && key!=Key::Home && key!=Key::End)return false;
            auto* group=find(id);if(!group)return false;const auto& tabs=group->tabs;
            const auto index=size_t(std::find(tabs.begin(),tabs.end(),id)-tabs.begin());
            const auto next=key==Key::Home?0:key==Key::End?tabs.size()-1:(index+tabs.size()+(key==Key::Left?-1:1))%tabs.size();
            activate(tabs[next],true);return true;
        });
        group.tabs[id]=&button;
    }
    Style spacer;spacer.grow=1;header.column().setStyle(spacer);
    auto closeStyle=theme::button();closeStyle.width=23;closeStyle.height=23;closeStyle.padding=0;closeStyle.radius=0;closeStyle.background=theme::input;
    header.button("x",[this,&layout]{close(layout.active);}).setStyle(closeStyle);
    auto& body=node.column().setStyle(fill());
    for(const auto& id:layout.tabs){auto* content=m_panels.at(id).content;content->reparent(body);content->setVisible(id==layout.active);}
    for(size_t corner=0;corner<group.handles.size();++corner) {
        // The action zone covers the rounded-off corner, like Blender's.
        Style handle;handle.absolute=true;handle.width=12;handle.height=12;handle.padding=0;
        if(corner&1)handle.right=0.0f;else handle.left=0;
        if(corner&2)handle.bottom=0.0f;else handle.top=0;
        handle.cursor=Cursor::Crosshair;handle.hoverBackground={};handle.pressedBackground={};
        handle.focusColor={};
        auto& button=node.button("",[]{ }).setStyle(handle);
        button.onPointerDown([this,&layout,&button,corner](float x,float y){
            m_cornerLayout=nullptr;m_cornerHandle=&button;m_corner=corner;
            m_cornerPressX=x;m_cornerPressY=y;m_cornerAxis.reset();m_cornerAction=CornerAction::None;
            m_cornerSwap=m_document.pointerControl();
            // Resolved on the first drag move; a plain click leaves nothing armed.
            m_cornerPending=&layout;
        });
        button.onDrag([this](float x,float y){dragCorner(x,y);},[this](bool commit){finishCornerDrag(commit);})
            .setTooltip("Split or Join Area",
                "Drag into this area to split it, or out into a neighboring area to join them.");
        group.handles[corner]=&button;
    }
    m_groups.push_back(std::move(group));
}
std::pair<float,float> DockSpace::minimum(const DockLayout& layout) const {
    if(layout.children.empty()) {
        float width=120,height=80;
        for(const auto& id:layout.tabs){const auto& panel=m_panels.at(id);width=std::max(width,panel.minWidth);height=std::max(height,panel.minHeight);}
        // Panel minimums apply to the page inside the one-pixel area border.
        return {width+2,height+2};
    }
    const auto a=minimum(layout.children[0]),b=minimum(layout.children[1]);
    return layout.axis==Direction::Row?std::pair{a.first+b.first+4,std::max(a.second,b.second)}
        :std::pair{std::max(a.first,b.first),a.second+b.second+4};
}
void DockSpace::size(DockLayout& layout,float width,float height) {
    if(layout.children.empty())return;
    const auto found=std::find_if(m_splits.begin(),m_splits.end(),[&](const auto& split){return split.layout==&layout;});
    if(found==m_splits.end())return;
    const bool row=layout.axis==Direction::Row;
    const float span=std::max(0.0f,(row?width:height)-4);
    const auto a=minimum(layout.children[0]),b=minimum(layout.children[1]);
    float low=row?a.first:a.second,high=span-(row?b.first:b.second);
    float first=high<low?span*low/(low+span-high):std::clamp(span*layout.ratio,low,high);
    first=std::clamp(first,0.0f,span);extent(*found->first,first,layout.axis);
    size(layout.children[0],row?first:width,row?height:first);
    size(layout.children[1],row?span-first:width,row?height:span-first);
}
void DockSpace::arrange() { const auto b=m_root->bounds();size(m_layout,b.width,b.height); }
void DockSpace::drag(const std::string& panel,float x,float y) {
    m_dragPanel=panel;m_target.clear();m_tabIndex=SIZE_MAX;m_preview->setVisible(false);
    for(const auto& group:m_groups) {
        const auto b=group.node->bounds();if(!b.contains(x,y))continue;
        for(const auto& id:group.layout->tabs)if(id!=panel){m_target=id;break;}
        if(m_target.empty())return;
        m_edge=DockEdge::Center;auto preview=b;
        if(group.header->bounds().contains(x,y)) {
            m_tabIndex=0;float caret=group.header->bounds().x;
            for(const auto& id:group.layout->tabs) {
                if(id==panel)continue;
                const auto tab=group.tabs.at(id)->bounds();
                if(x<tab.x+tab.width*.5f){caret=tab.x;break;}
                ++m_tabIndex;caret=tab.x+tab.width;
            }
            preview={caret,b.y,3,23};
        } else {
            const float nx=(x-b.x)/b.width,ny=(y-b.y)/b.height;
            const float nearest=std::min({nx,1-nx,ny,1-ny});
            if(nearest<.25f) {
                if(nearest==nx){m_edge=DockEdge::Left;preview.width*=.5f;}
                else if(nearest==1-nx){m_edge=DockEdge::Right;preview.x+=b.width*.5f;preview.width*=.5f;}
                else if(nearest==ny){m_edge=DockEdge::Top;preview.height*=.5f;}
                else {m_edge=DockEdge::Bottom;preview.y+=b.height*.5f;preview.height*=.5f;}
            }
        }
        auto s=Style{};s.absolute=true;s.left=preview.x-m_root->bounds().x;s.top=preview.y-m_root->bounds().y;
        s.width=preview.width;s.height=preview.height;s.background={67,133,196,95};s.borderWidth=2;s.borderColor=theme::accent;
        s.radius=12;
        m_preview->setStyle(s).setVisible(true);return;
    }
}
void DockSpace::finishDrag(bool commit) {
    const auto panel=std::exchange(m_dragPanel,{}),target=std::exchange(m_target,{});
    m_preview->setVisible(false);
    if(commit && !target.empty())dock(panel,target,m_edge,m_tabIndex);
}
void DockSpace::addSplitArea(const std::string& id) {
    if(m_panels.contains(id))return;
    auto& page=add(id,"Editor",Icon::Scene,120,80);
    auto title=theme::label(11);title.padding=Insets(12,10);
    page.label("New Editor Area").setStyle(title);
    auto hint=theme::label(9,theme::muted);hint.padding=Insets(12,3);
    page.label("Drag an editor tab here.").setStyle(hint);
    try { m_nextSplitArea=std::max(m_nextSplitArea,uint32_t(std::stoul(id.substr(13))+1)); }
    catch(const std::exception&) {}
}
std::string DockSpace::editorType(const std::string& panel) { return panel.substr(0,panel.find('#')); }
void DockSpace::setDuplicable(const std::string& id,std::function<void(Node&,const std::string&)> build) {
    if(!m_panels.contains(id) || !build)throw std::invalid_argument("UI: duplicable editor must be a registered panel");
    m_builders[id]=std::move(build);
}
bool DockSpace::instanceId(const std::string& id) const {
    const auto hash=id.find('#');
    return hash!=std::string::npos && hash+1<id.size() && m_builders.contains(id.substr(0,hash)) &&
        std::all_of(id.begin()+hash+1,id.end(),[](unsigned char ch){return ch>='0' && ch<='9';});
}
void DockSpace::addInstance(const std::string& id) {
    const auto type=editorType(id);const auto& base=m_panels.at(type);
    auto& page=add(id,base.title,base.icon,base.minWidth,base.minHeight,base.blenderIcon);
    auto& next=m_nextInstance[type];
    try { next=std::max(next,uint32_t(std::stoul(id.substr(type.size()+1))+1)); }
    catch(const std::exception&) {}
    m_builders.at(type)(page,id);
}
std::string DockSpace::newInstance(const std::string& type,std::set<std::string>& taken) {
    // Reuse a closed copy first, so repeated splits do not grow the panel set.
    for(const auto& [id,panel]:m_panels)
        if(editorType(id)==type && id!=type && !taken.contains(id) && !open(id)){taken.insert(id);return id;}
    auto& next=m_nextInstance[type];next=std::max(next,2u);
    const std::string id=type+"#"+std::to_string(next);
    addInstance(id);taken.insert(id);return id;
}
std::set<std::string> DockSpace::openIds(const DockLayout& layout) const {
    std::set<std::string> result;
    const auto walk=[&](const auto& self,const DockLayout& node)->void {
        result.insert(node.tabs.begin(),node.tabs.end());
        for(const auto& child:node.children)self(self,child);
    };
    walk(walk,layout);return result;
}
void DockSpace::fillEmpty(DockLayout& layout) {
    auto taken=openIds(layout);
    const auto walk=[&](const auto& self,DockLayout& node)->void {
        if(node.children.empty() && node.tabs.empty()) {
            const auto type=node.active;
            node=DockLayout::group({m_builders.contains(type)?newInstance(type,taken):
                "__split_area_"+std::to_string(m_nextSplitArea++)});
            return;
        }
        for(auto& child:node.children)self(self,child);
    };
    walk(walk,layout);
}
void DockSpace::clearOverlay() {
    if(std::exchange(m_overlayShown,false)){m_overlay->clear();m_overlay->setVisible(false);}
}
void DockSpace::overlayRect(Rect bounds,Color fill,float radius) {
    Style style;style.absolute=true;style.hitTest=false;
    style.left=bounds.x-m_root->bounds().x;style.top=bounds.y-m_root->bounds().y;
    style.width=bounds.width;style.height=bounds.height;style.background=fill;style.radius=radius;
    m_overlay->label("").setStyle(style);m_overlay->setVisible(true);m_overlayShown=true;
}
void DockSpace::overlayArrow(Rect bounds,Direction axis,bool forward) {
    // Blender marks the area a join removes with a large arrow pointing into it.
    const char* glyph=axis==Direction::Row?(forward?"\xE2\x86\x92":"\xE2\x86\x90"):(forward?"\xE2\x86\x93":"\xE2\x86\x91");
    const float size=std::clamp(std::min(bounds.width,bounds.height)*.45f,24.0f,96.0f);
    Style style=theme::label(size*.75f,{255,255,255,210});
    style.absolute=true;style.hitTest=false;style.textAlign=.5f;style.textWrap=TextWrap::None;
    style.width=bounds.width;style.height=size*1.4f;style.left=bounds.x-m_root->bounds().x;
    style.top=bounds.y+(bounds.height-size*1.4f)*.5f-m_root->bounds().y;
    m_overlay->label(glyph).setStyle(style);m_overlay->setVisible(true);m_overlayShown=true;
}
std::vector<DockSpace::Cell> DockSpace::cells() const {
    // Each area plus half of every adjoining splitter, so areas tile the tree.
    const auto root=m_tree->bounds();std::vector<Cell> result;
    for(const auto& group:m_groups) {
        auto r=group.node->bounds();
        if(r.x>root.x+.5f){r.x-=2;r.width+=2;}
        if(r.x+r.width<root.x+root.width-.5f)r.width+=2;
        if(r.y>root.y+.5f){r.y-=2;r.height+=2;}
        if(r.y+r.height<root.y+root.height-.5f)r.height+=2;
        result.push_back({r,*group.layout});
    }
    return result;
}
std::optional<DockLayout> DockSpace::tile(const std::vector<Cell>& cells) const {
    // Rebuild a split tree by finding straight cuts that cross no area.
    const auto root=m_tree->bounds();
    const auto build=[&](const auto& self,const std::vector<const Cell*>& part,Rect region)->std::optional<DockLayout> {
        if(part.size()==1)return part.front()->leaf;
        for(const bool row:{true,false}) {
            const float start=row?region.x:region.y,end=start+(row?region.width:region.height);
            const float rootStart=row?root.x:root.y,rootEnd=rootStart+(row?root.width:root.height);
            std::vector<float> cuts;
            for(const auto* cell:part) {
                const float edge=row?cell->r.x+cell->r.width:cell->r.y+cell->r.height;
                if(edge<end-.5f)cuts.push_back(edge);
            }
            std::sort(cuts.begin(),cuts.end());
            for(const float cut:cuts) {
                std::vector<const Cell*> first,second;bool clean=true;
                for(const auto* cell:part) {
                    const float a=row?cell->r.x:cell->r.y,b=a+(row?cell->r.width:cell->r.height);
                    if(b<=cut+.5f)first.push_back(cell);
                    else if(a>=cut-.5f)second.push_back(cell);
                    else {clean=false;break;}
                }
                if(!clean || first.empty() || second.empty())continue;
                Rect before=region,after=region;
                if(row){before.width=cut-region.x;after.x=cut;after.width=end-cut;}
                else {before.height=cut-region.y;after.y=cut;after.height=end-cut;}
                auto a=self(self,first,before),b=self(self,second,after);
                if(!a || !b)return std::nullopt;
                const float inset=start>rootStart+.5f?2.0f:0.0f,outset=end<rootEnd-.5f?2.0f:0.0f;
                const float span=end-start-inset-outset-4,firstSize=cut-start-inset-2;
                const float ratio=span>0?std::clamp(firstSize/span,.001f,.999f):.5f;
                return DockLayout::split(row?Direction::Row:Direction::Column,ratio,std::move(*a),std::move(*b));
            }
        }
        return std::nullopt;
    };
    std::vector<const Cell*> all;for(const auto& cell:cells)all.push_back(&cell);
    if(all.empty())return std::nullopt;
    return build(build,all,root);
}
std::optional<std::vector<DockSpace::Cell>> DockSpace::planJoin(const std::string& source,const std::string& target,
    Rect& removed) const {
    auto all=cells();
    const auto index=[&](const std::string& id){
        return std::find_if(all.begin(),all.end(),[&](const Cell& cell){
            return std::find(cell.leaf.tabs.begin(),cell.leaf.tabs.end(),id)!=cell.leaf.tabs.end();
        });
    };
    const auto s=index(source),t=index(target);
    if(s==all.end() || t==all.end() || s==t)return std::nullopt;
    const Rect S=s->r,T=t->r;
    const auto touching=[](float a,float b){return std::abs(a-b)<1.5f;};
    // Row: the areas sit side by side and share part of a vertical edge.
    bool row;
    if(touching(S.x+S.width,T.x) || touching(T.x+T.width,S.x))row=true;
    else if(touching(S.y+S.height,T.y) || touching(T.y+T.height,S.y))row=false;
    else return std::nullopt;
    const float sLo=row?S.y:S.x,sHi=sLo+(row?S.height:S.width);
    const float tLo=row?T.y:T.x,tHi=tLo+(row?T.height:T.width);
    const float lo=std::max(sLo,tLo),hi=std::min(sHi,tHi);
    const auto crossMin=[&](const DockLayout& leaf){
        if(leaf.tabs.empty() && !m_builders.contains(leaf.active))return row?82.0f:122.0f;
        if(leaf.tabs.empty()){const auto size=minimum(DockLayout::group({leaf.active}));return row?size.second:size.first;}
        const auto size=minimum(leaf);return row?size.second:size.first;
    };
    if(hi-lo<crossMin(s->leaf))return std::nullopt;
    const auto slice=[&](Rect r,float from,float to){
        if(row){r.y=from;r.height=to-from;}else{r.x=from;r.width=to-from;}return r;
    };
    std::vector<Cell> result;
    for(auto it=all.begin();it!=all.end();++it)if(it!=s && it!=t)result.push_back(*it);
    // Like Blender, misaligned leftovers are split off as their own areas. The
    // target's larger leftover keeps its editor; the others get a copy of
    // their editor when it can be duplicated, else start empty.
    const auto leftovers=[&](const Cell& cell,float cellLo,float cellHi,bool keepEditor)->bool {
        std::vector<std::pair<float,float>> parts;
        if(lo-cellLo>.5f)parts.emplace_back(cellLo,lo);
        if(cellHi-hi>.5f)parts.emplace_back(hi,cellHi);
        size_t keep=parts.size();
        if(keepEditor && !parts.empty())
            keep=parts.size()==2 && parts[1].second-parts[1].first>parts[0].second-parts[0].first?1:0;
        for(size_t i=0;i<parts.size();++i) {
            DockLayout leaf=i==keep?cell.leaf:DockLayout{};
            if(i!=keep)leaf.active=editorType(cell.leaf.active);
            if(parts[i].second-parts[i].first<crossMin(leaf))return false;
            result.push_back({slice(cell.r,parts[i].first,parts[i].second),std::move(leaf)});
        }
        return true;
    };
    if(!leftovers(*s,sLo,sHi,false) || !leftovers(*t,tLo,tHi,true))return std::nullopt;
    Rect merged=slice(S,lo,hi);
    if(row){merged.x=std::min(S.x,T.x);merged.width=std::max(S.x+S.width,T.x+T.width)-merged.x;}
    else {merged.y=std::min(S.y,T.y);merged.height=std::max(S.y+S.height,T.y+T.height)-merged.y;}
    result.push_back({merged,s->leaf});
    if(!tile(result))return std::nullopt;
    removed=slice(T,lo,hi);return result;
}
void DockSpace::dragCorner(float x,float y) {
    if(!m_cornerLayout) {
        if(!m_cornerPending || std::hypot(x-m_cornerPressX,y-m_cornerPressY)<8)return;
        m_cornerLayout=std::exchange(m_cornerPending,nullptr);
        // Where corners of several areas meet, act on the area the drag
        // enters, as if its own corner had been pressed. A press at only one
        // area's corner keeps that area, so dragging out of it still joins.
        for(const auto& group:m_groups) {
            if(group.layout==m_cornerLayout)continue;
            const auto b=group.node->bounds();
            if(x<b.x-2 || y<b.y-2 || x>=b.x+b.width+2 || y>=b.y+b.height+2)continue;
            for(size_t corner=0;corner<4;++corner) {
                const float cx=b.x+((corner&1)?b.width:0),cy=b.y+((corner&2)?b.height:0);
                if(std::hypot(m_cornerPressX-cx,m_cornerPressY-cy)<=26){m_cornerLayout=group.layout;m_corner=corner;break;}
            }
            break;
        }
    }
    const auto found=std::find_if(m_groups.begin(),m_groups.end(),[&](const Group& group){
        return group.layout==m_cornerLayout;
    });
    if(found==m_groups.end())return;
    m_cornerAction=CornerAction::None;m_cornerTarget.clear();clearOverlay();
    auto cursor=Cursor::Crosshair;
    const auto b=found->node->bounds();
    const auto all=cells();
    const auto under=std::find_if(all.begin(),all.end(),[&](const Cell& cell){return cell.r.contains(x,y);});
    const bool inside=under!=all.end() && under->leaf.tabs==m_cornerLayout->tabs;
    if(m_cornerSwap) {
        // Ctrl+drag, as in Blender: exchange contents with any other area.
        if(under!=all.end() && !inside && !under->leaf.tabs.empty()) {
            overlayRect(b,{67,133,196,60});overlayRect(under->r,{67,133,196,110});
            m_cornerAction=CornerAction::Swap;m_cornerTarget=under->leaf.tabs.front();
            cursor=Cursor::Arrow;
        }
    } else if(inside) {
        // As in Blender, the split direction follows the drag and a line marks
        // where the area divides; the new area opens on the pressed corner's side.
        const float dx=x-m_cornerPressX,dy=y-m_cornerPressY;
        const bool row=std::abs(dx)>=std::abs(dy);
        m_cornerAxis=row?Direction::Row:Direction::Column;
        const bool before=row?!(m_corner&1):!(m_corner&2);
        const float position=row?x:y,start=row?b.x:b.y,span=row?b.width:b.height;
        const auto sourceSize=minimum(*m_cornerLayout);
        const float sourceMin=row?sourceSize.first:sourceSize.second;
        const auto copy=editorType(m_cornerLayout->active);
        const auto copySize=m_builders.contains(copy)?minimum(DockLayout::group({copy})):std::pair{122.0f,82.0f};
        const float newMin=row?copySize.first:copySize.second,total=span-4;
        if(total<sourceMin+newMin) {
            overlayRect(b,{196,67,67,70});
        } else {
            const float first=std::clamp(position-start-2,before?newMin:sourceMin,total-(before?sourceMin:newMin));
            overlayRect(b,{255,255,255,18});
            Rect line=b;
            if(row){line.x+=first;line.width=4;}else{line.y+=first;line.height=4;}
            overlayRect(line,{235,240,250,235},0);
            m_cornerAction=CornerAction::Split;m_cornerBefore=before;m_cornerRatio=first/total;
            cursor=row?Cursor::ResizeHorizontal:Cursor::ResizeVertical;
        }
    } else if(under!=all.end() && !under->leaf.tabs.empty()) {
        // Joining keeps the source editor; the darkened area is removed.
        Rect removed;
        if(planJoin(m_cornerLayout->tabs.front(),under->leaf.tabs.front(),removed)) {
            overlayRect(removed,{0,0,0,140});
            const bool row=std::abs((removed.x+removed.width*.5f)-(b.x+b.width*.5f))>=
                std::abs((removed.y+removed.height*.5f)-(b.y+b.height*.5f));
            overlayArrow(removed,row?Direction::Row:Direction::Column,
                row?removed.x>b.x:removed.y>b.y);
            m_cornerAction=CornerAction::Join;m_cornerTarget=under->leaf.tabs.front();
            cursor=Cursor::Arrow;
        }
    }
    if(m_cornerHandle && m_cornerHandle->style().cursor!=cursor) {
        auto style=m_cornerHandle->style();style.cursor=cursor;m_cornerHandle->setStyle(style);
    }
}
void DockSpace::finishCornerDrag(bool commit) {
    clearOverlay();
    auto* source=std::exchange(m_cornerLayout,nullptr);m_cornerPending=nullptr;
    const auto action=std::exchange(m_cornerAction,CornerAction::None);
    const auto target=std::exchange(m_cornerTarget,{});
    if(auto* handle=std::exchange(m_cornerHandle,nullptr)) {
        auto style=handle->style();style.cursor=Cursor::Crosshair;handle->setStyle(style);
    }
    if(!commit || !source || source->tabs.empty() || action==CornerAction::None)return;
    const std::string key=source->tabs.front();
    if(action==CornerAction::Split) {
        auto candidate=m_layout;
        auto* group=findIn(candidate,key);if(!group)return;
        // Blender opens the same editor in the new area when it can.
        DockLayout added;added.active=editorType(source->active);
        auto original=std::move(*group);
        *group=DockLayout::split(*m_cornerAxis,std::clamp(m_cornerRatio,.001f,.999f),
            m_cornerBefore?std::move(added):std::move(original),m_cornerBefore?std::move(original):std::move(added));
        fillEmpty(candidate);setLayout(std::move(candidate));return;
    }
    if(action==CornerAction::Swap) {
        auto candidate=m_layout;
        auto *from=findIn(candidate,key),*to=findIn(candidate,target);
        if(!from || !to || from==to)return;
        std::swap(from->tabs,to->tabs);std::swap(from->active,to->active);
        setLayout(std::move(candidate));return;
    }
    Rect removed;
    auto plan=planJoin(key,target,removed);if(!plan)return;
    auto tree=tile(*plan);if(!tree)return;
    fillEmpty(*tree);
    // The removed area's editors close; the Window menu reopens them.
    setLayout(std::move(*tree));
}
}
