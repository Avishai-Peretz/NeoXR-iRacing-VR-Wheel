#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wheel.hpp"
#include <cassert>
#include <iostream>
#include <thread>

int main(int argc,char** argv){
 try{
  assert(argc==3);auto model=neo::loadModel(argv[1]);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
  hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
  Renderer renderer;renderer.init(device.Get(),640,480,&model);
  D3D11_TEXTURE2D_DESC desc{};desc.Width=640;desc.Height=480;desc.MipLevels=1;desc.ArraySize=1;
  desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;hr(device->CreateTexture2D(&desc,nullptr,&target));
  auto view=XMMatrixLookAtRH(XMVectorSet(0,0,.65f,1),XMVectorZero(),XMVectorSet(0,1,0,0));
  auto projection=XMMatrixPerspectiveFovRH(.6f,640.f/480,.01f,10);
  std::array<neo::ControlPose,neo::controlCount> poses{};
  renderer.draw(target.Get(),640,480,view*projection,poses,{0,0,.65f},.8f);
  D3D11_TEXTURE2D_DESC previewDesc=desc;previewDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  ComPtr<ID3D11Texture2D> previewTarget;hr(device->CreateTexture2D(&previewDesc,nullptr,&previewTarget));
  renderer.draw(previewTarget.Get(),640,480,view*projection,poses,{0,0,.65f},.8f);
  desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> readback;hr(device->CreateTexture2D(&desc,nullptr,&readback));context->CopyResource(readback.Get(),target.Get());
  D3D11_MAPPED_SUBRESOURCE pixels{};hr(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&pixels));
  unsigned visible=0;std::ofstream picture(argv[2],std::ios::binary);picture<<"P6\n640 480\n255\n";
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
   auto* pixel=static_cast<unsigned char*>(pixels.pData)+y*pixels.RowPitch+x*4;
   if(pixel[3])visible++;picture.write(reinterpret_cast<char*>(pixel),3);
  }
  context->Unmap(readback.Get(),0);assert(visible>10000&&visible<640*480);
  // A bad image must fail a replacement build, without invalidating the currently working renderer.
  auto image=std::make_shared<neo::ModelImage>();image->bytes={1,2,3,4};model.materials[0].images[0]=image;
  bool failed=false;try{Renderer bad;bad.init(device.Get(),640,480,&model);}catch(const std::exception&){failed=true;}assert(failed);
  renderer.draw(target.Get(),640,480,view*projection,poses,{0,0,.65f},.8f);
  std::cout<<"PASS: D3D11 WARP glTF materials, shader, texture upload, draw/readback and retention after failed replacement\n";
  return 0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<"\n";return 1;}
}
