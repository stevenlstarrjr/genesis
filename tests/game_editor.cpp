#include "editor/GameEditor.h"
#include "editor/ViewportMath.h"
#include "editor/SelectionGeometry.h"
#include "ProjectLoader.h"
#include "AuthoredLighting.h"
#include "SceneComponents.h"
#include "UiTestImage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <thread>

namespace fs=std::filesystem;
using namespace genesis;
int checks=0;
void require(bool value,const char* message) { ++checks; if(!value) throw std::runtime_error(message); }
void projectBrowserTests(const fs::path& temp,const fs::path& root,const fs::path& output) {
    const auto folder=temp/"browser";fs::create_directories(folder/"Models/Nested");fs::create_directories(folder/"Images");fs::create_directories(folder/"Empty");fs::create_directories(folder/".genesis");
    const auto box=root/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb";
    const auto model=folder/"Models/Cube.glb";fs::copy_file(box,model);
    fs::copy_file(box,folder/"Models/Nested/Second.glb");
    {std::ofstream file(folder/".genesis/ignored.py");file<<"ignored";}
    {std::ofstream file(folder/"Models/broken.glb");file<<"invalid model";}
    {std::ofstream file(folder/"Images/broken.png");file<<"invalid image";}
    ui::Surface texture;texture.width=16;texture.height=8;texture.pixels.assign(128,0xffef3020);
    saveUiImage(texture,folder/"Images/red.bmp");
    const auto file=folder/"main.gscene";{std::ofstream stream(file);stream<<"{\"name\":\"Browser\",\"entities\":[]}";}
    editor::ProjectAssets index(folder,root/"assets");index.begin();index.scanStep(1);
    require(index.scanning() && index.files().empty(),"partial scan does not replace the published index");
    while(index.scanning())index.scanStep(2);
    require(index.takeChanges() && index.folders().size()==4,"incremental index contains nested and empty directories");
    require(std::none_of(index.files().begin(),index.files().end(),[](const auto& item){return item.name=="ignored.py";}),"generated workspace files stay out of Project");
    const auto indexed=[&](const fs::path& path)->editor::ProjectAsset {const auto found=std::find_if(index.files().begin(),index.files().end(),[&](const auto& item){return item.path==path;});require(found!=index.files().end(),"asset is indexed by stable path");return *found;};
    editor::AssetThumbnails thumbnails;
    const auto preview=thumbnails.get(indexed(model));
    require(preview && preview->width==96 && preview->height==96,"glTF geometry produces a real thumbnail");
    require(std::count_if(preview->pixels.begin(),preview->pixels.end(),[](auto pixel){return pixel!=0xff363636 && pixel!=0xff3d3d3d;})>300,"model preview contains rasterized geometry");
    require(thumbnails.get(indexed(model))==preview,"thumbnail cache reuses decoded model preview");
    const auto image=thumbnails.get(indexed(folder/"Images/red.bmp"));
    require(image && (image->pixels[48*96+48]&0xffffff)==0xef3020,"image preview preserves actual image colors");
    require(!thumbnails.get(indexed(folder/"Models/broken.glb")) && !thumbnails.get(indexed(folder/"Images/broken.png")),"malformed assets safely fall back to file icons");
    if(!output.empty()){saveUiImage(*preview,output/"asset-model-preview.bmp");saveUiImage(*image,output/"asset-image-preview.bmp");}
    index.begin();while(index.scanning())index.scanStep();require(!index.takeChanges(),"unchanged scans preserve the published index");
    {std::ofstream asset(folder/"new.py");asset<<"# added";}
    index.begin();while(index.scanning())index.scanStep();require(index.takeChanges(),"new files are discovered on rescan");
    {std::ofstream asset(folder/"new.py",std::ios::app);asset<<"\n# modified";}
    index.begin();while(index.scanning())index.scanStep();require(index.takeChanges(),"modified file size/time invalidates browser state");
    fs::remove(folder/"new.py");index.begin();while(index.scanning())index.scanStep();require(index.takeChanges(),"deleted files leave the index");
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());gameplay::GameplayWorld world;
    editor::GameEditor edit(world,scene,root/"assets",folder);edit.setAssetWatch(false);edit.layout(1440,900);
    render::CameraSettings camera;camera.position={0,5,-10};camera.target={0,0,0};edit.setCamera(camera);
    const auto view=edit.viewport();const float x=view.x+view.width*.5f,y=view.y+view.height*.5f;
    const auto ground=edit.placementPoint(x,y);require(ground && std::abs((*ground)[0])<.0001f && std::abs((*ground)[1])<.0001f && std::abs((*ground)[2])<.0001f,"Scene-center drop ray reaches ground beneath camera target");
    require(!edit.placementPoint(10,10) && !edit.placementPoint(NAN,y),"UI panels and invalid pointer positions reject placement");
    edit.browseAssets("folder:Models");edit.layout(1440,900);
    // Folder-first grid: Nested, broken.glb, Cube.glb.
    const auto panel=edit.dockSpace().panelBounds("project");const float tx=188+2*90+40,ty=panel.y+23+24+22+30;
    auto drag=[&](float targetX,float targetY,bool release=true){edit.document().pointerDown(tx,ty);edit.document().pointerMove(targetX,targetY);if(release)edit.document().pointerUp(targetX,targetY);};
    drag(x,y,false);require(world.size()==0 && edit.document().hasPointerCapture(),"asset drag previews without creating an entity");
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-asset-drag.bmp");
    edit.document().keyDown(ui::Key::Escape);require(world.size()==0 && !edit.dirty(),"Escape cancels asset placement without history");
    drag(20,12);require(world.size()==0,"release outside Scene creates no object");
    drag(x,y);require(world.size()==1 && edit.selectedEntity()->renderable && edit.dirty(),"actual Project tile drag instantiates its model");
    require(std::abs(edit.selectedTransform()->position[1])<.0001f && !edit.takeFrameRequest(),"drop uses preview position without moving the camera");
    edit.undo();require(world.size()==0 && !edit.dirty(),"one undo removes the whole placement");edit.redo();require(world.size()==1,"one redo restores placement");edit.undo();
    edit.browseAssets("folder:Models");edit.layout(1440,900);drag(x,y,false);
    {std::ofstream added(folder/"Models/Before.txt");added<<"added while dragging";}
    edit.refreshProject(true);require(edit.document().hasPointerCapture() && world.size()==0,"refresh cannot destroy an active dragged tile");
    edit.document().cancelInput();edit.layout(1440,900);
    require(std::any_of(edit.projectAssets().files().begin(),edit.projectAssets().files().end(),[](const auto& item){return item.name=="Before.txt";}),"deferred refresh publishes after capture cancellation");
    edit.browseAssets("starter");edit.layout(1440,900);
    edit.document().pointerDown(228,ty);edit.document().pointerUp(228,ty);require(edit.selectedAsset().has_value(),"asset inspection selects a stable file path");
    const auto selected=edit.selectedAsset();{std::ofstream added(folder/"AAA.txt");added<<"sort before selected";}
    edit.refreshProject(true);require(edit.selectedAsset()==selected && !edit.dirty(),"refresh preserves selection by path without authoring changes");
    edit.browseAssets("folder:Images");edit.layout(1440,900);
    // broken.png and red.bmp, then inspect red.
    edit.document().pointerDown(318,ty);edit.document().pointerUp(318,ty);
    require(edit.selectedAsset()==folder/"Images/red.bmp","image tile opens the asset inspector");
    fs::rename(folder/"Images/red.bmp",folder/"Images/renamed.bmp");edit.refreshProject(true);
    require(!edit.selectedAsset() && !edit.selected() && !edit.dirty(),"renamed/deleted inspected file clears stale asset selection");
    edit.setAssetWatch(true);{std::ofstream added(folder/"watched.txt");added<<"watch";}
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    for(int i=0;i<6;++i)edit.layout(1440,900);
    require(std::any_of(edit.projectAssets().files().begin(),edit.projectAssets().files().end(),[](const auto& item){return item.name=="watched.txt";}),"Watch automatically discovers external file creation");
    edit.setAssetWatch(false);edit.browseAssets("starter");for(int i=0;i<5;++i)edit.render(1440,900);
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-project-thumbnails.bmp");
    const auto campfire=root/"assets/particle_prefabs/campfire.gprefab";
    require(indexed(campfire).prefab,"built-in campfire prefab appears in Project assets");
    edit.browseAssets("prefabs");edit.layout(1440,900);
    edit.document().pointerDown(228,ty);edit.document().pointerMove(x,y);edit.document().pointerUp(x,y);
    require(world.size()==1 && edit.selectedEntity()->particles
        && edit.selectedEntity()->particles->mode==gameplay::ParticleSystem::Mode::Emitter
        && edit.selectedEntity()->particles->layers.size()==4
        && edit.selectedEntity()->light,"dragging Campfire places editable emitter layers and light");
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"campfire-prefab-inspector.bmp");
    require(std::abs(edit.selectedTransform()->position[1])<.0001f,"campfire drop follows ground placement");
    require(edit.save(),"campfire prefab scene saves");
    SceneDocument savedScene;require(savedScene.load(file,error),error.c_str());gameplay::GameplayWorld savedWorld;
    savedScene.instantiate(savedWorld);
    require(savedWorld.size()==1 && savedWorld.snapshots()[0].particles
        && savedWorld.snapshots()[0].particles->mode==gameplay::ParticleSystem::Mode::Emitter
        && savedWorld.snapshots()[0].particles->layers.size()==4
        && savedWorld.snapshots()[0].light,"campfire prefab survives save and load");
    edit.undo();require(world.size()==0,"one undo removes the campfire prefab");
    edit.dockSpace().close("scene");edit.layout(1440,900);require(!edit.placeModel(model,x,y),"hidden Scene rejects model placement");
    edit.saveLayout();
}
void workspaceTests(const fs::path& temp,const fs::path& root,const fs::path& output) {
    const auto folder=temp/"workspace";fs::create_directory(folder);const auto file=folder/"main.gscene";
    {std::ofstream stream(file);stream<<"{\"name\":\"Workspace\",\"entities\":[]}";}
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());gameplay::GameplayWorld world;
    ui::DockLayout expected;fs::path settings;
    {
        editor::GameEditor edit(world,scene,root/"assets");edit.layout(1440,900);settings=edit.layoutPath();
        for(const auto* preset:{"Default","Tall","Wide"}) {
            edit.applyLayoutPreset(preset);const auto view=edit.viewport();
            require(view.width>=520 && view.height>=280,"preset retains useful live viewport");
            if(!output.empty())saveUiImage(edit.render(1440,900),output/(std::string("editor-layout-")+preset+".bmp"));
        }
        require(edit.dockSpace().selectEditor("inspector","scene"),"editor selector can move Scene into another area");
        edit.layout(1440,900);
        const auto scenePanelBounds=edit.dockSpace().panelBounds("scene"),movedView=edit.viewport();
        require(movedView.width>0 && movedView.x>=scenePanelBounds.x &&
            movedView.x+movedView.width<=scenePanelBounds.x+scenePanelBounds.width+1 && scenePanelBounds.x>500,
            "Scene viewport follows its selected editor area");
        edit.applyLayoutPreset("Default");
        const auto headerView=edit.viewport();
        edit.document().pointerDown(headerView.x+16,headerView.y-14);
        edit.document().pointerUp(headerView.x+16,headerView.y-14);
        require(edit.document().hasPopup(),"viewport header opens the editor type menu");
        if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-type-menu.bmp");
        edit.document().keyDown(ui::Key::Escape);
        edit.createEmpty();const auto id=edit.selected();require(edit.save(),"workspace test scene saves");
        edit.dockSpace().dock("inspector","hierarchy",ui::DockEdge::Center,0);edit.layout(1440,900);
        require(edit.selected()==id && !edit.dirty(),"docking preserves selected object and scene history");
        edit.dockSpace().close("scene");edit.layout(1440,900);
        require(edit.viewport().width==0 && edit.viewport().height==0,"closed Scene exposes no picking or rendering rectangle");
        const auto& pixels=edit.render(1440,900);
        require((pixels.pixels[400*pixels.width+500]>>24)==255,"hidden Scene is covered by opaque panel contents");
        // Window menu restores a closed Scene through the actual popup action.
        edit.document().pointerDown(18,12);edit.document().pointerUp(18,12);
        for(int i=0;i<5;++i)edit.document().keyDown(ui::Key::Right);
        edit.document().keyDown(ui::Key::Down);edit.document().keyDown(ui::Key::Enter);edit.document().keyUp(ui::Key::Enter);edit.layout(1440,900);
        require(edit.viewport().width>0 && edit.selected()==id && !edit.document().hasPopup(),"Window menu reopens Scene with viewport and selection intact");
        edit.showConsole();edit.dockSpace().close("project");edit.layout(1440,900);
        auto& dock=edit.dockSpace();
        auto splitLayout=dock.layout();
        const auto sceneArea=[&](const auto& self,ui::DockLayout& area)->ui::DockLayout* {
            if(std::find(area.tabs.begin(),area.tabs.end(),"scene")!=area.tabs.end())return &area;
            for(auto& child:area.children)if(auto* found=self(self,child))return found;
            return nullptr;
        };
        auto* source=sceneArea(sceneArea,splitLayout);
        require(source!=nullptr,"Scene area is available to split");
        auto originalArea=std::move(*source);
        *source=ui::DockLayout::split(ui::Direction::Row,.7f,std::move(originalArea),
            ui::DockLayout::group({"__split_area_42","hierarchy#2","project#2","console#3"}));
        require(dock.setLayout(std::move(splitLayout)),"corner split area can be saved");
        dock.show("project#2");edit.layout(1440,900);edit.browseAssets("models");edit.layout(1440,900);
        require(dock.visible("project#2") && dock.panelBounds("project#2").width>0,"Project copies open beside the original");
        expected=dock.layout();
        require(edit.saveLayout() && fs::exists(settings),"workspace writes separate settings file");
        require(!edit.dirty(),"saving workspace does not dirty scene");
        Json::Value saved;{std::ifstream stream(file);stream>>saved;}require(!saved.isMember("workspace"),"scene contains no editor docking state");
    }
    {
        editor::GameEditor reload(world,scene,root/"assets");reload.layout(1440,900);
        require(reload.dockSpace().visible("console") && !reload.dockSpace().open("project"),"active and closed panels survive reopening editor");
        require(reload.dockSpace().open("__split_area_42"),"corner split area survives reopening editor");
        require(reload.dockSpace().open("hierarchy#2") && reload.dockSpace().open("project#2") && reload.dockSpace().open("console#3"),
            "Hierarchy, Project and Console copies survive reopening editor");
        require(reload.dockSpace().layout().children.size()==expected.children.size(),"custom split tree survives reopening editor");
        const auto serialized=[&]{std::ifstream input(settings);return std::string(std::istreambuf_iterator<char>(input),{});};
        const auto before=serialized();require(reload.saveLayout() && serialized()==before,"workspace serialization roundtrips tab order and splitter ratios");
        reload.resetLayout();require(reload.dockSpace().open("project") && reload.dockSpace().visible("scene"),"Default restores closed panels");
        // Automatic destruction flushes a pending layout change before debounce.
        reload.dockSpace().close("hierarchy");
    }
    {
        editor::GameEditor reload(world,scene,root/"assets");
        require(!reload.dockSpace().open("hierarchy"),"shutdown flushes pending workspace changes");
    }
    {std::ofstream stream(settings);stream<<"{ broken json";}
    {
        editor::GameEditor reload(world,scene,root/"assets");reload.layout(1440,900);
        require(reload.dockSpace().visible("hierarchy") && reload.viewport().width>500,"corrupt settings fall back to Default");
        require(std::any_of(reload.messages().begin(),reload.messages().end(),[](const auto& text){return text.find("could not be loaded")!=std::string::npos;}),"invalid saved layout reports recovery");
    }
    {std::ifstream stream(settings);std::string text;std::getline(stream,text);require(text=="{ broken json","recovery leaves original invalid settings untouched until user edits layout");}
    Json::Value invalid;invalid["version"]=1;invalid["workspace"]["tabs"]=Json::arrayValue;
    invalid["workspace"]["tabs"].append("scene");invalid["workspace"]["tabs"].append("scene");invalid["workspace"]["active"]="scene";
    {std::ofstream stream(settings);stream<<invalid;}
    {
        editor::GameEditor reload(world,scene,root/"assets");require(reload.dockSpace().visible("hierarchy"),"duplicate saved panel rejects entire malformed layout");
        // Closing every panel still offers a recoverable, opaque workspace.
        for(const auto* id:{"scene","hierarchy","inspector","project","console"})reload.dockSpace().close(id);
        const auto& empty=reload.render(1100,720);require(reload.viewport().width==0 && (empty.pixels[300*empty.width+500]>>24)==255,"empty workspace is opaque and has no live Scene");
        reload.dockSpace().show("scene");reload.layout(1100,720);
        require(reload.viewport().width>=reload.dockSpace().panelBounds("scene").width-8,
            "Window restore recovers from empty workspace");
    }
}
void playbackTests(const fs::path& temp,const fs::path& root,const fs::path& output) {
    const auto folder=temp/"playback";fs::create_directory(folder);const auto file=folder/"main.gscene";
    {std::ofstream stream(file);stream<<"{\"name\":\"Playback\",\"entities\":[]}";}
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());
    gameplay::GameplayWorld world;const auto retired=world.create("retired");world.destroy(retired);
    const auto id=world.create("Authored");world.setTag(id,"keep",true);world.setLight(id,gameplay::Light{});
    editor::GameEditor edit(world,scene,root/"assets");edit.layout(1440,900);edit.select(id);
    require(!edit.dockSpace().open("game") && edit.gameViewport().width==0 &&
        edit.dockSpace().tabBounds("scene").height==0,"editor has one viewport without Scene and Game tabs");
    const auto editViewport=edit.viewport();
    auto original=*edit.selectedTransform();auto modified=original;modified.position[0]=3;
    edit.setTransform(modified);edit.undo(); // Keep a redo entry across Play.
    const auto authoredId=edit.selected();
    render::CameraSettings camera;camera.position={8,4,-12};camera.target={2,1,0};edit.setCamera(camera);
    const auto disk=[&]{std::ifstream in(file);return std::string(std::istreambuf_iterator<char>(in),{});};const auto beforeDisk=disk();
    int ticks=0;
    edit.setTickHandler([&](float dt){require(std::abs(dt-1.0f/60)<1e-7f,"simulation uses fixed delta");++ticks;
        gameplay::EntitySnapshot entity;if(world.snapshot(*authoredId,entity)){entity.transform.position[1]+=dt;world.setTransform(*authoredId,entity.transform);}
    });
    edit.step();edit.pause();edit.advance(1);require(ticks==0,"edit state cannot simulate or step");
    edit.play();require(edit.playState()==editor::GameEditor::PlayState::Playing &&
        edit.gameViewport().width==editViewport.width && edit.gameViewport().height>editViewport.height &&
        edit.viewport().width==0,"Play switches the shared viewport to game mode");
    require(world.valid(*authoredId) && !world.valid(retired),"runtime clone retains authored entity generations");
    edit.play();edit.advance(editor::GameEditor::fixedDelta*.5);require(ticks==0,"sub-tick elapsed time accumulates");
    edit.advance(editor::GameEditor::fixedDelta*.5);require(ticks==1 && edit.ticks()==1,"accumulation advances exactly one tick");
    edit.pause();const auto frozen=edit.simulationTime();edit.advance(10);edit.advance(NAN);edit.advance(-1);
    require(ticks==1 && edit.simulationTime()==frozen,"Pause freezes time without backlog");
    edit.step();require(ticks==2 && edit.ticks()==2 && edit.playState()==editor::GameEditor::PlayState::Paused,"Step runs exactly one tick and stays paused");
    require(!edit.dirty() && !edit.save() && disk()==beforeDisk,"runtime mutations cannot dirty or save over authored scene");
    edit.undo();edit.redo();require(edit.selectedTransform()->position[0]==0,"authoring history is inaccessible during Play");
    edit.dockSpace().close("scene");edit.layout(1440,900);edit.step();
    require(ticks==3 && edit.gameViewport().width==0,"closing the viewport keeps paused simulation valid");
    edit.play();edit.advance(editor::GameEditor::fixedDelta);
    require(ticks==4 && edit.gameViewport().width>0,"resume reopens the viewport without resetting simulation");
    edit.advance(editor::GameEditor::fixedDelta);require(ticks==5,"simulation continues in the shared viewport");
    edit.pause();edit.createEmpty();edit.remove();world.destroy(*authoredId);world.create("runtime only");
    auto runtimeCamera=camera;runtimeCamera.position[0]=99;edit.setCamera(runtimeCamera);
    const auto gameBeforeStop=edit.gameViewport();
    edit.stop();require(edit.playState()==editor::GameEditor::PlayState::Edit && edit.ticks()==0 &&
        edit.gameViewport().width==0 && edit.viewport().width>0 && edit.viewport().height<gameBeforeStop.height,
        "Stop restores edit mode, editing controls, and clock");
    require(world.size()==1 && world.valid(*authoredId) && edit.selected()==authoredId,"Stop restores exact authored identity and selection after runtime destruction");
    require(edit.selectedTransform()->position==original.position && edit.selectedEntity()->light.has_value() && edit.selectedEntity()->tags==std::vector<std::string>{"keep"},"Stop restores transforms tags and components");
    require(!edit.dirty() && edit.camera().position==camera.position && disk()==beforeDisk,"Stop restores clean state and editor camera without touching disk");
    edit.redo();require(edit.dirty() && edit.selectedTransform()->position[0]==3,"pre-Play redo survives");
    for(int cycle=0;cycle<3;++cycle) {
        const auto selection=edit.selected();edit.play();require(edit.ticks()==0,"each Play session starts at zero");
        edit.createEmpty();edit.stop();require(edit.selected()==selection && edit.dirty() && world.size()==1,"repeated Play/Stop preserves unsaved authored work");
    }
    edit.undo();require(!edit.dirty() && edit.selectedTransform()->position[0]==0,"runtime edits never entered authored undo history");
    edit.select(std::nullopt);edit.play();edit.createEmpty();edit.stop();require(!edit.selected(),"empty selection is restored exactly");
    edit.play();edit.pause();edit.layout(1440,900);
    require(edit.viewport().width==0 && edit.gameViewport().width>0,"Pause remains in game mode");
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-game-paused.bmp");
    edit.stop();
    edit.dockSpace().setLayout(ui::DockLayout::group({"scene","hierarchy","inspector","project","console"}));edit.saveLayout();
    {editor::GameEditor reload(world,scene,root/"assets");require(reload.dockSpace().layout().tabs.size()==5 && !reload.dockSpace().open("game"),"single-viewport workspace reloads");
        reload.play();require(reload.gameViewport().width>0,"Play uses the shared viewport after reloading");reload.stop();}
    Json::Value legacy;legacy["version"]=1;legacy["workspace"]["tabs"]=Json::arrayValue;
    for(const auto* id:{"scene","game","hierarchy","inspector","project","console"})
        legacy["workspace"]["tabs"].append(id);
    legacy["workspace"]["active"]="game";
    {std::ofstream stream(edit.layoutPath());stream<<legacy;}
    {editor::GameEditor reload(world,scene,root/"assets");
        require(reload.dockSpace().layout().tabs.size()==5 && !reload.dockSpace().open("game") &&
            reload.dockSpace().visible("scene"),"legacy Scene and Game tabs migrate to one viewport");}
}
void meshEditTests(const fs::path& temp,const fs::path& root) {
    editor::MeshSurfaceIndex visibility;
    visibility.rebuild({{-1,-1,0},{1,-1,0},{0,1,0},{-1,-1,1},{1,-1,1},{0,1,1}},
        {{0,1,2},{3,4,5}});
    const auto front=visibility.nearest({{0,0,-2},{0,0,1}});
    require(front && front->face==0 && std::abs(front->distance-2)<.001f,
        "mesh picking uses the nearest surface instead of the hidden face");
    const auto folder=temp/"mesh-edit";fs::create_directory(folder);
    const auto file=folder/"main.gscene";
    {std::ofstream stream(file);stream<<"{\"name\":\"Mesh Edit\",\"entities\":[]}";}
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());
    gameplay::GameplayWorld world;
    const auto id=world.create("Cube");
    editor::GameEditor edit(world,scene,root/"assets");edit.layout(1440,900);
    require(!edit.setEditorMode(editor::GameEditor::EditorMode::Edit),"Edit Mode requires a selected mesh");
    world.setRenderable(id,(root/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb").string());
    edit.select(id);
    require(edit.setEditorMode(editor::GameEditor::EditorMode::Edit) &&
        edit.editorMode()==editor::GameEditor::EditorMode::Edit,"selected mesh enters Edit Mode");
    const auto& topology=edit.meshOverlay();
    require(topology.vertices.size()>=8 && topology.edges.size()>=12,"Edit Mode loads mesh topology");
    const editor::ViewportCamera camera(edit.camera());
    const auto area=edit.viewport();
    const auto findPoint=[&](editor::Vec3 world)->std::optional<editor::gizmoMath::Vec2>{
        const auto point=editor::gizmoMath::project(world,camera,area);
        if(point && area.contains((*point)[0],(*point)[1]) && (*point)[0]>area.x+60)return point;
        return {};
    };
    bool vertexPicked=false;
    for(const auto& vertex:topology.vertices)if(const auto point=findPoint(vertex)) {
        if(edit.pickMeshElement((*point)[0],(*point)[1],camera)){vertexPicked=true;break;}
    }
    require(vertexPicked && !edit.meshOverlay().selected.empty(),"vertex mode picks visible mesh vertices");
    const auto before=*edit.editSelectionCenter();
    require(edit.beginMeshDrag(),"selected vertices begin one drag operation");
    edit.previewMeshDrag({0,.25f,0});edit.endMeshDrag(true);
    require(edit.selectedEntity()->renderable->meshVertices.size()>0 &&
        std::abs((*edit.editSelectionCenter())[1]-before[1]-.25f)<.01f,
        "drag moves welded source vertices in world space");
    edit.setMeshSelectionMode(editor::GameEditor::MeshSelectionMode::Edge);
    require(edit.meshSelectionMode()==editor::GameEditor::MeshSelectionMode::Edge,"edge selection mode activates");
    bool edgePicked=false;
    for(const auto& edge:edit.meshOverlay().edges) {
        const auto& points=edit.meshOverlay().vertices;
        const editor::Vec3 middle{(points[edge[0]][0]+points[edge[1]][0])*.5f,
            (points[edge[0]][1]+points[edge[1]][1])*.5f,
            (points[edge[0]][2]+points[edge[1]][2])*.5f};
        if(const auto point=findPoint(middle);point && edit.pickMeshElement((*point)[0],(*point)[1],camera)) {
            edgePicked=edit.meshOverlay().selected.size()>=2;break;
        }
    }
    require(edgePicked,"edge mode picks editable edges");
    const auto wholeViewport=std::vector<editor::gizmoMath::Vec2>{{area.x+65,area.y+5},
        {area.x+area.width-5,area.y+area.height-5}};
    require(!edit.meshRegionHits(editor::GameEditor::SelectionMode::Box,wholeViewport,0,camera).empty(),
        "box select finds visible edges in Edit Mode");
    edit.setMeshSelectionMode(editor::GameEditor::MeshSelectionMode::Face);
    require(edit.meshSelectionMode()==editor::GameEditor::MeshSelectionMode::Face,"face selection mode activates");
    bool facePicked=false;
    for(const auto& face:topology.faces) {
        const auto& points=edit.meshOverlay().vertices;
        const editor::Vec3 middle{(points[face[0]][0]+points[face[1]][0]+points[face[2]][0])/3,
            (points[face[0]][1]+points[face[1]][1]+points[face[2]][1])/3,
            (points[face[0]][2]+points[face[1]][2]+points[face[2]][2])/3};
        if(const auto point=findPoint(middle);point && edit.pickMeshElement((*point)[0],(*point)[1],camera)) {
            facePicked=edit.meshOverlay().selected.size()>=3;break;
        }
    }
    require(facePicked,"face mode picks editable triangles");
    require(!edit.meshRegionHits(editor::GameEditor::SelectionMode::Box,wholeViewport,0,camera).empty(),
        "box select finds visible faces in Edit Mode");
    edit.setMeshSelectionMode(editor::GameEditor::MeshSelectionMode::Vertex);
    const auto visibleVertices=edit.meshRegionHits(editor::GameEditor::SelectionMode::Box,wholeViewport,0,camera);
    require(!visibleVertices.empty(),"box select finds visible vertices in Edit Mode");
    edit.setMeshSelection({},visibleVertices);
    require(edit.meshOverlay().selected==visibleVertices,"region selection updates edited mesh selection");
    edit.setMeshSelection(visibleVertices,visibleVertices,false,true);
    require(edit.meshOverlay().selected.empty(),"toggle removes selected mesh elements");
    edit.setEditorMode(editor::GameEditor::EditorMode::Object);
    edit.undo();require(edit.selectedEntity()->renderable->meshVertices.empty(),"Undo restores source geometry");
    edit.redo();require(!edit.selectedEntity()->renderable->meshVertices.empty(),"Redo restores edited geometry");
    require(edit.save(),"edited mesh saves with the scene");
    SceneDocument reopened;require(reopened.load(file,error),error.c_str());
    gameplay::GameplayWorld loaded;reopened.instantiate(loaded);
    require(loaded.snapshots().size()==1 && !loaded.snapshots()[0].renderable->meshVertices.empty(),
        "edited mesh vertices survive scene reload");
}
void componentTests(const fs::path& temp,const fs::path& root,const fs::path& output) {
    const auto file=temp/"components.gscene";{std::ofstream stream(file);stream<<"{\"name\":\"Component test\",\"entities\":[]}";}
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());gameplay::GameplayWorld world;
    editor::GameEditor edit(world,scene,root/"assets");edit.layout(1440,900);edit.createEmpty();
    const auto id=edit.selected();gameplay::Transform transform;transform.position={2,3,4};transform.rotation={0,0,30};edit.setTransform(transform);
    edit.openComponentPicker();require(edit.document().hasPopup(),"Add Component opens searchable picker");
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-add-component.bmp");
    edit.document().textInput("mesh");edit.document().keyDown(ui::Key::Enter);
    require(edit.document().hasPopup(),"Mesh Renderer choice opens model picker");
    edit.document().textInput("textured");edit.document().keyDown(ui::Key::Enter);
    require(edit.selected()==id && world.size()==1 && edit.selectedEntity()->renderable->path.find("BoxTextured.glb")!=std::string::npos,"picker assigns model to existing entity");
    require(edit.selectedTransform()->position==transform.position,"adding mesh retains object transform");
    const auto cube=root/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb";
    require(edit.assignModel(cube),"model replacement succeeds");edit.undo();
    require(edit.selectedEntity()->renderable->path.find("BoxTextured.glb")!=std::string::npos,"undo restores original model");edit.redo();
    const auto corrupt=temp/"corrupt.glb";{std::ofstream stream(corrupt);stream<<"not a model";}
    require(!edit.assignModel(corrupt) && fs::equivalent(edit.selectedEntity()->renderable->path,cube),"corrupt model rejected without replacing valid mesh");
    edit.removeMesh();require(!edit.selectedEntity()->renderable && world.size()==1,"remove renderer retains entity");edit.undo();
    require(edit.selectedEntity()->renderable.has_value(),"undo restores removed renderer");
    gameplay::Renderable::Material material;material.color={.8f,.2f,.1f};material.metallic=.6f;material.roughness=.25f;
    require(edit.setMaterial(material),"material override accepted");
    if(!output.empty())saveUiImage(edit.render(1440,900),output/"editor-material-inspector.bmp");
    edit.undo();require(!edit.selectedEntity()->renderable->material,"undo clears material override");edit.redo();
    require(edit.selectedEntity()->renderable->material==material,"redo restores material factors");
    auto invalid=material;invalid.roughness=NAN;require(!edit.setMaterial(invalid),"nonfinite material rejected");
    edit.openComponentPicker();edit.document().textInput("spot");edit.document().keyDown(ui::Key::Enter);
    require(edit.selectedEntity()->light && edit.selectedEntity()->light->type==gameplay::Light::Type::Spot,"component picker adds spot light");
    auto light=*edit.selectedEntity()->light;light.intensity=125;light.range=8;light.innerAngle=17;light.outerAngle=40;
    require(edit.setLight(light),"spot parameters accepted");
    auto badLight=light;badLight.innerAngle=60;require(!edit.setLight(badLight),"inverted cone rejected");
    edit.addComponent(editor::GameEditor::Component::ReflectionProbe);
    auto probe=*edit.selectedEntity()->probe;probe.size={6,8,10};probe.blend=2;probe.priority=4;require(edit.setProbe(probe),"probe properties accepted");
    edit.duplicate();require(edit.selectedEntity()->light==light && edit.selectedEntity()->probe==probe && edit.selectedEntity()->renderable->material==material,"duplicate copies all component data");edit.undo();
    require(edit.save() && !edit.dirty(),"component scene saves cleanly");
    SceneDocument loaded;require(loaded.load(file,error),error.c_str());gameplay::GameplayWorld roundtrip;loaded.instantiate(roundtrip);const auto saved=roundtrip.snapshots()[0];
    require(saved.light==light && saved.probe==probe && saved.renderable->material==material,"material, light and probe survive save/load");
    edit.setLight(std::nullopt);edit.setProbe(std::nullopt);edit.removeMesh();require(edit.save(),"removed components save");
    require(loaded.load(file,error),error.c_str());loaded.instantiate(roundtrip);const auto empty=roundtrip.snapshots()[0];
    require(!empty.light && !empty.probe && !empty.renderable,"removed components stay absent after reload");
    edit.undo();edit.undo();edit.undo();
    require(edit.selectedEntity()->light && edit.selectedEntity()->probe && edit.selectedEntity()->renderable,"undo restores components after save");
    render::AuthoredLighting baseline;baseline.points.resize(4);
    auto state=baseline.withEntities({saved});require(state.points.size()==4 && state.spots.size()==1 && state.probes.size()==1,"authored lighting preserves setup lights and adds scene components");
    require(state.spots[0].position==transform.position && state.spots[0].intensity==125,"renderer receives authored transform and intensity");
    require(std::abs(state.spots[0].direction[0]+.5f)<.0001f && std::abs(state.spots[0].direction[1]+.8660254f)<.0001f,"spot direction follows rotated local minus Y axis");
    require(state.probes[0].boundsMin==std::array<float,3>{-1,-1,-1} && state.probes[0].boundsMax==std::array<float,3>{5,7,9},"probe size produces correct world box");
    auto overflow=saved;overflow.light->type=gameplay::Light::Type::Point;state=baseline.withEntities({overflow});require(state.points.size()==4 && state.omitted==1,"full light arrays are bounded safely");
    overflow.light->enabled=false;overflow.probe->enabled=false;state=baseline.withEntities({overflow});require(state.omitted==0 && state.probes.empty(),"disabled components consume no renderer slots");
    edit.setLightCapacity({0,0,0});require(!edit.setLight(gameplay::Light{}),"Inspector rejects addition when setup consumes light capacity");
    edit.duplicate();require(world.size()==1,"duplicate cannot bypass light capacity");
    Json::Value invalidJson;invalidJson["entities"]=Json::arrayValue;Json::Value bad=components::serialize(saved);bad["name"]="Invalid";bad["model"]=cube.generic_string();bad["reflection_probe"]["size"][0]=-1;invalidJson["entities"].append(bad);
    {std::ofstream stream(file);stream<<invalidJson;}require(!loaded.load(file,error),"invalid probe size rejected on load");
    bad["reflection_probe"]["size"][0]=6;bad["light"]["enabled"]="yes";invalidJson["entities"][0]=bad;{std::ofstream stream(file);stream<<invalidJson;}
    require(!loaded.load(file,error),"invalid component boolean rejected on load");
    edit.select(std::nullopt);require(!edit.assignModel(cube),"component assignment without selection is harmless");
}
void particleAuthoringTests(const fs::path& temp,const fs::path& root) {
    Json::Value legacy;legacy["particle_system"]["mode"]="campfire";
    gameplay::EntitySnapshot migrated;components::deserialize(legacy,migrated);
    require(migrated.particles && migrated.particles->mode==gameplay::ParticleSystem::Mode::Emitter
        && migrated.particles->layers.size()==4,"old campfire scenes migrate to generic emitter layers");
    migrated.particles->layers[0].count=4096;
    require(!components::valid(migrated),"visual emitter enforces total particle limit");
    const auto file=temp/"particles.gscene";{std::ofstream stream(file);stream<<"{\"name\":\"Particles\",\"entities\":[]}";}
    SceneDocument scene;std::string error;require(scene.load(file,error),error.c_str());gameplay::GameplayWorld world;
    editor::GameEditor edit(world,scene,root/"assets");edit.createEmpty();
    const auto collider=root/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb";
    require(edit.assignModel(collider) && edit.setParticleKinematic(true),"moving particle collider can be authored");
    edit.undo();require(!edit.selectedEntity()->renderable->particleKinematic,"moving collider setting undoes");
    edit.redo();require(edit.selectedEntity()->renderable->particleKinematic,"moving collider setting redoes");
    for(const auto component:{editor::GameEditor::Component::FluidParticles,editor::GameEditor::Component::ClothParticles,editor::GameEditor::Component::GranularParticles,editor::GameEditor::Component::ExplosionParticles,editor::GameEditor::Component::EmitterParticles}) {
        edit.addComponent(component);const auto mode=component==editor::GameEditor::Component::FluidParticles?gameplay::ParticleSystem::Mode::Fluid:
            component==editor::GameEditor::Component::ClothParticles?gameplay::ParticleSystem::Mode::Cloth:
            component==editor::GameEditor::Component::ExplosionParticles?gameplay::ParticleSystem::Mode::Explosion:
            component==editor::GameEditor::Component::EmitterParticles?gameplay::ParticleSystem::Mode::Emitter:gameplay::ParticleSystem::Mode::Granular;
        require(edit.selectedEntity()->particles && edit.selectedEntity()->particles->mode==mode,"particle picker creates requested mode");
        auto settings=*edit.selectedEntity()->particles;settings.dimensions={9,mode==gameplay::ParticleSystem::Mode::Cloth?1:4,8};
        settings.spacing=.2f;settings.mass=.1f;settings.stiffness=1234;
        if(mode==gameplay::ParticleSystem::Mode::Cloth)settings.pinEdge=gameplay::ParticleSystem::PinEdge::Both;
        if(mode==gameplay::ParticleSystem::Mode::Explosion){settings.burstSpeed=11;settings.blastRadius=4;settings.blastImpulse=12;settings.effectDuration=3;}
        if(mode==gameplay::ParticleSystem::Mode::Emitter){settings.layers[0].count=91;settings.layers[0].velocity={.2f,2.f,0};settings.layers[0].startColor={1.f,.4f,.1f,.8f};}
        require(edit.setParticles(settings),"particle settings accepted");
#ifndef GENESIS_WITH_PHYSX_PBD
#ifdef GENESIS_WITH_PHYSX_PBD
        edit.play();require(edit.playing(),"PhysX-enabled editor accepts particle playback");edit.stop();
#else
        edit.play();require(!edit.playing(),"unavailable PhysX runtime does not silently play particle scene");
#endif
#endif
        edit.undo();require(edit.selectedEntity()->particles->mode==mode && edit.selectedEntity()->particles->dimensions!=settings.dimensions,"particle settings undo as one edit");
        edit.redo();require(edit.selectedEntity()->particles==settings,"particle settings redo restores all values");
        require(edit.save(),"particle scene saves");
        SceneDocument loaded;require(loaded.load(file,error),error.c_str());gameplay::GameplayWorld restored;loaded.instantiate(restored);
        require(restored.snapshots()[0].particles==settings && restored.snapshots()[0].renderable->particleKinematic,
            "particle pins and moving collider survive scene roundtrip");
        auto invalid=settings;invalid.dimensions={128,128,128};require(!edit.setParticles(invalid),"particle count cap enforced");
        edit.setParticles(std::nullopt);require(edit.save(),"particle removal saves");
    }
}
int main(int argc,char** argv) {
    try {
        const fs::path output=argc>1 ? argv[1] : "";
        const fs::path root=GENESIS_ROOT;
        const fs::path temp=fs::temp_directory_path()/("genesis-editor-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(temp);
        const fs::path asset=root/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb";
        const fs::path file=temp/"test.gscene";
        Json::Value json; json["name"]="Test"; json["custom"]["keep"]=42; json["script"]="setup.py";
        json["entities"]=Json::Value(Json::arrayValue);
        Json::Value node; node["name"]="Cube"; node["model"]=asset.generic_string(); node["visible"]=false;
        json["entities"].append(node);
        { std::ofstream stream(file); stream<<json; }
        { std::ofstream stream(temp/"setup.py"); stream<<"# Preserved setup script\n"; }
        SceneDocument scene; std::string error;
        require(scene.load(file,error),error.c_str());
        require(scene.hasEntities(),"authored entities recognized");
        gameplay::GameplayWorld world; world.create("Old script entity"); scene.instantiate(world);
        require(world.size()==1,"authored entities replace script mesh entities");
        require(!world.snapshots()[0].renderable->visible,"visibility loads");
        require(ProjectLoader::resolve(file).kind==LaunchKind::Scene,"script scene resolves");
        editor::GameEditor editor(world,scene,root/"assets");
        editor.layout(1440,900); const auto viewport=editor.viewport();
        require(viewport.width>800 && viewport.height>500,"live viewport occupies the center");
        require(viewport.x>=220 && viewport.x+viewport.width<=1120,"viewport excludes side panels");
        const auto& surface=editor.render(1440,900);
        const auto center=surface.pixels[size_t(viewport.y+viewport.height*.5f)*surface.width+size_t(viewport.x+viewport.width*.5f)];
        require((center>>24)==0,"viewport hole is transparent for GPU scene");
        require((surface.pixels[20*surface.width+20]>>24)==255,"editor panels are opaque");
        if(!output.empty()) saveUiImage(surface,output/"editor-desktop.bmp");
        const float gripY=viewport.y+viewport.height*.5f;
        const auto hierarchy=editor.dockSpace().panelBounds("hierarchy");
        const float gripX=hierarchy.x+hierarchy.width+2;
        editor.document().pointerDown(gripX,gripY);
        editor.document().pointerMove(gripX+80,gripY); editor.document().pointerUp(gripX+80,gripY);
        editor.layout(1440,900);
        require(editor.viewport().x==viewport.x+80,"hierarchy separator resizes Yoga workspace");
        auto resized=editor.viewport();
        editor.document().pointerDown(resized.x+resized.width+2,gripY);
        editor.document().pointerMove(resized.x+resized.width-200,gripY);
        editor.document().pointerUp(resized.x+resized.width-200,gripY);
        editor.layout(1100,720);
        require(editor.viewport().width>=520,"enlarged panels clamp when window narrows");
        resized=editor.viewport();
        const auto sceneArea=editor.dockSpace().panelBounds("scene");
        const float lowerGrip=sceneArea.y+sceneArea.height+2;
        editor.document().pointerDown(resized.x+100,lowerGrip);
        editor.document().pointerMove(resized.x+100,lowerGrip-80);
        editor.document().pointerUp(resized.x+100,lowerGrip-80); editor.layout(1100,720);
        require(editor.viewport().height<resized.height && editor.viewport().height>200,"project divider resizes panel vertically");
        editor.layout(1440,900);
        editor.resetLayout(); require(editor.viewport().x==viewport.x,"reset layout restores panel widths");
        const auto& retina=editor.render(1100,720,2);
        require(retina.width==2200 && retina.height==1440 && editor.viewport().width>500,"editor high DPI retains usable logical layout");
        if(!output.empty()) saveUiImage(retina,output/"editor-minimum-2x.bmp");
        if(!output.empty()) saveUiImage(editor.render(1100,720),output/"editor-minimum.bmp");
        editor.render(1440,900);
        require(!editor.dirty(),"initial scene clean");
        const float projectTop=editor.dockSpace().panelBounds("project").y;
        // Search and select a real Project asset without mutating the scene.
        editor.document().pointerDown(950,projectTop+35);editor.document().pointerUp(950,projectTop+35);
        require(editor.document().wantsTextInput(),"Project asset search is reachable");
        editor.document().textInput("helmet");
        editor.render(1440,900);
        editor.document().pointerDown(229,projectTop+107);editor.document().pointerUp(229,projectTop+107);
        require(editor.selectedAsset() && editor.selectedAsset()->filename()=="DamagedHelmet.glb","filtered asset icon selects its real file");
        require(!editor.selected() && world.size()==1 && !editor.dirty(),"asset inspection does not create scene objects");
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-asset-inspector.bmp");
        editor.document().pointerDown(950,projectTop+35);editor.document().pointerUp(950,projectTop+35);
        editor.document().keyDown(ui::Key::SelectAll);editor.document().keyDown(ui::Key::Backspace);editor.document().cancelInput();
        editor.select(world.snapshots()[0].id);
        editor.document().pointerDown(18,12);editor.document().pointerUp(18,12);
        require(editor.document().hasPopup(),"File menu opens an actual popup");
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-file-menu.bmp");
        editor.document().keyDown(ui::Key::Escape);
        bool nameFocused=false;
        for(int i=0;i<100 && !nameFocused;++i) {
            editor.document().keyDown(ui::Key::Tab);
            nameFocused=editor.document().wantsTextInput() && editor.document().selectedText()=="Cube";
        }
        require(nameFocused,"inspector name is keyboard reachable");
        editor.document().textInput("Renamed"); editor.document().textInput(" cube");
        require(world.snapshots()[0].name=="Cube","name draft does not create history for each keystroke");
        editor.document().keyDown(ui::Key::Enter);
        require(world.snapshots()[0].name=="Renamed cube" && editor.dirty(),"name commits to scene");
        editor.document().cancelInput(); editor.undo();
        require(world.snapshots()[0].name=="Cube" && !editor.dirty(),"one undo reverts the complete rename");
        editor.showConsole(); editor.status("Console regression message");
        require(editor.messages().back()=="Console regression message","console receives real editor activity");
        editor.render(1440,900);
        if(!output.empty()) saveUiImage(editor.render(1440,900),output/"editor-console.bmp");
        editor.showConsole(false);
        editor.select(std::nullopt);
        if(!output.empty()) saveUiImage(editor.render(1100,720),output/"editor-empty-selection.bmp");
        editor.select(world.snapshots()[0].id); editor.layout(1440,900);
        const auto originalViewport=editor.viewport();
        editor.toggleViewportSidebar();editor.layout(1440,900);
        require(editor.viewportSidebarVisible() &&
            editor.viewportSidebarContains(originalViewport.x+originalViewport.width-20,originalViewport.y+100),
            "N sidebar opens along the right side of the viewport");
        require(!editor.viewportSidebarContains(originalViewport.x+40,originalViewport.y+100) &&
            editor.viewport().width==originalViewport.width,
            "N sidebar leaves the viewport and left tools in place");
        editor.setSelectedDimensions(world.snapshots()[0].id,{2,2,2});
        const float lockX=originalViewport.x+originalViewport.width-53;
        const float lockY=originalViewport.y+90;
        editor.document().pointerDown(lockX,lockY);editor.document().pointerUp(lockX,lockY);
        auto lockedTransform=*editor.selectedTransform();lockedTransform.position[0]=7;
        editor.setTransform(lockedTransform);
        require(editor.selectedTransform()->position[0]==0,"Item location lock constrains authored transforms");
        editor.document().pointerDown(lockX,lockY);editor.document().pointerUp(lockX,lockY);
        editor.setTransform(lockedTransform);
        require(editor.selectedTransform()->position[0]==7,"unlocking Item location permits transforms");
        editor.undo();
        require(editor.selected()==world.snapshots()[0].id,"undo retains Item panel selection");
        editor.setSelectedDimensions(world.snapshots()[0].id,{2,2,2});
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-n-sidebar.bmp");
        const float optionsX=originalViewport.x+originalViewport.width-35;
        const float optionsY=originalViewport.y+12;
        editor.document().pointerDown(optionsX,optionsY);
        editor.document().pointerUp(optionsX,optionsY);
        require(editor.document().hasPopup(),"viewport Options opens its popup");
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-n-options.bmp");
        editor.document().closePopup();
        editor.applyLayoutPreset("Wide");
        const auto wideViewport=editor.viewport();
        require(editor.viewportSidebarContains(wideViewport.x+wideViewport.width-15,wideViewport.y+80) &&
            !editor.viewportSidebarContains(wideViewport.x+wideViewport.width+5,wideViewport.y+80),
            "N sidebar remains inside the viewport after a layout preset");
        editor.resetLayout();
        const auto resetViewport=editor.viewport();
        require(editor.viewportSidebarContains(resetViewport.x+resetViewport.width-15,resetViewport.y+80) &&
            !editor.viewportSidebarContains(resetViewport.x+resetViewport.width+5,resetViewport.y+80),
            "N sidebar stays local to the viewport after resetting the layout");
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-n-sidebar-reset.bmp");
        const float viewTabX=resetViewport.x+resetViewport.width-14;
        const float viewTabY=resetViewport.y+143;
        editor.document().pointerDown(viewTabX,viewTabY);
        editor.document().pointerUp(viewTabX,viewTabY);
        editor.setViewportCursor({1,2,3});
        require(editor.viewportCursor()==editor::Vec3{1,2,3},"View sidebar updates the 3D cursor state");
        if(!output.empty())saveUiImage(editor.render(1440,900),output/"editor-n-sidebar-view.bmp");
        editor.toggleViewportSidebar();editor.layout(1440,900);
        require(!editor.viewportSidebarVisible() &&
            !editor.viewportSidebarContains(originalViewport.x+originalViewport.width-20,originalViewport.y+100),
            "N sidebar closes cleanly");
        gameplay::Transform transform; transform.position={3,2,1}; transform.rotation={15,25,35}; transform.scale={2,2,2};
        editor.setTransform(transform);
        require(world.snapshots()[0].transform.position[0]==3,"transform edits real world");
        require(editor.dirty(),"transform marks scene dirty");
        editor.undo(); require(world.snapshots()[0].transform.position[0]==0 && !editor.dirty(),"undo restores saved state");
        editor.redo(); require(world.snapshots()[0].transform.position[0]==3,"redo restores edit");
        editor.duplicate(); require(world.size()==2,"duplicate creates an entity");
        editor.remove(); require(world.size()==1,"delete removes selected entity");
        editor.undo(); require(world.size()==2,"undo restores deleted entity");
        editor.undo(); require(world.size()==1,"undo duplicate");
        require(editor.addModel(asset),"asset import accepted"); require(world.size()==2,"asset creates real entity");
        require(editor.takeFrameRequest(),"import requests camera frame");
        require(!editor.addModel(temp/"missing.glb"),"missing import rejected");
        const auto importId=editor.selected();const auto importTransform=*editor.selectedTransform();
        require(editor.beginTransformEdit(),"selected object starts transform transaction");
        auto invalid=importTransform;invalid.position[0]=NAN;editor.previewTransform(invalid);
        require(editor.selectedTransform()->position==importTransform.position,"nonfinite gizmo preview rejected");
        invalid=importTransform;invalid.scale[0]=0;editor.previewTransform(invalid);
        require(editor.selectedTransform()->scale==importTransform.scale,"zero-scale gizmo preview rejected");
        auto dragged=importTransform;dragged.position[0]=.75f;editor.previewTransform(dragged);
        render::CameraSettings camera; camera.position={5,4,-6}; camera.target={1,2,3}; camera.fovDegrees=45;
        editor.setCamera(camera);
        editor.setSaveHandler([](const fs::path&,std::string& message){message="Storage quota exceeded";return false;});
        require(!editor.save() && editor.dirty(),"host persistence failure preserves unsaved edits");
        require(!editor.editingTransform() && editor.selected()==importId,"saving commits active gesture without recreating the entity");
        int persisted=0;
        editor.setSaveHandler([&](const fs::path& path,std::string&){++persisted;return path==file && fs::is_regular_file(path);});
        require(editor.save(),"scene save succeeds"); require(!editor.dirty(),"save clears dirty state");
        require(persisted==1,"host persistence receives the written scene and can retry");
        editor.setSaveHandler({});
        SceneDocument reloaded; require(reloaded.load(file,error),error.c_str());
        require(reloaded.hasEditorOrbitPivot() && reloaded.editorCamera()->target==camera.target,"orbit pivot survives save/load");
        require(reloaded.editorCamera()->fovDegrees==45,"editor field of view survives save/load");
        gameplay::GameplayWorld restored; reloaded.instantiate(restored);
        require(restored.size()==2,"saved entities round trip");
        const auto entities=restored.snapshots();
        const auto cube=std::find_if(entities.begin(),entities.end(),[](const auto& entity){return entity.name=="Cube";});
        require(cube!=entities.end() && cube->transform.rotation[1]==25,"rotation survives save/load");
        require(cube!=entities.end() && !cube->renderable->visible,"visibility survives save/load");
        require(std::any_of(entities.begin(),entities.end(),[](const auto& entity){return entity.transform.position[0]==.75f;}),"dragged transform survives save and reload");
        Json::Value saved; { std::ifstream stream(file); stream>>saved; }
        require(saved["custom"]["keep"].asInt()==42 && saved["script"]=="setup.py","unknown fields and script references preserved");
        require(!fs::path(saved["entities"][0]["model"].asString()).is_absolute(),"assets saved relative to scene");
        saved["custom"]["external"]=true; {std::ofstream stream(file); stream<<saved;}
        require(!editor.save(),"external scene edits protected");
        saved.removeMember("script"); {std::ofstream stream(file); stream<<saved;}
        require(ProjectLoader::resolve(file).kind==LaunchKind::Scene,"scriptless authored scene resolves");
        saved["entities"][0]["scale"]=Json::Value(Json::arrayValue);
        for(int i=0;i<3;++i) saved["entities"][0]["scale"].append(0);
        {std::ofstream stream(file); stream<<saved;}
        require(!reloaded.load(file,error),"zero-scale scene rejected");
        require(reloaded.hasEntities(),"failed load preserves previous document");
        SceneDocument imported; imported.fromScript(temp/"setup.py");
        require(imported.save(world,{},error),"direct script creates companion scene");
        SceneDocument second; second.fromScript(temp/"setup.py");
        require(!second.save(world,{},error),"companion scene collision never overwrites");
        editor::Bounds bounds; bounds.include({-1,-1,-1}); bounds.include({1,1,1});
        auto ray=editor::cameraRay({0,0,-5},0,0,60,1.5f,.5f,.5f);
        require(std::abs(editor::intersect(ray,bounds)-4)<.001f,"center ray picks box");
        ray.origin={3,0,-5}; require(!std::isfinite(editor::intersect(ray,bounds)),"parallel ray outside box misses");
        ray.origin={0,0,0}; require(editor::intersect(ray,bounds)==0,"inside-box ray is valid");
        ray=editor::cameraRay({0,0,-5},0,0,60,1.5f,.8f,.5f); require(ray.direction[0]>0,"viewport right maps to camera right");
        const auto rectangle=editor::selection::rectangle({10,10},{90,90});
        require(editor::selection::overlaps(rectangle,{{80,40},{110,40},{110,60}}),
            "selection region includes triangles that cross its edge");
        require(!editor::selection::overlaps(rectangle,{{110,110},{130,110},{120,130}}),
            "selection region excludes distant triangles");
        require(editor::selection::contains(editor::selection::circle({50,50},18),{50,50}) &&
            !editor::selection::contains(editor::selection::circle({50,50},18),{80,50}),
            "circle selection respects its brush radius");
        editor.layout(1100,720); require(editor.viewport().width>500 && editor.viewport().height>350,"minimum window layout usable");
        editor.setQuality("high");
        require(!editor.takeQualityRequest(),"syncing renderer quality does not request another switch");
        require(!editor.dirty(),"quality preview does not change saved scene data");
        componentTests(temp,root,output);
        particleAuthoringTests(temp,root);
        workspaceTests(temp,root,output);
        playbackTests(temp,root,output);
        meshEditTests(temp,root);
        projectBrowserTests(temp,root,output);
        const auto first=world.snapshots().front().id;
        editor.createEmpty();const auto secondSelection=*editor.selected();
        editor.selectMany({first,secondSelection});
        require(editor.selectedIds().size()==2 && editor.isSelected(first) && editor.isSelected(secondSelection) && editor.selected()==secondSelection,
            "area selection keeps multiple entities and an active object");
        editor.selectMany({first},false,true);
        require(editor.selectedIds().size()==1 && editor.selected()==secondSelection,
            "toggle selection removes one entity without clearing the others");
        editor.setSelectionMode(editor::GameEditor::SelectionMode::Lasso);
        require(editor.tool()==editor::TransformTool::Select && editor.selectionMode()==editor::GameEditor::SelectionMode::Lasso,
            "lasso mode activates object selection");
        editor.saveLayout(); // Flush before removing this test's temporary project.
        // Only remove the unique test directory created by this process.
        fs::remove_all(temp);
        std::cout<<"PASS: "<<checks<<" editor checks (scene persistence, history, layout, picking)\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr<<"FAIL: "<<exception.what()<<"\n"; return 1; }
}
