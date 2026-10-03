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
 ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11ShaderResourceView> base,normal,orm;
 ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;
 struct alignas(16) Uniform {XMFLOAT4X4 mvp;XMFLOAT4 controls[neo::controlCount],pivots[neo::controlCount],axes[neo::controlCount],camera,motion;};
 static_assert(sizeof(Uniform)==960,"HLSL constant buffer layout must match");
 std::array<std::array<float,4>,neo::controlCount> pivots{},axes{};
 UINT count=0,stride=0;bool detailed=false;float modelWidth=.310f;
 float buttonTravel=.0012f,paddleAngle=.12f,stickAngle=.2f,flashStrength=.75f;
 void execute(){ComPtr<ID3D11CommandList> commands;HRESULT result=context->FinishCommandList(FALSE,&commands);
  if(FAILED(result))throw std::runtime_error("FinishCommandList failed, HRESULT="+std::to_string(static_cast<unsigned long>(result)));
  immediate->ExecuteCommandList(commands.Get(),TRUE);}
 ComPtr<ID3D11ShaderResourceView> texture(const std::filesystem::path& path,bool srgb){
  struct ComScope {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(result))CoUninitialize();}} com;
  if(FAILED(com.result)&&com.result!=RPC_E_CHANGED_MODE)hr(com.result);
  ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())));
  ComPtr<IWICBitmapDecoder> decoder;hr(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
  ComPtr<IWICBitmapFrameDecode> frame;hr(decoder->GetFrame(0,&frame));UINT w=0,h=0;hr(frame->GetSize(&w,&h));
  if(!w||!h||w>4096||h>4096)throw std::runtime_error("Wheel texture size must be 1..4096 pixels");
  ComPtr<IWICFormatConverter> convert;hr(factory->CreateFormatConverter(&convert));
  hr(convert->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
  std::vector<unsigned char> pixels(size_t(w)*h*4);hr(convert->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()));
  D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=0;t.ArraySize=1;t.Format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
  t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;t.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
  ComPtr<ID3D11Texture2D> tex;hr(dev->CreateTexture2D(&t,nullptr,&tex));ComPtr<ID3D11ShaderResourceView> view;hr(dev->CreateShaderResourceView(tex.Get(),nullptr,&view));
  context->UpdateSubresource(tex.Get(),0,nullptr,pixels.data(),w*4,0);context->GenerateMips(view.Get());execute();return view;
 }
 void init(ID3D11Device* d,UINT width,UINT height,const std::filesystem::path& assets={}){
  dev=d;dev->GetImmediateContext(&immediate);hr(dev->CreateDeferredContext(0,&context));detailed=!assets.empty();
  const char* placeholder=R"(
   cbuffer C:register(b0){row_major float4x4 mvp;float4 controls[18];float4 pivots[18];float4 axes[18];float4 camera;float4 motion;};
   struct V{float3 p:POSITION;float3 c:COLOR;float b:TEXCOORD;};
   struct P{float4 p:SV_POSITION;float3 c:COLOR;};
   P vertexMain(V v){P o;o.p=mul(float4(v.p,1),mvp);o.c=v.c;if(v.b>=0){float4 s=controls[(int)v.b];o.c=lerp(o.c,1,saturate(s.x+s.y*motion.w));}return o;}
   float4 pixelMain(P p):SV_TARGET{return float4(p.c,1);}
  )";
  const char* custom=R"(
   // controls[c]: x=press spring, y=flash, z=knob angle or stick X tilt, w=stick Y tilt.
   // motion: x=button travel, y=paddle angle, z=brightness, w=flash strength. camera.w=stick angle.
   cbuffer C:register(b0){row_major float4x4 mvp;float4 controls[18];float4 pivots[18];float4 axes[18];float4 camera;float4 motion;};
   Texture2D baseTex:register(t0);Texture2D normalTex:register(t1);Texture2D ormTex:register(t2);SamplerState sampleTex:register(s0);
   struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float3 base:COLOR0;float3 emission:COLOR1;float2 factors:TEXCOORD1;float control:TEXCOORD2;};
   struct P{float4 p:SV_POSITION;float3 local:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float3 base:COLOR0;float3 emission:COLOR1;float2 factors:TEXCOORD3;float glow:TEXCOORD4;};
   float3 rotX(float3 d,float a){float s=sin(a),c=cos(a);return float3(d.x,c*d.y-s*d.z,s*d.y+c*d.z);}
   float3 rotY(float3 d,float a){float s=sin(a),c=cos(a);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
   float3 rotAxis(float3 d,float3 k,float a){float s=sin(a),c=cos(a);return d*c+cross(k,d)*s+k*dot(k,d)*(1-c);}
   P vertexMain(V v){P o;o.glow=0;
    if(v.control>=0){int c=(int)v.control;float4 s=controls[c];float3 pivot=pivots[c].xyz;float3 d=v.p-pivot;
     if(c<8)v.p.z-=s.x*motion.x;
     else if(c<12){float a=s.x*motion.y*pivots[c].w;v.p=pivot+rotY(d,a);v.n=rotY(v.n,a);}
     else if(c<16){float3 k=axes[c].xyz;v.p=pivot+rotAxis(d,k,s.z);v.n=rotAxis(v.n,k,s.z);}
     else{float ax=-s.w*camera.w,ay=s.z*camera.w;v.p=pivot+rotY(rotX(d,ax),ay)-float3(0,0,s.x*motion.x);v.n=rotY(rotX(v.n,ax),ay);}
     o.glow=saturate(s.y*motion.w+saturate(s.x)*.2);}
    o.p=mul(float4(v.p,1),mvp);o.local=v.p;o.n=v.n;o.uv=v.uv;o.base=v.base;o.emission=v.emission;o.factors=v.factors;return o;}
   float3 mappedNormal(P p){float3 n=normalize(p.n);float3 dx=ddx(p.local),dy=ddy(p.local);float2 du=ddx(p.uv),dv=ddy(p.uv);
    float det=du.x*dv.y-du.y*dv.x;if(abs(det)<1e-12)return n;
    float3 t=(dx*dv.y-dy*du.y)/det;float3 b=(-dx*dv.x+dy*du.x)/det;
    t=t-n*dot(n,t);b=b-n*dot(n,b);float len=max(dot(t,t),dot(b,b));if(len<1e-16)return n;
    float3 tex=normalTex.Sample(sampleTex,p.uv).xyz*2-1;float scale=rsqrt(len);return normalize(t*scale*tex.x+b*scale*tex.y+n*tex.z);}
   float4 pixelMain(P p):SV_TARGET{
    float3 n=mappedNormal(p);float3 view=normalize(camera.xyz-p.local);if(dot(n,view)<0)n=-n;float3 light=normalize(float3(-.35,.65,1));float3 halfV=normalize(view+light);
    float3 color=baseTex.Sample(sampleTex,p.uv).rgb*p.base;float2 mr=ormTex.Sample(sampleTex,p.uv).bg*p.factors;
    float metal=saturate(mr.x),rough=clamp(mr.y,.06,1);float diffuse=saturate(dot(n,light));
    float exponent=lerp(160,3,rough*rough);float spec=pow(saturate(dot(n,halfV)),exponent)*(1-rough*.7);
    float3 f0=lerp(float3(.04,.04,.04),color,metal);float3 lit=color*(.30+(1-metal*.75)*diffuse*.75)+f0*(.10+spec*1.8);
    // Fixed studio lighting; game cockpit lights are not available to an OpenXR composition layer.
    float3 result=lit+p.emission;result=lerp(result,float3(1,1,1),p.glow);return float4(result*motion.z,1);}
  )";
  const char* shader=detailed?custom:placeholder;ComPtr<ID3DBlob> v,p,e;
  auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& result){HRESULT h=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,entry,profile,0,0,&result,&e);
   if(FAILED(h))throw std::runtime_error(e?std::string(static_cast<const char*>(e->GetBufferPointer()),e->GetBufferSize()):"Wheel shader compile failed");};
  compile("vertexMain","vs_5_0",v);compile("pixelMain","ps_5_0",p);
  hr(dev->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));hr(dev->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
  D3D11_BUFFER_DESC b{};D3D11_SUBRESOURCE_DATA data{};
  if(detailed){
   auto mesh=neo::loadModel(assets/L"wheel.neo");modelWidth=mesh.width;pivots=mesh.pivots;axes=mesh.axes;count=UINT(mesh.indices.size());stride=sizeof(neo::ModelVertex);
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
   base=texture(assets/L"wheel-basecolor.png",true);normal=texture(assets/L"wheel-normal.png",false);orm=texture(assets/L"wheel-orm.png",false);
   D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_ANISOTROPIC;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxAnisotropy=8;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxLOD=D3D11_FLOAT32_MAX;hr(dev->CreateSamplerState(&sd,&sampler));
  }else{
   D3D11_INPUT_ELEMENT_DESC elems[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
   hr(dev->CreateInputLayout(elems,3,v->GetBufferPointer(),v->GetBufferSize(),&layout));auto mesh=neo::wheel();count=UINT(mesh.size());stride=sizeof(neo::Vertex);
   b.ByteWidth=UINT(mesh.size()*sizeof(neo::Vertex));b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;data.pSysMem=mesh.data();hr(dev->CreateBuffer(&b,&data,&vertices));
  }
  b={};b.ByteWidth=sizeof(Uniform);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr(dev->CreateBuffer(&b,nullptr,&constants));
  D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;hr(dev->CreateRasterizerState(&r,&raster));
  D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS;hr(dev->CreateDepthStencilState(&ds,&depthState));
  D3D11_TEXTURE2D_DESC t{};t.Width=width;t.Height=height;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_D32_FLOAT;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  hr(dev->CreateTexture2D(&t,nullptr,&depth));hr(dev->CreateDepthStencilView(depth.Get(),nullptr,&dsv));
 }
 void draw(ID3D11Texture2D* target,UINT w,UINT h,FXMMATRIX mvp,const std::array<neo::ControlPose,neo::controlCount>& controls,XMFLOAT3 cameraLocal,float brightness){
  // OpenXR D3D11 swapchain resources can be TYPELESS. A null RTV descriptor
  // inherits that typeless format and fails with E_INVALIDARG. Select the
  // concrete sRGB format requested in xrCreateSwapchain.
  D3D11_TEXTURE2D_DESC targetDesc{};target->GetDesc(&targetDesc);
  D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};viewDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
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
  const float clear[4]={0,0,0,0};context->ClearRenderTargetView(rt,clear);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
  context->OMSetDepthStencilState(depthState.Get(),0);context->RSSetState(raster.Get());D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);
  context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);UINT offset=0;auto* vb=vertices.Get();context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
  context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
  Uniform u{};XMStoreFloat4x4(&u.mvp,mvp);
  for(int i=0;i<neo::controlCount;i++){const auto& c=controls[i];u.controls[i]={c.press,c.flash,c.a,c.b};u.pivots[i]={pivots[i][0],pivots[i][1],pivots[i][2],pivots[i][3]};u.axes[i]={axes[i][0],axes[i][1],axes[i][2],0};}
  u.camera={cameraLocal.x,cameraLocal.y,cameraLocal.z,stickAngle};u.motion={buttonTravel,paddleAngle,brightness,flashStrength};
  context->UpdateSubresource(constants.Get(),0,nullptr,&u,0,0);auto* cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);context->PSSetConstantBuffers(0,1,&cb);
  if(detailed){ID3D11ShaderResourceView* tex[]={base.Get(),normal.Get(),orm.Get()};context->PSSetShaderResources(0,3,tex);auto* sam=sampler.Get();context->PSSetSamplers(0,1,&sam);context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);context->DrawIndexed(count,0,0);}
  else context->Draw(count,0);execute();
 }
};
