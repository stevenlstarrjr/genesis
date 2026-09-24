#pragma once
#include "editor/TransformGizmo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace genesis::editor::selection {
using Point=gizmoMath::Vec2;
using Polygon=std::vector<Point>;
inline float cross(Point a,Point b,Point c) {
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
}
inline bool contains(const Polygon& polygon,Point point) {
    if(polygon.size()<3)return false;
    bool inside=false;
    for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
        const auto a=polygon[i],b=polygon[j];
        if(((a[1]>point[1])!=(b[1]>point[1])) &&
            point[0]<(b[0]-a[0])*(point[1]-a[1])/(b[1]-a[1])+a[0])inside=!inside;
    }
    return inside;
}
inline bool segmentIntersects(Point a,Point b,Point c,Point d) {
    const float abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
    if(((abC>0 && abD<0)||(abC<0 && abD>0)) &&
       ((cdA>0 && cdB<0)||(cdA<0 && cdB>0)))return true;
    const auto on=[](Point p,Point x,Point y){return p[0]>=std::min(x[0],y[0])-1e-4f && p[0]<=std::max(x[0],y[0])+1e-4f &&
        p[1]>=std::min(x[1],y[1])-1e-4f && p[1]<=std::max(x[1],y[1])+1e-4f;};
    return (std::abs(abC)<1e-4f && on(c,a,b)) || (std::abs(abD)<1e-4f && on(d,a,b)) ||
        (std::abs(cdA)<1e-4f && on(a,c,d)) || (std::abs(cdB)<1e-4f && on(b,c,d));
}
inline bool overlaps(const Polygon& a,const Polygon& b) {
    if(a.size()<3 || b.size()<3)return false;
    for(const auto& p:a)if(contains(b,p))return true;
    for(const auto& p:b)if(contains(a,p))return true;
    for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j)
        if(segmentIntersects(a[i],a[(i+1)%a.size()],b[j],b[(j+1)%b.size()]))return true;
    return false;
}
inline bool overlapsTriangle(const Polygon& region,const std::array<Point,3>& triangle) {
    if(region.size()<3)return false;
    float minX=INFINITY,minY=INFINITY,maxX=-INFINITY,maxY=-INFINITY;
    for(const auto& p:region){minX=std::min(minX,p[0]);minY=std::min(minY,p[1]);maxX=std::max(maxX,p[0]);maxY=std::max(maxY,p[1]);}
    float triMinX=INFINITY,triMinY=INFINITY,triMaxX=-INFINITY,triMaxY=-INFINITY;
    for(const auto& p:triangle){triMinX=std::min(triMinX,p[0]);triMinY=std::min(triMinY,p[1]);triMaxX=std::max(triMaxX,p[0]);triMaxY=std::max(triMaxY,p[1]);}
    if(triMaxX<minX || triMinX>maxX || triMaxY<minY || triMinY>maxY)return false;
    for(const auto& p:triangle)if(contains(region,p))return true;
    for(const auto& p:region) {
        const float a=cross(triangle[0],triangle[1],p);
        const float b=cross(triangle[1],triangle[2],p);
        const float c=cross(triangle[2],triangle[0],p);
        if((a>=0 && b>=0 && c>=0)||(a<=0 && b<=0 && c<=0))return true;
    }
    for(size_t i=0;i<region.size();++i)for(int j=0;j<3;++j)
        if(segmentIntersects(region[i],region[(i+1)%region.size()],triangle[j],triangle[(j+1)%3]))return true;
    return false;
}
inline Polygon rectangle(Point a,Point b) {
    const float left=std::min(a[0],b[0]),right=std::max(a[0],b[0]);
    const float top=std::min(a[1],b[1]),bottom=std::max(a[1],b[1]);
    return {{left,top},{right,top},{right,bottom},{left,bottom}};
}
inline Polygon circle(Point center,float radius,int segments=24) {
    Polygon result;result.reserve(segments);
    for(int i=0;i<segments;++i) {
        const float angle=float(i)*6.283185307f/segments;
        result.push_back({center[0]+radius*std::cos(angle),center[1]+radius*std::sin(angle)});
    }
    return result;
}
}
