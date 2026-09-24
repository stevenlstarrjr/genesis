#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace genesis::probes {
struct Vec {
    float x{}, y{}, z{};
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
inline Vec operator+(Vec a, Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec cross(Vec a, Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vec unit(Vec a) { return a * (1.0f / std::max(std::sqrt(dot(a,a)), 1e-8f)); }
struct Triangle { Vec a,b,c; };

// Static, two-sided geometry visibility only. No direct illumination is baked.
class Geometry {
    struct Node { Vec lo,hi; int first{},count{},left{-1},right{-1}; };
    std::vector<Triangle> triangles;
    std::vector<int> order;
    std::vector<Node> nodes;
    int build(int first, int count) {
        Node n; n.first=first; n.count=count;
        n.lo={1e30f,1e30f,1e30f}; n.hi={-1e30f,-1e30f,-1e30f};
        for(int i=first;i<first+count;++i) for(Vec p : {triangles[order[i]].a,triangles[order[i]].b,triangles[order[i]].c}) {
            n.lo={std::min(n.lo.x,p.x),std::min(n.lo.y,p.y),std::min(n.lo.z,p.z)};
            n.hi={std::max(n.hi.x,p.x),std::max(n.hi.y,p.y),std::max(n.hi.z,p.z)};
        }
        int index=int(nodes.size()); nodes.push_back(n);
        if(count>8) {
            Vec span=n.hi-n.lo; int axis=span.y>span.x?1:0; if(span.z>span[axis]) axis=2;
            int middle=first+count/2;
            std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](int a,int b){
                const auto& ta=triangles[a]; const auto& tb=triangles[b];
                return (ta.a[axis]+ta.b[axis]+ta.c[axis]) < (tb.a[axis]+tb.b[axis]+tb.c[axis]);
            });
            int left=build(first,middle-first), right=build(middle,first+count-middle);
            nodes[index].left=left; nodes[index].right=right;
        }
        return index;
    }
    void intersect(int index, Vec o, Vec d, float& nearest) const {
        const Node& n=nodes[index]; float t0=0.001f,t1=nearest;
        for(int axis=0;axis<3;++axis) {
            if(std::abs(d[axis])<1e-8f) { if(o[axis]<n.lo[axis] || o[axis]>n.hi[axis]) return; }
            else {
                float a=(n.lo[axis]-o[axis])/d[axis], b=(n.hi[axis]-o[axis])/d[axis];
                t0=std::max(t0,std::min(a,b)); t1=std::min(t1,std::max(a,b));
                if(t0>t1) return;
            }
        }
        if(n.left>=0) { intersect(n.left,o,d,nearest); intersect(n.right,o,d,nearest); return; }
        for(int i=n.first;i<n.first+n.count;++i) {
            const auto& t=triangles[order[i]]; Vec e1=t.b-t.a,e2=t.c-t.a,p=cross(d,e2);
            float det=dot(e1,p); if(std::abs(det)<1e-8f) continue;
            float inv=1.0f/det; Vec s=o-t.a; float u=dot(s,p)*inv;
            if(u<0 || u>1) continue;
            Vec q=cross(s,e1); float v=dot(d,q)*inv;
            if(v<0 || u+v>1) continue;
            float distance=dot(e2,q)*inv;
            if(distance>0.001f && distance<nearest) nearest=distance;
        }
    }
public:
    explicit Geometry(std::vector<Triangle> input):triangles(std::move(input)) {
        order.resize(triangles.size()); std::iota(order.begin(),order.end(),0);
        if(!order.empty()) build(0,int(order.size()));
    }
    float distance(Vec origin,Vec direction,float limit=100.0f) const {
        if(!nodes.empty()) intersect(0,origin,direction,limit);
        return limit;
    }
};

inline Vec cubeDirection(int face,float u,float v) {
    float x=u*2-1,y=1-v*2;
    switch(face) {
    case 0:return unit({1,y,-x}); case 1:return unit({-1,y,x});
    case 2:return unit({x,1,-y}); case 3:return unit({x,-1,y});
    case 4:return unit({x,y,1}); default:return unit({-x,y,-1});
    }
}

struct Grid {
    static constexpr int resolution=16;
    std::array<float,4> origin{},spacing{},counts{};
    std::vector<float> visibility; // probe columns, six cubemap faces vertically
    std::vector<float> lighting;   // six cosine-convolved sky/ground visibility lobes
    int size() const { return int(counts[0]*counts[1]*counts[2]); }
    Grid(const Geometry& scene,Vec lo,Vec hi) {
        for(int axis=0;axis<3;++axis) {
            int count=std::clamp(int(std::ceil((hi[axis]-lo[axis])/3.0f)),2,axis==1?5:9);
            counts[axis]=float(count);
            spacing[axis]=(hi[axis]-lo[axis])/float(count);
            origin[axis]=lo[axis]+spacing[axis]*0.5f;
        }
        counts[3]=float(size()); origin[3]=1.0f;
        visibility.resize(size()*resolution*resolution*6);
        lighting.resize(size()*6*4);
        for(int z=0;z<int(counts[2]);++z) for(int y=0;y<int(counts[1]);++y) for(int x=0;x<int(counts[0]);++x) {
            int index=x+int(counts[0])*(y+int(counts[1])*z);
            Vec position{origin[0]+x*spacing[0],origin[1]+y*spacing[1],origin[2]+z*spacing[2]};
            for(int face=0;face<6;++face) {
                for(int py=0;py<resolution;++py) for(int px=0;px<resolution;++px) {
                    Vec d=cubeDirection(face,(px+0.5f)/resolution,(py+0.5f)/resolution);
                    visibility[(face*resolution+py)*size()*resolution+index*resolution+px]=scene.distance(position,d);
                }
                Vec normal=cubeDirection(face,0.5f,0.5f);
                Vec tangent=unit(cross(std::abs(normal.y)<0.99f?Vec{0,1,0}:Vec{1,0,0},normal));
                Vec bitangent=cross(normal,tangent);
                float sky=0,ground=0;
                for(int ray=0;ray<128;++ray) {
                    float f=ray+0.5f, r=std::sqrt(f/128.0f), phi=6.28318530718f*std::fmod(f*0.61803398875f,1.0f);
                    Vec d=tangent*(r*std::cos(phi))+bitangent*(r*std::sin(phi))+normal*std::sqrt(1-f/128.0f);
                    if(scene.distance(position,d)>=99.9f) { if(d.y>=0) sky+=1.0f/128; else ground+=1.0f/128; }
                }
                size_t offset=(face*size()+index)*4;
                lighting[offset]=sky; lighting[offset+1]=ground;
                lighting[offset+2]=std::max(0.0f,1.0f-sky-ground); lighting[offset+3]=1;
            }
        }
    }
};
} // namespace genesis::probes
