#pragma once
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "animation.hpp"
namespace neo {
struct ModelVertex {float p[3],n[3],uv[2],base[3],emission[3],factors[2],control;};
static_assert(sizeof(ModelVertex)==68,"Asset vertex layout must match converter");
struct Model {std::vector<ModelVertex> vertices;std::vector<uint32_t> indices;float width=.310f;std::array<std::array<float,4>,controlCount> pivots{};};
// Knob/joystick axis centre: least-squares intersection of side-wall normals in XY.
// Knobs spin about their mean depth; joysticks tilt about their rear (base) depth.
inline void fitPivots(Model& m){
 for(int c=firstKnob;c<controlCount;c++){
  double a=0,b=0,d=0,e=0,f=0,z=0,back=10;size_t n=0;
  for(const auto& v:m.vertices){if(int(v.control)!=c)continue;n++;z+=v.p[2];back=std::min(back,double(v.p[2]));
   double nx=v.n[0],ny=v.n[1],l=std::hypot(nx,ny);if(l<.95)continue;nx/=l;ny/=l;
   double m00=1-nx*nx,m01=-nx*ny,m11=1-ny*ny;a+=m00;b+=m01;d+=m11;e+=m00*v.p[0]+m01*v.p[1];f+=m01*v.p[0]+m11*v.p[1];}
  double det=a*d-b*b;if(!n||std::abs(det)<1e-9)continue;
  m.pivots[c]={float((d*e-b*f)/det),float((a*f-b*e)/det),float(c<firstStick?z/double(n):back),1};
 }
}
inline Model loadModel(const std::filesystem::path& path){
 std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("Cannot open wheel.neo; reinstall assets or set Model=placeholder");
 const auto size=in.tellg();in.seekg(0);char magic[8]{};uint32_t version=0,vc=0,ic=0;Model m;
 auto read=[&](void* data,size_t bytes){if(!in.read(static_cast<char*>(data),std::streamsize(bytes)))throw std::runtime_error("Truncated wheel.neo");};
 read(magic,8);read(&version,4);read(&vc,4);read(&ic,4);read(&m.width,4);
 if(std::memcmp(magic,"NEOWHL2\0",8)||version!=2||!vc||vc>2000000||!ic||ic>6000000||ic%3||!std::isfinite(m.width)||m.width<.1f||m.width>1.f)
 throw std::runtime_error("Invalid wheel.neo header");
 const uint64_t expected=88ull+uint64_t(vc)*sizeof(ModelVertex)+uint64_t(ic)*4;
 if(size<0||uint64_t(size)!=expected)throw std::runtime_error("Invalid wheel.neo length");
 read(m.pivots.data()+8,64);for(auto& p:m.pivots)for(float f:p)if(!std::isfinite(f)||std::abs(f)>10)throw std::runtime_error("Invalid paddle pivot");
 m.vertices.resize(vc);m.indices.resize(ic);read(m.vertices.data(),m.vertices.size()*sizeof(ModelVertex));read(m.indices.data(),m.indices.size()*4);
 for(const auto& v:m.vertices){
  // Explicit field checks avoid pointer arithmetic across struct subobjects.
  auto finite=[](const float* f,size_t n){for(size_t i=0;i<n;i++)if(!std::isfinite(f[i]))return false;return true;};
  if(!finite(v.p,3)||!finite(v.n,3)||!finite(v.uv,2)||!finite(v.base,3)||!finite(v.emission,3)||!finite(v.factors,2)||!std::isfinite(v.control)||
   v.control< -1||v.control>=controlCount||std::floor(v.control)!=v.control)throw std::runtime_error("Invalid wheel vertex");
  for(float p:v.p)if(std::abs(p)>10)throw std::runtime_error("Wheel vertex out of bounds");
 }
 for(auto i:m.indices)if(i>=vc)throw std::runtime_error("Wheel index out of bounds");
 fitPivots(m);return m;
}
}
