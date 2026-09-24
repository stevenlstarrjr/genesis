#define SDL_MAIN_HANDLED
#include "editor/SdlEditorHost.h"
#include <iostream>
#include <stdexcept>

using namespace genesis;
using namespace genesis::editor;
namespace {
int checks=0;
void require(bool condition,const char* message) { ++checks; if(!condition) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b)<.0002f; }
bool near(const Vec3& a,const Vec3& b) { return near(a[0],b[0]) && near(a[1],b[1]) && near(a[2],b[2]); }
render::CameraSettings initial() { render::CameraSettings c; c.position={0,0,-10}; c.target={0,0,0}; return c; }
void mathTests() {
    ViewportCamera c(initial());
    require(near(c.position(),{0,0,-10}) && near(c.distance(),10),"initial orbit pose preserves camera");
    c.begin(ViewportCamera::Gesture::Orbit); c.drag(40,20,600); c.end();
    require(c.yaw()>0 && c.pitch()<0,"turntable drag rotates both axes");
    require(near(c.pivot(),{0,0,0}) && near(c.distance(),10),"orbit preserves pivot/radius");
    c.begin(ViewportCamera::Gesture::Orbit); c.drag(0,100000,600); c.end();
    require(c.pitch()>-1.5707f,"pole clamp prevents upside-down/singular camera");
    c=ViewportCamera(initial()); c.begin(ViewportCamera::Gesture::Pan); c.drag(100,50,600); c.end();
    require(c.pivot()[0]<0 && c.pivot()[1]>0,"pan grabs scene in screen space");
    require(near(c.yaw(),0) && near(c.pitch(),0) && near(c.distance(),10),"pan preserves orientation/radius");
    const auto p=c.position(); require(near(p[0],c.pivot()[0]) && near(p[1],c.pivot()[1]),"pan translates eye and pivot together");
    ViewportCamera tall(initial()); tall.begin(ViewportCamera::Gesture::Pan); tall.drag(200,100,1200);
    require(near(tall.position(),c.position()),"pan scales with viewport height");
    c=ViewportCamera(initial()); c.wheel(2); require(c.distance()<10,"wheel forward zooms in");
    c.wheel(-2); require(near(c.distance(),10),"wheel zoom reversible");
    c.wheel(1e6); require(near(c.distance(),.1f),"zoom never crosses pivot");
    c.wheel(-1e6); require(near(c.distance(),10000),"zoom-out bounded");
    c=ViewportCamera(initial()); c.begin(ViewportCamera::Gesture::Zoom); c.drag(0,-20,600);
    require(c.distance()<10,"Ctrl-MMB up zooms in"); c.cancel(); require(near(c.position(),initial().position),"Escape restores pre-drag pose");
    Bounds bounds; bounds.include({3,0,-2}); bounds.include({5,2,2});
    c.frame(bounds,1.5f); require(near(c.pivot(),{4,1,0}),"frame selection sets orbit pivot");
    const float wide=c.distance(); c.frame(bounds,.5f); require(c.distance()>wide,"frame accommodates narrow viewport");
    const Vec3 eye=c.position(); c.setPivotDepth(20); require(near(eye,c.position()) && near(c.distance(),20),"legacy pivot migration keeps viewpoint");
    const auto saved=c.position(); c.begin(ViewportCamera::Gesture::Orbit); c.drag(NAN,1,600);
    require(near(saved,c.position()),"invalid input rejected");
}
void gizmoTests() {
    using namespace gizmoMath;
    const ui::Rect viewport{100,50,800,600};ViewportCamera camera(initial());gameplay::Transform transform;
    auto screen=[&](Vec3 point){const auto p=project(point,camera,viewport);require(p.has_value(),"gizmo point projects in front of camera");return *p;};
    auto frame=GizmoFrame::make(TransformTool::Move,transform,camera,viewport);
    auto p=screen(add(frame.origin,mul(frame.axes[0],frame.size*.6f)));
    require(frame.hit(p[0],p[1],camera,viewport)==0,"move shaft is pickable with the geometry used to render it");
    require(frame.hit(0,0,camera,viewport)==-1,"gizmos cannot intercept panel input");
    require(std::none_of(frame.lines.begin(),frame.lines.end(),[](const auto& l){return l.handle==2;}),"camera-aligned linear axis is hidden instead of producing singular drags");
    TransformGizmo gizmo;require(gizmo.begin(TransformTool::Move,transform,camera,viewport,p[0],p[1]),"begin move");
    const auto moved=gizmo.drag(p[0]+30,p[1],false);
    require(near(moved.position[0],frame.size/3) && near(moved.position[1],0),"move follows ray-plane intersection on chosen axis");
    require(near(gizmo.drag(p[0]+30,p[1],true).position[0],.5f),"move snaps to half-unit increments relative to drag origin");
    require(near(gizmo.drag(p[0]+30,p[1],true,SnapSteps{.1f,5.0f,.05f}).position[0],
        std::round(moved.position[0]/.1f)*.1f),"fine snap preset changes move increments");
    require(near(gizmo.drag(p[0]+15,p[1],false).position[0],frame.size/6),"drag samples do not accumulate translation error");gizmo.end();
    auto angledSettings=initial();angledSettings.position={4,3,-10};
    ViewportCamera angledCamera(angledSettings);
    const auto angledFrame=GizmoFrame::make(TransformTool::Move,transform,angledCamera,viewport);
    for(int excluded=0;excluded<3;++excluded){
        const auto marker=std::find_if(angledFrame.markers.begin(),angledFrame.markers.end(),[&](const auto& item){
            return item.shape==GizmoMarker::Shape::Plane && item.handle==excluded+3;});
        require(marker!=angledFrame.markers.end(),"each axis has a secondary plane handle");
        const auto point=project(marker->center,angledCamera,viewport);
        require(point && gizmo.begin(TransformTool::Move,transform,angledCamera,viewport,(*point)[0],(*point)[1])
            && gizmo.handle()==excluded+3,"secondary plane handle is pickable");
        const auto planar=gizmo.drag((*point)[0]+30,(*point)[1]+15,false);
        require(near(planar.position[excluded],0) && !near(planar.position,Vec3{}),
            "secondary handle moves in the plane excluding its colored axis");gizmo.end();
    }
    transform.position={0,0,-20};require(GizmoFrame::make(TransformTool::Move,transform,camera,viewport).lines.empty(),"behind-camera object has no gizmo");
    transform={};frame=GizmoFrame::make(TransformTool::Rotate,transform,camera,viewport);
    auto ringPoint=[&](int axis,float angle){return add(frame.origin,mul(add(mul(frame.axes[(axis+1)%3],std::cos(angle)),mul(frame.axes[(axis+2)%3],std::sin(angle))),frame.size));};
    p=screen(ringPoint(2,pi/4));require(gizmo.begin(TransformTool::Rotate,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==2,"rotation ring is pickable away from intersections");
    auto end=screen(ringPoint(2,pi/4+.4f));auto rotated=gizmo.drag(end[0],end[1],true);
    require(near(rotated.rotation[2],-30),"local rotation snaps to 15 degrees in renderer's Euler convention");
    end=screen(ringPoint(2,pi/4+pi*.5f));rotated=gizmo.drag(end[0],end[1],false);
    require(near(rotated.rotation[2],-90) && near(rotated.position,transform.position),"rotation follows circle and retains pivot");gizmo.end();
    transform.rotation={23,35,-19};frame=GizmoFrame::make(TransformTool::Rotate,transform,camera,viewport);
    bool found=false;float startAngle=0;
    for(int i=1;i<64 && !found;++i){startAngle=i*2*pi/64;p=screen(ringPoint(1,startAngle));found=frame.hit(p[0],p[1],camera,viewport)==1;}
    require(found && gizmo.begin(TransformTool::Rotate,transform,camera,viewport,p[0],p[1]),"already-rotated object's local ring starts drag");
    end=screen(ringPoint(1,startAngle+.3f));rotated=gizmo.drag(end[0],end[1],false);
    auto expected=basis(transform.rotation),actual=basis(rotated.rotation);
    for(int i=0;i<3;++i)require(near(actual[i],rotate(expected[i],frame.axes[1],.3f)),"local rotation composes with existing orientation");gizmo.end();
    for(float y:{-90.0f,90.0f}) {
        const Vec3 angles{23,y,17};const auto original=basis(angles),roundtrip=basis(euler(original,angles));
        for(int i=0;i<3;++i)require(near(original[i],roundtrip[i]),"Euler conversion remains finite and equivalent at gimbal lock");
    }
    transform={};transform.rotation[2]=90;transform.scale={-2,3,4};
    frame=GizmoFrame::make(TransformTool::Scale,transform,camera,viewport);
    p=screen(add(frame.origin,mul(frame.axes[0],frame.size*.6f)));
    require(gizmo.begin(TransformTool::Scale,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==0,"scale handles follow rotated local axes");
    end=screen(add(frame.origin,mul(frame.axes[0],frame.size*.83f)));
    auto scaled=gizmo.drag(end[0],end[1],true);
    require(near(scaled.scale,{-2.4f,3,4}),"axis scale snaps to ten percent and preserves negative scale");
    end=screen(add(frame.origin,mul(frame.axes[0],-frame.size)));
    scaled=gizmo.drag(end[0],end[1],false);require(std::abs(scaled.scale[0])>=.001f && scaled.scale[0]<0,"scale cannot cross zero or flip sign");gizmo.end();
    p=screen(add(frame.origin,mul(frame.axes[0],-frame.size)));
    require(gizmo.begin(TransformTool::Scale,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==0,
        "negative scale cube is pickable");
    end=screen(add(frame.origin,mul(frame.axes[0],-frame.size*1.25f)));
    require(std::abs(gizmo.drag(end[0],end[1],false).scale[0])>2,
        "dragging the negative scale cube outward enlarges the axis");gizmo.end();
    p=screen(frame.origin);require(gizmo.begin(TransformTool::Scale,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==3,"center cube starts uniform scale");
    require(near(gizmo.drag(p[0]+45,p[1],false).scale,{-3,4.5f,6}),"uniform scaling preserves aspect ratio");gizmo.end();
    camera.wheel(-5);frame=GizmoFrame::make(TransformTool::Move,transform,camera,viewport);
    p=screen(frame.origin);end=screen(add(frame.origin,mul(frame.axes[0],frame.size)));
    require(near(end[0]-p[0],90),"handle stays ninety logical pixels after camera zoom");
    camera=ViewportCamera(initial());transform={};
    frame=GizmoFrame::make(TransformTool::Transform,transform,camera,viewport);
    require(std::any_of(frame.lines.begin(),frame.lines.end(),[](const auto& line){return line.handle==0;}) &&
        std::any_of(frame.lines.begin(),frame.lines.end(),[](const auto& line){return line.handle==5;}) &&
        std::any_of(frame.lines.begin(),frame.lines.end(),[](const auto& line){return line.handle==6;}) &&
        std::any_of(frame.lines.begin(),frame.lines.end(),[](const auto& line){return line.handle==9;}),
        "combined transform frame contains move, rotate, axis scale and uniform scale handles");
    require(std::count_if(frame.markers.begin(),frame.markers.end(),[](const auto& marker){
        return marker.shape==GizmoMarker::Shape::Cone;})>=2 &&
        std::count_if(frame.markers.begin(),frame.markers.end(),[](const auto& marker){
        return marker.shape==GizmoMarker::Shape::Cube;})>=2,
        "combined gizmo uses solid arrowheads and scale cubes");
    p=screen(add(frame.origin,mul(frame.axes[0],frame.size*.3f)));
    require(gizmo.begin(TransformTool::Transform,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==0,
        "combined move shaft picks translation");
    require(gizmo.drag(p[0]+30,p[1],false).position[0]>0,"combined move handle translates");gizmo.end();
    p=screen(add(frame.origin,add(mul(frame.axes[0],frame.size*.52f),
        mul(frame.axes[1],frame.size*.045f))));
    require(gizmo.begin(TransformTool::Transform,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==6,
        "combined axis scale box picks scale instead of the move shaft");
    require(gizmo.drag(p[0]+30,p[1],false).scale[0]>1,"combined axis scale handle scales");gizmo.end();
    const auto combinedPlane=GizmoFrame::make(TransformTool::Transform,transform,angledCamera,viewport);
    const auto bluePlane=std::find_if(combinedPlane.markers.begin(),combinedPlane.markers.end(),[](const auto& item){
        return item.shape==GizmoMarker::Shape::Plane && item.handle==11;});
    require(bluePlane!=combinedPlane.markers.end(),"combined gizmo keeps the blue plane handle");
    const auto bluePoint=project(bluePlane->center,angledCamera,viewport);
    require(bluePoint && gizmo.begin(TransformTool::Transform,transform,angledCamera,viewport,(*bluePoint)[0],(*bluePoint)[1])
        && gizmo.handle()==11,"combined blue plane handle is pickable");
    const auto blueMove=gizmo.drag((*bluePoint)[0]+25,(*bluePoint)[1]+12,false);
    require(near(blueMove.position[1],0) && !near(blueMove.position,Vec3{}),
        "combined blue plane handle moves on red and green axes only");gizmo.end();
    p=screen(add(frame.origin,mul(add(frame.axes[0],frame.axes[1]),frame.size*.70710678f)));
    require(gizmo.begin(TransformTool::Transform,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==5,
        "combined rotation ring picks rotation");
    end=screen(add(frame.origin,mul(add(mul(frame.axes[0],std::cos(pi/4+.3f)),
        mul(frame.axes[1],std::sin(pi/4+.3f))),frame.size)));
    require(std::abs(gizmo.drag(end[0],end[1],false).rotation[2])>1,
        "combined rotation handle rotates");gizmo.end();
    p=screen(frame.origin);
    require(gizmo.begin(TransformTool::Transform,transform,camera,viewport,p[0],p[1]) && gizmo.handle()==9,
        "combined center handle picks uniform scale");
    require(gizmo.drag(p[0]+45,p[1],false).scale[0]>1,"combined uniform scale handle scales");gizmo.end();
}
void hostTests() {
    // Exercise the real SDL adapter without taking over the user's desktop.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"); require(SDL_Init(SDL_INIT_VIDEO),"SDL dummy initialization");
    SDL_Window* window=SDL_CreateWindow("Navigation tests",1440,900,SDL_WINDOW_HIDDEN);
    require(window!=nullptr,"hidden test window");
    {
        gameplay::GameplayWorld world; world.create("Test mesh"); SceneDocument scene;
        GameEditor editor(world,scene,{}); editor.layout(1440,900);
        SdlEditorHost host(editor,window,initial()); auto& c=host.camera(); const auto view=editor.viewport();
        const float x=view.x+view.width*.5f,y=view.y+view.height*.5f;
        auto button=[&](SDL_EventType type,Uint8 button,float px,float py) {
            SDL_Event e{}; e.type=type; e.button.button=button; e.button.x=px; e.button.y=py; return host.event(e);
        };
        auto motion=[&](float dx,float dy) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=dx; e.motion.yrel=dy; e.motion.x=-10; e.motion.y=-10; return host.event(e); };
        auto key=[&](SDL_Keycode code) { SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.key=code; return host.event(e); };
        auto wheel=[&](float steps,float px,float py,bool flipped=false) {
            SDL_Event e{}; e.type=SDL_EVENT_MOUSE_WHEEL; e.wheel.y=steps; e.wheel.mouse_x=px; e.wheel.mouse_y=py;
            e.wheel.direction=flipped?SDL_MOUSEWHEEL_FLIPPED:SDL_MOUSEWHEEL_NORMAL; return host.event(e);
        };
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,20,300);
        require(!c.dragging(),"MMB over side panel never starts navigation");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,x,y); require(c.dragging(),"MMB starts viewport orbit");
        motion(25,15); require(!near(c.yaw(),0),"captured motion outside viewport still orbits");
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,-20,-20); require(!c.dragging(),"release outside viewport ends capture");
        SDL_SetModState(SDL_KMOD_SHIFT); const auto pivot=c.pivot(); const float yaw=c.yaw();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,x,y); motion(30,20);
        require(!near(c.pivot(),pivot) && near(c.yaw(),yaw),"Shift-MMB routes to pan");
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,x,y);
        SDL_SetModState(SDL_KMOD_CTRL); const float distance=c.distance();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,x,y); motion(0,-30);
        require(c.distance()<distance,"Ctrl-MMB routes to zoom");
        key(SDLK_ESCAPE); require(!c.dragging() && near(c.distance(),distance),"Escape cancels drag and capture");
        SDL_SetModState(SDL_KMOD_NONE);
        wheel(1,20,400); require(near(c.distance(),distance),"panel wheel does not zoom scene");
        wheel(1,x,y); require(c.distance()<distance,"viewport wheel zooms");
        wheel(1,x,y,true); require(near(c.distance(),distance),"flipped wheel respected");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,x,y); motion(10,10);
        SDL_Event lost{}; lost.type=SDL_EVENT_WINDOW_FOCUS_LOST; host.event(lost);
        require(!c.dragging() && !SDL_GetWindowRelativeMouseMode(window),"focus loss releases navigation");
        key(SDLK_KP_PERIOD); require(editor.takeFrameRequest(),"numpad decimal frames selection");
        key(SDLK_F); require(editor.takeFrameRequest(),"F frame alias preserved");
        key(SDLK_N); require(editor.viewportSidebarVisible(),"N opens the viewport sidebar");
        const float sidebarX=view.x+view.width-20,sidebarY=view.y+100;
        require(editor.viewportSidebarContains(sidebarX,sidebarY),"viewport sidebar occupies the right edge");
        const float sidebarDistance=c.distance();
        wheel(1,sidebarX,sidebarY); require(near(c.distance(),sidebarDistance),"sidebar wheel does not zoom the viewport");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,sidebarX,sidebarY);
        require(!c.dragging(),"middle click on sidebar does not orbit the viewport");
        key(SDLK_N); require(!editor.viewportSidebarVisible(),"N closes the viewport sidebar");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,1300,67);
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,1300,67);
        require(editor.document().wantsTextInput(),"inspector text field accepts focus");
        key(SDLK_F); key(SDLK_KP_PERIOD); require(!editor.takeFrameRequest(),"text focus blocks navigation shortcuts");
        key(SDLK_E);require(editor.tool()==TransformTool::Move,"typing E does not change the transform tool");
        const auto before=c.position(); button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,x,y); motion(30,30);
        require(near(before,c.position()),"RMB never drives editor camera");
        require(!editor.dirty(),"navigation never changes authored scene objects");
        editor.document().cancelInput();c=ViewportCamera(initial());
        key(SDLK_Q);require(editor.tool()==TransformTool::Select && host.gizmoFrame().lines.empty(),"Q selects without handles");
        editor.setSelectionMode(GameEditor::SelectionMode::Box);
        require(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,x-50,y-40),"box select captures pointer down");
        SDL_Event boxMotion{};boxMotion.type=SDL_EVENT_MOUSE_MOTION;boxMotion.motion.x=x+50;boxMotion.motion.y=y+40;
        host.event(boxMotion);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,x+50,y+40);
        const auto boxRequest=host.takeSelectionRequest();
        require(boxRequest && boxRequest->mode==GameEditor::SelectionMode::Box && boxRequest->points.size()==2 &&
            boxRequest->points.back()[0]==x+50,"box select emits the dragged rectangle");
        editor.setSelectionMode(GameEditor::SelectionMode::Circle);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,x,y);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,x,y);
        const auto circleRequest=host.takeSelectionRequest();
        require(circleRequest && circleRequest->mode==GameEditor::SelectionMode::Circle,
            "circle select emits a brush region from a click");
        editor.setSelectionMode(GameEditor::SelectionMode::Lasso);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,x-20,y-20);
        boxMotion.motion.x=x+20;boxMotion.motion.y=y-20;host.event(boxMotion);
        boxMotion.motion.x=x+20;boxMotion.motion.y=y+20;host.event(boxMotion);
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,x-20,y+20);
        const auto lassoRequest=host.takeSelectionRequest();
        require(lassoRequest && lassoRequest->mode==GameEditor::SelectionMode::Lasso && lassoRequest->points.size()>=4,
            "lasso select records its freehand path");
        editor.setSelectionMode(GameEditor::SelectionMode::Tweak);
        const auto tweakBefore=editor.selectedTransform();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,x,y);
        const auto tweakRequest=host.takeSelectionRequest();
        require(tweakRequest && tweakRequest->mode==GameEditor::SelectionMode::Tweak,
            "tweak click requests mesh selection");
        boxMotion.motion.x=x+30;boxMotion.motion.y=y+15;host.event(boxMotion);
        require(editor.editingTransform(),"tweak drag begins a reversible transform");
        key(SDLK_ESCAPE);
        require(!editor.editingTransform() && editor.selectedTransform()->position==tweakBefore->position,
            "Escape cancels a tweak drag");
        key(SDLK_E);require(editor.tool()==TransformTool::Rotate,"E selects rotation");
        key(SDLK_R);require(editor.tool()==TransformTool::Scale,"R selects scale");key(SDLK_W);
        key(SDLK_T);require(editor.tool()==TransformTool::Transform,"T selects combined transform");key(SDLK_W);
        SDL_SetModState(SDL_KMOD_SHIFT);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,x+80,y+80);
        require(!near(host.cursorPosition(),{0,0,0}),"Shift-right-click places the 3D cursor");
        SDL_Event resetCursor{};resetCursor.type=SDL_EVENT_KEY_DOWN;resetCursor.key.key=SDLK_C;resetCursor.key.mod=SDL_KMOD_SHIFT;
        host.event(resetCursor);
        require(near(host.cursorPosition(),{0,0,0}),"Shift-C resets the 3D cursor");
        SDL_SetModState(SDL_KMOD_NONE);
        auto frame=host.gizmoFrame();
        auto start=*gizmoMath::project(gizmoMath::add(frame.origin,gizmoMath::mul(frame.axes[0],frame.size*.6f)),c,view);
        const auto original=*editor.selectedTransform();const auto originalId=editor.selected();
        auto dragTo=[&](float px,float py){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=px;e.motion.y=py;return host.event(e);};
        require(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,start[0],start[1]) && editor.editingTransform(),"gizmo intercepts left click before scene picking");
        for(int i=1;i<=5;++i)dragTo(start[0]+i*10,start[1]);
        require(editor.dirty() && editor.selected()==originalId && editor.selectedTransform()->position[0]>0,"live drag updates real entity without changing selection");
        require(near(host.gizmoFrame().origin,editor.selectedTransform()->position),"move handle follows the live pivot");
        wheel(2,x,y);key(SDLK_DELETE);require(world.size()==1 && near(c.distance(),10),"active transform consumes deletion and camera zoom");
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,start[0]+50,start[1]);
        require(!editor.editingTransform(),"release commits transform capture");
        editor.undo();require(!editor.dirty() && near(editor.selectedTransform()->position,original.position),"one undo reverts every sample of the drag");
        // Start/cancel a gesture without destroying the redo record.
        const auto cancelId=editor.selected();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,start[0],start[1]);dragTo(start[0]+70,start[1]);key(SDLK_ESCAPE);
        require(!editor.dirty() && editor.selected()==cancelId && !editor.editingTransform(),"Escape rolls back gesture and retains entity identity");
        editor.redo();require(editor.dirty(),"cancel preserves redo history");editor.undo();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,start[0],start[1]);dragTo(start[0]+30,start[1]);host.event(lost);
        require(!editor.dirty() && !editor.editingTransform(),"focus loss rolls back incomplete transform");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,start[0],start[1]);
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,start[0],start[1]);
        editor.redo();require(editor.dirty(),"click without dragging preserves redo");editor.undo();
        SDL_SetModState(SDL_KMOD_CTRL);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,start[0],start[1]);dragTo(start[0]+30,start[1]);
        require(near(editor.selectedTransform()->position[0]/.5f,std::round(editor.selectedTransform()->position[0]/.5f)),"host Ctrl modifier enables snapping");
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,-20,-20);require(!editor.editingTransform(),"release outside window ends transform capture");
        SDL_SetModState(SDL_KMOD_NONE);editor.undo();require(!editor.dirty(),"outside release still creates one undo record");
        auto tab=editor.dockSpace().tabBounds("console");const auto cameraBeforeDock=c.position();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,tab.x+15,tab.y+10);dragTo(x,y);
        require(editor.dockSpace().dragging() && !editor.editingTransform(),"SDL tab drag crosses viewport without starting a gizmo");
        wheel(2,x,y);key(SDLK_DELETE);key(SDLK_R);
        require(world.size()==1 && editor.tool()==TransformTool::Move && near(c.position(),cameraBeforeDock),"dock capture blocks scene deletion, tools and camera zoom");
        key(SDLK_ESCAPE);require(!editor.dockSpace().dragging() && !editor.document().hasPointerCapture(),"SDL Escape cancels docking and releases capture");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,tab.x+15,tab.y+10);dragTo(x,y);host.event(lost);
        require(!editor.dockSpace().dragging() && editor.dockSpace().visible("scene"),"SDL focus loss retains original workspace");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,tab.x+15,tab.y+10);dragTo(x,y);
        SDL_Event resized{};resized.type=SDL_EVENT_WINDOW_RESIZED;host.event(resized);
        require(!editor.dockSpace().dragging(),"window resize cancels an in-flight dock gesture");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,tab.x+15,tab.y+10);dragTo(x,y);
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,x,y);editor.layout(1440,900);
        require(editor.viewport().width==0 && editor.dockSpace().visible("console"),"SDL drop tabs Console over Scene and disables scene input area");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,x,y);require(!c.dragging(),"hidden Scene cannot start camera capture");
        editor.dockSpace().show("scene");editor.layout(1440,900);
        require(editor.viewport().width>0 && !editor.dirty(),"Scene tab restores live viewport without authoring changes");
        host.update();const auto editPose=c.position(),editPivot=c.pivot();const auto editSelection=editor.selected();
        editor.setTickHandler([&](float dt){host.gameTick(dt);});
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,679,12);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,679,12);
        require(editor.playState()==GameEditor::PlayState::Playing,"native Play button starts the embedded session");
        host.update();editor.layout(1440,900);const auto game=editor.gameViewport();
        const float gx=game.x+game.width*.5f,gy=game.y+game.height*.5f;
        require(!host.gameFocused(),"Play requires an explicit Game click before accepting runtime input");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,gx,gy);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,gx,gy);
        require(host.gameFocused(),"clicking Game focuses runtime input");
        const auto gameStart=host.gameCamera().position;
        SDL_Event moveKey{};moveKey.type=SDL_EVENT_KEY_DOWN;moveKey.key.key=SDLK_W;moveKey.key.scancode=SDL_SCANCODE_W;
        require(host.event(moveKey),"Game captures movement key");editor.advance(1.0/60);
        require(!near(host.gameCamera().position,gameStart) && near(c.position(),editPose),"runtime movement cannot move the editor camera");
        key(SDLK_DELETE);key(SDLK_R);key(SDLK_F);
        require(world.size()==1 && editor.selected()==editSelection && editor.tool()==TransformTool::Move && !editor.takeFrameRequest(),"runtime keys cannot delete, frame or change editor tools");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,gx,gy);motion(50,20);
        require(host.gameCamera().target!=render::CameraSettings{}.target && near(c.position(),editPose),"Game mouse look has a separate camera");
        editor.pause();const auto frozenGame=host.gameCamera().position,frozenTarget=host.gameCamera().target;
        editor.advance(1);motion(50,20);require(near(host.gameCamera().position,frozenGame) && near(host.gameCamera().target,frozenTarget),"paused Game camera ignores elapsed time and mouse look");
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,gx,gy);key(SDLK_ESCAPE);
        require(!host.gameFocused() && !SDL_GetWindowRelativeMouseMode(window),"Escape releases runtime focus and mouse capture");
        editor.play();editor.advance(1.0/60);require(near(host.gameCamera().position,frozenGame),"released keys do not stick on resume");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,gx,gy);host.event(moveKey);host.event(lost);editor.advance(1.0/60);
        require(!host.gameFocused() && near(host.gameCamera().position,frozenGame),"window focus loss clears held runtime keys");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,gx,gy);host.event(moveKey);editor.dockSpace().close("game");editor.layout(1440,900);host.update();editor.advance(1.0/60);
        require(!host.gameFocused() && near(host.gameCamera().position,frozenGame),"closing Game clears focus and held keys");
        editor.dockSpace().show("scene");editor.layout(1440,900);
        const auto sceneView=editor.viewport();const auto sx=sceneView.x+sceneView.width*.5f,sy=sceneView.y+sceneView.height*.5f;
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,sx,sy);motion(30,10);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,sx,sy);
        require(!near(c.position(),editPose),"Scene navigation remains available during Play");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,706,12);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,706,12);
        require(editor.playState()==GameEditor::PlayState::Paused,"native Pause button freezes simulation");
        const auto beforeStep=editor.ticks();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,733,12);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,733,12);
        require(editor.ticks()==beforeStep+1 && editor.playState()==GameEditor::PlayState::Paused,"native Step button advances exactly one paused tick");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,760,12);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,760,12);
        require(!editor.playing(),"native Stop button ends the session");
        host.update();require(near(c.position(),editPose) && near(c.pivot(),editPivot) && editor.selected()==editSelection,"Stop restores the host's edit camera and selection");
        editor.play();host.update();require(near(host.gameCamera().position,render::CameraSettings{}.position),"new Play resets runtime camera");
        editor.dockSpace().dock("game","scene",ui::DockEdge::Right);editor.layout(1440,900);
        const auto splitGame=editor.gameViewport(),splitScene=editor.viewport();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,splitGame.x+100,splitGame.y+100);
        button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,splitGame.x+100,splitGame.y+100);
        const auto beforeWheel=c.distance();wheel(1,splitScene.x+100,splitScene.y+100);
        require(host.gameFocused() && c.distance()<beforeWheel,"Scene wheel navigation works beside a focused Game");
        editor.dockSpace().show("console");editor.layout(1440,900);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,18,12);button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,18,12);
        require(!host.gameFocused() && editor.document().hasPopup(),"opening editor menu releases runtime focus");
        key(SDLK_ESCAPE);
        require(host.canClose() && !editor.playing(),"closing editor safely restores a clean authored scene");
    }
    {
        gameplay::GameplayWorld world;SceneDocument scene;
        GameEditor editor(world,scene,std::filesystem::path(GENESIS_ROOT)/"assets");editor.setAssetWatch(false);
        editor.browseAssets("starter");editor.layout(1440,900);
        auto pose=initial();pose.position={0,5,-10};
        SdlEditorHost host(editor,window,pose);host.update();
        const auto view=editor.viewport(),panel=editor.dockSpace().panelBounds("project");
        const float x=view.x+view.width*.5f,y=view.y+view.height*.5f,tx=228,ty=panel.y+23+24+22+30;
        auto button=[&](SDL_EventType type,float px,float py){SDL_Event e{};e.type=type;e.button.button=SDL_BUTTON_LEFT;e.button.x=px;e.button.y=py;return host.event(e);};
        auto drag=[&]{button(SDL_EVENT_MOUSE_BUTTON_DOWN,tx,ty);SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=x;e.motion.y=y;host.event(e);};
        auto key=[&](SDL_Keycode code){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;host.event(e);};
        drag();const auto camera=host.camera().position();
        require(editor.document().hasPointerCapture() && world.size()==0,"SDL asset drag captures without placing or selecting a scene object");
        SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=3;wheel.wheel.mouse_x=x;wheel.wheel.mouse_y=y;host.event(wheel);
        key(SDLK_DELETE);key(SDLK_R);
        require(near(host.camera().position(),camera) && editor.tool()==TransformTool::Move && world.size()==0,"asset capture blocks camera zoom and scene shortcuts");
        key(SDLK_ESCAPE);button(SDL_EVENT_MOUSE_BUTTON_UP,x,y);
        require(!editor.document().hasPointerCapture() && world.size()==0 && !editor.dirty(),"SDL Escape cancels placement and ignores the trailing release");
        drag();SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;host.event(lost);button(SDL_EVENT_MOUSE_BUTTON_UP,x,y);
        require(!editor.document().hasPointerCapture() && world.size()==0,"SDL focus loss cancels asset drag");
        drag();SDL_Event resized{};resized.type=SDL_EVENT_WINDOW_RESIZED;host.event(resized);button(SDL_EVENT_MOUSE_BUTTON_UP,x,y);
        require(world.size()==0 && !editor.document().hasPointerCapture(),"SDL resize cancels asset drag");
        drag();button(SDL_EVENT_MOUSE_BUTTON_UP,x,y);
        require(world.size()==1 && near(editor.selectedTransform()->position,Vec3{0,0,0}),"SDL Project drop uses the Scene camera ground ray");
        editor.undo();require(world.size()==0 && !editor.dirty(),"SDL placement is one undoable edit");
        const auto model=(std::filesystem::path(GENESIS_ROOT)/"assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb").string();
        SDL_Event drop{};drop.type=SDL_EVENT_DROP_FILE;drop.drop.data=model.c_str();drop.drop.x=x;drop.drop.y=y;
        require(host.event(drop) && world.size()==1 && near(editor.selectedTransform()->position,Vec3{0,0,0}),"OS model drop into Scene uses the same placement path");editor.undo();
        editor.dockSpace().show("game");editor.layout(1440,900);
        const auto game=editor.gameViewport();drop.drop.x=game.x+game.width*.5f;drop.drop.y=game.y+game.height*.5f;
        host.event(drop);require(world.size()==0 && !editor.dirty(),"OS drop over Game cannot change authored scene");
    }
    SDL_DestroyWindow(window); SDL_Quit();
}
void nativeCursorTests() {
    require(SDL_Init(SDL_INIT_VIDEO),"native SDL initialization");
    auto* window=SDL_CreateWindow("Splitter cursor tests",1440,900,SDL_WINDOW_HIDDEN);
    require(window!=nullptr,"hidden native cursor test window");
    auto* arrow=SDL_GetDefaultCursor();require(arrow!=nullptr,"native driver supplies default cursor");
    {
        gameplay::GameplayWorld world;SceneDocument scene;GameEditor editor(world,scene,{});
        editor.setAssetWatch(false);editor.layout(1440,900);SdlEditorHost host(editor,window,initial());
        auto motion=[&](float x,float y){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=x;e.motion.y=y;host.event(e);};
        auto button=[&](SDL_EventType type,float x,float y){SDL_Event e{};e.type=type;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;host.event(e);};
        const auto hierarchy=editor.dockSpace().panelBounds("hierarchy");
        const auto sx=hierarchy.x+hierarchy.width+2,sy=hierarchy.y+80;
        motion(sx,sy);auto* horizontal=SDL_GetCursor();
        require(horizontal && horizontal!=arrow && editor.document().cursor()==ui::Cursor::ResizeHorizontal,"native vertical divider selects horizontal resize cursor");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,sx,sy);motion(sx+60,sy);
        SDL_Event leave{};leave.type=SDL_EVENT_WINDOW_MOUSE_LEAVE;host.event(leave);
        require(SDL_GetCursor()==horizontal,"native resize cursor persists during capture outside window");
        SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;host.event(lost);
        require(SDL_GetCursor()==arrow && !editor.document().hasPointerCapture(),"native focus loss restores arrow and releases splitter");
        editor.resetLayout();editor.layout(1440,900);
        auto project=editor.dockSpace().panelBounds("project");motion(project.x+300,project.y-2);auto* vertical=SDL_GetCursor();
        require(vertical && vertical!=arrow && vertical!=horizontal && editor.document().cursor()==ui::Cursor::ResizeVertical,"native horizontal divider selects distinct vertical resize cursor");
        button(SDL_EVENT_MOUSE_BUTTON_DOWN,project.x+300,project.y-2);button(SDL_EVENT_MOUSE_BUTTON_UP,-20,-20);
        require(SDL_GetCursor()==arrow,"native splitter release outside restores arrow");
        editor.resetLayout();editor.layout(1440,900);motion(sx,sy);
        editor.dockSpace().close("hierarchy");editor.layout(1440,900);host.update();
        require(SDL_GetCursor()==arrow,"closing hovered panel updates cursor without mouse motion");
        editor.resetLayout();editor.layout(1440,900);project=editor.dockSpace().panelBounds("project");
        motion(project.x+300,project.y-2);host.event(leave);
        require(SDL_GetCursor()==arrow,"native hover cursor resets when mouse leaves");
        motion(project.x+300,project.y-2);
    }
    require(SDL_GetCursor()==arrow,"destroying cursor-owning adapter restores default cursor");
    SDL_DestroyWindow(window);SDL_Quit();
}
}
int main(int argc,char** argv) {
    try { if(argc>1 && std::string(argv[1])=="--native-cursors")nativeCursorTests();else{mathTests(); gizmoTests(); hostTests();} std::cout<<"PASS: "<<checks<<" viewport navigation and transform checks\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; SDL_Quit(); return 1; }
}
