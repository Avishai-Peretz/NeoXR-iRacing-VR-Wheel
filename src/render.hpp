#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include <objbase.h>
#include <string>
#include <wrl/client.h>
#include <stdexcept>
#include <filesystem>
#include <cstring>
#include <map>
#include <tuple>
#include "geometry.hpp"
#include "model.hpp"
using Microsoft::WRL::ComPtr;
using namespace DirectX;
inline void hr(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D/WIC operation failed, HRESULT="+std::to_string(static_cast<unsigned long>(h)));}
struct Renderer {
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> immediate,context;
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
 ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vertices,indices,constants;
 ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depthState;
 struct Material {std::array<ComPtr<ID3D11ShaderResourceView>,6> images;ComPtr<ID3D11SamplerState> sampler;};
 std::vector<Material> materials;std::vector<neo::ModelPart> parts;
 ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;
 struct alignas(16) Uniform {XMFLOAT4X4 mvp;XMFLOAT4 controls[neo::controlCount],pivots[neo::controlCount],axes[neo::controlCount],camera,motion,light;};
 static_assert(sizeof(Uniform)==976,"HLSL constant buffer layout must match");
 std::array<std::array<float,4>,neo::controlCount> pivots{},axes{};
 UINT count=0,stride=0;bool detailed=false;float modelWidth=.310f;
 float buttonTravel=.0012f,paddleAngle=.12f,clutchAngle=.21f,stickAngle=.2f,flashStrength=.75f;int editMode=0;
 std::array<float,neo::controlCount> highlight{};  // 0..1 per control, tints it to point it out
 std::array<float,4> background{0,0,0,0};  // linear RGBA; transparent for the VR composition layer
 float turn=0;  // steering angle the model is drawn at, so the studio lighting stays still as it turns
 float opacity=1;  // below 1 the game shows through, e.g. to line the wheel up with the in-game one
 void execute(){ComPtr<ID3D11CommandList> commands;HRESULT result=context->FinishCommandList(FALSE,&commands);
  if(FAILED(result))throw std::runtime_error("FinishCommandList failed, HRESULT="+std::to_string(static_cast<unsigned long>(result)));
  immediate->ExecuteCommandList(commands.Get(),TRUE);}
 ComPtr<ID3D11ShaderResourceView> texture(const neo::ModelImage* image,bool srgb,int channel,int kind){
  UINT w=1,h=1;std::vector<unsigned char> pixels{255,255,255,255};
  if(kind==1)pixels={128,128,255,255};
  if(image){
   if(image->height){w=image->width;h=image->height;pixels=image->bytes;}
   else{
    struct ComScope {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(result))CoUninitialize();}} com;
    if(FAILED(com.result)&&com.result!=RPC_E_CHANGED_MODE)hr(com.result);
    ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IWICStream> stream;hr(factory->CreateStream(&stream));
    hr(stream->InitializeFromMemory(const_cast<BYTE*>(image->bytes.data()),DWORD(image->bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;hr(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;hr(decoder->GetFrame(0,&frame));hr(frame->GetSize(&w,&h));
    if(!w||!h||w>8192||h>8192)throw std::runtime_error("Wheel texture size must be 1..8192 pixels");
    ComPtr<IWICFormatConverter> convert;hr(factory->CreateFormatConverter(&convert));
    hr(convert->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    pixels.resize(size_t(w)*h*4);hr(convert->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()));
   }
   if(channel>=0)for(size_t i=0;i<pixels.size();i+=4){auto value=pixels[i+channel];pixels[i]=pixels[i+1]=pixels[i+2]=value;pixels[i+3]=255;}
  }
  D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=0;t.ArraySize=1;t.Format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
  t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;t.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
  ComPtr<ID3D11Texture2D> tex;hr(dev->CreateTexture2D(&t,nullptr,&tex));ComPtr<ID3D11ShaderResourceView> view;hr(dev->CreateShaderResourceView(tex.Get(),nullptr,&view));
  context->UpdateSubresource(tex.Get(),0,nullptr,pixels.data(),w*4,0);context->GenerateMips(view.Get());execute();return view;
 }
 void init(ID3D11Device* d,UINT width,UINT height,const neo::Model* model=nullptr){
  dev=d;dev->GetImmediateContext(&immediate);hr(dev->CreateDeferredContext(0,&context));detailed=model!=nullptr;
  const char* placeholder=R"(
   cbuffer C:register(b0){row_major float4x4 mvp;float4 controls[18];float4 pivots[18];float4 axes[18];float4 camera;float4 motion;};
   struct V{float3 p:POSITION;float3 c:COLOR;float b:TEXCOORD;};
   struct P{float4 p:SV_POSITION;float3 c:COLOR;};
   P vertexMain(V v){P o;o.p=mul(float4(v.p,1),mvp);o.c=v.c;if(v.b>=0){float4 s=controls[(int)v.b];o.c=lerp(o.c,1,saturate(s.x+s.y*motion.w));
    o.c=lerp(o.c,float3(.1,.75,1),axes[(int)v.b].w*.7);}return o;}
   float4 pixelMain(P p):SV_TARGET{return float4(p.c,1);}
  )";
  const char* custom=R"(
   // controls[c]: x=press spring, y=flash, z=paddle travel angle, knob angle or stick X tilt, w=stick Y tilt.
   // motion: x=button travel, y=edit mode, z=brightness, w=flash strength. camera.w=stick angle.
   cbuffer C:register(b0){row_major float4x4 mvp;float4 controls[18];float4 pivots[18];float4 axes[18];float4 camera;float4 motion;float4 light;};
   Texture2D baseTex:register(t0);Texture2D normalTex:register(t1);Texture2D metalTex:register(t2);Texture2D roughTex:register(t3);Texture2D aoTex:register(t4);Texture2D emitTex:register(t5);SamplerState sampleTex:register(s0);
   struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float3 base:COLOR0;float3 emission:COLOR1;float2 factors:TEXCOORD1;float control:TEXCOORD2;};
   struct P{float4 p:SV_POSITION;float3 local:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float3 base:COLOR0;float3 emission:COLOR1;float2 factors:TEXCOORD3;float glow:TEXCOORD4;float highlight:TEXCOORD5;};
   float3 rotX(float3 d,float a){float s=sin(a),c=cos(a);return float3(d.x,c*d.y-s*d.z,s*d.y+c*d.z);}
   float3 rotY(float3 d,float a){float s=sin(a),c=cos(a);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
   float3 rotAxis(float3 d,float3 k,float a){float s=sin(a),c=cos(a);return d*c+cross(k,d)*s+k*dot(k,d)*(1-c);}
   P vertexMain(V v){P o;o.glow=0;o.highlight=0;
    if(v.control>=0){int c=(int)v.control;float4 s=controls[c];float3 pivot=pivots[c].xyz;float3 d=v.p-pivot;o.highlight=axes[c].w;
     if(c<8)v.p.z-=s.x*motion.x;
     else if(c<12){float a=s.x*s.z*pivots[c].w;v.p=pivot+rotY(d,a);v.n=rotY(v.n,a);}
     else if(c<16){float3 k=axes[c].xyz;v.p=pivot+rotAxis(d,k,s.z);v.n=rotAxis(v.n,k,s.z);}
     else{float ax=-s.w*camera.w,ay=s.z*camera.w;v.p=pivot+rotY(rotX(d,ax),ay)-float3(0,0,s.x*motion.x);v.n=rotY(rotX(v.n,ax),ay);}
     // Only the push buttons are backlit, so only they light up.
     if(c<8)o.glow=saturate(s.y*motion.w+saturate(s.x)*.2);}
    o.p=mul(float4(v.p,1),mvp);o.local=v.p;o.n=v.n;o.uv=v.uv;o.base=v.base;o.emission=v.emission;o.factors=v.factors;return o;}
   float3 mappedNormal(P p){float3 n=normalize(p.n);float3 dx=ddx(p.local),dy=ddy(p.local);float2 du=ddx(p.uv),dv=ddy(p.uv);
    float det=du.x*dv.y-du.y*dv.x;if(abs(det)<1e-12)return n;
    float3 t=(dx*dv.y-dy*du.y)/det;float3 b=(-dx*dv.x+dy*du.x)/det;
    t=t-n*dot(n,t);b=b-n*dot(n,b);float len=max(dot(t,t),dot(b,b));if(len<1e-16)return n;
    float3 tex=normalTex.Sample(sampleTex,p.uv).xyz*2-1;float scale=rsqrt(len);return normalize(t*scale*tex.x+b*scale*tex.y+n*tex.z);}
   // Fixed studio lighting; game cockpit lights are not available to an OpenXR composition layer.
   // A dim studio with softboxes, as spherical Gaussians blurred by the surface roughness. Directions are
   // in the unsteered wheel frame (+Y up, +Z toward the driver); light.xy turns them with the wheel.
   // light.z is the overall opacity; the output is premultiplied for the compositor.
   float3 toModel(float3 d){return float3(light.x*d.x-light.y*d.y,light.y*d.x+light.x*d.y,d.z);}
   float lobe(float3 d,float3 axis,float sharpness,float rough){float a=rough*rough,m=2/max(a*a,.002);
    float s=sharpness*m/(sharpness+m);return m/(sharpness+m)*exp(s*(dot(d,toModel(axis))-1));}
   float3 studio(float3 d,float rough){
    float up=dot(d,toModel(float3(0,1,0)));
    float3 room=lerp(float3(.03,.03,.033),float3(.13,.13,.14),smoothstep(-.6,.1,up))+float3(.12,.12,.13)*smoothstep(0,1,up);
    // A broad dim panel behind the driver: driver-facing carbon reflects it, which shows its weave.
    return room+float3(.55,.55,.58)*lobe(d,normalize(float3(0,.3,1)),3,rough)
     +float3(1,.97,.92)*5*lobe(d,normalize(float3(-.35,.65,1)),40,rough)
     +float3(.9,.95,1)*2*lobe(d,normalize(float3(0,1,.3)),14,rough)
     +1.2*lobe(d,normalize(float3(1,.15,.35)),25,rough)+1.2*lobe(d,normalize(float3(-1,.15,.35)),25,rough);}
   float4 pixelMain(P p):SV_TARGET{
    float3 n=mappedNormal(p);float3 view=normalize(camera.xyz-p.local);if(dot(n,view)<0)n=-n;
    float3 color=baseTex.Sample(sampleTex,p.uv).rgb*p.base;
    float occlusion=aoTex.Sample(sampleTex,p.uv).r,metal=saturate(metalTex.Sample(sampleTex,p.uv).r*p.factors.x),rough=clamp(roughTex.Sample(sampleTex,p.uv).r*p.factors.y,.04,1);
    float nv=saturate(dot(n,view));float3 f0=lerp(float3(.04,.04,.04),color,metal);
    float3 fresnel=f0+(max(float3(1,1,1)-rough,f0)-f0)*pow(1-nv,5);
    float3 diffuse=color*(1-metal)*(.12+.85*studio(n,1));
    float3 specular=fresnel*studio(reflect(-view,n),rough);
    float3 result=(diffuse*(1-fresnel*.5)+specular)*occlusion+p.emission*emitTex.Sample(sampleTex,p.uv).rgb;result=lerp(result,float3(1,1,1),p.glow);result*=motion.z;
    float rim=pow(1-saturate(abs(dot(normalize(p.n),view))),2);
    result=lerp(saturate(result),float3(.1,.75,1),saturate(p.highlight)*(.85+rim*.15));
    // Edit mode outline: blue = move, orange = adjust, green = confirm save.
    if(motion.y>0){float3 tint=motion.y<1.5?float3(.1,.45,1):motion.y<2.5?float3(1,.45,.05):float3(.15,1,.3);
     result=lerp(result,tint,saturate(.15+rim*.85));}
    float3 output=result*light.z;if(light.w>0)output=pow(saturate(output),1/2.2);
    return float4(output,light.z);}
  )";
  const char* shader=detailed?custom:placeholder;ComPtr<ID3DBlob> v,p,e;
  auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& result){HRESULT h=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,entry,profile,0,0,&result,&e);
   if(FAILED(h))throw std::runtime_error(e?std::string(static_cast<const char*>(e->GetBufferPointer()),e->GetBufferSize()):"Wheel shader compile failed");};
  compile("vertexMain","vs_5_0",v);compile("pixelMain","ps_5_0",p);
  hr(dev->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));hr(dev->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
  D3D11_BUFFER_DESC b{};D3D11_SUBRESOURCE_DATA data{};
  if(detailed){
   const auto& mesh=*model;parts=mesh.parts;modelWidth=mesh.width;pivots=mesh.pivots;axes=mesh.axes;count=UINT(mesh.indices.size());stride=sizeof(neo::ModelVertex);
   D3D11_INPUT_ELEMENT_DESC elems[]={
    {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",1,DXGI_FORMAT_R32G32B32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"TEXCOORD",2,DXGI_FORMAT_R32_FLOAT,0,64,D3D11_INPUT_PER_VERTEX_DATA,0}};
   hr(dev->CreateInputLayout(elems,7,v->GetBufferPointer(),v->GetBufferSize(),&layout));
   b.ByteWidth=UINT(mesh.vertices.size()*sizeof(neo::ModelVertex));b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;data.pSysMem=mesh.vertices.data();hr(dev->CreateBuffer(&b,&data,&vertices));
   b.ByteWidth=UINT(mesh.indices.size()*4);b.BindFlags=D3D11_BIND_INDEX_BUFFER;data.pSysMem=mesh.indices.data();hr(dev->CreateBuffer(&b,&data,&indices));
   // Share textures across materials (the supplied glTF uses three atlases).
   std::map<std::tuple<const neo::ModelImage*,bool,int,int>,ComPtr<ID3D11ShaderResourceView>> cache;
   for(const auto& source:mesh.materials){Material material;
    for(int i=0;i<6;i++){auto key=std::make_tuple(source.images[i].get(),i==0||i==5,source.channels[i],i==1?1:0);
     auto found=cache.find(key);if(found==cache.end())found=cache.emplace(key,texture(source.images[i].get(),i==0||i==5,source.channels[i],i)).first;
     material.images[i]=found->second;}
    auto wrap=[](int mode){return mode==1?D3D11_TEXTURE_ADDRESS_CLAMP:mode==2?D3D11_TEXTURE_ADDRESS_MIRROR:D3D11_TEXTURE_ADDRESS_WRAP;};
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_ANISOTROPIC;sd.AddressU=wrap(source.wrap[0]);sd.AddressV=wrap(source.wrap[1]);sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxAnisotropy=8;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxLOD=D3D11_FLOAT32_MAX;
    hr(dev->CreateSamplerState(&sd,&material.sampler));materials.push_back(std::move(material));
   }
  }else{
   D3D11_INPUT_ELEMENT_DESC elems[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
   hr(dev->CreateInputLayout(elems,3,v->GetBufferPointer(),v->GetBufferSize(),&layout));auto mesh=neo::wheel();count=UINT(mesh.size());stride=sizeof(neo::Vertex);
   b.ByteWidth=UINT(mesh.size()*sizeof(neo::Vertex));b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;data.pSysMem=mesh.data();hr(dev->CreateBuffer(&b,&data,&vertices));
  }
  b={};b.ByteWidth=sizeof(Uniform);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr(dev->CreateBuffer(&b,nullptr,&constants));
  D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;hr(dev->CreateRasterizerState(&r,&raster));
  D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS;hr(dev->CreateDepthStencilState(&ds,&depthState));
  resize(width,height);
 }
 // The depth buffer must match the size of the targets passed to draw.
 void resize(UINT width,UINT height){
  dsv.Reset();depth.Reset();
  D3D11_TEXTURE2D_DESC t{};t.Width=width;t.Height=height;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_D32_FLOAT;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  hr(dev->CreateTexture2D(&t,nullptr,&depth));hr(dev->CreateDepthStencilView(depth.Get(),nullptr,&dsv));
 }
 void draw(ID3D11Texture2D* target,UINT w,UINT h,FXMMATRIX mvp,const std::array<neo::ControlPose,neo::controlCount>& controls,XMFLOAT3 cameraLocal,float brightness){
  // OpenXR D3D11 swapchain resources can be TYPELESS. A null RTV descriptor
  // inherits that typeless format and fails with E_INVALIDARG. Select the
  // concrete sRGB format requested in xrCreateSwapchain.
  D3D11_TEXTURE2D_DESC targetDesc{};target->GetDesc(&targetDesc);
  D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
  viewDesc.Format=targetDesc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:targetDesc.Format;
  if(targetDesc.SampleDesc.Count>1){
   if(targetDesc.ArraySize>1){viewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;viewDesc.Texture2DMSArray.FirstArraySlice=0;viewDesc.Texture2DMSArray.ArraySize=1;}
   else viewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMS;
  }else if(targetDesc.ArraySize>1){viewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;viewDesc.Texture2DArray.MipSlice=0;viewDesc.Texture2DArray.FirstArraySlice=0;viewDesc.Texture2DArray.ArraySize=1;}
  else{viewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;viewDesc.Texture2D.MipSlice=0;}
  ComPtr<ID3D11RenderTargetView> rtv;HRESULT result=dev->CreateRenderTargetView(target,&viewDesc,&rtv);
  if(FAILED(result))throw std::runtime_error("CreateRenderTargetView failed, HRESULT="+std::to_string(static_cast<unsigned long>(result))+
   ", resourceFormat="+std::to_string(targetDesc.Format)+", bindFlags="+std::to_string(targetDesc.BindFlags)+
   ", arraySize="+std::to_string(targetDesc.ArraySize)+", samples="+std::to_string(targetDesc.SampleDesc.Count));
  context->ClearState();auto* rt=rtv.Get();context->OMSetRenderTargets(1,&rt,dsv.Get());
  context->ClearRenderTargetView(rt,background.data());context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
  context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);
  context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);UINT offset=0;auto* vb=vertices.Get();context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
  context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
  Uniform u{};XMStoreFloat4x4(&u.mvp,mvp);
  for(int i=0;i<neo::controlCount;i++){const auto& c=controls[i];u.controls[i]={c.press,c.flash,i<8||i>=12?c.a:i<10?paddleAngle:clutchAngle,c.b};u.pivots[i]={pivots[i][0],pivots[i][1],pivots[i][2],pivots[i][3]};u.axes[i]={axes[i][0],axes[i][1],axes[i][2],highlight[i]};}
  u.camera={cameraLocal.x,cameraLocal.y,cameraLocal.z,stickAngle};u.motion={buttonTravel,float(editMode),brightness,flashStrength};u.light={std::cos(turn),std::sin(turn),opacity,viewDesc.Format==DXGI_FORMAT_R8G8B8A8_UNORM?1.f:0.f};
  context->UpdateSubresource(constants.Get(),0,nullptr,&u,0,0);auto* cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);context->PSSetConstantBuffers(0,1,&cb);
  if(detailed){context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);
   for(const auto& part:parts){auto& material=materials.at(part.material);ID3D11ShaderResourceView* tex[6];for(int i=0;i<6;i++)tex[i]=material.images[i].Get();
    context->PSSetShaderResources(0,6,tex);auto* sam=material.sampler.Get();context->PSSetSamplers(0,1,&sam);context->DrawIndexed(part.count,part.first,0);}
  }
  else context->Draw(count,0);execute();
 }
};
