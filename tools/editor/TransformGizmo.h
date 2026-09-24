#pragma once
#include "editor/ViewportCamera.h"
#include "GameplayWorld.h"
#include "ui/Toolkit.h"
#include <optional>
#include <vector>

namespace genesis::editor {
enum class TransformTool { Select, Move, Rotate, Scale, Transform };
struct SnapSteps { float move=.5f, rotateDegrees=15.0f, scale=.1f; };
namespace gizmoMath {
using Vec2 = std::array<float,2>;
inline Vec3 add(Vec3 a,Vec3 b) { for(int i=0;i<3;++i)a[i]+=b[i];return a; }
inline Vec3 sub(Vec3 a,Vec3 b) { for(int i=0;i<3;++i)a[i]-=b[i];return a; }
inline Vec3 mul(Vec3 a,float s) { for(auto& v:a)v*=s;return a; }
inline float dot(Vec3 a,Vec3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Vec3 cross(Vec3 a,Vec3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
inline Vec3 unit(Vec3 a) { return mul(a,1/std::max(std::sqrt(dot(a,a)),1e-8f)); }
inline constexpr float pi=3.14159265359f, radians=pi/180;
using Basis=std::array<Vec3,3>; // Columns, matching bx::mtxSRT in the renderer.
inline Basis basis(Vec3 degrees) {
    const float sx=std::sin(degrees[0]*radians),cx=std::cos(degrees[0]*radians);
    const float sy=std::sin(degrees[1]*radians),cy=std::cos(degrees[1]*radians);
    const float sz=std::sin(degrees[2]*radians),cz=std::cos(degrees[2]*radians);
    return {{{cy*cz,cz*sx*sy-cx*sz,cx*cz*sy+sx*sz},
             {cy*sz,cx*cz+sx*sy*sz,-cz*sx+cx*sy*sz},{-sy,cy*sx,cx*cy}}};
}
inline Vec3 rotate(Vec3 v,Vec3 axis,float angle) {
    return add(add(mul(v,std::cos(angle)),mul(cross(axis,v),std::sin(angle))),mul(axis,dot(axis,v)*(1-std::cos(angle))));
}
inline Vec3 euler(const Basis& b,Vec3 previous) {
    Vec3 result{};result[1]=std::asin(std::clamp(-b[2][0],-1.0f,1.0f));
    if(std::abs(std::cos(result[1]))>1e-5f) {
        result[0]=std::atan2(b[2][1],b[2][2]);result[2]=std::atan2(b[1][0],b[0][0]);
    } else {
        result[2]=previous[2]*radians;
        result[0]=std::atan2(-b[1][2],b[1][1])+(result[1]>0?result[2]:-result[2]);
    }
    for(int i=0;i<3;++i)result[i]=previous[i]+std::remainder(result[i]/radians-previous[i],360.0f);
    return result;
}
inline std::optional<Vec2> project(Vec3 point,const ViewportCamera& camera,ui::Rect viewport) {
    const auto ray=cameraRay(camera.position(),camera.yaw(),camera.pitch(),camera.fov(),1,.5f,.5f);
    const auto relative=sub(point,camera.position());const float depth=dot(relative,ray.direction);
    if(depth<=.01f || !std::isfinite(depth) || viewport.width<=0 || viewport.height<=0)return {};
    const Vec3 right{std::cos(camera.yaw()),0,-std::sin(camera.yaw())};
    const auto up=cross(ray.direction,right);
    const float pixels=viewport.height/(2*depth*std::tan(camera.fov()*.5f*radians));
    return Vec2{viewport.x+viewport.width*.5f+dot(relative,right)*pixels,
                viewport.y+viewport.height*.5f-dot(relative,up)*pixels};
}
inline Ray rayAt(Vec2 mouse,const ViewportCamera& camera,ui::Rect viewport) {
    return cameraRay(camera.position(),camera.yaw(),camera.pitch(),camera.fov(),viewport.width/viewport.height,
        (mouse[0]-viewport.x)/viewport.width,(mouse[1]-viewport.y)/viewport.height);
}
inline std::optional<Vec3> plane(const Ray& ray,Vec3 origin,Vec3 normal) {
    const float divisor=dot(ray.direction,normal);if(std::abs(divisor)<1e-5f)return {};
    const float t=dot(sub(origin,ray.origin),normal)/divisor;
    if(t<0 || !std::isfinite(t))return {};return add(ray.origin,mul(ray.direction,t));
}
inline float segmentDistance(Vec2 p,Vec2 a,Vec2 b) {
    const float dx=b[0]-a[0],dy=b[1]-a[1],length=dx*dx+dy*dy;
    const float t=length>0?std::clamp(((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length,0.0f,1.0f):0;
    return std::hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy);
}
}

struct GizmoLine { Vec3 a,b; int handle; bool pickOnly=false; };
struct GizmoMarker { enum class Shape { Cone, Cube, Plane }; Vec3 center; Vec3 direction; float size; int handle; Shape shape; };
// Geometry, picking and drags share the same camera and logical-pixel size.
// Move uses world axes; rotation and scale use the object's local axes.
struct GizmoFrame {
    TransformTool tool=TransformTool::Select;
    Vec3 origin{};
    gizmoMath::Basis axes{};
    float size=0;
    std::vector<GizmoLine> lines;
    std::vector<GizmoMarker> markers;
    static GizmoFrame make(TransformTool tool,const gameplay::Transform& transform,const ViewportCamera& camera,
        ui::Rect viewport,std::optional<bool> localOrientation=std::nullopt) {
        using namespace gizmoMath;
        GizmoFrame frame;frame.tool=tool;frame.origin=transform.position;
        if(tool==TransformTool::Select || !project(frame.origin,camera,viewport))return frame;
        const auto forward=rayAt({viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f},camera,viewport).direction;
        frame.size=dot(sub(frame.origin,camera.position()),forward)*2*std::tan(camera.fov()*.5f*radians)*90/viewport.height;
        frame.axes=localOrientation.value_or(tool!=TransformTool::Move)
            ?basis(transform.rotation):Basis{{{1,0,0},{0,1,0},{0,0,1}}};
        const auto line=[&](Vec3 a,Vec3 b,int handle,bool pickOnly=false){frame.lines.push_back({a,b,handle,pickOnly});};
        const auto box=[&](Vec3 center,float size,int handle) {
            std::array<Vec3,8> corners;
            for(int i=0;i<8;++i) {
                corners[i]=center;
                for(int axis=0;axis<3;++axis)corners[i]=add(corners[i],mul(frame.axes[axis],(i&(1<<axis)?1.0f:-1.0f)*size));
            }
            for(int i=0;i<8;++i)for(int axis=0;axis<3;++axis)if(!(i&(1<<axis)))line(corners[i],corners[i|(1<<axis)],handle,true);
            if(handle!=3 && handle!=9)frame.markers.push_back({center,{},size,handle,GizmoMarker::Shape::Cube});
        };
        const auto planeHandle=[&](int axis,float distance,int handle){
            const auto center=add(frame.origin,mul(frame.axes[axis],distance));
            const auto a=mul(frame.axes[(axis+1)%3],frame.size*.052f);
            const auto b=mul(frame.axes[(axis+2)%3],frame.size*.052f);
            const auto corners=std::array<Vec3,4>{add(add(center,a),b),add(sub(center,a),b),
                sub(sub(center,a),b),add(sub(center,b),a)};
            for(int i=0;i<4;++i)line(corners[i],corners[(i+1)%4],handle,true);
            frame.markers.push_back({center,frame.axes[axis],frame.size*.052f,handle,GizmoMarker::Shape::Plane});
        };
        for(int axis=0;axis<3;++axis) {
            const auto direction=frame.axes[axis];
            if(tool==TransformTool::Rotate ||
                (tool==TransformTool::Transform && std::abs(dot(direction,forward))>.25f)) {
                auto point=[&](float angle){return add(frame.origin,mul(add(mul(frame.axes[(axis+1)%3],std::cos(angle)),mul(frame.axes[(axis+2)%3],std::sin(angle))),frame.size));};
                for(int i=0;i<64;++i){
                    const auto a=point(i*2*pi/64),b=point((i+1)*2*pi/64);
                    // Only the camera-facing arc is visible and pickable.
                    if(dot(sub(mul(add(a,b),.5f),frame.origin),sub(camera.position(),frame.origin))>=0)
                        line(a,b,tool==TransformTool::Transform?axis+3:axis);
                }
            }
            if(tool!=TransformTool::Rotate) {
                // An axis pointing at the camera has no usable drag direction.
                const auto a=project(frame.origin,camera,viewport),b=project(add(frame.origin,mul(direction,frame.size)),camera,viewport);
                if(!b || std::hypot((*a)[0]-(*b)[0],(*a)[1]-(*b)[1])<12)continue;
                const float length=tool==TransformTool::Transform?frame.size*.78f:frame.size;
                const auto end=add(frame.origin,mul(direction,length));
                line(add(frame.origin,mul(direction,length*.14f)),end,axis);
                if(tool==TransformTool::Scale){
                    line(add(frame.origin,mul(direction,-length*.14f)),add(frame.origin,mul(direction,-length)),axis);
                    box(end,frame.size*.055f,axis);
                    box(add(frame.origin,mul(direction,-length)),frame.size*.055f,axis);
                }
                else {
                    const auto base=add(frame.origin,mul(direction,length*.8f));
                    for(int side=0;side<4;++side) {
                        const auto wing=add(base,mul(frame.axes[(axis+1+side/2)%3],frame.size*.065f*(side%2?1.0f:-1.0f)));
                        line(wing,end,axis,true);
                    }
                    frame.markers.push_back({end,direction,frame.size*.065f,axis,GizmoMarker::Shape::Cone});
                    if(tool==TransformTool::Move)
                        planeHandle(axis,-length*.48f,axis+3);
                }
                if(tool==TransformTool::Transform){
                    box(add(frame.origin,mul(direction,frame.size*.52f)),frame.size*.045f,axis+6);
                    planeHandle(axis,-frame.size*.48f,axis+10);
                }
            }
        }
        if(tool==TransformTool::Scale)box(frame.origin,frame.size*.065f,3);
        if(tool==TransformTool::Transform)box(frame.origin,frame.size*.055f,9);
        return frame;
    }
    int hit(float x,float y,const ViewportCamera& camera,ui::Rect viewport) const {
        using namespace gizmoMath;
        if(!viewport.contains(x,y))return -1;
        float nearest=7;int result=-1;
        for(const auto& line:lines) {
            const auto a=project(line.a,camera,viewport),b=project(line.b,camera,viewport);
            if(!a || !b)continue;
            const float d=segmentDistance({x,y},*a,*b);
            if(d<nearest-.25f || (std::abs(d-nearest)<=.25f && line.handle>result)){
                nearest=d;result=line.handle;
            }
        }
        return result;
    }
};

class TransformGizmo {
public:
    bool dragging() const { return m_handle>=0; }
    int handle() const { return m_handle; }
    const GizmoFrame& frame() const { return m_frame; }
    bool begin(TransformTool tool,const gameplay::Transform& transform,const ViewportCamera& camera,
        ui::Rect viewport,float x,float y,std::optional<bool> localOrientation=std::nullopt) {
        using namespace gizmoMath;
        m_frame=GizmoFrame::make(tool,transform,camera,viewport,localOrientation);m_handle=m_frame.hit(x,y,camera,viewport);
        if(m_handle<0)return false;
        m_before=transform;m_camera=camera;m_viewport=viewport;m_start={x,y};m_angle=0;
        m_planeMove=(tool==TransformTool::Move && m_handle>=3 && m_handle<=5) ||
            (tool==TransformTool::Transform && m_handle>=10 && m_handle<=12);
        m_action=tool==TransformTool::Transform
            ? (m_planeMove?TransformTool::Move:m_handle>=6?TransformTool::Scale:m_handle>=3?TransformTool::Rotate:TransformTool::Move)
            :tool;
        m_uniform=(tool==TransformTool::Scale && m_handle==3) || (tool==TransformTool::Transform && m_handle==9);
        if(m_uniform)return true;
        m_axisIndex=tool==TransformTool::Transform?(m_planeMove?m_handle-10:m_handle%3):m_handle%3;
        m_axis=m_frame.axes[m_axisIndex];
        if(m_action==TransformTool::Scale){
            float nearest=INFINITY;
            for(const auto& line:m_frame.lines)if(line.handle==m_handle){
                const auto a=project(line.a,camera,viewport),b=project(line.b,camera,viewport);
                if(!a || !b)continue;
                const float distance=segmentDistance(m_start,*a,*b);
                if(distance<nearest){
                    nearest=distance;
                    if(dot(sub(mul(add(line.a,line.b),.5f),m_frame.origin),m_axis)<0)
                        m_axis=mul(m_frame.axes[m_axisIndex],-1.0f);
                    else m_axis=m_frame.axes[m_axisIndex];
                }
            }
        }
        if(m_action==TransformTool::Rotate) {
            const auto ray=rayAt(m_start,camera,viewport);
            const auto point=plane(ray,m_frame.origin,m_axis);
            m_planeRotation=point && std::abs(dot(ray.direction,m_axis))>.12f;
            if(m_planeRotation)m_previous=unit(sub(*point,m_frame.origin));
            else {
                // Edge-on rings use the projected tangent at the picked segment.
                float nearest=INFINITY;
                for(const auto& line:m_frame.lines)if(line.handle==m_handle) {
                    auto a=project(line.a,camera,viewport),b=project(line.b,camera,viewport);if(!a || !b)continue;
                    const float d=segmentDistance(m_start,*a,*b);
                    if(d<nearest){nearest=d;m_tangent={(*b)[0]-(*a)[0],(*b)[1]-(*a)[1]};}
                }
                const float length=m_tangent[0]*m_tangent[0]+m_tangent[1]*m_tangent[1];
                if(length<1e-6f){end();return false;}
                for(auto& value:m_tangent)value*=2*pi/64/length;
            }
        } else if(m_planeMove){
            m_normal=m_axis;
            const auto point=plane(rayAt(m_start,camera,viewport),m_frame.origin,m_normal);
            if(!point){end();return false;}m_previous=*point;
        } else {
            const auto forward=rayAt({viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f},camera,viewport).direction;
            m_normal=unit(sub(forward,mul(m_axis,dot(forward,m_axis))));
            const auto point=plane(rayAt(m_start,camera,viewport),m_frame.origin,m_normal);
            if(!point){end();return false;}m_previous=*point;
        }
        return true;
    }
    gameplay::Transform drag(float x,float y,bool snap,SnapSteps steps={}) {
        using namespace gizmoMath;
        auto result=m_before;if(!dragging() || !std::isfinite(x) || !std::isfinite(y))return result;
        const auto quantize=[&](float value,float step){return snap?std::round(value/step)*step:value;};
        if(m_action==TransformTool::Rotate) {
            if(m_planeRotation) {
                const auto point=plane(rayAt({x,y},m_camera,m_viewport),m_frame.origin,m_axis);
                if(point) {
                    const auto direction=unit(sub(*point,m_frame.origin));
                    m_angle+=std::atan2(dot(m_axis,cross(m_previous,direction)),dot(m_previous,direction));m_previous=direction;
                }
            } else m_angle=(x-m_start[0])*m_tangent[0]+(y-m_start[1])*m_tangent[1];
            const float angle=quantize(m_angle,steps.rotateDegrees*radians);if(std::abs(angle)<1e-6f)return result;
            auto rotation=basis(m_before.rotation);
            for(auto& axis:rotation)axis=rotate(axis,m_axis,angle);
            result.rotation=euler(rotation,m_before.rotation);
        } else if(m_planeMove){
            const auto point=plane(rayAt({x,y},m_camera,m_viewport),m_frame.origin,m_normal);
            if(!point)return result;
            const auto delta=sub(*point,m_previous);
            for(int axis=0;axis<3;++axis)if(axis!=m_axisIndex)
                result.position=add(result.position,mul(m_frame.axes[axis],quantize(dot(delta,m_frame.axes[axis]),steps.move)));
        } else {
            float amount=0;
            if(m_uniform)amount=((x-m_start[0])-(y-m_start[1]))/90;
            else {
                const auto point=plane(rayAt({x,y},m_camera,m_viewport),m_frame.origin,m_normal);
                if(!point)return result;amount=dot(sub(*point,m_previous),m_axis);
                if(m_action==TransformTool::Scale)amount/=m_frame.size;
            }
            if(m_action==TransformTool::Move)result.position=add(m_before.position,mul(m_axis,quantize(amount,steps.move)));
            else {
                const float factor=std::clamp(1+quantize(amount,steps.scale),.001f,10000.0f);
                for(int axis=0;axis<3;++axis)if(m_uniform || m_axisIndex==axis)
                    result.scale[axis]=std::copysign(std::clamp(std::abs(m_before.scale[axis])*factor,.001f,100000.0f),m_before.scale[axis]);
            }
        }
        return result;
    }
    void end() { m_handle=-1; }
private:
    GizmoFrame m_frame;
    gameplay::Transform m_before;
    ViewportCamera m_camera;
    ui::Rect m_viewport;
    gizmoMath::Vec2 m_start{},m_tangent{};
    Vec3 m_axis{},m_normal{},m_previous{};
    int m_handle=-1;
    int m_axisIndex=-1;
    TransformTool m_action=TransformTool::Select;
    bool m_uniform=false;
    bool m_planeMove=false;
    float m_angle=0;
    bool m_planeRotation=false;
};
}
