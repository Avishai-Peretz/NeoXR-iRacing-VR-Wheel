#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include "bridge.hpp"
#include "wheel.hpp"

// C API for the setup wizard's live wheel preview: the layer's own WheelRig drawn into a window,
// so every binding the user makes animates the wheel exactly as it will in VR.

namespace {

constexpr float verticalFov=.5f;  // radians
constexpr float fitWidth=.54f,fitHeight=.42f;  // half extents to keep in view, as fractions of the wheel width

float linear(BYTE srgb){return std::pow(srgb/255.f,2.2f);}

class Preview {
public:
 Preview(HWND window,const std::filesystem::path& folder,const std::filesystem::path& settings,COLORREF background)
  :folder_(folder){
  RECT client{};GetClientRect(window,&client);
  width_=UINT(std::max<LONG>(1,client.right));height_=UINT(std::max<LONG>(1,client.bottom));
  createDevice();
  createSwapchain(window);
  rig_.init(device_.Get(),width_,height_,neo::Config(folder_,settings));
  rig_.renderer.background={linear(GetRValue(background)),linear(GetGValue(background)),linear(GetBValue(background)),1};
 }

 void configure(const std::filesystem::path& settings){rig_.configure(neo::Config(folder_,settings));}

 void resize(UINT width,UINT height){
  width=std::max(1u,width);height=std::max(1u,height);
  if(width==width_&&height==height_)return;
  ComPtr<ID3D11DeviceContext> context;device_->GetImmediateContext(&context);
  context->ClearState();context->Flush();
  hr(swapchain_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0));
  rig_.renderer.resize(width,height);
  width_=width;height_=height;
 }

 // Steering or buttons may be null when that device is unavailable. `highlight` is a control slot to
 // point out, or -1. `yaw` and `pitch` orbit the camera around the wheel, in radians.
 void frame(const NeoInputState* steering,const NeoInputState* buttons,int highlight,float yaw,float pitch){
  float dt=tick();time_+=dt;
  DIJOYSTATE2 pad{};if(buttons)pad=fromBridge(*buttons);
  auto poses=rig_.animate(buttons?&pad:nullptr,dt);
  float pulse=.5f+.5f*std::sin(time_*6);
  for(int i=0;i<neo::controlCount;i++)rig_.renderer.highlight[i]=i==highlight?pulse:0;

  float turn=steering?rig_.steering(fromBridge(*steering)):0;
  XMMATRIX model=XMMatrixRotationZ(-turn);
  float aspect=float(width_)/float(height_),size=rig_.renderer.modelWidth;
  float tanV=std::tan(verticalFov/2),tanH=tanV*aspect;
  float distance=std::max(fitWidth*size/tanH,fitHeight*size/tanV);
  XMVECTOR eye=XMVectorSet(distance*std::sin(yaw)*std::cos(pitch),distance*std::sin(pitch),distance*std::cos(yaw)*std::cos(pitch),1);
  XMMATRIX view=XMMatrixLookAtRH(eye,XMVectorZero(),XMVectorSet(0,1,0,0));
  XMMATRIX projection=XMMatrixPerspectiveFovRH(verticalFov,aspect,distance*.1f,distance*10);
  // The shader lights in model space, so it needs the eye position there too.
  XMFLOAT3 cameraInModel;XMStoreFloat3(&cameraInModel,XMVector3TransformCoord(eye,XMMatrixInverse(nullptr,model)));

  ComPtr<ID3D11Texture2D> back;hr(swapchain_->GetBuffer(0,IID_PPV_ARGS(&back)));
  rig_.renderer.draw(back.Get(),width_,height_,model*view*projection,poses,cameraInModel,rig_.brightness);
  hr(swapchain_->Present(0,0));
 }

private:
 void createDevice(){
  // WARP keeps the preview working on machines whose GPU driver refuses a second device.
  for(D3D_DRIVER_TYPE type:{D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP})
   if(SUCCEEDED(D3D11CreateDevice(nullptr,type,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,nullptr)))return;
  throw std::runtime_error("No Direct3D 11 device available");
 }
 void createSwapchain(HWND window){
  ComPtr<IDXGIDevice> dxgi;hr(device_.As(&dxgi));
  ComPtr<IDXGIAdapter> adapter;hr(dxgi->GetAdapter(&adapter));
  ComPtr<IDXGIFactory2> factory;hr(adapter->GetParent(IID_PPV_ARGS(&factory)));
  DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width_;desc.Height=height_;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
  hr(factory->CreateSwapChainForHwnd(device_.Get(),window,&desc,nullptr,nullptr,&swapchain_));
  factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
 }
 float tick(){
  auto now=std::chrono::steady_clock::now();
  float dt=started_?std::chrono::duration<float>(now-last_).count():0;
  started_=true;last_=now;
  return std::clamp(dt,0.f,.1f);
 }

 std::filesystem::path folder_;
 ComPtr<ID3D11Device> device_;
 ComPtr<IDXGISwapChain1> swapchain_;
 UINT width_=1,height_=1;
 neo::WheelRig rig_;
 std::chrono::steady_clock::time_point last_;
 bool started_=false;
 float time_=0;
};

thread_local std::wstring lastError;

template<class Action> int guarded(Action action){
 try{action();return 1;}
 catch(const std::exception& e){std::string text=e.what();lastError.assign(text.begin(),text.end());}
 catch(...){lastError=L"Preview failed";}
 return 0;
}

}

extern "C" {

// `folder` is the NeoXR folder that relative paths in `settings` resolve against.
// `background` is 0x00BBGGRR. Returns null on failure; see neo_preview_error.
__declspec(dllexport) void* neo_preview_create(HWND window,const wchar_t* folder,const wchar_t* settings,COLORREF background){
 Preview* preview=nullptr;
 guarded([&]{preview=new Preview(window,folder,settings,background);});
 return preview;
}

__declspec(dllexport) int neo_preview_configure(void* preview,const wchar_t* settings){
 return guarded([&]{static_cast<Preview*>(preview)->configure(settings);});
}

__declspec(dllexport) int neo_preview_resize(void* preview,int width,int height){
 return guarded([&]{static_cast<Preview*>(preview)->resize(UINT(std::max(width,1)),UINT(std::max(height,1)));});
}

__declspec(dllexport) int neo_preview_frame(void* preview,const NeoInputState* steering,const NeoInputState* buttons,int highlight,float yaw,float pitch){
 return guarded([&]{static_cast<Preview*>(preview)->frame(steering,buttons,highlight,yaw,pitch);});
}

__declspec(dllexport) const wchar_t* neo_preview_error(){return lastError.c_str();}

__declspec(dllexport) void neo_preview_destroy(void* preview){delete static_cast<Preview*>(preview);}

}
