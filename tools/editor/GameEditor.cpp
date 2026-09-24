#include "editor/GameEditor.h"
#include "ui/Theme.h"
#include "SceneComponents.h"
#include "ModelValidation.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace genesis::ui;
namespace fs = std::filesystem;
namespace genesis::editor {
namespace {
Style labelStyle(float size=10, Color color=theme::muted) { return theme::label(size,color); }
Node& compactButton(Node& parent,const std::string& text,std::function<void()> action) {
    return parent.button(text,std::move(action)).setStyle(theme::button());
}
std::string lower(std::string text) { for(auto& c:text) c=char(std::tolower(static_cast<unsigned char>(c))); return text; }
Style toolbarStyle() {
    Style s; s.direction=Direction::Row; s.height=24; s.shrink=0; s.align=Align::Center;
    s.padding=Insets(4,1); s.gap=3; s.background=theme::raised; return s;
}
bool sameTransform(const gameplay::Transform& a,const gameplay::Transform& b) {
    return a.position==b.position && a.rotation==b.rotation && a.scale==b.scale;
}
bool validTransform(const gameplay::Transform& transform) {
    for(auto* vector:{&transform.position,&transform.rotation,&transform.scale})for(float v:*vector)if(!std::isfinite(v))return false;
    for(float scale:transform.scale)if(std::abs(scale)<.001f)return false;
    return true;
}
}
GameEditor::GameEditor(gameplay::GameplayWorld& world,SceneDocument& scene,const fs::path& assets,const fs::path& projectRoot)
    : m_world(world),m_scene(scene) {
    Style root; m_document.root().setStyle(root);
    const auto label=[](float size=9,Color color=theme::text){return theme::label(size,color);};
    const auto toolbar=[]() {
        Style s; s.direction=Direction::Row; s.height=24; s.shrink=0; s.align=Align::Center;
        s.padding=Insets(4,1); s.gap=3; s.background=theme::raised; return s;
    };
    // The menu bar also carries playback and workspace controls. Popup content is mounted last.
    auto menuStyle=toolbar(); menuStyle.padding=0; menuStyle.background=theme::input;
    auto& menuRow=m_document.root().row().setStyle(menuStyle);
    Style menuZone;menuZone.direction=Direction::Row;menuZone.width=0;menuZone.grow=1;
    menuZone.align=Align::Center;menuZone.shrink=1;
    auto& leftZone=menuRow.row().setStyle(menuZone);
    auto& menuMount=leftZone.row();
    auto mountStyle=menuMount.style();mountStyle.shrink=0;menuMount.setStyle(mountStyle);
    Style playbackStyle;playbackStyle.direction=Direction::Row;playbackStyle.align=Align::Center;
    playbackStyle.gap=2;playbackStyle.shrink=0;
    auto& playback=menuRow.row().setStyle(playbackStyle);
    auto iconButton=theme::button();iconButton.width=25;iconButton.height=21;
    iconButton.minHeight=0;iconButton.padding=0;iconButton.radius=2;
    iconButton.borderWidth=0;iconButton.background={45,45,45,255};
    m_playButton=&playback.button("",[this]{play();}).setStyle(iconButton).setIcon(Icon::Play,12)
        .setTooltip("Play","Run the scene.");
    m_pauseButton=&playback.button("",[this]{pause();}).setStyle(iconButton).setIcon(Icon::Pause,12)
        .setTooltip("Pause","Pause the running scene.").setEnabled(false);
    m_stepButton=&playback.button("",[this]{step();}).setStyle(iconButton).setIcon(Icon::Step,12)
        .setTooltip("Step","Advance one frame while paused.").setEnabled(false);
    m_stopButton=&playback.button("",[this]{stop();}).setStyle(iconButton).setGodotIcon("Stop",12)
        .setTooltip("Stop","Restore the edit scene.").setEnabled(false);
    auto rightZone=menuZone;rightZone.justify=Justify::End;rightZone.gap=5;
    auto& commands=menuRow.row().setStyle(rightZone);
    auto& layoutMount=commands.row();

    Style workspace; workspace.direction=Direction::Row; workspace.grow=1;
    m_dock=std::make_unique<DockSpace>(m_document,m_document.root(),[this]{
        if(!m_loadingLayout){m_layoutPending=true;m_layoutChanged=std::chrono::steady_clock::now();}
    });
    auto& left=m_dock->add("hierarchy","Hierarchy",Icon::Scene,180,200,"outliner");
    auto& center=m_dock->add("scene","Scene",Icon::Scene,520,280,"view3d");
    auto& project=m_dock->add("project","Project",Icon::Folder,340,170,"filebrowser");
    auto& consolePage=m_dock->add("console","Console",Icon::Console,240,140,"console");
    auto& right=m_dock->add("inspector","Inspector",Icon::File,300,240,"properties");
    auto empty=label(9,theme::muted); empty.textWrap=TextWrap::Word; empty.padding=8;
    m_projectAssets=std::make_unique<ProjectAssets>(projectRoot.empty()?scene.path().parent_path():projectRoot,assets);
    buildHierarchy(left,"hierarchy");buildProject(project,"project");buildConsole(consolePage,"console");
    m_dock->setDuplicable("hierarchy",[this](Node& page,const std::string& id){
        buildHierarchy(page,id);rebuildTree(*m_hierarchies.back());
    });
    m_dock->setDuplicable("project",[this](Node& page,const std::string& id){
        buildProject(page,id);rebuildAssetFolders(*m_projects.back());refreshAssets(*m_projects.back());
    });
    m_dock->setDuplicable("console",[this](Node& page,const std::string& id){
        buildConsole(page,id);refreshConsole(*m_consoles.back());
    });
    Style viewportHeader;viewportHeader.direction=Direction::Row;viewportHeader.height=27;
    viewportHeader.shrink=0;viewportHeader.align=Align::Center;viewportHeader.gap=2;
    viewportHeader.padding=Insets(4,1);viewportHeader.background={43,43,43,255};
    auto& viewportTools=center.row().setStyle(viewportHeader);
    m_viewportHeader=&viewportTools;
    auto headerGroup=[]() {Style s;s.direction=Direction::Row;s.align=Align::Center;s.shrink=0;s.gap=2;return s;};
    auto headerButton=[](Node& parent,const std::string& caption,std::function<void()> action) -> Node& {
        auto style=theme::button();style.height=23;style.minHeight=0;style.radius=3;
        style.fontSize=9;style.padding=Insets(5,2);style.background={48,48,48,255};
        style.hoverBackground={65,65,65,255};style.borderWidth=0;
        return parent.button(caption,std::move(action)).setStyle(style);
    };
    auto& headerLeft=viewportTools.row().setStyle(headerGroup());
    auto editorTypeStyle=theme::button();editorTypeStyle.width=35;editorTypeStyle.height=23;
    editorTypeStyle.minHeight=0;editorTypeStyle.padding=Insets(3,2);editorTypeStyle.radius=3;
    editorTypeStyle.background={48,48,48,255};editorTypeStyle.hoverBackground={65,65,65,255};
    m_sceneEditorTypeButton=&headerLeft.button("⌄",[this]{
        m_dock->openEditorMenu("scene",*m_sceneEditorTypeButton);
    }).setStyle(editorTypeStyle).setBlenderIcon("view3d",14)
        .setTooltip("Editor Type","Choose the editor shown in this area.");
    auto& modeMount=headerLeft.row().setStyle(headerGroup());
    m_meshModeMount=&headerLeft.row().setStyle(headerGroup());
    m_meshModeMount->setVisible(false);
    auto& viewportMenuMount=headerLeft.row().setStyle(headerGroup());
    auto headerSpacer=Style{};headerSpacer.width=0;headerSpacer.grow=1;
    viewportTools.row().setStyle(headerSpacer);
    auto& headerMiddle=viewportTools.row().setStyle(headerGroup());
    auto& orientationMount=headerMiddle.row().setStyle(headerGroup());
    auto& pivotMount=headerMiddle.row().setStyle(headerGroup());
    m_snapHeaderButton=&headerButton(headerMiddle,"",[this]{setSnapping(!m_snap);});
    auto snapStyle=m_snapHeaderButton->style();snapStyle.width=24;snapStyle.padding=0;
    m_snapHeaderButton->setStyle(snapStyle).setBlenderIcon("snap_off",15)
        .setTooltip("Snap","Snap transforms to the selected increment.");
    auto& snapOptionsMount=headerMiddle.row().setStyle(headerGroup());
    viewportTools.row().setStyle(headerSpacer);
    auto& headerRight=viewportTools.row().setStyle(headerGroup());
    m_gizmoHeaderButton=&headerButton(headerRight,"",[this]{setGizmoVisible(!m_showGizmo);});
    auto gizmoStyle=m_gizmoHeaderButton->style();gizmoStyle.width=24;gizmoStyle.padding=0;
    gizmoStyle.background={64,93,124,255};
    m_gizmoHeaderButton->setStyle(gizmoStyle).setBlenderIcon("gizmo",15)
        .setTooltip("Show Gizmo","Show transform gizmos in the viewport.");
    m_gridHeaderButton=&headerButton(headerRight,"",[this]{setGridVisible(!m_grid);});
    auto gridHeaderStyle=m_gridHeaderButton->style();gridHeaderStyle.width=24;gridHeaderStyle.padding=0;
    gridHeaderStyle.background={64,93,124,255};
    m_gridHeaderButton->setStyle(gridHeaderStyle).setBlenderIcon("overlay",15)
        .setTooltip("Show Overlays","Show the grid and viewport overlays.");
    auto& overlayOptionsMount=headerRight.row().setStyle(headerGroup());
    const char* shadingNames[]{"Wireframe","Solid","Material Preview","Rendered"};
    const char* shadingIcons[]{"shading_wire","shading_solid","shading_texture","shading_rendered"};
    for(size_t index=0;index<m_shadingButtons.size();++index) {
        auto& button=headerButton(headerRight,"",[this,index,shadingNames]{
            setViewportShading(static_cast<ViewportShading>(index));
            status(std::string("Viewport shading: ")+shadingNames[index]);
        });
        auto style=button.style();style.width=23;style.padding=0;
        m_shadingButtons[index]=&button.setStyle(style).setBlenderIcon(shadingIcons[index],15)
            .setTooltip(shadingNames[index],std::string("Use ")+shadingNames[index]+" viewport shading.");
    }
    auto& shadingOptionsMount=headerRight.row().setStyle(headerGroup());
    setViewportShading(m_viewportShading);
    auto check=theme::button();check.fontSize=9;check.height=20;check.minHeight=0;
    setQuality("medium");
    Style viewport; viewport.grow=1; m_viewport=&center.column().setStyle(viewport);
    Style strip; strip.absolute=true; strip.left=8; strip.top=8; strip.width=42;
    strip.padding=Insets(3); strip.gap=2; strip.background={34,34,34,225};
    strip.borderWidth=1; strip.borderColor={23,23,23,255}; strip.radius=4;
    m_viewportToolStrip=&m_viewport->column().setStyle(strip);
    buildViewportSidebar();
    const char* viewportToolIcons[]{"ToolSelect","ToolMove","ToolRotate","ToolScale","ToolTransform"};
    const char* selectionIcons[]{"select_set","select_tweak","select_box","select_circle","select_lasso"};
    auto toolButtonStyle=theme::button(); toolButtonStyle.width=34; toolButtonStyle.height=30;
    toolButtonStyle.padding=0; toolButtonStyle.radius=3; toolButtonStyle.background={45,45,45,255};
    toolButtonStyle.hoverBackground={72,72,72,255};
    const char* selectionNames[]{"Select","Tweak","Box Select","Circle Select","Lasso Select"};
    const char* selectionHints[]{"Pick an object.","Select and move an object.","Drag a rectangle to select.",
        "Paint selection with a circle. Scroll to resize it.","Draw a freeform area to select."};
    for(size_t i=0;i<m_selectionButtons.size();++i)
        m_selectionButtons[i]=&m_viewportToolStrip->button("",[this,i]{setSelectionMode(static_cast<SelectionMode>(i));})
            .setStyle(toolButtonStyle).setBlenderIcon(selectionIcons[i],21)
            .setTooltip(selectionNames[i],selectionHints[i]);
    m_viewportToolButtons[0]=m_selectionButtons[0];
    auto stripGap=Style{}; stripGap.height=6; stripGap.shrink=0;
    m_viewportToolStrip->label("").setStyle(stripGap);
    m_cursorToolButton=&m_viewportToolStrip->button("",[this]{setCursorPlacement(!m_cursorPlacement);})
        .setStyle(toolButtonStyle).setBlenderIcon("cursor",21)
        .setTooltip("3D Cursor","Click in the viewport to place the 3D cursor.");
    m_viewportToolStrip->label("").setStyle(stripGap);
    const char* transformNames[]{"Select","Move","Rotate","Scale","Transform"};
    const char* transformHints[]{"Select objects.","Move the selection.","Rotate the selection.",
        "Scale the selection.","Move, rotate, and scale the selection."};
    const char* transformShortcuts[]{"Q","W","E","R","T"};
    for(size_t i=1;i<m_viewportToolButtons.size();++i) {
        m_viewportToolButtons[i]=&m_viewportToolStrip->button("",[this,i]{setTool(static_cast<TransformTool>(i));})
            .setStyle(toolButtonStyle).setGodotIcon(viewportToolIcons[i],22)
            .setTooltip(transformNames[i],transformHints[i],transformShortcuts[i]);
    }
    m_viewportToolStrip->label("").setStyle(stripGap);
    m_viewportToolStrip->button("",[this,assets]{
        addModel(assets/"glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb");
    }).setStyle(toolButtonStyle).setBlenderIcon("add",22)
        .setTooltip("Add Object","Add a box model to the scene.");
    setTool(m_tool);
    // The viewport is a transparent hole through the UI surface for either camera.
    auto sceneStyle=center.style();sceneStyle.background={};center.setStyle(sceneStyle);
    const auto sample=assets/"glTF-Sample-Models/2.0";

    m_emptyInspector=&right.label("Select an object or asset to view its properties.").setStyle(empty);
    m_assetInspector=&right.column(); auto assetInspectorStyle=m_assetInspector->style(); assetInspectorStyle.padding=10; assetInspectorStyle.gap=8;
    m_assetInspector->setStyle(assetInspectorStyle).setVisible(false);
    m_assetTitle=&m_assetInspector->label("").setStyle(label(10));
    m_assetPath=&m_assetInspector->label("").setStyle(empty);
    auto previewStyle=Style{};previewStyle.height=140;previewStyle.padding=8;previewStyle.background=theme::input;
    m_assetPreview=&m_assetInspector->label("").setStyle(previewStyle).setVisible(false);
    m_assetAdd=&compactButton(*m_assetInspector,"Add to Scene",[this]{if(m_selectedAsset) addModel(*m_selectedAsset);});
    m_inspector=std::make_unique<ScrollView>(right);
    auto inspectorContent=m_inspector->content().style(); inspectorContent.padding=0; m_inspector->content().setStyle(inspectorContent);
    auto& identity=m_inspector->content().column(); Style identityStyle; identityStyle.padding=Insets(8,8); identityStyle.gap=4; identityStyle.shrink=0; identity.setStyle(identityStyle);
    auto& objectHeader=identity.row(); auto objectRow=objectHeader.style(); objectRow.gap=4; objectRow.align=Align::Center; objectRow.shrink=0; objectHeader.setStyle(objectRow);
    auto objectIcon=label(); objectIcon.width=26; objectIcon.height=24; objectHeader.label("").setStyle(objectIcon).setIcon(Icon::Cube,22);
    m_visible=&objectHeader.checkBox("",true,[this](bool visible){
        if(!m_selected) return;checkpoint();m_world.setVisible(*m_selected,visible);changed();rebuildTree();
    }); auto active=check; active.width=20; active.padding=0; m_visible->setStyle(active);
    m_name=&objectHeader.textField("",{}); auto nameStyle=theme::field(); nameStyle.grow=1; m_name->setStyle(nameStyle);
    m_name->setPlaceholder("Object name").onTextCommitted([this](const std::string& name){
        if(!m_selected) return;gameplay::EntitySnapshot entity;if(!m_world.snapshot(*m_selected,entity)||entity.name==name)return;
        checkpoint();m_world.setName(*m_selected,name);changed();rebuildTree();updateInspector();
    });
    m_selection=&identity.label("").setStyle(label(8,theme::muted));
    auto& transform=section(m_inspector->content(),"Transform");
    const char* groups[]{"Position","Rotation","Scale"};const char* axes[]{"X","Y","Z"};
    for(int group=0;group<3;++group) {
        auto& row=transform.row();auto s=row.style();s.align=Align::Center;s.gap=3;s.shrink=0;row.setStyle(s);
        s=label();s.width=61;row.label(groups[group]).setStyle(s);
        for(int axis=0;axis<3;++axis) {
            auto& cell=row.row();s=cell.style();s.width=0;s.grow=1;s.gap=2;s.align=Align::Center;cell.setStyle(s);
            s=label(8,theme::muted);s.width=10;cell.label(axes[axis]).setStyle(s);
            auto& field=cell.numberField(group==2?1:0,group==2?.001f:-100000,100000,group==1?1:.1f,[this,group,axis](float value){
                gameplay::EntitySnapshot entity;if(!m_selected || !m_world.snapshot(*m_selected,entity))return;
                auto& vector=group==0?entity.transform.position:group==1?entity.transform.rotation:entity.transform.scale;
                vector[axis]=value;setTransform(entity.transform);
            });
            s=theme::field();s.grow=1;s.width=0;s.padding=Insets(3,2);field.setStyle(s);m_transform[group*3+axis]=&field;
        }
    }
    buildComponentInspector();

    auto statusBar=toolbar();statusBar.height=20;statusBar.background=theme::input;statusBar.padding=Insets(6,2);
    auto& footer=m_document.root().row().setStyle(statusBar);
    auto statusStyle=label(8,theme::muted);statusStyle.grow=1;statusStyle.shrink=1;
    m_status=&footer.label("Ready").setStyle(statusStyle);
    footer.label("Q/W/E/R/T Tools  |  Shift+RMB 3D Cursor  |  Ctrl Snap  |  MMB Orbit  |  F Frame").setStyle(label(8,theme::muted));
    m_menus=std::make_unique<MenuBar>(m_document,menuMount);
    const auto selected=[this]{return m_selected.has_value();};
    m_menus->add("File",{{"Save Scene","Ctrl+S",[this]{save();},[this]{return !playing();}},{"Play Scene","",[this]{play();}}});
    m_menus->add("Edit",{
        {"Undo","Ctrl+Z",[this]{undo();},[this]{return !playing() && !m_undo.empty();}},
        {"Redo","Ctrl+Y",[this]{redo();},[this]{return !playing() && !m_redo.empty();}},
        {},{"Duplicate","Ctrl+D",[this]{duplicate();},selected},{"Delete","Delete",[this]{remove();},selected},
        {},{"Frame Selected","F",[this]{frameSelection();}}
    });
    m_menus->add("Assets",{
        {"Show Project","",[this]{showConsole(false);}},
        {"Add Cube","",[this,sample]{addModel(sample/"Box/glTF-Binary/Box.glb");}},
        {"Add Textured Cube","",[this,sample]{addModel(sample/"BoxTextured/glTF-Binary/BoxTextured.glb");}},
        {"Add Helmet","",[this,sample]{addModel(sample/"DamagedHelmet/glTF-Binary/DamagedHelmet.glb");}}
    });
    m_menus->add("GameObject",{{"Create Empty","",[this]{createEmpty();}},{"Duplicate","Ctrl+D",[this]{duplicate();},selected},{"Delete","Delete",[this]{remove();},selected}});
    m_menus->add("Component",{{"Add Component...","",[this]{openComponentPicker();},selected},{"Reset Transform","",[this]{setTransform({});},selected}});
    m_menus->add("Window",{
        {"Hierarchy","",[this]{m_dock->show("hierarchy");}},
        {"Scene","",[this]{m_dock->show("scene");}},
        {"Inspector","",[this]{m_dock->show("inspector");}},
        {"Project","",[this]{showConsole(false);}},{"Console","",[this]{showConsole();}},
        {},{"Reset Layout","",[this]{resetLayout();}}
    });
    m_menus->add("Help",{{"Editor Controls","",[this]{status("MMB: orbit | Shift+MMB: pan | Wheel: zoom | F: frame | Ctrl+S: save | Ctrl+D: duplicate");showConsole();}}});
    m_layoutMenu=std::make_unique<MenuBar>(m_document,layoutMount);
    m_layoutMenu->add("Layout",{
        {"Default","",[this]{applyLayoutPreset("Default");}},
        {"Tall","",[this]{applyLayoutPreset("Tall");}},
        {"Wide","",[this]{applyLayoutPreset("Wide");}},
        {},{"Save Layout","",[this]{if(saveLayout())status("Workspace layout saved.");}},
        {},{"Reset to Default","",[this]{resetLayout();}}
    });
    m_viewportMenu=std::make_unique<MenuBar>(m_document,viewportMenuMount);
    m_viewportMenu->add("View",{
        {"Frame Selected","F",[this]{frameSelection();}},
        {"Toggle Grid","",[this]{setGridVisible(!m_grid);}}
    });
    m_viewportMenu->add("Select",{
        {"All","",[this]{
            std::vector<gameplay::EntityId> ids;
            for(const auto& entity:m_world.snapshots())ids.push_back(entity.id);
            selectMany(ids);
        }},
        {"None","",[this]{selectMany({});}},
        {"Invert","",[this]{
            std::vector<gameplay::EntityId> ids;
            for(const auto& entity:m_world.snapshots())if(!isSelected(entity.id))ids.push_back(entity.id);
            selectMany(ids);
        }}
    });
    m_viewportMenu->add("Add",{
        {"Empty","",[this]{createEmpty();}},
        {"Cube","",[this,sample]{addModel(sample/"Box/glTF-Binary/Box.glb");}},
        {"Textured Cube","",[this,sample]{addModel(sample/"BoxTextured/glTF-Binary/BoxTextured.glb");}},
        {"Helmet","",[this,sample]{addModel(sample/"DamagedHelmet/glTF-Binary/DamagedHelmet.glb");}}
    });
    m_viewportMenu->add("Object",{
        {"Duplicate","Ctrl+D",[this]{duplicate();},selected},
        {"Delete","Delete",[this]{remove();},selected}
    });
    m_modeDropdown=std::make_unique<Dropdown>(m_document,modeMount,
        std::vector<DropdownChoice>{{"Object Mode","object_data"},{"Edit Mode","editmode_hlt"}},0,
        [this](size_t index){setEditorMode(index?EditorMode::Edit:EditorMode::Object);});
    m_modeDropdown->button().setTooltip("Viewport Mode","Switch between object and mesh editing.","Tab");
    const char* meshModeIcons[]{"vertexsel","edgesel","facesel"};
    const char* meshModeNames[]{"Vertex Select","Edge Select","Face Select"};
    for(size_t i=0;i<m_meshModeButtons.size();++i) {
        auto& button=m_meshModeMount->button("",[this,i]{setMeshSelectionMode(static_cast<MeshSelectionMode>(i));});
        auto style=theme::button();style.width=23;style.height=23;style.minHeight=0;style.padding=0;
        style.radius=2;style.background=i==0?theme::selection:Color{48,48,48,255};
        m_meshModeButtons[i]=&button.setStyle(style).setBlenderIcon(meshModeIcons[i],15)
            .setTooltip(meshModeNames[i],"Select and move mesh components.");
    }
    m_orientationDropdown=std::make_unique<Dropdown>(m_document,orientationMount,
        std::vector<DropdownChoice>{{"Global","orientation_global"},{"Local","orientation_local"}},0,
        [this](size_t index){setLocalOrientation(index==1);});
    m_orientationDropdown->button().setTooltip("Transform Orientation","Choose global or local axes.");
    m_pivotDropdown=std::make_unique<Dropdown>(m_document,pivotMount,
        std::vector<DropdownChoice>{{"Median Point","pivot_median"},
            {"3D Cursor","pivot_cursor",false},{"Individual Origins","pivot_individual",false}},0,[](size_t){});
    auto pivotDropdownStyle=m_pivotDropdown->button().style();
    pivotDropdownStyle.width=26;pivotDropdownStyle.padding=0;
    m_pivotDropdown->button().setStyle(pivotDropdownStyle).setText("v")
        .setTooltip("Transform Pivot","Choose the center used for transforms.");
    const auto styleHeaderArrow=[](Node& button) {
        auto style=theme::button();style.width=17;style.height=23;style.minHeight=0;
        style.fontSize=9;style.padding=0;style.radius=2;
        style.background={48,48,48,255};style.hoverBackground={65,65,65,255};
        style.borderWidth=0;button.setStyle(style).setIcon(Icon::ChevronDown,10);
    };
    m_snapMenu=std::make_unique<MenuBar>(m_document,snapOptionsMount);
    auto& snapOptions=m_snapMenu->add("",{
        {"Enable Snapping","",[this]{setSnapping(!m_snap);},{},[this]{return m_snap;}},
        {},{"Fine  ·  0.1 / 5° / 5%","",[this]{setSnapPreset(SnapPreset::Fine);},{},[this]{return m_snapPreset==SnapPreset::Fine;}},
        {"Standard  ·  0.5 / 15° / 10%","",[this]{setSnapPreset(SnapPreset::Standard);},{},[this]{return m_snapPreset==SnapPreset::Standard;}},
        {"Coarse  ·  1 / 45° / 25%","",[this]{setSnapPreset(SnapPreset::Coarse);},{},[this]{return m_snapPreset==SnapPreset::Coarse;}}
    });styleHeaderArrow(snapOptions);
    snapOptions.setTooltip("Snapping Options","Set the transform snapping increment.");
    m_overlayMenu=std::make_unique<MenuBar>(m_document,overlayOptionsMount);
    auto& overlayOptions=m_overlayMenu->add("",{
        {"Grid","",[this]{setGridVisible(!m_grid);},{},[this]{return m_grid;}},
        {"Gizmos","",[this]{setGizmoVisible(!m_showGizmo);},{},[this]{return m_showGizmo;}},
        {"Animate Models","",[this]{m_animate=!m_animate;},{},[this]{return m_animate;}}
    });styleHeaderArrow(overlayOptions);
    overlayOptions.setTooltip("Overlay Options","Choose which viewport overlays appear.");
    m_shadingMenu=std::make_unique<MenuBar>(m_document,shadingOptionsMount);
    auto& shadingOptions=m_shadingMenu->add("",{
        {"Wireframe","",[this]{setViewportShading(ViewportShading::Wireframe);},{},[this]{return m_viewportShading==ViewportShading::Wireframe;}},
        {"Solid","",[this]{setViewportShading(ViewportShading::Solid);},{},[this]{return m_viewportShading==ViewportShading::Solid;}},
        {"Material Preview","",[this]{setViewportShading(ViewportShading::MaterialPreview);},{},[this]{return m_viewportShading==ViewportShading::MaterialPreview;}},
        {"Rendered","",[this]{setViewportShading(ViewportShading::Rendered);},{},[this]{return m_viewportShading==ViewportShading::Rendered;}},
        {},{"Quality: Low","",[this]{m_qualityRequest="low";setQuality("low");},{},[this]{return m_qualityPreset=="low";}},
        {"Quality: Medium","",[this]{m_qualityRequest="medium";setQuality("medium");},{},[this]{return m_qualityPreset=="medium";}},
        {"Quality: High","",[this]{m_qualityRequest="high";setQuality("high");},{},[this]{return m_qualityPreset=="high";}}
    });styleHeaderArrow(shadingOptions);
    shadingOptions.setTooltip("Shading Options","Choose viewport shading and quality.");
    m_resourcePicker=std::make_unique<ResourcePicker>(m_document);
    auto dragStyle=theme::label(9);dragStyle.absolute=true;dragStyle.width=280;dragStyle.height=40;dragStyle.padding=6;dragStyle.background=theme::input;dragStyle.borderWidth=1;
    m_assetDragLabel=&m_document.root().label("").setStyle(dragStyle).setVisible(false);
    m_colorPicker=std::make_unique<ColorPicker>(m_document);
    m_sidebarRotationDropdown=std::make_unique<Dropdown>(m_document,*m_sidebarRotationMount,
        std::vector<DropdownChoice>{{"XYZ Euler",""}},0,[](size_t){});
    auto rotationStyle=m_sidebarRotationDropdown->button().style();
    rotationStyle.width=226;rotationStyle.height=19;rotationStyle.fontSize=9;rotationStyle.textAlign=0;
    rotationStyle.background={48,48,48,255};
    m_sidebarRotationDropdown->button().setStyle(rotationStyle);
    m_sidebarRotationDropdown->setButtonLabel("XYZ Euler");
    auto arrowStyle=theme::button();arrowStyle.absolute=true;arrowStyle.left=211;
    arrowStyle.top=0;arrowStyle.width=20;arrowStyle.height=19;
    arrowStyle.padding=0;arrowStyle.fontSize=9;arrowStyle.background={48,48,48,255};
    m_sidebarRotationMount->button("⌄",[this]{m_sidebarRotationDropdown->open();}).setStyle(arrowStyle);
    buildViewportOptionsPopup();
    for(auto* field:{m_materialColor.get(),m_lightColor.get(),m_emitterStartColor.get(),m_emitterEndColor.get()})
        field->setPicker(*m_colorPicker);
    refreshProject(true);
    const auto entities=m_world.snapshots();m_savedState=fingerprint();changed();rebuildTree();
    if(!entities.empty())select(entities.back().id);else select(std::nullopt);
    status("Ready");
    if(!scene.path().empty())m_layoutPath=scene.path().parent_path()/".genesis"/"editor-layout.json";
    loadLayout();m_loadingLayout=false;
}
void GameEditor::layout(float width,float height) {
    m_layoutWidth=width; m_layoutHeight=height;
    m_document.layout(width,height);
    m_dock->arrange();m_document.layout(width,height);
    const bool showSceneSelector=m_dock->editorButtonBounds("scene").width<=0;
    if(showSceneSelector!=m_sceneEditorTypeVisible) {
        m_sceneEditorTypeVisible=showSceneSelector;
        m_sceneEditorTypeButton->setVisible(showSceneSelector);
        m_document.layout(width,height);
    }
    arrangeViewportSidebar();
    if(!m_loadingLayout)updateAssets();
    if(m_layoutPending && std::chrono::steady_clock::now()-m_layoutChanged>std::chrono::milliseconds(600)
        && !m_document.hasPointerCapture())saveLayout();
}
void GameEditor::resetLayout() {
    applyLayoutPreset("Default");
}
void GameEditor::status(const std::string& message) {
    m_status->setText(message);
    m_messages.push_back(message);
    if (m_messages.size()>100) m_messages.erase(m_messages.begin());
    refreshConsole();
}
void GameEditor::buildHierarchy(Node& page,const std::string& id) {
    auto view=std::make_unique<HierarchyView>();view->id=id;auto* raw=view.get();
    auto& tools=page.row().setStyle(toolbarStyle());
    compactButton(tools,"+",[this]{createEmpty();});
    view->filter=&tools.textField("",[this,raw](const std::string&){rebuildTree(*raw);});
    auto search=theme::field(); search.grow=1; search.height=19; view->filter->setStyle(search).setPlaceholder("Search");
    // Count lives with the hierarchy content, so it follows the docked page.
    auto count=theme::label(8,theme::muted);count.height=18;count.padding=Insets(6,2);
    view->count=&page.label("").setStyle(count);
    auto empty=theme::label(9,theme::muted); empty.textWrap=TextWrap::Word; empty.padding=8;
    view->empty=&page.label("No matching objects").setStyle(empty).setVisible(false);
    view->tree=std::make_unique<TreeView>(page,[this](const std::string& item){
        select(item=="scene" ? std::nullopt : std::optional<gameplay::EntityId>(static_cast<uint32_t>(std::stoul(item))));
    });
    m_hierarchies.push_back(std::move(view));
}
void GameEditor::buildConsole(Node& page,const std::string& id) {
    auto view=std::make_unique<ConsoleView>();view->id=id;
    auto& tools=page.row().setStyle(toolbarStyle());
    compactButton(tools,"Clear",[this]{m_messages.clear();refreshConsole();});
    auto caption=theme::label(8,theme::muted); caption.grow=1; caption.shrink=1; caption.padding.left=5;
    tools.label("Editor messages").setStyle(caption);
    view->list=std::make_unique<ScrollView>(page);
    m_consoles.push_back(std::move(view));
}
void GameEditor::buildProject(Node& page,const std::string& id) {
    auto view=std::make_unique<ProjectView>();view->id=id;auto* raw=view.get();
    auto& tools=page.row().setStyle(toolbarStyle());
    compactButton(tools,"Create +",[this]{createEmpty();});
    compactButton(tools,"Refresh",[this]{refreshProject();});
    auto check=theme::button();check.fontSize=9;check.height=20;check.minHeight=0;
    tools.checkBox("Watch",m_assetWatch,[this](bool value){m_assetWatch=value;}).setStyle(check);
    auto caption=theme::label(8,theme::muted); caption.grow=1; caption.shrink=1; caption.padding.left=5;
    tools.label(m_scene.path().parent_path().filename().string()).setStyle(caption);
    view->filter=&tools.textField("",[this,raw](const std::string&){raw->page=0;refreshAssets(*raw);});
    auto search=theme::field(); search.width=180; search.height=19; view->filter->setStyle(search).setPlaceholder("Search assets");
    Style body; body.direction=Direction::Row; body.grow=1;
    auto& projectBody=page.row().setStyle(body);
    Style folders; folders.width=180; folders.shrink=0; folders.background={48,48,48,255};
    auto& folderPane=projectBody.column().setStyle(folders);
    view->folders=std::make_unique<TreeView>(folderPane,[this,raw](const std::string& category){browseAssets(*raw,category);});
    Style content; content.grow=1;
    auto& projectContent=projectBody.column().setStyle(content);
    auto breadcrumb=theme::label(9); breadcrumb.height=22; breadcrumb.padding=Insets(8,3); breadcrumb.background={52,52,52,255};
    view->breadcrumb=&projectContent.label("Assets").setStyle(breadcrumb);
    view->grid=std::make_unique<ScrollView>(projectContent);
    auto gridStyle=view->grid->content().style(); gridStyle.direction=Direction::Row; gridStyle.wrap=true; gridStyle.gap=8;
    gridStyle.align=Align::Start; gridStyle.padding=8; view->grid->content().setStyle(gridStyle);
    auto& footer=projectContent.row().setStyle(toolbarStyle());
    auto footerStyle=theme::label(8,theme::muted);footerStyle.grow=1;footerStyle.shrink=1;
    view->count=&footer.label("").setStyle(footerStyle);
    view->prev=&compactButton(footer,"<",[this,raw]{if(raw->page){--raw->page;raw->grid->viewport().scrollTo(0);refreshAssets(*raw);}});
    view->next=&compactButton(footer,">",[this,raw]{++raw->page;raw->grid->viewport().scrollTo(0);refreshAssets(*raw);});
    m_projects.push_back(std::move(view));
}
void GameEditor::refreshConsole() { for(auto& view:m_consoles)refreshConsole(*view); }
void GameEditor::refreshConsole(ConsoleView& view) {
    auto& content=view.list->content(); content.clear();
    if (m_messages.empty()) content.label("No editor messages").setStyle(labelStyle(9));
    for (auto it=m_messages.rbegin();it!=m_messages.rend();++it) {
        auto style=labelStyle(9,it->find("failed")!=std::string::npos ? theme::warning : theme::text);
        style.padding=Insets(6,4); style.textWrap=TextWrap::Word; style.background=theme::input;
        content.label(*it).setStyle(style);
    }
    view.list->viewport().scrollTo(0);
}
void GameEditor::createEmpty() {
    m_document.cancelInput();endTransformEdit(true);checkpoint();const auto id=m_world.create("GameObject");
    changed();rebuildTree();select(id);status("Created GameObject");
}
void GameEditor::setQuality(const std::string& preset) {
    m_qualityPreset=preset;
    const char* presets[]{"low","medium","high"};
    for(size_t index=0;index<m_qualityButtons.size();++index) {
        if(!m_qualityButtons[index])continue;
        auto style=m_qualityButtons[index]->style();
        style.background=preset==presets[index] ? theme::selection : theme::raised;
        style.textColor=preset==presets[index] ? theme::accent : theme::muted;
        m_qualityButtons[index]->setStyle(style);
    }
}
GameEditor::State GameEditor::capture() const {
    State state; state.entities=m_world.snapshots();
    for(size_t i=0;i<state.entities.size();++i) if (m_selected==state.entities[i].id) state.selection=i;
    return state;
}
std::string GameEditor::fingerprint() const {
    std::ostringstream stream; stream<<std::setprecision(9);
    for (const auto& entity:m_world.snapshots()) {
        stream<<std::quoted(entity.name);
        for(auto* vector:{&entity.transform.position,&entity.transform.rotation,&entity.transform.scale}) for(float v:*vector) stream<<' '<<v;
        for(const auto& tag:entity.tags) stream<<std::quoted(tag);
        if(entity.renderable) stream<<std::quoted(entity.renderable->path)<<entity.renderable->visible;
        stream<<components::serialize(entity);
        stream<<'\n';
    }
    return stream.str();
}
bool GameEditor::dirty() const { return m_session ? m_session->dirty : fingerprint()!=m_savedState; }
void GameEditor::checkpoint() { if(playing())return; m_undo.push_back(capture()); if(m_undo.size()>100) m_undo.erase(m_undo.begin()); m_redo.clear(); }
void GameEditor::changed() {
    ++m_revision;
}
void GameEditor::restore(const State& state) {
    if(m_editorMode==EditorMode::Edit)setEditorMode(EditorMode::Object);
    m_world.clear(); std::vector<gameplay::EntityId> ids;
    for (const auto& entity:state.entities) {
        const auto id=m_world.create(entity.name,entity.transform); ids.push_back(id);
        for(const auto& tag:entity.tags) m_world.setTag(id,tag,true);
        m_world.copyComponents(id,entity);
    }
    changed(); rebuildTree(); select(ids.empty()?std::nullopt:std::optional<gameplay::EntityId>(ids[std::min(state.selection,ids.size()-1)]));
}
void GameEditor::undo() { if(playing())return; if(editingTransform()){endTransformEdit(false);return;} if(m_undo.empty()) return; m_redo.push_back(capture()); auto state=std::move(m_undo.back()); m_undo.pop_back(); restore(state); status("Undo"); }
void GameEditor::redo() { if(playing())return; endTransformEdit(false); if(m_redo.empty()) return; m_undo.push_back(capture()); auto state=std::move(m_redo.back()); m_redo.pop_back(); restore(state); status("Redo"); }
void GameEditor::rebuildTree() { for(auto& view:m_hierarchies)rebuildTree(*view); }
void GameEditor::rebuildTree(HierarchyView& view) {
    std::vector<TreeItem> children; const auto query=lower(view.filter->text());
    for (const auto& entity:m_world.snapshots()) if(query.empty() || lower(entity.name).find(query)!=std::string::npos)
        children.push_back({std::to_string(entity.id),(entity.renderable && !entity.renderable->visible ? "[hidden]  " : "")+entity.name,{},true,Icon::Cube});
    const auto selected=m_selected ? std::to_string(*m_selected) : "scene";
    const bool found=std::any_of(children.begin(),children.end(),[&](const auto& item){return item.id==selected;});
    view.count->setText(query.empty() ? std::to_string(m_world.size())+(m_world.size()==1 ? " object" : " objects") : std::to_string(children.size())+" / "+std::to_string(m_world.size()));
    view.empty->setText(query.empty() ? "Add a model from Assets to start your scene." : "No matching objects");
    view.empty->setVisible(children.empty());
    view.tree->setItems({{"scene",m_scene.name(),std::move(children),true,Icon::Scene}});
    view.tree->select(found || !m_selected ? selected : "");
}
void GameEditor::select(std::optional<gameplay::EntityId> id) {
    m_document.closePopup();
    endTransformEdit(false);
    if(m_editorMode==EditorMode::Edit && id!=m_selected)setEditorMode(EditorMode::Object);
    const bool wasAsset=m_selectedAsset.has_value();m_selectedAsset.reset();
    const auto previous=m_selected;
    m_selected=id && m_world.valid(*id) ? id : std::nullopt;
    m_selectedIds.clear();if(m_selected)m_selectedIds.push_back(*m_selected);
    if(m_selected!=previous)m_selectedEmitterLayer=0;
    gameplay::EntitySnapshot entity;
    const bool exists=m_selected && m_world.snapshot(*m_selected,entity);
    for(auto& view:m_hierarchies) {
        const bool matches=!m_selected || (exists && lower(entity.name).find(lower(view->filter->text()))!=std::string::npos);
        view->tree->select(matches ? (m_selected ? std::to_string(*m_selected) : "scene") : "");
    }
    updateInspector();
    if(wasAsset)refreshAssets();
}
void GameEditor::setSnapping(bool enabled) {
    m_snap=enabled;
    if(m_snapHeaderButton) {
        auto style=m_snapHeaderButton->style();
        style.background=enabled?Color{64,93,124,255}:Color{48,48,48,255};
        m_snapHeaderButton->setStyle(style).setBlenderIcon(enabled?"snap_on":"snap_off",15);
    }
    status(enabled?"Snapping on. Ctrl also enables snapping.":"Snapping off. Hold Ctrl while dragging to snap.");
    refreshViewportSidebar();
}
SnapSteps GameEditor::snapSteps() const {
    switch(m_snapPreset) {
        case SnapPreset::Fine:return {.1f,5.0f,.05f};
        case SnapPreset::Coarse:return {1.0f,45.0f,.25f};
        default:return {};
    }
}
void GameEditor::setSnapPreset(SnapPreset preset) {
    m_snapPreset=preset;
    const auto steps=snapSteps();
    std::ostringstream message;
    message<<"Snap steps: move "<<steps.move<<", rotate "<<steps.rotateDegrees
        <<" degrees, scale "<<steps.scale*100<<"%.";
    status(message.str());
}
void GameEditor::setGridVisible(bool visible) {
    m_grid=visible;
    if(m_gridHeaderButton) {
        auto style=m_gridHeaderButton->style();
        style.background=visible?Color{64,93,124,255}:Color{48,48,48,255};
        m_gridHeaderButton->setStyle(style);
    }
    refreshViewportSidebar();
}
void GameEditor::setGizmoVisible(bool visible) {
    m_showGizmo=visible;
    if(m_gizmoHeaderButton) {
        auto style=m_gizmoHeaderButton->style();
        style.background=visible?Color{64,93,124,255}:Color{48,48,48,255};
        m_gizmoHeaderButton->setStyle(style);
    }
    refreshViewportSidebar();
}
bool GameEditor::isSelected(gameplay::EntityId id) const {
    return std::find(m_selectedIds.begin(),m_selectedIds.end(),id)!=m_selectedIds.end();
}
void GameEditor::selectMany(const std::vector<gameplay::EntityId>& ids,bool extend,bool toggle) {
    std::vector<gameplay::EntityId> result=extend || toggle?m_selectedIds:std::vector<gameplay::EntityId>{};
    for(const auto id:ids) {
        if(!m_world.valid(id))continue;
        const auto found=std::find(result.begin(),result.end(),id);
        if(toggle) {if(found==result.end())result.push_back(id);else result.erase(found);}
        else if(found==result.end())result.push_back(id);
    }
    select(result.empty()?std::optional<gameplay::EntityId>{}:std::optional<gameplay::EntityId>{result.back()});
    m_selectedIds=std::move(result);
}
void GameEditor::updateInspector() {
    gameplay::EntitySnapshot entity; const bool valid=m_selected && m_world.snapshot(*m_selected,entity);
    m_inspector->viewport().setVisible(valid); m_emptyInspector->setVisible(!valid);
    m_assetInspector->setVisible(false);
    m_selection->setText(valid ? (entity.particles ? "Particle System" : entity.light ? "Light object" : entity.probe ? "Reflection Probe" : entity.renderable ? "Mesh object" : "GameObject") : "");
    m_name->setText(valid?entity.name:""); m_visible->setChecked(valid && entity.renderable && entity.renderable->visible);
    m_visible->setVisible(valid && entity.renderable.has_value());
    m_visible->setEnabled(valid && entity.renderable.has_value());
    m_resource->setText(entity.renderable ? fs::path(entity.renderable->path).filename().string() : "None (Mesh)");
    for(int group=0;group<3;++group) for(int axis=0;axis<3;++axis)
        m_transform[group*3+axis]->setValue((group==0?entity.transform.position:group==1?entity.transform.rotation:entity.transform.scale)[axis]);
    updateComponentInspector(entity);
    refreshViewportSidebar();
}
void GameEditor::setTransform(const gameplay::Transform& transform) {
    endTransformEdit(true);
    const auto current=selectedTransform();
    const auto constrained=withTransformLocks(transform);
    if(!current || !validTransform(constrained) || sameTransform(*current,constrained))return;
    checkpoint(); m_world.setTransform(*m_selected,constrained); changed(); updateInspector();
}
gameplay::Transform GameEditor::withTransformLocks(const gameplay::Transform& transform) const {
    auto result=transform;
    if(!m_selected)return result;
    const auto locks=m_sidebarLocks.find(*m_selected);
    if(locks==m_sidebarLocks.end())return result;
    const auto original=selectedTransform();if(!original)return result;
    for(int group=0;group<3;++group)for(int axis=0;axis<3;++axis)if(locks->second[group*3+axis]) {
        auto& values=group==0?result.position:group==1?result.rotation:result.scale;
        const auto& previous=group==0?original->position:group==1?original->rotation:original->scale;
        values[axis]=previous[axis];
    }
    return result;
}
std::optional<gameplay::Transform> GameEditor::selectedTransform() const {
    gameplay::EntitySnapshot entity;
    if(m_selected && m_world.snapshot(*m_selected,entity))return entity.transform;
    return {};
}
void GameEditor::setTool(TransformTool tool) {
    endTransformEdit(false);m_tool=tool;m_cursorPlacement=false;
    if(tool==TransformTool::Select)m_selectionMode=SelectionMode::Select;
    for(size_t i=1;i<m_viewportToolButtons.size();++i) {
        if(m_viewportToolButtons[i]) {
            auto s=m_viewportToolButtons[i]->style();s.background=i==size_t(tool)?theme::selection:Color{45,45,45,255};
            m_viewportToolButtons[i]->setStyle(s);
        }
    }
    for(size_t i=0;i<m_selectionButtons.size();++i) if(m_selectionButtons[i]) {
        auto s=m_selectionButtons[i]->style();s.background=tool==TransformTool::Select && i==size_t(m_selectionMode)
            ?theme::selection:Color{45,45,45,255};m_selectionButtons[i]->setStyle(s);
    }
    if(m_cursorToolButton) {auto s=m_cursorToolButton->style();s.background={45,45,45,255};m_cursorToolButton->setStyle(s);}
    refreshViewportSidebar();
}
void GameEditor::setLocalOrientation(bool local) {
    m_localOrientation=local;
    status(local?"Local transform orientation":"Global transform orientation");
}
void GameEditor::setSelectionMode(SelectionMode mode) {
    setTool(TransformTool::Select);m_selectionMode=mode;
    for(size_t i=0;i<m_selectionButtons.size();++i) if(m_selectionButtons[i]) {
        auto s=m_selectionButtons[i]->style();s.background=i==size_t(mode)?theme::selection:Color{45,45,45,255};
        m_selectionButtons[i]->setStyle(s);
    }
    const char* names[]{"Select","Tweak","Box Select","Circle Select","Lasso Select"};
    status(std::string(names[size_t(mode)])+" tool");
    refreshViewportSidebar();
}
void GameEditor::setCursorPlacement(bool enabled) {
    endTransformEdit(false);m_cursorPlacement=enabled;
    if(m_cursorToolButton) {
        auto s=m_cursorToolButton->style();s.background=enabled?theme::selection:Color{45,45,45,255};
        m_cursorToolButton->setStyle(s);
    }
    for(size_t i=0;i<m_viewportToolButtons.size();++i) if(m_viewportToolButtons[i]) {
        auto s=m_viewportToolButtons[i]->style();s.background=!enabled && i==size_t(m_tool)?theme::selection:Color{45,45,45,255};
        m_viewportToolButtons[i]->setStyle(s);
    }
    for(size_t i=0;i<m_selectionButtons.size();++i) if(m_selectionButtons[i]) {
        auto s=m_selectionButtons[i]->style();s.background=!enabled && m_tool==TransformTool::Select && i==size_t(m_selectionMode)
            ?theme::selection:Color{45,45,45,255};m_selectionButtons[i]->setStyle(s);
    }
    status(enabled?"Click in the Scene view to place the 3D cursor.":"3D cursor placement off.");
}
bool GameEditor::viewportToolStripContains(float x,float y) const {
    return m_viewportToolStrip && m_viewportToolStrip->bounds().contains(x,y);
}
bool GameEditor::beginTransformEdit() {
    m_document.cancelInput();endTransformEdit(false);
    const auto transform=selectedTransform();if(!transform)return false;
    m_transformEdit=TransformEdit{*m_selected,*transform,capture()};return true;
}
void GameEditor::previewTransform(const gameplay::Transform& transform) {
    if(!m_transformEdit)return;
    const auto constrained=withTransformLocks(transform);
    if(!validTransform(constrained))return;
    const auto current=selectedTransform();if(!current || sameTransform(*current,constrained))return;
    m_world.setTransform(m_transformEdit->id,constrained);changed();updateInspector();
}
void GameEditor::endTransformEdit(bool commit) {
    if(!m_transformEdit)return;
    auto edit=std::move(*m_transformEdit);m_transformEdit.reset();
    gameplay::EntitySnapshot entity;if(!m_world.snapshot(edit.id,entity) || sameTransform(entity.transform,edit.original))return;
    if(commit && !playing()) {
        m_undo.push_back(std::move(edit.before));if(m_undo.size()>100)m_undo.erase(m_undo.begin());m_redo.clear();
        status("Transform changed. Ctrl+Z to undo.");
    } else if(!commit) {m_world.setTransform(edit.id,edit.original);changed();updateInspector();}
}
bool GameEditor::addModel(const fs::path& path) {
    const auto extension=lower(path.extension().string());
    return extension==".gprefab" || extension==".gpreset"?addPrefab(path):addModelAt(path,{0,1,0},true);
}
void GameEditor::setViewportShading(ViewportShading mode) {
    m_viewportShading=mode;
    for(size_t index=0;index<m_shadingButtons.size();++index) {
        auto style=m_shadingButtons[index]->style();
        const bool active=index==size_t(mode);
        style.background=active?Color{64,93,124,255}:Color{48,48,48,255};
        style.textColor=active?Color{239,246,252,255}:theme::muted;
        m_shadingButtons[index]->setStyle(style);
    }
    refreshViewportSidebar();
}
bool GameEditor::addModelAt(const fs::path& path,const Vec3& position,bool frame) {
    std::error_code ec; const auto resolved=fs::weakly_canonical(path,ec); const auto extension=lower(path.extension().string());
    if(ec || !fs::is_regular_file(resolved) || (extension!=".gltf" && extension!=".glb")) { status("Import needs an existing .glb or .gltf model."); return false; }
    std::string error;if(!validateModel(resolved,error)){status(error);return false;}
    m_document.cancelInput();endTransformEdit(true); // Commit the current inspector before selection changes on import.
    checkpoint(); gameplay::Transform transform; transform.position=position;
    const auto id=m_world.create(path.stem().string(),transform); m_world.setRenderable(id,resolved.string());
    changed(); rebuildTree(); select(id); if(frame)frameSelection(); status("Added "+path.filename().string()); return true;
}
void GameEditor::duplicate() {
    endTransformEdit(true);
    gameplay::EntitySnapshot entity; if(!m_selected || !m_world.snapshot(*m_selected,entity)) return;
    if(entity.light && entity.light->enabled && !lightSlotAvailable(entity.light->type,false)) {
        status("Cannot duplicate: no available light slot of this type. Disable or remove another light first.");return;
    }
    checkpoint(); entity.transform.position[0]+=1.5f; const auto id=m_world.create(entity.name+" copy",entity.transform);
    for(const auto& tag:entity.tags) m_world.setTag(id,tag,true);
    m_world.copyComponents(id,entity);
    changed(); rebuildTree(); select(id); status("Duplicated object");
}
void GameEditor::remove() {
    endTransformEdit(true);
    if(m_selectedIds.empty())return;
    const auto ids=m_selectedIds;checkpoint();
    for(const auto id:ids)if(m_world.valid(id))m_world.destroy(id);
    m_selected.reset();changed();rebuildTree();select(std::nullopt);
    status(ids.size()==1?"Deleted object. Undo restores it.":"Deleted selected objects. Undo restores them.");
}
bool GameEditor::save() {
    if(playing()){status("Stop Play before saving the authored scene. Play changes are temporary.");return false;}
    endTransformEdit(true);
    endMeshDrag(true);
    if(m_document.wantsTextInput())m_document.keyDown(Key::Enter);
    std::string error; if(!m_scene.save(m_world,m_camera,error)) { status("Save failed: "+error); return false; }
    if(m_saveHandler && !m_saveHandler(m_scene.path(),error)) { status("Save failed: "+error); return false; }
    m_savedState=fingerprint(); changed(); status("Saved "+m_scene.path().string()); return true;
}
}
