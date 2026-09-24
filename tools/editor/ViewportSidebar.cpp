#include "editor/GameEditor.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace genesis::editor {
using namespace ui;

void GameEditor::buildViewportSidebar() {
    constexpr float buttonFontSize=9.0f;
    Style shell;shell.absolute=true;shell.left=0;shell.top=28;shell.width=270;shell.height=395;
    shell.direction=Direction::Row;shell.background={};
    m_viewportSidebar=&m_viewport->row().setStyle(shell).setVisible(false);
    Style options=theme::button();options.absolute=true;options.top=2;options.width=69;
    options.height=21;options.padding=Insets(5,2);options.font=Font::Regular;options.fontSize=buttonFontSize;
    options.background={54,54,54,255};
    m_sidebarOptionsButton=&m_viewport->button("Options ⌄",[this]{openViewportOptionsPopup();})
        .setStyle(options).setTooltip("Viewport Options","Transform options for the scene viewport.");
    Style body;body.width=244;body.grow=1;body.padding=Insets(9,4);body.gap=3;
    body.background={51,51,51,248};body.borderWidth=1;
    body.borderColor={34,34,34,255};body.radius=3;
    auto& content=m_viewportSidebar->column().setStyle(body);
    Style page;page.grow=1;page.gap=3;page.align=Align::Stretch;page.scroll=true;
    for(size_t tab=0;tab<m_sidebarPages.size();++tab)
        m_sidebarPages[tab]=&content.column().setStyle(page).setVisible(tab==0);
    auto itemPage=page;itemPage.gap=2.5f;m_sidebarPages[0]->setStyle(itemPage);

    auto section=theme::label(10);section.font=Font::Semibold;section.height=18;
    section.padding=Insets(4,2);section.background={47,47,47,255};
    auto sublabel=theme::label(9,theme::muted);sublabel.height=15;
    auto& transformHeader=m_sidebarPages[0]->row();
    auto transformHeaderStyle=section;transformHeaderStyle.direction=Direction::Row;
    transformHeader.setStyle(transformHeaderStyle);
    auto transformTitle=theme::label(10);transformTitle.grow=1;transformTitle.font=Font::Semibold;
    transformHeader.label("⌄  Transform").setStyle(transformTitle);
    auto headerMenu=theme::button();headerMenu.width=17;headerMenu.height=16;
    headerMenu.padding=0;headerMenu.background={};headerMenu.font=Font::Regular;headerMenu.fontSize=buttonFontSize;
    transformHeader.button("...",[this]{openViewportOptionsPopup();}).setStyle(headerMenu)
        .setTooltip("Transform Options","Open viewport transform options.");
    Style headerGap;headerGap.height=7;headerGap.shrink=0;
    m_sidebarPages[0]->label("").setStyle(headerGap);
    m_sidebarName=&m_sidebarPages[0]->label("").setStyle(sublabel).setVisible(false);
    const char* groups[]{"Location","Rotation","Scale"};
    const char* axes[]{"X","Y","Z"};
    for(int group=0;group<3;++group) {
        m_sidebarPages[0]->label(groups[group]).setStyle(sublabel);
        for(int axis=0;axis<3;++axis) {
            auto& row=m_sidebarPages[0]->row();
            Style line;line.direction=Direction::Row;line.width=219;line.height=19;line.align=Align::Center;
            line.gap=0;line.shrink=0;row.setStyle(line);
            auto axisStyle=theme::label(9);axisStyle.width=16;axisStyle.height=18;
            axisStyle.textAlign=.5f;
            axisStyle.background={82,82,82,255};row.label(axes[axis]).setStyle(axisStyle);
            auto& field=row.numberField(group==2?1:0,group==2?.001f:-100000,100000,
                group==1?1:.1f,[this,group,axis](float value) {
                    auto transform=selectedTransform();if(!transform)return;
                    auto& values=group==0?transform->position:group==1?transform->rotation:transform->scale;
                    values[axis]=value;setTransform(*transform);
                });
            auto fieldStyle=theme::field();fieldStyle.width=0;fieldStyle.grow=1;
            fieldStyle.height=18;fieldStyle.fontSize=9;fieldStyle.textAlign=1;
            fieldStyle.padding=Insets(4,1);fieldStyle.padding.right=group==1?0:4;
            fieldStyle.background={82,82,82,255};
            fieldStyle.borderWidth=0;fieldStyle.radius=0;
            m_sidebarTransform[group*3+axis]=&field.setStyle(fieldStyle)
                .setNumberDecimals(group==0?4:group==1?0:3).setDragAdjustable();
            if(group<2) {
                auto unit=theme::label(9);unit.width=group==0?15:9;unit.height=18;
                unit.textAlign=.5f;unit.background={82,82,82,255};
                row.label(group==0?"m":"°").setStyle(unit);
            }
            Style lockGap;lockGap.width=20;row.label("").setStyle(lockGap);
            Style lock=theme::button();lock.width=24;lock.height=18;lock.padding=0;
            lock.background={};lock.hoverBackground={63,63,63,255};
            m_sidebarLockButtons[group*3+axis]=&row.button("",[this,group,axis]{
                if(!m_selected)return;
                auto& locks=m_sidebarLocks[*m_selected];
                locks[group*3+axis]=!locks[group*3+axis];
                refreshViewportSidebar();
            }).setStyle(lock).setBlenderIcon("unlocked",12)
                .setTooltip("Lock Axis","Prevent changes to this transform axis.");
        }
        if(group==1){
            Style mount;mount.direction=Direction::Row;mount.height=19;mount.shrink=0;
            m_sidebarRotationMount=&m_sidebarPages[0]->row().setStyle(mount);
        }
    }
    m_sidebarPages[0]->label("Dimensions:").setStyle(sublabel);
    for(int axis=0;axis<3;++axis) {
        auto& row=m_sidebarPages[0]->row();Style line;line.direction=Direction::Row;
        line.width=226;line.height=19;line.shrink=0;row.setStyle(line);
        auto axisStyle=theme::label(9);axisStyle.width=16;axisStyle.height=18;
        axisStyle.textAlign=.5f;
        axisStyle.background={82,82,82,255};row.label(axes[axis]).setStyle(axisStyle);
        auto valueStyle=theme::label(9);valueStyle.grow=1;valueStyle.height=18;
        valueStyle.textAlign=1;
        valueStyle.padding=Insets(4,1);valueStyle.background={82,82,82,255};
        m_sidebarDimensions[axis]=&row.label("—").setStyle(valueStyle);
    }

    m_sidebarPages[1]->label("Active Tool").setStyle(section);
    m_sidebarToolName=&m_sidebarPages[1]->label("Move").setStyle(sublabel);
    const char* toolNames[]{"Select","Move","Rotate","Scale","Transform"};
    for(size_t i=0;i<m_sidebarTools.size();++i) {
        auto& button=m_sidebarPages[1]->button(toolNames[i],[this,i]{setTool(static_cast<TransformTool>(i));});
        auto style=theme::button();style.height=19;style.minHeight=0;style.font=Font::Regular;style.fontSize=buttonFontSize;
        m_sidebarTools[i]=&button.setStyle(style);
    }
    m_sidebarPages[1]->label("Selection").setStyle(section);
    const char* selectNames[]{"Select","Tweak","Box","Circle","Lasso"};
    for(size_t i=0;i<m_sidebarSelectTools.size();++i) {
        auto& button=m_sidebarPages[1]->button(selectNames[i],[this,i]{setSelectionMode(static_cast<SelectionMode>(i));});
        auto style=theme::button();style.height=19;style.minHeight=0;style.font=Font::Regular;style.fontSize=buttonFontSize;
        m_sidebarSelectTools[i]=&button.setStyle(style);
    }
    auto check=theme::label(buttonFontSize);check.height=19;check.padding=Insets(22,1);
    m_sidebarSnap=&m_sidebarPages[1]->checkBox("Snapping",m_snap,[this](bool value){setSnapping(value);}).setStyle(check);
    m_sidebarPages[1]->label("Orientation").setStyle(section);
    auto compactButton=theme::button();compactButton.height=19;
    compactButton.font=Font::Regular;compactButton.fontSize=buttonFontSize;
    m_sidebarPages[1]->button("Global",[this]{setLocalOrientation(false);refreshViewportSidebar();}).setStyle(compactButton);
    m_sidebarPages[1]->button("Local",[this]{setLocalOrientation(true);refreshViewportSidebar();}).setStyle(compactButton);

    m_sidebarPages[2]->label("⌄  View").setStyle(section);
    m_sidebarPages[2]->label("Focal Length").setStyle(sublabel);
    m_sidebarFov=&m_sidebarPages[2]->numberField(31.18f,1,300,1,[this](float value){
        m_camera.fovDegrees=2.0f*std::atan(36.0f/(2.0f*value))*180.0f/3.14159265358979323846f;
        ++m_cameraRestoreRevision;
    }).setStyle([&]{auto s=theme::field();s.height=18;s.fontSize=8;s.shrink=0;return s;}()).setDragAdjustable();
    m_sidebarPages[2]->label("⌄  3D Cursor").setStyle(section);
    m_sidebarPages[2]->label("Location").setStyle(sublabel);
    for(int axis=0;axis<3;++axis) {
        auto& row=m_sidebarPages[2]->row();Style line;line.direction=Direction::Row;
        line.height=19;line.align=Align::Center;line.gap=2;line.shrink=0;row.setStyle(line);
        auto axisStyle=theme::label(8);axisStyle.width=20;axisStyle.textAlign=.5f;
        axisStyle.background={66,66,66,255};row.label(axes[axis]).setStyle(axisStyle);
        auto& field=row.numberField(0,-100000,100000,.1f,[this,axis](float value){
            auto cursor=m_viewportCursor;cursor[axis]=value;setViewportCursor(cursor);
        });
        auto fieldStyle=theme::field();fieldStyle.width=0;fieldStyle.grow=1;
        fieldStyle.height=18;fieldStyle.fontSize=8;fieldStyle.padding=Insets(5,1);
        m_sidebarCursor[axis]=&field.setStyle(fieldStyle).setDragAdjustable();
    }
    m_sidebarPages[2]->label("Viewport Options").setStyle(section);
    m_sidebarPages[2]->button("Frame Selected",[this]{frameSelection();}).setStyle(compactButton);
    m_sidebarGrid=&m_sidebarPages[2]->checkBox("Grid",m_grid,[this](bool value){setGridVisible(value);}).setStyle(check);
    m_sidebarGizmo=&m_sidebarPages[2]->checkBox("Gizmos",m_showGizmo,[this](bool value){setGizmoVisible(value);}).setStyle(check);
    m_sidebarPages[2]->label("Viewport Shading").setStyle(section);
    const char* shading[]{"Wireframe","Solid","Material Preview","Rendered"};
    for(size_t i=0;i<4;++i)m_sidebarPages[2]->button(shading[i],[this,i]{
        setViewportShading(static_cast<ViewportShading>(i));
    }).setStyle(compactButton);

    Style tabs;tabs.width=26;tabs.padding=Insets(1,2);tabs.gap=1;
    tabs.background={};
    auto& tabStrip=m_viewportSidebar->column().setStyle(tabs);
    const char* names[]{"Item","Tool","View"};
    for(size_t i=0;i<m_sidebarTabs.size();++i) {
        auto style=theme::button();style.width=23;style.height=45;style.minHeight=0;
        style.padding=Insets(0);style.font=Font::Regular;style.fontSize=buttonFontSize;
        style.textAlign=.5f;style.verticalText=true;
        style.background={49,49,49,255};style.hoverBackground={75,75,75,255};
        m_sidebarTabs[i]=&tabStrip.button(names[i],[this,i]{showViewportSidebarTab(i);}).setStyle(style)
            .setTooltip(std::string(names[i])+" Sidebar", "", "N");
    }
    showViewportSidebarTab(0);
}
void GameEditor::arrangeViewportSidebar() {
    if(!m_viewportSidebar)return;
    const auto bounds=m_viewport->bounds();
    const float width=std::min(270.0f,std::max(0.0f,bounds.width));
    const float desiredHeight=m_viewportSidebarTab==0?395.0f:m_viewportSidebarTab==1?400.0f:382.0f;
    const float height=std::min(desiredHeight,std::max(0.0f,bounds.height-28.0f));
    const float left=std::max(0.0f,bounds.width-width);
    if(width==m_sidebarWidth && height==m_sidebarHeight &&
        m_viewportSidebar->style().left==left && m_sidebarOptionsButton->style().left==
            std::max(0.0f,bounds.width-69.0f))return;
    m_sidebarWidth=width;m_sidebarHeight=height;
    auto style=m_viewportSidebar->style();style.width=width;style.height=height;
    style.left=left;
    m_viewportSidebar->setStyle(style);
    auto optionStyle=m_sidebarOptionsButton->style();
    optionStyle.left=std::max(0.0f,bounds.width-69.0f);
    m_sidebarOptionsButton->setStyle(optionStyle);
    m_document.layout(m_layoutWidth,m_layoutHeight);
}
void GameEditor::toggleViewportSidebar() {
    m_viewportSidebarVisible=!m_viewportSidebarVisible;
    m_viewportSidebar->setVisible(m_viewportSidebarVisible);
    if(m_viewportSidebarVisible)refreshViewportSidebar();
    m_document.layout(m_layoutWidth,m_layoutHeight);
}
bool GameEditor::viewportSidebarContains(float x,float y) const {
    return !playing() &&
        ((m_viewportSidebarVisible && m_viewportSidebar && m_viewportSidebar->bounds().contains(x,y)) ||
            (m_sidebarOptionsButton && m_sidebarOptionsButton->bounds().contains(x,y)));
}
void GameEditor::showViewportSidebarTab(size_t tab) {
    m_viewportSidebarTab=std::min(tab,m_sidebarPages.size()-1);
    for(size_t i=0;i<m_sidebarPages.size();++i) {
        m_sidebarPages[i]->setVisible(i==m_viewportSidebarTab);
        auto style=m_sidebarTabs[i]->style();
        style.background=i==m_viewportSidebarTab?Color{82,82,82,255}:Color{49,49,49,255};
        m_sidebarTabs[i]->setStyle(style);
    }
    arrangeViewportSidebar();
    refreshViewportSidebar();
}
void GameEditor::refreshViewportSidebar() {
    if(!m_viewportSidebar)return;
    gameplay::EntitySnapshot entity;
    const bool selected=m_selected && m_world.snapshot(*m_selected,entity);
    const auto found=selected?m_sidebarLocks.find(*m_selected):m_sidebarLocks.end();
    m_sidebarName->setText(selected?entity.name:"No object selected");
    for(int group=0;group<3;++group)for(int axis=0;axis<3;++axis) {
        auto* field=m_sidebarTransform[group*3+axis];
        const float value=selected?(group==0?entity.transform.position:
            group==1?entity.transform.rotation:entity.transform.scale)[axis]:(group==2?1.0f:0.0f);
        if(field->value()!=value)field->setValue(value);
        const bool locked=found!=m_sidebarLocks.end() && found->second[group*3+axis];
        field->setEnabled(selected && !locked);
        m_sidebarLockButtons[group*3+axis]->setEnabled(selected);
        m_sidebarLockButtons[group*3+axis]->setBlenderIcon(locked?"locked":"unlocked",12);
    }
    for(int axis=0;axis<3;++axis) {
        if(!selected || !m_hasSelectedDimensions || m_selectedDimensionsId!=m_selected) {
            m_sidebarDimensions[axis]->setText("—");continue;
        }
        std::ostringstream value;value<<std::fixed<<std::setprecision(3)<<m_selectedDimensions[axis]<<" m";
        m_sidebarDimensions[axis]->setText(value.str());
    }
    const char* tools[]{"Select","Move","Rotate","Scale","Transform"};
    m_sidebarToolName->setText(tools[size_t(m_tool)]);
    for(size_t i=0;i<m_sidebarTools.size();++i) {
        auto style=m_sidebarTools[i]->style();
        style.background=i==size_t(m_tool)?theme::selection:Color{52,52,52,255};
        m_sidebarTools[i]->setStyle(style);
    }
    for(size_t i=0;i<m_sidebarSelectTools.size();++i) {
        auto style=m_sidebarSelectTools[i]->style();
        style.background=m_tool==TransformTool::Select && i==size_t(m_selectionMode)?theme::selection:Color{52,52,52,255};
        m_sidebarSelectTools[i]->setStyle(style);
    }
    m_sidebarSnap->setChecked(m_snap);
    m_sidebarGrid->setChecked(m_grid);m_sidebarGizmo->setChecked(m_showGizmo);
    const float focal=36.0f/(2.0f*std::tan(m_camera.fovDegrees*3.14159265358979323846f/360.0f));
    if(!m_sidebarFov->focused() && std::abs(m_sidebarFov->value()-focal)>.01f)
        m_sidebarFov->setValue(focal);
    for(int axis=0;axis<3;++axis)if(!m_sidebarCursor[axis]->focused() &&
        m_sidebarCursor[axis]->value()!=m_viewportCursor[axis])
        m_sidebarCursor[axis]->setValue(m_viewportCursor[axis]);
}
void GameEditor::setViewportCursor(const Vec3& position) {
    m_viewportCursor=position;
    refreshViewportSidebar();
}
void GameEditor::setSelectedDimensions(gameplay::EntityId id,const std::array<float,3>& dimensions) {
    if(!m_selected || *m_selected!=id ||
        (m_hasSelectedDimensions && m_selectedDimensionsId==id && m_selectedDimensions==dimensions))return;
    m_selectedDimensions=dimensions;m_selectedDimensionsId=id;m_hasSelectedDimensions=true;
    refreshViewportSidebar();
}
void GameEditor::buildViewportOptionsPopup() {
    m_sidebarOptionsPopup=&m_document.root().column();
    Style popup;popup.absolute=true;popup.width=218;popup.padding=Insets(8,7);popup.gap=4;
    popup.background={30,30,30,255};popup.borderWidth=1;popup.borderColor={20,20,20,255};
    popup.radius=3;m_sidebarOptionsPopup->setStyle(popup).setVisible(false);
    auto heading=theme::label(9);heading.height=17;
    m_sidebarOptionsPopup->label("Transform").setStyle(heading);
    auto rowStyle=Style{};rowStyle.direction=Direction::Row;rowStyle.height=19;rowStyle.shrink=0;
    auto& affect=m_sidebarOptionsPopup->row().setStyle(rowStyle);
    auto label=theme::label(9);label.width=78;affect.label("Affect Only").setStyle(label);
    auto unavailable=theme::label(9);unavailable.height=19;unavailable.padding=Insets(22,1);
    affect.checkBox("Origins",false,[](bool){}).setStyle(unavailable).setEnabled(false);
    for(const char* name:{"Locations","Parents"}) {
        auto& row=m_sidebarOptionsPopup->row().setStyle(rowStyle);
        auto spacer=Style{};spacer.width=78;row.label("").setStyle(spacer);
        row.checkBox(name,false,[](bool){}).setStyle(unavailable).setEnabled(false);
    }
}
void GameEditor::openViewportOptionsPopup() {
    if(!m_sidebarOptionsPopup || playing())return;
    m_document.closePopup();
    const auto button=m_sidebarOptionsButton->bounds(),viewport=m_viewport->bounds();
    auto style=m_sidebarOptionsPopup->style();
    style.left=std::max(viewport.x,button.x+button.width-218.0f);
    style.top=button.y+button.height+5.0f;
    m_sidebarOptionsPopup->setStyle(style);
    m_document.openPopup(*m_sidebarOptionsPopup,*m_sidebarOptionsButton);
}
}
