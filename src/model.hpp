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
#include <string>
#include <memory>
namespace neo {
struct ModelVertex {float p[3],n[3],uv[2],base[3],emission[3],factors[2],control;};
static_assert(sizeof(ModelVertex)==68,"Asset vertex layout must match converter");
struct FileStamp {
 bool exists=false; uintmax_t size=0; std::filesystem::file_time_type time{};
 bool operator==(const FileStamp& b) const {return exists==b.exists&&size==b.size&&time==b.time;}
 bool operator!=(const FileStamp& b) const {return !(*this==b);}
};
inline FileStamp fileStamp(const std::filesystem::path& p){
 std::error_code ec;FileStamp s;s.exists=std::filesystem::is_regular_file(p,ec);if(!s.exists)return s;
 s.size=std::filesystem::file_size(p,ec);if(ec)return {};s.time=std::filesystem::last_write_time(p,ec);return ec?FileStamp{}:s;
}
struct Dependency {std::filesystem::path path;FileStamp stamp;};
// Encoded PNG/JPEG/etc or uncompressed embedded RGBA. Kept in memory until GPU upload.
struct ModelImage {std::vector<unsigned char> bytes;unsigned width=0,height=0;};
struct ModelMaterial {
 // base, normal, metallic, roughness, occlusion, emission. Missing images use neutral 1x1 textures.
 std::array<std::shared_ptr<ModelImage>,6> images{};
 std::array<int,6> channels{{-1,-1,0,0,0,-1}};
 std::array<int,2> wrap{{0,0}};
 unsigned uvSet=0;
};
struct ModelPart {uint32_t first=0,count=0,material=0;std::string name;};
struct Model {std::vector<ModelVertex> vertices;std::vector<uint32_t> indices;float width=.310f;
 std::array<std::array<float,4>,controlCount> pivots{},axes{};
 std::vector<ModelMaterial> materials;std::vector<ModelPart> parts;std::vector<Dependency> dependencies;
};
// Round knobs and joysticks face +Z. Thumb rollers are long cylinders lying in the face plane,
// so an elongated knob spins about its longest principal axis instead.
// The axis centre is the least-squares meeting point of the side-wall normals.
inline void fitPivots(Model& m){
 for(int c=firstKnob;c<controlCount;c++){
  double mean[3]={},cov[3][3]={},back=10;size_t n=0;
  for(const auto& v:m.vertices)if(int(v.control)==c){n++;for(int i=0;i<3;i++)mean[i]+=v.p[i];back=std::min(back,double(v.p[2]));}
  if(!n)continue;for(double& x:mean)x/=double(n);
  for(const auto& v:m.vertices)if(int(v.control)==c)for(int i=0;i<3;i++)for(int j=0;j<3;j++)cov[i][j]+=(v.p[i]-mean[i])*(v.p[j]-mean[j]);
  double axis[3]={0,0,1},tr=cov[0][0]+cov[1][1],gap=std::hypot((cov[0][0]-cov[1][1])/2,cov[0][1]);
  if(c<firstStick&&tr/2-gap>0&&(tr/2+gap)>2.25*(tr/2-gap)){
   double x[3]={1,1,1};for(int it=0;it<100;it++){double y[3]={};for(int i=0;i<3;i++)for(int j=0;j<3;j++)y[i]+=cov[i][j]*x[j];
    double l=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);if(l<1e-30)break;for(int i=0;i<3;i++)x[i]=y[i]/l;}
   double s=x[1]<0?-1:1;for(int i=0;i<3;i++)axis[i]=x[i]*s;}
  double u[3]={1,0,0};if(std::abs(axis[0])>.9){u[0]=0;u[1]=1;}
  double dot=u[0]*axis[0]+u[1]*axis[1]+u[2]*axis[2],len=0;for(int i=0;i<3;i++){u[i]-=dot*axis[i];len+=u[i]*u[i];}for(double& x:u)x/=std::sqrt(len);
  double w[3]={axis[1]*u[2]-axis[2]*u[1],axis[2]*u[0]-axis[0]*u[2],axis[0]*u[1]-axis[1]*u[0]};
  auto along=[](const double* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
  double a=0,b=0,d=0,e=0,f=0;
  for(const auto& v:m.vertices){if(int(v.control)!=c)continue;
   double nu=along(u,v.n),nw=along(w,v.n),l=std::hypot(nu,nw);if(l<.9)continue;nu/=l;nw/=l;
   double pu=along(u,v.p),pw=along(w,v.p),m00=1-nu*nu,m01=-nu*nw,m11=1-nw*nw;a+=m00;b+=m01;d+=m11;e+=m00*pu+m01*pw;f+=m01*pu+m11*pw;}
  double det=a*d-b*b;if(std::abs(det)<1e-9)continue;
  double cu=(d*e-b*f)/det,cw=(a*f-b*e)/det,ca=axis[0]*mean[0]+axis[1]*mean[1]+axis[2]*mean[2],p[3];
  for(int i=0;i<3;i++)p[i]=u[i]*cu+w[i]*cw+axis[i]*ca;
  m.pivots[c]={float(p[0]),float(p[1]),float(c<firstStick?p[2]:back),1};m.axes[c]={float(axis[0]),float(axis[1]),float(axis[2]),0};
 }
}
// Native import; names determine controls, never geometry deformation.
Model loadModel(const std::filesystem::path& path);
int controlSlot(const std::string& name);
}
