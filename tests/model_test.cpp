#ifdef NDEBUG
#undef NDEBUG
#endif
#include "model.hpp"
#include "model_watch.hpp"
#include <thread>
#include <chrono>
#include <cassert>
#include <sstream>
#include <iostream>
#include <limits>
int main(int argc,char** argv){
 assert(argc>=2);auto m=neo::loadModel(argv[1]);auto triangles=m.indices.size()/3;assert(triangles>150000&&triangles<1000000);assert(m.width>.31f&&m.width<.32f);
 bool controls[neo::controlCount]{};for(const auto& v:m.vertices)if(v.control>=0)controls[int(v.control)]=true;
 for(bool present:controls)assert(present);
 // Fitted knob/joystick centres lie inside each part, within 3 mm of the parts' measured hubs.
 const float hubs[6][2]={{.0864f,-.0485f},{-.0880f,-.0452f},{-.0475f,-.0546f},{.0461f,-.0555f},{-.0491f,.0501f},{.0501f,.0488f}};
 for(int c=neo::firstKnob;c<neo::controlCount;c++){const auto& p=m.pivots[c];assert(p[3]==1);
  assert(std::hypot(p[0]-hubs[c-neo::firstKnob][0],p[1]-hubs[c-neo::firstKnob][1])<.003f);
  // B9/B10 and B11/B12 are thumb rollers lying in the face plane; the rest face forward.
  float z=std::abs(m.axes[c][2]);assert(c<14?z<.3f:z>.99f);}

 // Fixtures prove that unrelated wheels keep their source geometry and names select only animation.
 auto temp=std::filesystem::temp_directory_path()/("neoxr-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directories(temp);
 auto write=[&](const char* name,const std::string& data){std::ofstream out(temp/name,std::ios::binary);out<<data;};
 auto obj=[](float width){std::ostringstream out;out<<"mtllib wheel.mtl\no B1_push_btn\nusemtl wheel\nv 0 0 0\nv "<<width<<" 0 0\nv 0 1 0\nf 1 2 3\n";return out.str();};
 write("wheel.mtl","newmtl wheel\nKd 0.2 0.4 0.6\n");write("wheel.obj",obj(2));
 auto custom=neo::loadModel(temp/"wheel.obj");assert(custom.width==2);assert(custom.vertices.size()==3);
 for(auto& v:custom.vertices)assert(v.control==0);
 assert(custom.dependencies.size()>=2);assert(custom.vertices[1].p[0]==2);assert(custom.vertices[2].p[1]==1);
 // A textured material can select UV1 without changing UV0 or the source geometry.
 {
  std::shared_ptr<neo::ModelImage> image;
  for(const auto& material:m.materials)for(const auto& candidate:material.images)if(candidate&&!candidate->height)image=candidate;
  assert(image);std::ofstream texture(temp/"uv.png",std::ios::binary);texture.write(reinterpret_cast<const char*>(image->bytes.data()),image->bytes.size());texture.close();
  float data[]={0,0,0,1,0,0,0,1,0, 0,0,0,0,0,0, .25f,.75f,.5f,.5f,.75f,.25f};
  std::ofstream buffer(temp/"uv.bin",std::ios::binary);buffer.write(reinterpret_cast<const char*>(data),sizeof(data));buffer.close();
  std::string fixture=R"({"asset":{"version":"2.0"},"buffers":[{"uri":"uv.bin","byteLength":84}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],"images":[{"uri":"uv.png"}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_1":2},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
  write("uv.gltf",fixture);auto selected=neo::loadModel(temp/"uv.gltf");assert(selected.materials[0].uvSet==1);
  assert(std::abs(selected.vertices[0].uv[0]-.25f)<1e-6f);assert(std::abs(selected.vertices[0].uv[1]-.75f)<1e-6f);
  fixture.replace(fixture.find("\"TEXCOORD_1\":2"),14,"\"TEXCOORD_2\":2");write("missing-uv.gltf",fixture);
  bool rejected=false;try{neo::loadModel(temp/"missing-uv.gltf");}catch(const std::exception&){rejected=true;}assert(rejected);
 }
 bool caught=false;write("broken.gltf","{broken");try{neo::loadModel(temp/"broken.gltf");}catch(const std::exception&){caught=true;}assert(caught);
 if(argc>2){auto fbx=neo::loadModel(argv[2]);assert(!fbx.indices.empty());std::cout<<"FBX fixture imported: "<<fbx.indices.size()/3<<" triangles\n";}
 {
  neo::ModelWatch watch;watch.select(temp/"wheel.obj");
  auto wait=[&](){for(int i=0;i<100;i++){std::string error;auto result=watch.take(error);if(result)return result;std::this_thread::sleep_for(std::chrono::milliseconds(100));}throw std::runtime_error("Reload timed out");};
  auto first=wait();assert(first->width==2);
  write("wheel.obj",obj(3));auto second=wait();assert(second->width==3);
  write("wheel.mtl","newmtl wheel\nKd 0.8 0.2 0.1\n");auto third=wait();assert(std::abs(third->vertices[0].base[0]-.8f)<.001f);
  write("wheel.obj","partial save");std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  std::string error;assert(!watch.take(error));assert(third->width==3); // caller retains previous generation
  write("wheel.obj",obj(4));auto recovered=wait();assert(recovered->width==4);
 }
 std::filesystem::remove_all(temp);
 std::cout<<"Model geometry, controls, pivots, native formats, material dependencies, failed-save recovery and background reload passed\n";
}
