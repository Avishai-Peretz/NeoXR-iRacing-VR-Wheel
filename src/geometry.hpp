#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
namespace neo {
constexpr float pi=3.14159265358979323846f;
struct Vertex { float x,y,z,r,g,b,button; };
inline float steering(long value,float degrees,bool invert) {
    float t=std::clamp(float(value)/10000.f,-1.f,1.f);
    return t*degrees*.5f*pi/180.f*(invert?-1.f:1.f);
}
inline void box(std::vector<Vertex>& v,float x,float y,float z,float w,float h,float d,
                float r,float g,float b,int button=-1) {
    const float p[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    const int f[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
    for(int a=0;a<6;a++) for(int k : {0,1,2,0,2,3}) {
        int n=f[a][k]; float shade=.65f+.07f*a;
        v.push_back({x+p[n][0]*w/2,y+p[n][1]*h/2,z+p[n][2]*d/2,
                     r*shade,g*shade,b*shade,float(button)});
    }
}
inline void disk(std::vector<Vertex>& v,float x,float y,float z,float radius,float depth,
                 float r,float g,float b,int button=-1) {
    constexpr int n=32;
    auto add=[&](float a,float rr,float zz,float shade) {
        v.push_back({x+rr*std::cos(a),y+rr*std::sin(a),zz,r*shade,g*shade,b*shade,float(button)});
    };
    for(int i=0;i<n;i++) {
        float a=2*pi*i/n,beta=2*pi*(i+1)/n;
        add(0,0,z+depth/2,1);add(a,radius,z+depth/2,1);add(beta,radius,z+depth/2,1);
        add(0,0,z-depth/2,.5);add(beta,radius,z-depth/2,.5);add(a,radius,z-depth/2,.5);
        add(a,radius,z-depth/2,.7);add(beta,radius,z-depth/2,.7);add(beta,radius,z+depth/2,.7);
        add(a,radius,z-depth/2,.7);add(beta,radius,z+depth/2,.7);add(a,radius,z+depth/2,.7);
    }
}
// Approximate 310 mm test yoke. Dimensions are placeholders, not measured CAD.
inline std::vector<Vertex> wheel() {
    std::vector<Vertex> v;
    box(v,0,0,0,.235f,.055f,.014f,.12f,.13f,.15f);
    box(v,0,.065f,0,.225f,.035f,.014f,.12f,.13f,.15f);
    box(v,0,-.065f,0,.225f,.035f,.014f,.12f,.13f,.15f);
    for(float side : {-1.f,1.f}) {
        box(v,side*.135f,0,.006f,.040f,.190f,.036f,.22f,.22f,.24f);
        box(v,side*.094f,0,-.038f,.027f,.125f,.008f,.18f,.18f,.18f);
        disk(v,side*.060f,-.057f,.020f,.013f,.018f,.3f,.3f,.32f);
    }
    disk(v,0,0,.014f,.038f,.016f,.13f,.13f,.15f);
    const float p[8][5]={{-.073f,.067f,1,1,1},{-.091f,.047f,.05f,.2f,1},
        {.073f,.067f,1,.04f,.03f},{.091f,.047f,.05f,.2f,1},
        {-.092f,-.040f,1,.55f,.02f},{-.100f,-.068f,.05f,1,.08f},
        {.092f,-.040f,1,1,.05f},{.100f,-.068f,.02f,.8f,1}};
    for(int i=0;i<8;i++) disk(v,p[i][0],p[i][1],.018f,.008f,.008f,p[i][2],p[i][3],p[i][4],i);
    return v;
}
}
