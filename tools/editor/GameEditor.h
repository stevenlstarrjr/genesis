#pragma once
#include "SceneDocument.h"
#include "ui/EditorWidgets.h"
#include "ui/DockSpace.h"
#include "editor/TransformGizmo.h"
#include "editor/ProjectAssets.h"
#include "editor/EditMesh.h"
#include <array>
#include <optional>
#include <functional>
#include <utility>
#include <chrono>
#include <unordered_map>

namespace genesis::editor {
// Editor application/model. The host supplies the 3D image in viewport(); all
// surrounding controls use the same Yoga/ThorVG toolkit as game UI.
class GameEditor {
public:
    enum class EditorMode { Object, Edit };
    enum class MeshSelectionMode { Vertex, Edge, Face };
    GameEditor(gameplay::GameplayWorld& world, SceneDocument& scene, const std::filesystem::path& assetRoot, const std::filesystem::path& projectRoot={});
    ~GameEditor();
    ui::Document& document() { return m_document; }
    void layout(float width,float height);
    ui::Rect viewport() const { return !playing() && m_dock->visible("scene") ? m_viewport->bounds() : ui::Rect{}; }
    ui::Rect gameViewport() const { return playing() && m_dock->visible("scene") ? m_viewport->bounds() : ui::Rect{}; }
    bool viewportSidebarVisible() const { return m_viewportSidebarVisible; }
    void toggleViewportSidebar();
    bool viewportSidebarContains(float x,float y) const;
    const Vec3& viewportCursor() const { return m_viewportCursor; }
    void setViewportCursor(const Vec3& position);
    enum class PlayState { Edit, Playing, Paused };
    PlayState playState() const { return m_playState; }
    bool playing() const { return m_playState!=PlayState::Edit; }
    void play();
    void pause();
    void step();
    void stop();
    EditorMode editorMode() const { return m_editorMode; }
    bool setEditorMode(EditorMode mode);
    MeshSelectionMode meshSelectionMode() const { return m_meshSelectionMode; }
    void setMeshSelectionMode(MeshSelectionMode mode);
    bool pickMeshElement(float x,float y,const ViewportCamera& camera);
    std::optional<Vec3> editSelectionCenter() const;
    bool beginMeshDrag();
    void previewMeshDrag(Vec3 worldDelta);
    void endMeshDrag(bool commit);
    bool meshDragActive() const { return m_meshDragActive; }
    struct MeshOverlay {
        std::vector<Vec3> vertices;
        std::vector<std::array<uint32_t,2>> edges;
        std::vector<std::array<uint32_t,3>> faces;
        std::vector<uint32_t> selected;
    };
    const MeshOverlay& meshOverlay() const { return m_meshOverlay; }
    static constexpr double fixedDelta=1.0/60.0;
    void advance(double elapsed);
    uint64_t ticks() const { return m_ticks; }
    double simulationTime() const { return m_ticks*fixedDelta; }
    using TickHandler=std::function<void(float)>;
    void setTickHandler(TickHandler handler) { m_tickHandler=std::move(handler); }
    const render::CameraSettings& camera() const { return m_camera; }
    uint64_t cameraRestoreRevision() const { return m_cameraRestoreRevision; }
    const ui::Surface& render(float width,float height,float scale=1) { layout(width,height); return m_document.render(width,height,scale); }
    std::optional<gameplay::EntityId> selected() const { return m_selected; }
    void select(std::optional<gameplay::EntityId> id);
    bool addModel(const std::filesystem::path& path);
    bool addPrefab(const std::filesystem::path& path);
    bool placeModel(const std::filesystem::path& path,float x,float y);
    std::optional<Vec3> placementPoint(float x,float y) const;
    void refreshProject(bool immediate=false);
    void browseAssets(const std::string& folder);
    const ProjectAssets& projectAssets() const { return *m_projectAssets; }
    void setAssetWatch(bool enabled) { m_assetWatch=enabled; }
    enum class Component { MeshRenderer, PointLight, SpotLight, AreaLight, ReflectionProbe, FluidParticles, ClothParticles, GranularParticles, ExplosionParticles, EmitterParticles };
    void createEmpty();
    std::optional<gameplay::EntitySnapshot> selectedEntity() const;
    bool assignModel(const std::filesystem::path& path);
    void removeMesh();
    bool setMaterial(std::optional<gameplay::Renderable::Material> material);
    bool setParticleKinematic(bool enabled);
    bool setLight(std::optional<gameplay::Light> light);
    bool setProbe(std::optional<gameplay::ReflectionProbe> probe);
    bool setParticles(std::optional<gameplay::ParticleSystem> particles);
    void addComponent(Component component);
    void openComponentPicker();
    void openModelPicker();
    void setLightCapacity(std::array<int,3> capacity);
    void duplicate();
    void remove();
    void undo();
    void redo();
    bool save();
    // Platform hosts can persist the just-written scene (e.g. IndexedDB).
    // Failure must keep the editor dirty even though its temporary file exists.
    using SaveHandler = std::function<bool(const std::filesystem::path&,std::string&)>;
    void setSaveHandler(SaveHandler handler) { m_saveHandler=std::move(handler); }
    bool dirty() const;
    void setTransform(const gameplay::Transform& transform);
    void setSelectedDimensions(gameplay::EntityId id,const std::array<float,3>& dimensions);
    std::optional<gameplay::Transform> selectedTransform() const;
    TransformTool tool() const { return m_tool; }
    void setTool(TransformTool tool);
    enum class SelectionMode { Select, Tweak, Box, Circle, Lasso };
    std::vector<uint32_t> meshRegionHits(SelectionMode mode,
        const std::vector<gizmoMath::Vec2>& points,float radius,const ViewportCamera& camera) const;
    void setMeshSelection(const std::vector<uint32_t>& base,const std::vector<uint32_t>& hits,
        bool extend=false,bool toggle=false);
    SelectionMode selectionMode() const { return m_selectionMode; }
    void setSelectionMode(SelectionMode mode);
    const std::vector<gameplay::EntityId>& selectedIds() const { return m_selectedIds; }
    bool isSelected(gameplay::EntityId id) const;
    void selectMany(const std::vector<gameplay::EntityId>& ids,bool extend=false,bool toggle=false);
    bool cursorPlacement() const { return m_cursorPlacement; }
    void setCursorPlacement(bool enabled);
    bool viewportToolStripContains(float x,float y) const;
    bool snapping() const { return m_snap; }
    SnapSteps snapSteps() const;
    bool localOrientation() const { return m_localOrientation; }
    void setLocalOrientation(bool local);
    // A live gesture is one undo record. Cancel restores the original entity
    // without recreating IDs, discarding redo, or adding an empty history entry.
    bool beginTransformEdit();
    void previewTransform(const gameplay::Transform& transform);
    void endTransformEdit(bool commit=true);
    bool editingTransform() const { return m_transformEdit.has_value(); }
    void setCamera(const render::CameraSettings& camera) { m_camera = camera; }
    void status(const std::string& message);
    const std::vector<std::string>& messages() const { return m_messages; }
    void showConsole(bool show=true) { m_dock->show(show ? "console" : "project"); }
    void resetLayout();
    void applyLayoutPreset(const std::string& name);
    ui::DockSpace& dockSpace() { return *m_dock; }
    bool saveLayout();
    const std::filesystem::path& layoutPath() const { return m_layoutPath; }
    std::optional<std::filesystem::path> selectedAsset() const;
    void frameSelection() { m_frameRequested = true; }
    bool takeFrameRequest() { return std::exchange(m_frameRequested,false); }
    std::optional<std::string> takeQualityRequest() { return std::exchange(m_qualityRequest,std::nullopt); }
    void setQuality(const std::string& preset);
    enum class ViewportShading { Wireframe, Solid, MaterialPreview, Rendered };
    ViewportShading viewportShading() const { return m_viewportShading; }
    void setViewportShading(ViewportShading mode);
    bool grid() const { return m_grid; }
    bool animate() const { return m_animate; }
    bool gizmoVisible() const { return m_showGizmo; }
    uint64_t revision() const { return m_revision; }
    const std::filesystem::path& scenePath() const { return m_scene.path(); }
private:
    struct State { std::vector<gameplay::EntitySnapshot> entities; size_t selection = 0; };
    gameplay::GameplayWorld& m_world;
    SceneDocument& m_scene;
    ui::Document m_document;
    std::unique_ptr<ui::MenuBar> m_menus;
    std::unique_ptr<ui::MenuBar> m_layoutMenu;
    std::unique_ptr<ui::MenuBar> m_viewportMenu;
    std::unique_ptr<ui::MenuBar> m_snapMenu,m_overlayMenu,m_shadingMenu;
    std::unique_ptr<ui::Dropdown> m_modeDropdown,m_orientationDropdown,m_pivotDropdown,m_sidebarRotationDropdown;
    ui::Node* m_meshModeMount{};
    std::array<ui::Node*,3> m_meshModeButtons{};
    EditorMode m_editorMode=EditorMode::Object;
    MeshSelectionMode m_meshSelectionMode=MeshSelectionMode::Vertex;
    std::unique_ptr<EditMesh> m_editMesh;
    std::unique_ptr<MeshSurfaceIndex> m_meshSurface;
    std::vector<uint32_t> m_editSelectedVertices;
    MeshOverlay m_meshOverlay;
    std::vector<gameplay::Renderable::MeshVertexEdit> m_meshDragOriginal;
    std::optional<State> m_meshDragBefore;
    bool m_meshDragActive=false;
    void refreshMeshOverlay();
    std::unique_ptr<ui::ResourcePicker> m_resourcePicker;
    std::unique_ptr<ui::ColorPicker> m_colorPicker;
    std::unique_ptr<ui::ColorField> m_materialColor,m_lightColor,m_emitterStartColor,m_emitterEndColor;
    std::unique_ptr<ui::ScrollView> m_inspector;
    std::unique_ptr<ui::DockSpace> m_dock;
    std::vector<ProjectAsset> m_assets;
    std::optional<std::filesystem::path> m_selectedAsset;
    std::unique_ptr<ProjectAssets> m_projectAssets;
    AssetThumbnails m_thumbnails;
    struct AssetTile { std::filesystem::path path;ui::Node* node;bool ready=false; };
    // Hierarchy, Project and Console can be open in several areas (Blender-style
    // copies); each copy has its own filter, folder and paging state.
    struct HierarchyView { std::string id; ui::Node *filter{},*count{},*empty{}; std::unique_ptr<ui::TreeView> tree; };
    struct ConsoleView { std::string id; std::unique_ptr<ui::ScrollView> list; };
    struct ProjectView {
        std::string id; ui::Node *filter{},*count{},*breadcrumb{},*prev{},*next{};
        std::unique_ptr<ui::TreeView> folders; std::unique_ptr<ui::ScrollView> grid;
        std::vector<AssetTile> tiles; size_t page=0; std::string category="all";
    };
    std::vector<std::unique_ptr<HierarchyView>> m_hierarchies;
    std::vector<std::unique_ptr<ConsoleView>> m_consoles;
    std::vector<std::unique_ptr<ProjectView>> m_projects;
    void buildHierarchy(ui::Node& page,const std::string& id);
    void buildConsole(ui::Node& page,const std::string& id);
    void buildProject(ui::Node& page,const std::string& id);
    void rebuildTree(HierarchyView& view);
    void refreshConsole(ConsoleView& view);
    void refreshAssets(ProjectView& view);
    void rebuildAssetFolders(ProjectView& view);
    void browseAssets(ProjectView& view,const std::string& folder);
    bool m_assetWatch=true,m_assetRefresh=false;
    std::chrono::steady_clock::time_point m_assetScanTime{};
    ui::Node *m_assetPreview{},*m_assetDragLabel{};
    std::optional<std::filesystem::path> m_assetDrag;
    float m_assetDragX=0,m_assetDragY=0;
    std::vector<std::string> m_messages;
    ui::Node *m_viewport{}, *m_viewportHeader{}, *m_viewportToolStrip{}, *m_cursorToolButton{}, *m_sceneEditorTypeButton{}, *m_status{}, *m_name{}, *m_visible{}, *m_resource{}, *m_selection{};
    bool m_sceneEditorTypeVisible=true;
    ui::Node *m_viewportSidebar{},*m_sidebarName{},*m_sidebarToolName{};
    ui::Node *m_sidebarOptionsButton{},*m_sidebarOptionsPopup{},*m_sidebarRotationMount{};
    std::array<ui::Node*,3> m_sidebarPages{},m_sidebarTabs{};
    std::array<ui::Node*,9> m_sidebarTransform{};
    std::array<ui::Node*,9> m_sidebarLockButtons{};
    std::array<ui::Node*,3> m_sidebarDimensions{};
    std::unordered_map<gameplay::EntityId,std::array<bool,9>> m_sidebarLocks;
    std::array<float,3> m_selectedDimensions{};
    bool m_hasSelectedDimensions=false;
    std::optional<gameplay::EntityId> m_selectedDimensionsId;
    std::array<ui::Node*,3> m_sidebarCursor{};
    std::array<ui::Node*,5> m_sidebarTools{},m_sidebarSelectTools{};
    ui::Node *m_sidebarSnap{},*m_sidebarGrid{},*m_sidebarGizmo{},*m_sidebarFov{};
    bool m_viewportSidebarVisible=false;
    Vec3 m_viewportCursor{};
    size_t m_viewportSidebarTab=0;
    float m_sidebarWidth=-1,m_sidebarHeight=-1;
    void buildViewportSidebar();
    void buildViewportOptionsPopup();
    void openViewportOptionsPopup();
    void arrangeViewportSidebar();
    void showViewportSidebarTab(size_t tab);
    void refreshViewportSidebar();
    gameplay::Transform withTransformLocks(const gameplay::Transform& transform) const;
    ui::Node *m_emptyInspector{};
    ui::Node *m_assetInspector{}, *m_assetTitle{}, *m_assetPath{}, *m_assetAdd{};
    float m_layoutWidth=1440, m_layoutHeight=900;
    std::filesystem::path m_layoutPath;
    bool m_layoutPending=false,m_loadingLayout=true;
    std::chrono::steady_clock::time_point m_layoutChanged;
    std::array<ui::Node*,3> m_qualityButtons{};
    std::array<ui::Node*,4> m_shadingButtons{};
    ui::Node *m_gridHeaderButton{}, *m_snapHeaderButton{}, *m_gizmoHeaderButton{};
    ViewportShading m_viewportShading=ViewportShading::Solid;
    std::array<ui::Node*,5> m_viewportToolButtons{};
    std::array<ui::Node*,5> m_selectionButtons{};
    std::array<ui::Node*,9> m_transform{};
    ui::Node *m_meshSection{},*m_rendererSection{},*m_materialBody{},*m_materialOverride{},*m_particleKinematic{},*m_addComponent{};
    ui::Node *m_lightSection{},*m_lightEnabled{},*m_lightShadows{},*m_spotFields{},*m_areaFields{};
    ui::Node *m_probeSection{},*m_probeEnabled{};
    ui::Node *m_particleSection{},*m_particleEnabled{},*m_particlePhysicalFields{},*m_particleFluidFields{},*m_particleClothFields{},*m_particleExplosionFields{};
    ui::Node *m_emitterSection{},*m_emitterName{},*m_emitterLayerLabel{},*m_emitterPrev{},*m_emitterNext{},*m_emitterAdd{},*m_emitterRemove{};
    std::array<ui::Node*,2> m_emitterShapes{};
    std::array<ui::Node*,19> m_emitterFields{};
    size_t m_selectedEmitterLayer=0;
    std::array<ui::Node*,5> m_particleTypes{};
    std::array<ui::Node*,4> m_particlePinEdges{};
    std::array<ui::Node*,4> m_particleExplosionValues{};
    std::array<ui::Node*,10> m_particleFields{};
    std::array<ui::Node*,2> m_materialFields{};
    std::array<ui::Node*,7> m_lightFields{};
    std::array<ui::Node*,3> m_lightTypes{};
    std::array<ui::Node*,5> m_probeFields{};
    std::array<int,3> m_lightCapacity{4,4,2};
    std::optional<gameplay::EntityId> m_selected;
    std::vector<gameplay::EntityId> m_selectedIds;
    render::CameraSettings m_camera;
    std::vector<State> m_undo,m_redo;
    struct PlaySession {
        gameplay::GameplayWorld authored;
        std::optional<gameplay::EntityId> selection;
        std::optional<std::filesystem::path> asset;
        render::CameraSettings camera;
        bool dirty=false;
    };
    std::unique_ptr<PlaySession> m_session;
    PlayState m_playState=PlayState::Edit;
    TickHandler m_tickHandler;
    double m_accumulator=0;
    uint64_t m_ticks=0,m_cameraRestoreRevision=0;
    ui::Node *m_playButton{},*m_pauseButton{},*m_stepButton{},*m_stopButton{};
    void updatePlayback();
    void tick();
    struct TransformEdit { gameplay::EntityId id; gameplay::Transform original; State before; };
    std::optional<TransformEdit> m_transformEdit;
    TransformTool m_tool=TransformTool::Move;
    SelectionMode m_selectionMode=SelectionMode::Select;
    bool m_cursorPlacement=false;
    bool m_snap=false;
    enum class SnapPreset { Fine, Standard, Coarse };
    SnapPreset m_snapPreset=SnapPreset::Standard;
    bool m_localOrientation=false;
    std::string m_savedState;
    SaveHandler m_saveHandler;
    std::optional<std::string> m_qualityRequest;
    uint64_t m_revision=0;
    bool m_frameRequested=false,m_grid=true,m_animate=false,m_showGizmo=true;
    std::string m_qualityPreset="medium";
    void setSnapping(bool enabled);
    void setSnapPreset(SnapPreset preset);
    void setGridVisible(bool visible);
    void setGizmoVisible(bool visible);
    State capture() const;
    std::string fingerprint() const;
    void restore(const State& state);
    void checkpoint();
    void changed();
    void rebuildTree();
    void updateInspector();
    void refreshConsole();
    void refreshAssets();
    void updateAssets();
    void rebuildAssetFolders();
    void dragAsset(const std::filesystem::path& path,float x,float y);
    void finishAssetDrag(bool commit);
    bool addModelAt(const std::filesystem::path& path,const Vec3& position,bool frame);
    bool addPrefabAt(const std::filesystem::path& path,const Vec3& position,bool frame);
    void inspectAsset(size_t index);
    void buildComponentInspector();
    void updateComponentInspector(const gameplay::EntitySnapshot& entity);
    void componentsChanged();
    bool lightSlotAvailable(gameplay::Light::Type type,bool replacing=true) const;
    void loadLayout();
};
}
