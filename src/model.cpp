#include "model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/DefaultIOSystem.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <algorithm>
#include <functional>
#include <map>
#include <cctype>

namespace neo {
int controlSlot(const std::string& name){
 for(int i=0;i<8;i++)if(name=="B"+std::to_string(i+1)+"_push_btn")return i;
 static const char* names[]={"left_shifter","right_shifter","left_clutch_lever","right_clutch_lever",
  "B9_B10_rotary_knob","B11_B12_rotary_knob","B37_B38_rotary_knob","B39_B40_rotary_knob","joystick_left","joystick_right"};
 for(int i=0;i<10;i++)if(name==names[i])return 8+i;
 return name=="joistick_right"?17:-1; // original NEO X spelling
}
namespace {
constexpr size_t maxVertices=4000000,maxIndices=15000000,maxFileBytes=256*1024*1024;
struct TrackedIO : Assimp::DefaultIOSystem {
 std::map<std::filesystem::path,FileStamp> reads;
 Assimp::IOStream* Open(const char* name,const char* mode="rb") override {
  auto path=std::filesystem::absolute(std::filesystem::u8path(name)).lexically_normal();
  auto stamp=fileStamp(path);reads.emplace(path,stamp);
  if(stamp.exists&&stamp.size>maxFileBytes)return nullptr;
  return Assimp::DefaultIOSystem::Open(name,mode);
 }
};
std::vector<unsigned char> readImage(const std::filesystem::path& path){
 std::ifstream file(path,std::ios::binary|std::ios::ate);
 if(!file)throw std::runtime_error("Missing texture: "+path.u8string());
 auto size=file.tellg();if(size<=0||size>maxFileBytes)throw std::runtime_error("Texture file size exceeds limit");
 std::vector<unsigned char> bytes(static_cast<size_t>(size));file.seekg(0);
 if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Texture changed during load");
 return bytes;
}
}
Model loadModel(const std::filesystem::path& source){
 auto path=std::filesystem::absolute(source).lexically_normal();
 auto ext=path.extension().u8string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
 if(ext!=".fbx"&&ext!=".obj"&&ext!=".gltf"&&ext!=".glb")throw std::runtime_error("Select an FBX, OBJ, glTF or GLB wheel");
 Assimp::Importer importer;auto* io=new TrackedIO;importer.SetIOHandler(io);
 const aiScene* scene=importer.ReadFile(path.u8string(),aiProcess_Triangulate|aiProcess_GenSmoothNormals|aiProcess_ValidateDataStructure|aiProcess_SortByPType);
 if(!scene||!scene->mRootNode)throw std::runtime_error(std::string("Model import failed: ")+importer.GetErrorString());
 if(scene->mNumMeshes>4096||scene->mNumMaterials>512)throw std::runtime_error("Model has too many meshes/materials");
 Model model;model.materials.resize(scene->mNumMaterials);
 std::map<std::string,std::shared_ptr<ModelImage>> images;
 size_t imageBytes=0;
 auto image=[&](const aiString& ref){
  std::string key=ref.C_Str();auto found=images.find(key);if(found!=images.end())return found->second;
  auto result=std::make_shared<ModelImage>();
  if(auto* embedded=scene->GetEmbeddedTexture(ref.C_Str())){
   if(!embedded->mHeight){if(embedded->mWidth>maxFileBytes)throw std::runtime_error("Embedded image exceeds limit");
    auto* first=reinterpret_cast<const unsigned char*>(embedded->pcData);result->bytes.assign(first,first+embedded->mWidth);
   }else{
    if(embedded->mWidth>8192||embedded->mHeight>8192)throw std::runtime_error("Embedded texture dimensions exceed limit");
    result->width=embedded->mWidth;result->height=embedded->mHeight;result->bytes.resize(size_t(result->width)*result->height*4);
    for(size_t i=0;i<size_t(result->width)*result->height;i++){auto c=embedded->pcData[i];result->bytes[i*4]=c.r;result->bytes[i*4+1]=c.g;result->bytes[i*4+2]=c.b;result->bytes[i*4+3]=c.a;}
   }
  }else{
   auto texture=(path.parent_path()/std::filesystem::u8path(key)).lexically_normal();io->reads.emplace(texture,fileStamp(texture));result->bytes=readImage(texture);
  }
  imageBytes+=result->bytes.size();if(imageBytes>512*1024*1024)throw std::runtime_error("Model textures exceed memory limit");
  images[key]=result;return result;
 };
 const bool gltf=ext==".gltf"||ext==".glb";
 for(unsigned i=0;i<scene->mNumMaterials;i++){
  auto* material=scene->mMaterials[i];auto& out=model.materials[i];
  bool selectedUV=false;
  const aiTextureType primary[]={aiTextureType_BASE_COLOR,aiTextureType_NORMALS,aiTextureType_METALNESS,aiTextureType_DIFFUSE_ROUGHNESS,aiTextureType_AMBIENT_OCCLUSION,aiTextureType_EMISSIVE};
  for(int t=0;t<6;t++){
   aiTextureType type=primary[t];
   if(!material->GetTextureCount(type)){if(t==0)type=aiTextureType_DIFFUSE;else if(t==4)type=aiTextureType_LIGHTMAP;else if(t==5)type=aiTextureType_EMISSION_COLOR;}
   if(!material->GetTextureCount(type))continue;
   aiString ref;unsigned uv=0;aiTextureMapMode wrap[2]={aiTextureMapMode_Wrap,aiTextureMapMode_Wrap};
   if(material->GetTexture(type,0,&ref,nullptr,&uv,nullptr,nullptr,wrap)!=AI_SUCCESS)continue;
   if(uv>=AI_MAX_NUMBER_OF_TEXTURECOORDS)throw std::runtime_error("Material references an invalid UV map");
   if(selectedUV&&out.uvSet!=uv)throw std::runtime_error("A material uses different UV maps for its textures; use one shared UV map per material");
   out.uvSet=uv;selectedUV=true;
   out.images[t]=image(ref);if(t==0){out.wrap[0]=int(wrap[0]);out.wrap[1]=int(wrap[1]);}
   if(gltf&&t==2)out.channels[t]=2;if(gltf&&t==3)out.channels[t]=1;
  }
 }
 float minX=1e30f,maxX=-1e30f;
 std::function<void(const aiNode*,const aiMatrix4x4&,int,int)> visit;
 visit=[&](const aiNode* node,const aiMatrix4x4& parent,int inherited,int depth){
  if(depth>128)throw std::runtime_error("Model hierarchy exceeds limit");
  auto transform=parent*node->mTransformation;aiMatrix3x3 normal(transform);
  if(std::abs(normal.Determinant())<1e-12f)throw std::runtime_error("Model has a singular transform");
  normal.Inverse().Transpose();
  int selected=controlSlot(node->mName.C_Str());if(selected<0)selected=inherited;
  for(unsigned n=0;n<node->mNumMeshes;n++){
   auto* mesh=scene->mMeshes[node->mMeshes[n]];
   if(mesh->mPrimitiveTypes!=aiPrimitiveType_TRIANGLE)continue;
   if(mesh->HasBones())throw std::runtime_error("Skinned wheels are unsupported; export rigid named parts");
   if(model.vertices.size()+mesh->mNumVertices>maxVertices||model.indices.size()+size_t(mesh->mNumFaces)*3>maxIndices)throw std::runtime_error("Model exceeds geometry limits (4 million vertices or 5 million triangles)");
   if(!mesh->HasNormals()||mesh->mMaterialIndex>=model.materials.size())throw std::runtime_error("Invalid mesh material/normals");
   int slot=controlSlot(mesh->mName.C_Str());if(slot<0)slot=selected;
   auto* mat=scene->mMaterials[mesh->mMaterialIndex];aiColor4D base(1,1,1,1);aiColor3D emission(0,0,0);float metal=0,rough=.7f;
   if(mat->Get(AI_MATKEY_BASE_COLOR,base)!=AI_SUCCESS)mat->Get(AI_MATKEY_COLOR_DIFFUSE,base);
   mat->Get(AI_MATKEY_COLOR_EMISSIVE,emission);mat->Get(AI_MATKEY_METALLIC_FACTOR,metal);mat->Get(AI_MATKEY_ROUGHNESS_FACTOR,rough);
   auto offset=uint32_t(model.vertices.size());ModelPart part{uint32_t(model.indices.size()),mesh->mNumFaces*3,mesh->mMaterialIndex,node->mName.C_Str()};
   const auto& modelMaterial=model.materials[mesh->mMaterialIndex];
   bool textured=std::any_of(modelMaterial.images.begin(),modelMaterial.images.end(),[](const auto& image){return bool(image);});
   if(textured&&!mesh->HasTextureCoords(modelMaterial.uvSet))throw std::runtime_error("Mesh '"+std::string(node->mName.C_Str())+"' is missing the UV map selected by its material");
   for(unsigned v=0;v<mesh->mNumVertices;v++){
    auto p=transform*mesh->mVertices[v],norm=normal*mesh->mNormals[v];norm.Normalize();auto uv=mesh->HasTextureCoords(modelMaterial.uvSet)?mesh->mTextureCoords[modelMaterial.uvSet][v]:aiVector3D();
    auto color=mesh->HasVertexColors(0)?mesh->mColors[0][v]:aiColor4D(1,1,1,1);
    ModelVertex vertex{{p.x,p.y,p.z},{norm.x,norm.y,norm.z},{uv.x,1-uv.y},{base.r*color.r,base.g*color.g,base.b*color.b},{emission.r,emission.g,emission.b},{metal,rough},float(slot)};
    // Assimp presents bottom-left UVs (including glTF); WIC textures use top-left rows.
    auto finite=[](const float* values,size_t count){for(size_t k=0;k<count;k++)if(!std::isfinite(values[k]))return false;return true;};
    if(!finite(vertex.p,3)||!finite(vertex.n,3)||!finite(vertex.uv,2)||!finite(vertex.base,3)||!finite(vertex.emission,3)||!finite(vertex.factors,2))throw std::runtime_error("Non-finite model data");
    model.vertices.push_back(vertex);minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);
   }
   for(unsigned f=0;f<mesh->mNumFaces;f++){
    const auto& face=mesh->mFaces[f];if(face.mNumIndices!=3)throw std::runtime_error("Non-triangle mesh");
    for(unsigned k=0;k<3;k++){if(face.mIndices[k]>=mesh->mNumVertices)throw std::runtime_error("Invalid mesh index");model.indices.push_back(offset+face.mIndices[k]);}
   }
   model.parts.push_back(part);
  }
  for(unsigned c=0;c<node->mNumChildren;c++)visit(node->mChildren[c],transform,selected,depth+1);
 };
 visit(scene->mRootNode,aiMatrix4x4(),-1,0);
 model.width=maxX-minX;if(model.indices.empty()||!std::isfinite(model.width)||model.width<1e-6f)throw std::runtime_error("Wheel has no usable triangle geometry or width");
 // Hinges derive from this model only, never from NEO X measurements.
 for(int c=8;c<12;c++){
  float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};bool found=false;
  for(const auto& v:model.vertices)if(int(v.control)==c){found=true;for(int k=0;k<3;k++){lo[k]=std::min(lo[k],v.p[k]);hi[k]=std::max(hi[k],v.p[k]);}}
  if(found)model.pivots[c]={std::abs(lo[0])<std::abs(hi[0])?lo[0]:hi[0],(lo[1]+hi[1])/2,hi[2],c==8||c==10?1.f:-1.f};
 }
 for(auto& axis:model.axes)axis={0,0,1,0};fitPivots(model);
 for(const auto& read:io->reads){if(fileStamp(read.first)!=read.second)throw std::runtime_error("Model dependency changed during import; retrying");model.dependencies.push_back({read.first,read.second});}
 return model;
}
}
