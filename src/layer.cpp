#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include "input.hpp"
#include "render.hpp"
#include "click.hpp"
#include "edit.hpp"
// Single-instance/session proof of concept. Other sessions are passed through.
namespace {
HMODULE module=nullptr;std::wstring folder;std::recursive_mutex lock;
PFN_xrGetInstanceProcAddr nextGipa=nullptr;XrInstance instance=XR_NULL_HANDLE;
#define FN_LIST(X) X(CreateSession) X(DestroySession) X(DestroyInstance) X(EndFrame) X(WaitFrame) X(BeginSession) X(EnumerateSwapchainFormats) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) X(GetSystemProperties)
#define DECL(n) PFN_xr##n next##n=nullptr;
FN_LIST(DECL)
void log(const char* s){OutputDebugStringA((std::string("NeoXR: ")+s+"\n").c_str());
 try{std::ofstream out(std::filesystem::path(folder+L"\\NeoXR.log"),std::ios::app);out<<s<<'\n';}catch(...){} }
std::wstring ini(){return folder+L"\\NeoXR.ini";}
int number(const wchar_t* key,int def){return int(GetPrivateProfileIntW(L"Wheel",key,def,ini().c_str()));}
float real(const wchar_t* key,float def){wchar_t b[80];std::wstring ds=std::to_wstring(def);
 GetPrivateProfileStringW(L"Wheel",key,ds.c_str(),b,80,ini().c_str());try{float value=std::stof(b);return std::isfinite(value)?value:def;}catch(...){return def;}}
void check(XrResult r){if(XR_FAILED(r))throw std::runtime_error("OpenXR operation failed");}
struct Eye {XrSwapchain chain=XR_NULL_HANDLE;std::vector<XrSwapchainImageD3D11KHR> images;};
struct State {
 // gameSpace is the space of the game's own projection layer; the wheel is anchored and composited in it
 // so that game or runtime recentering moves the wheel together with the cockpit. gameSpace may be
 // arbitrarily rotated (iRacing uses STAGE rolled 180 degrees); levelSpace is our gravity-aligned reference.
 XrSession session=XR_NULL_HANDLE;XrSpace gameSpace=XR_NULL_HANDLE,levelSpace=XR_NULL_HANDLE;std::array<Eye,2> eyes;
 Renderer renderer;WheelInput input,buttons;bool separateButtons=false,ready=false,visible=true;
 bool shouldRender=false,stereo=false,anchored=false,saved=false;bool prevF8=false,prevF9=false,prevCloser=false,prevFarther=false,prevTiltUp=false,prevTiltDown=false;
 int recenterKey=0,recenterButton=-1;bool prevRecenterKey=false,prevRecenterButton=false;
 UINT width=1024,height=1024;uint32_t maxLayers=0;
 XMFLOAT4X4 head{},anchor{};float wheelWidth=.31f,rotation=900,tilt=0,yaw=0,px=0,py=-.3f,pz=-.5f;
 EditMode edit;int prevEdit=EditMode::Off;float sensitivity=1;std::array<float,6> before{};
 int axis=0;bool invert=false;int buttonMap[12]={0,1,2,3,4,5,6,7,-1,-1,-1,-1};float brightness=.8f;
 // knobMap[k]={clockwise,counter-clockwise}; stickMap[j]={up,down,left,right,push}.
 int knobMap[4][2]={{8,9},{10,11},{36,37},{38,39}},stickMap[2][5]={},stickPov[2]={-1,-1};bool prevKnob[4][2]={};
 int clutchAxis[2]={-1,-1};float clutchRest[2]={-10000,-10000},clutchFull[2]={10000,10000};
 neo::Animator animator;ClickSound click;float knobStep=.26f;LARGE_INTEGER lastFrame{};
 ~State(){for(auto& e:eyes)if(e.chain)nextDestroySwapchain(e.chain);if(levelSpace)nextDestroySpace(levelSpace);}
};
std::unique_ptr<State> state;bool target=false;
XMMATRIX pose(XrPosef p){return XMMatrixRotationQuaternion(XMVectorSet(p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w))*
 XMMatrixTranslation(p.position.x,p.position.y,p.position.z);}
// Keeps only the head's heading about gravity (levelSpace +Y), so looking down while recentering does not
// tilt the wheel. head is in gameSpace; toGame maps levelSpace into gameSpace.
XMMATRIX level(FXMMATRIX head,CXMMATRIX toGame){
 auto h=head*XMMatrixInverse(nullptr,toGame);XMFLOAT4X4 m;XMStoreFloat4x4(&m,h);
 // Row-vector convention: row 2 is the head's +Z (backward) axis.
 float yaw=std::atan2(m._31,m._33);return XMMatrixRotationY(yaw)*XMMatrixTranslation(m._41,m._42,m._43)*toGame;
}
// Accepts F1-F24, a key name below, a single letter or digit, or a decimal virtual-key code. Empty or 0 disables.
int keyCode(const wchar_t* text){
 if(!text[0])return 0;
 struct Name {const wchar_t* name;int key;} names[]={{L"Tab",VK_TAB},{L"Insert",VK_INSERT},{L"Delete",VK_DELETE},{L"Home",VK_HOME},
  {L"End",VK_END},{L"Pause",VK_PAUSE},{L"ScrollLock",VK_SCROLL},{L"Backspace",VK_BACK}};
 for(const auto& n:names)if(_wcsicmp(text,n.name)==0)return n.key;if((text[0]==L'F'||text[0]==L'f')&&text[1]){int n=_wtoi(text+1);if(n>=1&&n<=24)return VK_F1+n-1;}
 if(!text[1]&&iswalnum(text[0]))return towupper(text[0]);return _wtoi(text);
}
void initialize(State& s,const XrSessionCreateInfo* info){
 const auto* node=static_cast<const XrBaseInStructure*>(info->next);ID3D11Device* device=nullptr;
 for(;node;node=node->next)if(node->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
 device=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(node)->device;
 if(!device)throw std::runtime_error("D3D11 required; unsupported session passed through");
 XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};check(nextGetSystemProperties(instance,info->systemId,&props));s.maxLayers=props.graphicsProperties.maxLayerCount;
 s.width=std::min(1024u,props.graphicsProperties.maxSwapchainImageWidth);s.height=std::min(1024u,props.graphicsProperties.maxSwapchainImageHeight);
 uint32_t count=0;check(nextEnumerateSwapchainFormats(s.session,0,&count,nullptr));std::vector<int64_t> formats(count);
 check(nextEnumerateSwapchainFormats(s.session,count,&count,formats.data()));
 // sRGB target: shader colors are linear; runtime decodes for compositing.
 int64_t format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
 if(std::find(formats.begin(),formats.end(),format)==formats.end())throw std::runtime_error("RGBA8 sRGB swapchain unsupported");
 for(auto& eye:s.eyes){XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
 ci.format=format;ci.sampleCount=1;ci.width=s.width;ci.height=s.height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
 check(nextCreateSwapchain(s.session,&ci,&eye.chain));check(nextEnumerateSwapchainImages(eye.chain,0,&count,nullptr));
 eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
 check(nextEnumerateSwapchainImages(eye.chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())));}
 wchar_t modelName[80];GetPrivateProfileStringW(L"Wheel",L"Model",L"custom",modelName,80,ini().c_str());
 std::filesystem::path assets;
 if(_wcsicmp(modelName,L"custom")==0){wchar_t path[MAX_PATH];GetPrivateProfileStringW(L"Wheel",L"AssetDirectory",L"assets",path,MAX_PATH,ini().c_str());
  assets=std::filesystem::path(path);if(assets.is_relative())assets=std::filesystem::path(folder)/assets;}
 else if(_wcsicmp(modelName,L"placeholder")!=0)throw std::runtime_error("Model must be custom or placeholder");
 s.renderer.init(device,s.width,s.height,assets);
 s.renderer.buttonTravel=std::clamp(real(L"ButtonTravelMm",1.2f),0.f,5.f)/1000.f;
 s.renderer.paddleAngle=std::clamp(real(L"PaddleAngleDegrees",7),0.f,25.f)*neo::pi/180;
 s.brightness=std::clamp(real(L"Brightness",.8f),.1f,2.f);
 log(s.renderer.detailed?"Custom wheel loaded: 23 parts with baked material textures":"Procedural placeholder selected");
 s.wheelWidth=std::clamp(real(L"WidthMm",310),100.f,600.f)/1000.f;s.rotation=std::clamp(real(L"RotationDegrees",900),90.f,2520.f);
 s.tilt=std::clamp(real(L"TiltDegrees",0),-90.f,90.f);s.yaw=std::clamp(real(L"YawDegrees",0),-90.f,90.f);s.px=real(L"X",0);s.py=real(L"Y",-.3f);s.pz=real(L"Z",-.5f);
 s.axis=number(L"SteeringAxis",0);s.invert=number(L"Invert",0)!=0;
 if(!s.input.open(number(L"SteeringDevice",-1)))throw std::runtime_error("Configure SteeringDevice using NeoXR-Input first");
 int buttonDevice=number(L"ButtonDevice",-1);s.separateButtons=buttonDevice>=0;
 if(s.separateButtons&&!s.buttons.open(buttonDevice))throw std::runtime_error("ButtonDevice cannot be opened");
 for(int i=0;i<8;i++){std::wstring k=L"Button"+std::to_wstring(i);s.buttonMap[i]=number(k.c_str(),i);}
 const wchar_t* paddleKeys[]={L"LeftPaddleButton",L"RightPaddleButton",L"LeftClutchButton",L"RightClutchButton"};
 for(int i=0;i<4;i++)s.buttonMap[8+i]=number(paddleKeys[i],-1);
 s.renderer.clutchAngle=std::clamp(real(L"ClutchAngleDegrees",12),0.f,40.f)*neo::pi/180;
 const wchar_t* clutches[]={L"LeftClutch",L"RightClutch"};
 for(int j=0;j<2;j++){std::wstring k=clutches[j];s.clutchAxis[j]=number((k+L"Axis").c_str(),-1);
  s.clutchRest[j]=real((k+L"Rest").c_str(),-10000);s.clutchFull[j]=real((k+L"Full").c_str(),10000);}
 const wchar_t* knobs[]={L"B9B10Knob",L"B11B12Knob",L"B37B38Knob",L"B39B40Knob"};
 for(int k=0;k<4;k++){s.knobMap[k][0]=number((knobs[k]+std::wstring(L"CW")).c_str(),s.knobMap[k][0]);
  s.knobMap[k][1]=number((knobs[k]+std::wstring(L"CCW")).c_str(),s.knobMap[k][1]);}
 const wchar_t* sticks[]={L"LeftStick",L"RightStick"};const wchar_t* directions[]={L"Up",L"Down",L"Left",L"Right",L"Push"};
 for(int j=0;j<2;j++){for(int d=0;d<5;d++)s.stickMap[j][d]=number((sticks[j]+std::wstring(directions[d])).c_str(),-1);
  s.stickPov[j]=number((sticks[j]+std::wstring(L"POV")).c_str(),-1);}
 s.knobStep=std::clamp(real(L"KnobStepDegrees",15),0.f,90.f)*neo::pi/180;
 for(int c=neo::firstKnob;c<neo::firstStick;c++)s.animator.recentre[c]=std::abs(s.renderer.axes[c][2])<.9f;
 s.renderer.stickAngle=std::clamp(real(L"StickAngleDegrees",12),0.f,30.f)*neo::pi/180;
 s.renderer.flashStrength=std::clamp(real(L"FlashStrength",.75f),0.f,1.f);
 wchar_t sound[MAX_PATH]={};GetPrivateProfileStringW(L"Wheel",L"ClickSoundFile",L"",sound,MAX_PATH,ini().c_str());
 std::filesystem::path soundFile(sound);if(!soundFile.empty()&&soundFile.is_relative())soundFile=std::filesystem::path(folder)/soundFile;
 s.click.init(std::clamp(real(L"ClickVolume",.5f),0.f,1.f),soundFile);
 wchar_t text[80]={};GetPrivateProfileStringW(L"Wheel",L"RecenterKey",L"",text,80,ini().c_str());s.recenterKey=keyCode(text);
 s.recenterButton=number(L"RecenterButton",-1);
 XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
 rs.poseInReferenceSpace.orientation.w=1;check(nextCreateReferenceSpace(s.session,&rs,&s.levelSpace));
 // Calibration=x,y,z,qx,qy,qz,qw: the levelled head pose in the game's tracking space.
 GetPrivateProfileStringW(L"Wheel",L"Calibration",L"",text,80,ini().c_str());XrPosef c{};
 float* v[]={&c.position.x,&c.position.y,&c.position.z,&c.orientation.x,&c.orientation.y,&c.orientation.z,&c.orientation.w};
 if(swscanf_s(text,L"%f,%f,%f,%f,%f,%f,%f",v[0],v[1],v[2],v[3],v[4],v[5],v[6])==7){
  float sum=0,n=0;for(int i=0;i<7;i++)sum+=*v[i];for(int i=3;i<7;i++)n+=*v[i]* *v[i];
  if(std::isfinite(sum)&&std::abs(n-1)<.01f){XMStoreFloat4x4(&s.head,pose(c));s.saved=s.anchored=true;log("Saved calibration loaded");}}
 GetPrivateProfileStringW(L"Wheel",L"EditKey",L"Tab",text,80,ini().c_str());int editKey=keyCode(text);
 s.sensitivity=std::clamp(real(L"EditSensitivity",1),.1f,10.f);if(editKey)s.edit.start(module,editKey);
 s.ready=true;log("D3D11 stereo renderer and input initialized; F8 calibrates, F9 toggles, EditKey places the wheel with the mouse");
}
XrResult XRAPI_CALL createSession(XrInstance i,const XrSessionCreateInfo* ci,XrSession* out){
 std::lock_guard<std::recursive_mutex> guard(lock);XrResult result=nextCreateSession(i,ci,out);
 if(XR_SUCCEEDED(result)&&target&&!state&&number(L"Enabled",0)){
  try{state=std::make_unique<State>();state->session=*out;initialize(*state,ci);}
  catch(const std::exception& e){log(e.what());state.reset();}catch(...){log("Initialization failed");state.reset();}}
 return result;
}
XrResult XRAPI_CALL destroySession(XrSession s){std::lock_guard<std::recursive_mutex> guard(lock);if(state&&state->session==s)state.reset();return nextDestroySession(s);}
XrResult XRAPI_CALL destroyInstance(XrInstance i){std::lock_guard<std::recursive_mutex> guard(lock);state.reset();auto r=nextDestroyInstance(i);instance=XR_NULL_HANDLE;return r;}
XrResult XRAPI_CALL beginSession(XrSession s,const XrSessionBeginInfo* b){auto r=nextBeginSession(s,b);
 std::lock_guard<std::recursive_mutex> guard(lock);if(state&&state->session==s){state->stereo=b->primaryViewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;state->anchored=state->saved;}return r;}
XrResult XRAPI_CALL waitFrame(XrSession s,const XrFrameWaitInfo* w,XrFrameState* f){auto r=nextWaitFrame(s,w,f);
 std::lock_guard<std::recursive_mutex> guard(lock);if(state&&state->session==s)state->shouldRender=XR_SUCCEEDED(r)&&f->shouldRender;return r;}
XrResult XRAPI_CALL endFrame(XrSession session,const XrFrameEndInfo* f){
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(!state||state->session!=session||!state->ready||!state->stereo||!state->shouldRender||!f->layerCount||f->layerCount>=state->maxLayers)return nextEndFrame(session,f);
 auto& s=*state;
 bool f8=(GetAsyncKeyState(VK_F8)&0x8000)!=0,f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
 bool recenter=f8&&!s.prevF8;if(f9&&!s.prevF9)s.visible=!s.visible;s.prevF8=f8;s.prevF9=f9;
 if(s.recenterKey){bool k=(GetAsyncKeyState(s.recenterKey)&0x8000)!=0;recenter|=k&&!s.prevRecenterKey;s.prevRecenterKey=k;}
 XrSpace space=XR_NULL_HANDLE;
 for(uint32_t i=0;i<f->layerCount&&!space;i++)if(f->layers[i]&&f->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)space=f->layers[i]->space;
 if(!space)return nextEndFrame(session,f);
 if(space!=s.gameSpace){if(s.gameSpace)log("Game switched reference space (game recenter); wheel follows the cockpit");s.gameSpace=space;}
 bool chord=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000);
 auto tap=[&](int key,bool& prev){bool down=chord&&(GetAsyncKeyState(key)&0x8000);bool hit=down&&!prev;prev=down;return hit;};
 auto save=[](const wchar_t* key,const wchar_t* format,float v){wchar_t value[32];swprintf_s(value,format,v);
  WritePrivateProfileStringW(L"Wheel",key,value,ini().c_str());char line[64];snprintf(line,sizeof line,"%ls=%ls",key,value);log(line);};
 bool closer=tap(VK_DOWN,s.prevCloser),farther=tap(VK_UP,s.prevFarther);
 if(closer||farther){s.pz=std::clamp(s.pz+(closer?.01f:-.01f),-1.5f,-.1f);save(L"Z",L"%.2f",s.pz);}
 bool tiltUp=tap(VK_PRIOR,s.prevTiltUp),tiltDown=tap(VK_NEXT,s.prevTiltDown);
 if(tiltUp||tiltDown){s.tilt=std::clamp(s.tilt+(tiltUp?1.f:-1.f),-90.f,90.f);save(L"TiltDegrees",L"%.0f",s.tilt);}
 {int mode=s.edit.mode;std::array<float,6> now={s.px,s.py,s.pz,s.tilt,s.yaw,s.wheelWidth};
  if(mode!=EditMode::Off&&s.prevEdit==EditMode::Off){s.before=now;log("Edit mode: left-drag moves, right-drag sets distance, M switches to size/rotate, Esc then Enter saves");}
  float lx=float(s.edit.leftX.exchange(0)),ly=float(s.edit.leftY.exchange(0)),rx=float(s.edit.rightX.exchange(0)),ry=float(s.edit.rightY.exchange(0));
  float wheel=float(s.edit.scroll.exchange(0))/WHEEL_DELTA,k=s.sensitivity;
  if(mode==EditMode::Move){s.px=std::clamp(s.px+lx*.0005f*k,-1.f,1.f);s.py=std::clamp(s.py-ly*.0005f*k,-1.5f,1.f);
   s.pz=std::clamp(s.pz+ry*.0005f*k-wheel*.01f,-1.5f,-.1f);}
  else if(mode==EditMode::Adjust){s.wheelWidth=std::clamp(s.wheelWidth+(lx-ly)*.0002f*k+wheel*.005f,.1f,.6f);
   s.tilt=std::clamp(s.tilt-ry*.1f*k,-90.f,90.f);s.yaw=std::clamp(s.yaw+rx*.1f*k,-90.f,90.f);}
  switch(s.edit.result.exchange(EditMode::None)){
   case EditMode::Save:save(L"X",L"%.3f",s.px);save(L"Y",L"%.3f",s.py);save(L"Z",L"%.3f",s.pz);save(L"TiltDegrees",L"%.1f",s.tilt);
    save(L"YawDegrees",L"%.1f",s.yaw);save(L"WidthMm",L"%.0f",s.wheelWidth*1000);log("Edit mode: placement saved");break;
   case EditMode::Cancel:s.px=s.before[0];s.py=s.before[1];s.pz=s.before[2];s.tilt=s.before[3];s.yaw=s.before[4];s.wheelWidth=s.before[5];
    log("Edit mode: cancelled, placement restored");break;}
  s.prevEdit=mode;s.renderer.editMode=mode;}
 if(!s.visible)return nextEndFrame(session,f);
 try{
  if(!s.input.poll())return nextEndFrame(session,f);if(s.separateButtons)s.buttons.poll();
  {const auto& in=s.separateButtons?s.buttons:s.input;int b=s.recenterButton;
   bool h=in.valid&&b>=0&&b<128&&(in.state.rgbButtons[b]&128);recenter|=h&&!s.prevRecenterButton;s.prevRecenterButton=h;}
  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=f->displayTime;li.space=space;
  XrViewState vs{XR_TYPE_VIEW_STATE};uint32_t count=0;std::array<XrView,2> views={XrView{XR_TYPE_VIEW},XrView{XR_TYPE_VIEW}};
  check(nextLocateViews(session,&li,&vs,2,&count,views.data()));
  auto required=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
  if(count!=2||(vs.viewStateFlags&required)!=required)return nextEndFrame(session,f);
  if(!s.anchored||recenter){auto p=views[0].pose;const auto& b=views[1].pose.position;
   p.position={(p.position.x+b.x)/2,(p.position.y+b.y)/2,(p.position.z+b.z)/2};
   XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};auto valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
   auto head=pose(p);
   if(XR_SUCCEEDED(nextLocateSpace(s.levelSpace,space,f->displayTime,&loc))&&(loc.locationFlags&valid)==valid)head=level(head,pose(loc.pose));
   XMStoreFloat4x4(&s.head,head);s.anchored=true;
   // Only an explicit recenter is saved; the automatic first-frame placement is not.
   if(recenter){XMVECTOR scale,q,t;XMMatrixDecompose(&scale,&q,&t,head);XMFLOAT4 qf;XMFLOAT3 tf;XMStoreFloat4(&qf,q);XMStoreFloat3(&tf,t);
    wchar_t value[80];swprintf_s(value,L"%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f",tf.x,tf.y,tf.z,qf.x,qf.y,qf.z,qf.w);
    WritePrivateProfileStringW(L"Wheel",L"Calibration",value,ini().c_str());s.saved=true;log("Calibration saved");}}
  XMStoreFloat4x4(&s.anchor,XMMatrixRotationX(s.tilt*neo::pi/180)*XMMatrixRotationY(s.yaw*neo::pi/180)*XMMatrixTranslation(s.px,s.py,s.pz)*XMLoadFloat4x4(&s.head));
  auto model=XMMatrixScaling(s.wheelWidth/s.renderer.modelWidth,s.wheelWidth/s.renderer.modelWidth,s.wheelWidth/s.renderer.modelWidth)*
   XMMatrixRotationZ(-neo::steering(s.input.axis(s.axis),s.rotation,s.invert))*XMLoadFloat4x4(&s.anchor);
  const auto& input=s.separateButtons?s.buttons:s.input;
  auto held=[&](int b){return input.valid&&b>=0&&b<128&&(input.state.rgbButtons[b]&128);};
  std::array<neo::ControlInput,neo::controlCount> controls{};
  for(int i=0;i<12;i++)controls[i].down=held(s.buttonMap[i]);
  for(int j=0;j<2;j++){int a=s.clutchAxis[j];float span=s.clutchFull[j]-s.clutchRest[j];
   if(input.valid&&a>=0&&a<8&&std::abs(span)>1)controls[10+j].analog=std::clamp((float(input.axis(a))-s.clutchRest[j])/span,0.f,1.f);}
  for(int k=0;k<4;k++)for(int d=0;d<2;d++){bool h=held(s.knobMap[k][d]);if(h&&!s.prevKnob[k][d])controls[neo::firstKnob+k].turn+=d?1:-1;s.prevKnob[k][d]=h;}
  for(int j=0;j<2;j++){auto& c=controls[neo::firstStick+j];const int* m=s.stickMap[j];
   c.x=float(held(m[3]))-float(held(m[2]));c.y=float(held(m[0]))-float(held(m[1]));c.down=held(m[4]);
   int pov=s.stickPov[j];if(input.valid&&pov>=0&&pov<4&&LOWORD(input.state.rgdwPOV[pov])!=0xFFFF){
    float a=float(input.state.rgdwPOV[pov])/100*neo::pi/180;c.x=std::round(std::sin(a));c.y=std::round(std::cos(a));}}
  LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
  float dt=s.lastFrame.QuadPart?float(double(now.QuadPart-s.lastFrame.QuadPart)/double(frequency.QuadPart)):0;s.lastFrame=now;
  auto clicked=s.animator.update(controls,dt,s.knobStep);if(clicked!=neo::Click::None)s.click.play(clicked);
  std::array<neo::ControlPose,neo::controlCount> poses;for(int i=0;i<neo::controlCount;i++)poses[i]=s.animator.pose(i);
  std::array<XrCompositionLayerProjectionView,2> pv={XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  for(int eye=0;eye<2;eye++){
   auto& e=s.eyes[eye];uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(nextAcquireSwapchainImage(e.chain,&ai,&index));
   XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;check(nextWaitSwapchainImage(e.chain,&wi));
   XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
   try{auto a=views[eye].fov;float nearZ=.02f;
    auto projection=XMMatrixPerspectiveOffCenterRH(std::tan(a.angleLeft)*nearZ,std::tan(a.angleRight)*nearZ,
      std::tan(a.angleDown)*nearZ,std::tan(a.angleUp)*nearZ,nearZ,10.f);
    auto mvp=model*XMMatrixInverse(nullptr,pose(views[eye].pose))*projection;
    XMFLOAT3 cameraLocal;const auto& ep=views[eye].pose.position;
    XMStoreFloat3(&cameraLocal,XMVector3TransformCoord(XMVectorSet(ep.x,ep.y,ep.z,1),XMMatrixInverse(nullptr,model)));
    s.renderer.draw(e.images.at(index).texture,s.width,s.height,mvp,poses,cameraLocal,s.brightness);
   }catch(...){nextReleaseSwapchainImage(e.chain,&ri);throw;}
   check(nextReleaseSwapchainImage(e.chain,&ri));pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;
   pv[eye].subImage.swapchain=e.chain;pv[eye].subImage.imageRect.extent={int32_t(s.width),int32_t(s.height)};
  }
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;
  layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.viewCount=2;layer.views=pv.data();
  std::vector<const XrCompositionLayerBaseHeader*> layers(f->layers,f->layers+f->layerCount);
  layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));auto frame=*f;frame.layerCount=uint32_t(layers.size());frame.layers=layers.data();
  // Never retry xrEndFrame: even a failed call can have consumed the frame.
  XrResult result=nextEndFrame(session,&frame);if(XR_FAILED(result)){log("Compositor rejected frame; disabling overlay for this session");s.ready=false;}return result;
 }catch(const std::exception& e){log(e.what());s.ready=false;}catch(...){log("Render failed; overlay disabled");s.ready=false;}
 return nextEndFrame(session,f);
}
XrResult XRAPI_CALL createReferenceSpace(XrSession session,const XrReferenceSpaceCreateInfo* ci,XrSpace* out){
 XrResult r=nextCreateReferenceSpace(session,ci,out);
 static int logged=0;
 if(target&&ci&&XR_SUCCEEDED(r)&&logged++<32){const auto& p=ci->poseInReferenceSpace;char line[160];
  snprintf(line,sizeof line,"Game created reference space type=%d offset=(%.3f,%.3f,%.3f) rotation=(%.3f,%.3f,%.3f,%.3f)",
   int(ci->referenceSpaceType),p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w);log(line);}
 return r;
}
XrResult XRAPI_CALL gipa(XrInstance i,const char* name,PFN_xrVoidFunction* f){
 if(!name||!f)return XR_ERROR_VALIDATION_FAILURE;
 #define HOOK(n,fn) if(std::strcmp(name,"xr" #n)==0){*f=reinterpret_cast<PFN_xrVoidFunction>(fn);return XR_SUCCESS;}
 HOOK(GetInstanceProcAddr,gipa) HOOK(CreateSession,createSession) HOOK(DestroySession,destroySession)
 HOOK(DestroyInstance,destroyInstance) HOOK(EndFrame,endFrame) HOOK(WaitFrame,waitFrame) HOOK(BeginSession,beginSession)
 HOOK(CreateReferenceSpace,createReferenceSpace)
 return nextGipa(i,name,f);
}
XrResult XRAPI_CALL createInstance(const XrInstanceCreateInfo* ci,const XrApiLayerCreateInfo* info,XrInstance* out){
 if(!info||!info->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
 if(instance!=XR_NULL_HANDLE)return XR_ERROR_LIMIT_REACHED;
 auto* n=info->nextInfo;nextGipa=n->nextGetInstanceProcAddr;auto chained=*info;chained.nextInfo=n->next;
 auto r=n->nextCreateApiLayerInstance(ci,&chained,out);if(XR_FAILED(r))return r;instance=*out;
 #define LOAD(n) nextGipa(instance,"xr" #n,reinterpret_cast<PFN_xrVoidFunction*>(&next##n));
 FN_LIST(LOAD)
 wchar_t path[MAX_PATH]={};GetModuleFileNameW(module,path,MAX_PATH);folder=path;folder=folder.substr(0,folder.find_last_of(L"\\/"));
 wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring name=exe;name=name.substr(name.find_last_of(L"\\/")+1);
 wchar_t allowed[MAX_PATH]={};GetPrivateProfileStringW(L"Wheel",L"Process",L"iRacingSim64DX11.exe",allowed,MAX_PATH,ini().c_str());target=_wcsicmp(name.c_str(),allowed)==0;
 if(target)log("Layer loaded in configured process (NeoXR 0.2.1)");return r;
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* l,const char*,XrNegotiateApiLayerRequest* a){
 if(!l||!a||l->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||a->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||
 l->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||a->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||
 l->structSize!=sizeof(*l)||a->structSize!=sizeof(*a)||l->minInterfaceVersion>1||l->maxInterfaceVersion<1||l->maxApiVersion<XR_MAKE_VERSION(1,0,0)||l->minApiVersion>XR_MAKE_VERSION(1,0,34))return XR_ERROR_INITIALIZATION_FAILED;
 a->layerInterfaceVersion=1;a->layerApiVersion=XR_MAKE_VERSION(1,0,34);a->getInstanceProcAddr=gipa;a->createApiLayerInstance=createInstance;return XR_SUCCESS;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,LPVOID){if(why==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);}return TRUE;}
