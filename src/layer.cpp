#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "config.hpp"
#include "controls.hpp"
#include "edit.hpp"
#include "input.hpp"
#include "iracing.hpp"
#include "passthrough.hpp"
#include "placement.hpp"
#include "wheel.hpp"

// NeoXR is an OpenXR API layer: it forwards every call to the next layer or runtime and, for the one
// configured game, adds a projection layer with the wheel to each submitted frame. It supports a single
// D3D11 stereo session; anything else is passed through untouched.
namespace {

// ---------------------------------------------------------------------------------------------------
// Layer-wide state and the next layer's entry points
// ---------------------------------------------------------------------------------------------------

HMODULE module=nullptr;
std::filesystem::path folder;  // where NeoXR.dll, NeoXR.ini and NeoXR.log live
bool targetProcess=false;      // true only inside the configured game executable
std::recursive_mutex lock;
PFN_xrGetInstanceProcAddr nextGipa=nullptr;
XrInstance instance=XR_NULL_HANDLE;         // the game's newest instance
XrInstance overlayInstance=XR_NULL_HANDLE;  // the instance whose session has the wheel
bool passthroughEnabled=false;              // NeoXR enabled passthrough on the newest instance

#define FN_LIST(X) X(CreateSession) X(DestroySession) X(DestroyInstance) X(EndFrame) X(WaitFrame) X(BeginSession) \
 X(EnumerateSwapchainFormats) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
 X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) \
 X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) X(GetSystemProperties) X(PollEvent)
#define DECLARE_NEXT(name) PFN_xr##name next##name=nullptr;
FN_LIST(DECLARE_NEXT)

neo::Config config(){return neo::Config(folder);}

void log(const std::string& line){
 OutputDebugStringA(("NeoXR: "+line+"\n").c_str());
 SYSTEMTIME t;GetLocalTime(&t);char stamp[16];snprintf(stamp,sizeof stamp,"%02d:%02d:%02d.%03d ",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
 try{std::ofstream(folder/L"NeoXR.log",std::ios::app)<<stamp<<line<<'\n';}catch(...){}
}
template<class... Args> void logf(const char* format,Args... args){
 char line[256];snprintf(line,sizeof line,format,args...);log(line);
}

// Crash early: every OpenXR failure becomes an exception naming the call that failed.
void check(XrResult result,const char* call){
 if(XR_FAILED(result))throw std::runtime_error(std::string(call)+" failed, XrResult="+std::to_string(result));
}
#define XR_CHECK(call) check((call),#call)

// ---------------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------------

constexpr XrViewConfigurationType stereoViews=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
// sRGB target: shader colors are linear and the runtime decodes them for compositing.
constexpr int64_t swapchainFormat=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr uint32_t maxEyeResolution=1600;  // at least iRacing's 1424x1568 eye images, so fine detail such as carbon weave survives
constexpr float nearPlane=.02f,farPlane=10.f;
constexpr float nudgeMetres=.01f,nudgeDegrees=1.f;
constexpr int maxLoggedSpaces=32;

XMMATRIX toMatrix(const XrPosef& p){
 const auto& q=p.orientation;
 return XMMatrixRotationQuaternion(XMVectorSet(q.x,q.y,q.z,q.w))*XMMatrixTranslation(p.position.x,p.position.y,p.position.z);
}
XMMATRIX projection(const XrFovf& fov){
 return XMMatrixPerspectiveOffCenterRH(std::tan(fov.angleLeft)*nearPlane,std::tan(fov.angleRight)*nearPlane,
  std::tan(fov.angleDown)*nearPlane,std::tan(fov.angleUp)*nearPlane,nearPlane,farPlane);
}
template<class Flags> bool hasAll(Flags value,Flags required){return (value&required)==required;}
bool keyHeld(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}

// Owns an OpenXR handle and destroys it with the matching next-layer function, so partial
// initialization never leaks swapchains or spaces.
template<class Handle> class Owned {
public:
 using Destroy=XrResult(XRAPI_PTR*)(Handle);
 explicit Owned(Destroy destroy):destroy_(destroy){}
 ~Owned(){if(handle_)destroy_(handle_);}
 Owned(const Owned&)=delete;Owned& operator=(const Owned&)=delete;
 Handle get() const{return handle_;}
 Handle* put(){return &handle_;}
private:
 Destroy destroy_;Handle handle_=XR_NULL_HANDLE;
};

struct Eye {
 Owned<XrSwapchain> chain{nextDestroySwapchain};
 std::vector<XrSwapchainImageD3D11KHR> images;
};

// Acquires and waits on construction and always releases: a throw while drawing cannot leave the
// swapchain image acquired. Call release() on the success path to see its result.
class AcquiredImage {
public:
 explicit AcquiredImage(const Eye& eye):chain_(eye.chain.get()){
  uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XR_CHECK(nextAcquireSwapchainImage(chain_,&acquire,&index));
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
  XR_CHECK(nextWaitSwapchainImage(chain_,&wait));
  texture_=eye.images.at(index).texture;acquired_=true;
 }
 ~AcquiredImage(){if(acquired_){XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};nextReleaseSwapchainImage(chain_,&info);}}
 AcquiredImage(const AcquiredImage&)=delete;AcquiredImage& operator=(const AcquiredImage&)=delete;
 ID3D11Texture2D* texture() const{return texture_;}
 void release(){
  acquired_=false;XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  XR_CHECK(nextReleaseSwapchainImage(chain_,&info));
 }
private:
 XrSwapchain chain_;ID3D11Texture2D* texture_=nullptr;bool acquired_=false;
};

// Seconds between successive calls; 0 on the first call.
class FrameClock {
public:
 float tick(){
  LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
  float dt=last_.QuadPart?float(double(now.QuadPart-last_.QuadPart)/double(frequency.QuadPart)):0;
  last_=now;return dt;
 }
private:
 LARGE_INTEGER last_{};
};

ID3D11Device* findD3D11Device(const XrSessionCreateInfo& info){
 for(auto* node=static_cast<const XrBaseInStructure*>(info.next);node;node=node->next)
  if(node->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)return reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(node)->device;
 return nullptr;
}

// The game's own projection layer space. Anchoring the wheel there makes game or runtime
// recentering move the wheel together with the cockpit.
XrSpace findGameSpace(const XrFrameEndInfo& frame){
 for(uint32_t i=0;i<frame.layerCount;i++)
  if(frame.layers[i]&&frame.layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)return frame.layers[i]->space;
 return XR_NULL_HANDLE;
}

// ---------------------------------------------------------------------------------------------------
// Overlay: everything NeoXR adds to one session. It is created with the game's session and destroyed
// with it, so every resource it holds follows the session lifetime.
// ---------------------------------------------------------------------------------------------------

class Overlay {
public:
 Overlay(XrSession session,const XrSessionCreateInfo& info);
 Overlay(const Overlay&)=delete;Overlay& operator=(const Overlay&)=delete;

 XrSession session() const{return session_;}
 void begin(const XrSessionBeginInfo& info){
  stereo_=info.primaryViewConfigurationType==stereoViews;
  anchored_=hasSavedCalibration_;
 }
 void waited(bool shouldRender){shouldRender_=shouldRender;}
 XrResult endFrame(const XrFrameEndInfo& frame);

private:
 // Startup, in dependency order.
 void createSwapchains(XrSystemId system);
 void createRenderer(ID3D11Device* device,const neo::Config& settings);
 void openInput(const neo::Config& settings);
 void loadPlacement(const neo::Config& settings);
 void loadRecentering(const neo::Config& settings);
 void reloadSettings();

 // One frame, in order.
 void noteSkip(const char* reason);
 void pollInput();
 bool readRecenterAndToggleKeys();
 void trackGameSpace(XrSpace space);
 void logGameDepth(const XrFrameEndInfo& frame);
 void followCar();
 void applyNudges();
 void applyEdit();
 void showRoom(bool on);
 std::optional<std::array<XrView,2>> locateViews(XrSpace space,XrTime time);
 void calibrate(const std::array<XrView,2>& views,XrSpace space,XrTime time,bool save);
 XMMATRIX wheelToGame() const;
 XrCompositionLayerProjectionView renderEye(int eye,const XrView& view,FXMMATRIX model,const std::array<neo::ControlPose,neo::controlCount>& poses);
 XrResult submit(const XrFrameEndInfo& frame,XrSpace space,const std::array<XrCompositionLayerProjectionView,2>& views);

 void saveSetting(const wchar_t* key,const wchar_t* format,float value);
 void savePlacement(const neo::Placement& previous);
 void useCarAlignment(const neo::CarAlignment& alignment);
 void useGeneralPlacement();
 void forgetCarAlignment();
 const WheelInput& buttonSource() const{return separateButtons_?buttons_:wheel_;}

 XrSession session_;
 std::array<Eye,2> eyes_;
 Owned<XrSpace> levelSpace_{nextDestroySpace};  // gravity-aligned reference for levelling calibration
 XrSpace gameSpace_=XR_NULL_HANDLE;
 UINT width_=maxEyeResolution,height_=maxEyeResolution;
 uint32_t maxLayers_=0;
 bool stereo_=false,shouldRender_=false,failed_=false,visible_=true,loggedRenderAnyway_=false;

 neo::WheelRig rig_;
 WheelInput wheel_,buttons_;
 bool separateButtons_=false,wheelLost_=false,buttonsLost_=false;
 ULONGLONG nextWheelReopen_=0,nextButtonsReopen_=0;
 const char* skipReason_=nullptr;  // why the last frame had no wheel, or null if it had one
 FrameClock clock_;

 neo::Placement placement_,placementBeforeEdit_;
 XMFLOAT4X4 head_{};
 bool anchored_=false,hasSavedCalibration_=false;

 struct Hotkeys {neo::EdgeTrigger recenter,toggle,recenterKey,recenterButton,closer,farther,tiltUp,tiltDown;} keys_;
 int recenterKey_=0,recenterButton_=-1;

 EditMode edit_;
 int previousEditMode_=EditMode::Off;
 float editSensitivity_=1,editOpacity_=.5f;

 // While the current car has a saved alignment, placement_ and head_ hold it and the general
 // placement waits here, so leaving the car or forgetting its alignment restores it.
 neo::IracingTelemetry telemetry_;
 std::wstring car_;
 bool carAligned_=false,generalAnchored_=false,depthLogged_=false;
 neo::Placement generalPlacement_;
 XMFLOAT4X4 generalHead_{};
 ULONGLONG nextCarCheck_=0;

 neo::Passthrough room_;
 bool enabled_=false,allowPassthrough_=true;
 ULONGLONG nextSettingsCheck_=0;
 neo::FileStamp settingsStamp_{},candidateStamp_{};
 std::wstring calibrationText_;
};

Overlay::Overlay(XrSession session,const XrSessionCreateInfo& info):session_(session){
 ID3D11Device* device=findD3D11Device(info);
 if(!device)throw std::runtime_error("D3D11 required; unsupported session passed through");
 const neo::Config settings=config();
 createSwapchains(info.systemId);
 createRenderer(device,settings);
 loadPlacement(settings);
 openInput(settings);
 loadRecentering(settings);
 int editKey=neo::parseKey(settings.text(L"EditKey",L"Tab"));
 editSensitivity_=settings.real(L"EditSensitivity",1,.1f,10.f);
 editOpacity_=settings.real(L"EditOpacity",.5f,.1f,1.f);
 enabled_=settings.flag(L"Enabled",false);allowPassthrough_=settings.flag(L"Passthrough",true);
 settingsStamp_=candidateStamp_=neo::fileStamp(folder/L"NeoXR.ini");calibrationText_=settings.text(L"Calibration");
 edit_.start(module,enabled_?editKey:0);
 if(passthroughEnabled)room_.init(nextGipa,instance,session_,[](const std::string& line){log(line);});
 log("D3D11 stereo renderer and input initialized; F8 calibrates, F9 toggles, EditKey places the wheel with the mouse");
}

void Overlay::createSwapchains(XrSystemId system){
 XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
 XR_CHECK(nextGetSystemProperties(instance,system,&props));
 maxLayers_=props.graphicsProperties.maxLayerCount;
 width_=std::min(maxEyeResolution,props.graphicsProperties.maxSwapchainImageWidth);
 height_=std::min(maxEyeResolution,props.graphicsProperties.maxSwapchainImageHeight);

 uint32_t count=0;
 XR_CHECK(nextEnumerateSwapchainFormats(session_,0,&count,nullptr));
 std::vector<int64_t> formats(count);
 XR_CHECK(nextEnumerateSwapchainFormats(session_,count,&count,formats.data()));
 if(std::find(formats.begin(),formats.end(),swapchainFormat)==formats.end())throw std::runtime_error("RGBA8 sRGB swapchain unsupported");

 for(auto& eye:eyes_){
  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;ci.format=swapchainFormat;ci.sampleCount=1;
  ci.width=width_;ci.height=height_;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
  XR_CHECK(nextCreateSwapchain(session_,&ci,eye.chain.put()));
  XR_CHECK(nextEnumerateSwapchainImages(eye.chain.get(),0,&count,nullptr));
  eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
  XR_CHECK(nextEnumerateSwapchainImages(eye.chain.get(),count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())));
 }
}

void Overlay::createRenderer(ID3D11Device* device,const neo::Config& settings){
 rig_.init(device,width_,height_,settings);
 log("Model watcher started; native FBX / OBJ / glTF import runs in the background");
}

void Overlay::loadPlacement(const neo::Config& settings){
 placement_.x=settings.real(L"X",0);placement_.y=settings.real(L"Y",-.3f);placement_.z=settings.real(L"Z",-.5f);
 placement_.tiltDegrees=settings.real(L"TiltDegrees",0);placement_.yawDegrees=settings.real(L"YawDegrees",0);
 placement_.width=settings.real(L"WidthMm",316)/1000.f;
 placement_=placement_.clamped();
}

void Overlay::openInput(const neo::Config& settings){
 wheel_.open(settings.integer(L"SteeringDevice",-1));
 int buttonDevice=settings.integer(L"ButtonDevice",-1);separateButtons_=buttonDevice>=0;
 buttons_.open(buttonDevice);
}

void Overlay::reloadSettings(){
 ULONGLONG now=GetTickCount64();if(now<nextSettingsCheck_)return;nextSettingsCheck_=now+300;
 auto stamp=neo::fileStamp(folder/L"NeoXR.ini");
 if(!stamp.exists||stamp==settingsStamp_)return;
 if(stamp!=candidateStamp_){candidateStamp_=stamp;return;}
 // Never replace an in-progress mouse alignment with file values.
 if(edit_.mode!=EditMode::Off)return;
 try{
  const auto settings=config();rig_.configure(settings);
  enabled_=settings.flag(L"Enabled",false);allowPassthrough_=settings.flag(L"Passthrough",true);
  if(!enabled_||!allowPassthrough_)room_.show(false);
  edit_.toggleKey=enabled_?neo::parseKey(settings.text(L"EditKey",L"Tab")):0;
  editSensitivity_=settings.real(L"EditSensitivity",1,.1f,10.f);editOpacity_=settings.real(L"EditOpacity",.5f,.1f,1.f);
  recenterKey_=neo::parseKey(settings.text(L"RecenterKey"));recenterButton_=settings.integer(L"RecenterButton",-1);
  int steering=settings.integer(L"SteeringDevice",-1),buttons=settings.integer(L"ButtonDevice",-1);
  if(steering!=wheel_.index)wheel_.open(steering);
  if(buttons!=buttons_.index)buttons_.open(buttons);
  separateButtons_=buttons>=0;
  const auto previousGeneral=carAligned_?generalPlacement_:placement_;
  useGeneralPlacement();loadPlacement(settings);
  auto calibration=settings.text(L"Calibration");
  if(calibration!=calibrationText_){
   calibrationText_=calibration;
   if(auto saved=neo::parseCalibration(calibration)){head_=*saved;hasSavedCalibration_=anchored_=true;}
   else hasSavedCalibration_=anchored_=false;
  }
  if(!car_.empty())if(auto saved=neo::parseAlignment(settings.section(L"Cars").text(car_))){
   if(neo::applyPlacementChanges(saved->placement,previousGeneral,placement_)){
    settings.section(L"Cars").write(car_,neo::formatAlignment(saved->placement,XMLoadFloat4x4(&saved->head)));
    log("Live placement changes applied to the current car alignment");
   }
   useCarAlignment(*saved);
  }
  settingsStamp_=neo::fileStamp(folder/L"NeoXR.ini");failed_=false;log("Settings reloaded live");
 }catch(const std::exception& ex){settingsStamp_=stamp;log(std::string("Settings reload failed: ")+ex.what());}
}

void Overlay::loadRecentering(const neo::Config& settings){
 recenterKey_=neo::parseKey(settings.text(L"RecenterKey"));
 recenterButton_=settings.integer(L"RecenterButton",-1);
 XrReferenceSpaceCreateInfo local{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
 local.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;local.poseInReferenceSpace.orientation.w=1;
 XR_CHECK(nextCreateReferenceSpace(session_,&local,levelSpace_.put()));
 if(auto saved=neo::parseCalibration(settings.text(L"Calibration"))){
  head_=*saved;hasSavedCalibration_=anchored_=true;log("Saved calibration loaded");
 }
}

XrResult Overlay::endFrame(const XrFrameEndInfo& frame){
 auto passThrough=[&]{return nextEndFrame(session_,&frame);};
 auto skip=[&](const char* reason){noteSkip(reason);return passThrough();};
 reloadSettings();
 std::string reloadMessage;if(rig_.pollModel(reloadMessage))failed_=false;
 if(!reloadMessage.empty())log(reloadMessage);
 if(!enabled_)return skip("disabled in the app");
 if(!rig_.ready)return skip("waiting for a valid wheel model");
 if(failed_)return passThrough();
 if(!stereo_)return skip("the session is not stereo");
 // iRacing never ends its session after the headset comes off, so shouldRender stays false for good
 // while it keeps submitting frames the runtime still shows; the wheel follows the game's layers instead.
 if(!frame.layerCount)return skip(shouldRender_?"the game submitted no layers":"the runtime asked for no rendering (headset off or app hidden)");
 if(!shouldRender_&&!loggedRenderAnyway_){log("Runtime asks for no rendering but the game keeps drawing; the wheel stays with the game");loggedRenderAnyway_=true;}
 if(frame.layerCount>=maxLayers_)return skip("the game uses every compositor layer");

 bool recenter=readRecenterAndToggleKeys();
 XrSpace space=findGameSpace(frame);
 if(!space)return skip("the game submitted no projection layer");
 trackGameSpace(space);
 logGameDepth(frame);
 followCar();
 applyNudges();
 applyEdit();
 if(!visible_)return skip("hidden with F9");

 try{
  pollInput();
  const WheelInput& buttons=buttonSource();
  recenter|=keys_.recenterButton.rising(buttons.valid&&buttonHeld(buttons.state,recenterButton_));

  auto views=locateViews(space,frame.displayTime);
  if(!views)return skip("head tracking is not valid");
  if(!anchored_||recenter)calibrate(*views,space,frame.displayTime,recenter);

  XMMATRIX model=wheelToGame();rig_.renderer.turn=rig_.steering(wheel_.state);
  auto poses=rig_.animate(buttons.valid?&buttons.state:nullptr,clock_.tick());
  std::array<XrCompositionLayerProjectionView,2> projected{};
  for(int eye=0;eye<2;eye++)projected[eye]=renderEye(eye,(*views)[eye],model,poses);
  noteSkip(nullptr);
  return submit(frame,space,projected);
 }catch(const std::exception& e){log(e.what());failed_=true;}
 catch(...){log("Render failed; overlay disabled");failed_=true;}
 return passThrough();
}

// Logs only changes, so a long pause is one line when it starts and one when the wheel returns.
void Overlay::noteSkip(const char* reason){
 if(reason==skipReason_)return;
 if(reason)logf("Wheel not drawn: %s",reason);else if(skipReason_)log("Wheel drawn again");
 skipReason_=reason;
}

// A lost device does not hide the wheel: it stays centred and is reopened every two seconds.
void Overlay::pollInput(){
 auto keep=[&](WheelInput& input,bool& lost,ULONGLONG& nextReopen,const char* name){
  if(input.poll()){if(lost){logf("%s input is back",name);lost=false;}return;}
  if(!lost){logf("%s input lost; the wheel stays centred while NeoXR reconnects",name);lost=true;}
  ULONGLONG now=GetTickCount64();
  if(now>=nextReopen){nextReopen=now+2000;input.reopen();}
 };
 keep(wheel_,wheelLost_,nextWheelReopen_,"Steering");
 if(separateButtons_)keep(buttons_,buttonsLost_,nextButtonsReopen_,"Button");
}

bool Overlay::readRecenterAndToggleKeys(){
 bool recenter=keys_.recenter.rising(keyHeld(VK_F8));
 if(keys_.toggle.rising(keyHeld(VK_F9)))visible_=!visible_;
 if(recenterKey_)recenter|=keys_.recenterKey.rising(keyHeld(recenterKey_));
 return recenter;
}

void Overlay::trackGameSpace(XrSpace space){
 if(space==gameSpace_)return;
 if(gameSpace_)log("Game switched reference space (game recenter); wheel follows the cockpit");
 gameSpace_=space;
}

// Ctrl+Shift+Up/Down moves the wheel 1 cm; Ctrl+Shift+PageUp/PageDown pitches it 1 degree.
void Overlay::applyNudges(){
 bool chord=keyHeld(VK_CONTROL)&&keyHeld(VK_SHIFT);
 auto pressed=[&](neo::EdgeTrigger& edge,int key){return edge.rising(chord&&keyHeld(key));};
 bool closer=pressed(keys_.closer,VK_DOWN),farther=pressed(keys_.farther,VK_UP);
 bool up=pressed(keys_.tiltUp,VK_PRIOR),down=pressed(keys_.tiltDown,VK_NEXT);
 if(!closer&&!farther&&!up&&!down)return;
 neo::Placement previous=placement_;
 if(closer||farther)placement_.z+=closer?nudgeMetres:-nudgeMetres;
 if(up||down)placement_.tiltDegrees+=up?nudgeDegrees:-nudgeDegrees;
 placement_=placement_.clamped();savePlacement(previous);
}

// Polls iRacing about once a second for the player's car and switches to its saved alignment.
void Overlay::followCar(){
 ULONGLONG now=GetTickCount64();
 if(now<nextCarCheck_||edit_.mode!=EditMode::Off)return;
 nextCarCheck_=now+1000;
 std::wstring car=telemetry_.car();
 if(car==car_)return;
 car_=car;
 auto saved=car.empty()?std::nullopt:neo::parseAlignment(config().section(L"Cars").text(car));
 if(saved){useCarAlignment(*saved);logf("Car %ls: using its saved alignment with the in-game wheel",car.c_str());return;}
 useGeneralPlacement();
 if(!car.empty())logf("Car %ls: no saved alignment; show the in-game wheel, line this one up with EditKey and save to keep it for this car",car.c_str());
}

void Overlay::useCarAlignment(const neo::CarAlignment& alignment){
 if(!carAligned_){generalPlacement_=placement_;generalHead_=head_;generalAnchored_=anchored_;}
 placement_=alignment.placement;XMStoreFloat4x4(&head_,XMLoadFloat4x4(&alignment.head));anchored_=carAligned_=true;
}
void Overlay::useGeneralPlacement(){
 if(!carAligned_)return;
 placement_=generalPlacement_;head_=generalHead_;anchored_=generalAnchored_;carAligned_=false;
}
void Overlay::forgetCarAlignment(){
 if(!carAligned_){log("Edit mode: this car has no saved alignment to forget");return;}
 config().section(L"Cars").erase(car_);useGeneralPlacement();
 logf("Car %ls: alignment forgotten; using the general placement",car_.c_str());
}

// Diagnostics: whether the game hands its depth buffer to the runtime, which hand occlusion would need.
void Overlay::logGameDepth(const XrFrameEndInfo& frame){
 if(depthLogged_)return;
 depthLogged_=true;
 for(uint32_t i=0;i<frame.layerCount;i++){
  if(!frame.layers[i]||frame.layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
  const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(frame.layers[i]);
  logf("Game layer %u: projection, %u views, flags=0x%llx",i,projection->viewCount,(unsigned long long)projection->layerFlags);
  for(auto* node=static_cast<const XrBaseInStructure*>(projection->viewCount?projection->views[0].next:nullptr);node;node=node->next){
   if(node->type!=XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR){logf("Game view extension struct type=%d",int(node->type));continue;}
   const auto* depth=reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(node);
   logf("Game submits depth: near=%.3f far=%.3f range=%.2f..%.2f size=%dx%d",depth->nearZ,depth->farZ,depth->minDepth,depth->maxDepth,
    depth->subImage.imageRect.extent.width,depth->subImage.imageRect.extent.height);
   return;
  }
 }
 log("Game does not submit depth with its frames");
}

// Consumes the mouse motion gathered by the edit-mode hooks since the last frame.
void Overlay::applyEdit(){
 int mode=edit_.mode;
 if(mode!=EditMode::Off&&previousEditMode_==EditMode::Off){
  placementBeforeEdit_=placement_;
  log("Edit mode: left-drag moves, right-drag sets distance, M switches to size/rotate, H shows the room through the cameras, Esc then Enter saves, Esc then Delete forgets this car's alignment");
 }
 neo::Drag drag{float(edit_.leftX.exchange(0)),float(edit_.leftY.exchange(0)),float(edit_.rightX.exchange(0)),
  float(edit_.rightY.exchange(0)),float(edit_.scroll.exchange(0))/WHEEL_DELTA};
 if(mode==EditMode::Move)placement_=neo::dragged(placement_,neo::DragMode::Move,drag,editSensitivity_);
 else if(mode==EditMode::Adjust)placement_=neo::dragged(placement_,neo::DragMode::Adjust,drag,editSensitivity_);
 switch(edit_.result.exchange(EditMode::None)){
  case EditMode::Save:savePlacement(placementBeforeEdit_);break;
  case EditMode::Cancel:placement_=placementBeforeEdit_;log("Edit mode: cancelled, placement restored");break;
  case EditMode::Forget:placement_=placementBeforeEdit_;forgetCarAlignment();break;
 }
 if(edit_.passthrough.exchange(false)&&mode!=EditMode::Off)showRoom(!room_.showing());
 if(mode==EditMode::Off&&room_.showing())showRoom(false);
 previousEditMode_=mode;rig_.renderer.editMode=mode;
 rig_.renderer.opacity=mode==EditMode::Off?1:editOpacity_;
}

// The cameras' view replaces the game while editing, so the wheel can be lined up with the real one.
void Overlay::showRoom(bool on){
 if(on&&!allowPassthrough_){log("Passthrough is disabled in the app");return;}
 if(on&&!passthroughEnabled){log("Passthrough: the runtime does not offer it; turn on Passthrough over Meta Quest Link in the Meta Horizon Link app's Beta settings");return;}
 if(!room_.show(on))return;
 log(on?"Passthrough on; the game is hidden until H or the end of edit mode":"Passthrough off");
}

std::optional<std::array<XrView,2>> Overlay::locateViews(XrSpace space,XrTime time){
 XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=stereoViews;info.displayTime=time;info.space=space;
 XrViewState state{XR_TYPE_VIEW_STATE};uint32_t count=0;
 std::array<XrView,2> views{XrView{XR_TYPE_VIEW},XrView{XR_TYPE_VIEW}};
 XR_CHECK(nextLocateViews(session_,&info,&state,2,&count,views.data()));
 if(count!=2||!hasAll<XrViewStateFlags>(state.viewStateFlags,XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))return std::nullopt;
 return views;
}

// Anchors the wheel to the head between the eyes, levelled against gravity. Only an explicit
// recenter is saved; the automatic first-frame placement is not.
void Overlay::calibrate(const std::array<XrView,2>& views,XrSpace space,XrTime time,bool save){
 XrPosef centre=views[0].pose;const auto& a=views[0].pose.position;const auto& b=views[1].pose.position;
 centre.position={(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2};
 XMMATRIX head=toMatrix(centre);
 XrSpaceLocation level{XR_TYPE_SPACE_LOCATION};
 if(XR_SUCCEEDED(nextLocateSpace(levelSpace_.get(),space,time,&level))&&
    hasAll<XrSpaceLocationFlags>(level.locationFlags,XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))
  head=neo::levelHead(head,toMatrix(level.pose));
 // A car's alignment is fixed to its cockpit, so recentering only changes the general calibration.
 if(carAligned_){XMStoreFloat4x4(&generalHead_,head);generalAnchored_=true;}
 else{XMStoreFloat4x4(&head_,head);anchored_=true;}
 if(save){
  config().write(L"Calibration",neo::formatCalibration(head));hasSavedCalibration_=true;
  log(carAligned_?"Calibration saved; this car keeps its alignment with the in-game wheel":"Calibration saved");
 }
}

XMMATRIX Overlay::wheelToGame() const{
 float scale=placement_.width/rig_.renderer.modelWidth;
 return XMMatrixScaling(scale,scale,scale)*XMMatrixRotationZ(-rig_.steering(wheel_.state))*placement_.relativeToHead()*XMLoadFloat4x4(&head_);
}

XrCompositionLayerProjectionView Overlay::renderEye(int eye,const XrView& view,FXMMATRIX model,const std::array<neo::ControlPose,neo::controlCount>& poses){
 AcquiredImage image(eyes_[eye]);
 XMMATRIX mvp=model*XMMatrixInverse(nullptr,toMatrix(view.pose))*projection(view.fov);
 // The shader lights in model space, so it needs the eye position there too.
 const auto& p=view.pose.position;XMFLOAT3 cameraInModel;
 XMStoreFloat3(&cameraInModel,XMVector3TransformCoord(XMVectorSet(p.x,p.y,p.z,1),XMMatrixInverse(nullptr,model)));
 rig_.renderer.draw(image.texture(),width_,height_,mvp,poses,cameraInModel,rig_.brightness);
 image.release();
 XrCompositionLayerProjectionView out{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
 out.pose=view.pose;out.fov=view.fov;out.subImage.swapchain=eyes_[eye].chain.get();
 out.subImage.imageRect.extent={int32_t(width_),int32_t(height_)};
 return out;
}

XrResult Overlay::submit(const XrFrameEndInfo& frame,XrSpace space,const std::array<XrCompositionLayerProjectionView,2>& views){
 XrCompositionLayerProjection wheel{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
 wheel.space=space;wheel.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;wheel.viewCount=2;wheel.views=views.data();
 std::vector<const XrCompositionLayerBaseHeader*> layers(frame.layers,frame.layers+frame.layerCount);
 // Link composites passthrough reliably only as the bottom layer, so the room replaces the game's layers.
 if(auto* room=room_.layer())layers={room};
 layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&wheel));
 XrFrameEndInfo withWheel=frame;withWheel.layerCount=uint32_t(layers.size());withWheel.layers=layers.data();
 // Never retry xrEndFrame: even a failed call can have consumed the frame.
 XrResult result=nextEndFrame(session_,&withWheel);
 if(XR_FAILED(result)){log("Compositor rejected frame; disabling overlay for this session");failed_=true;}
 return result;
}

void Overlay::saveSetting(const wchar_t* key,const wchar_t* format,float value){
 wchar_t text[32];swprintf_s(text,format,value);
 config().write(key,text);logf("%ls=%ls",key,text);
}

// With a known car the placement is saved as that car's alignment, together with the calibration it
// was made against; `previous` stays the general placement. Without iRacing it is the general one.
void Overlay::savePlacement(const neo::Placement& previous){
 if(!car_.empty()){
  if(!carAligned_){generalPlacement_=previous;generalHead_=head_;generalAnchored_=anchored_;carAligned_=true;}
  config().section(L"Cars").write(car_,neo::formatAlignment(placement_,XMLoadFloat4x4(&head_)));
  logf("Car %ls: alignment saved",car_.c_str());
  return;
 }
 saveSetting(L"X",L"%.3f",placement_.x);saveSetting(L"Y",L"%.3f",placement_.y);saveSetting(L"Z",L"%.3f",placement_.z);
 saveSetting(L"TiltDegrees",L"%.1f",placement_.tiltDegrees);saveSetting(L"YawDegrees",L"%.1f",placement_.yawDegrees);
 saveSetting(L"WidthMm",L"%.0f",placement_.width*1000);
}

// ---------------------------------------------------------------------------------------------------
// OpenXR hooks: thin adapters that route calls for our session to the Overlay
// ---------------------------------------------------------------------------------------------------

std::unique_ptr<Overlay> overlay;
Overlay* overlayFor(XrSession session){return overlay&&overlay->session()==session?overlay.get():nullptr;}

XrResult XRAPI_CALL createSession(XrInstance i,const XrSessionCreateInfo* ci,XrSession* out){
 std::lock_guard<std::recursive_mutex> guard(lock);
 XrResult result=nextCreateSession(i,ci,out);
 if(!targetProcess)return result;
 if(XR_FAILED(result)){logf("Game's xrCreateSession failed, XrResult=%d",int(result));return result;}
 log("Game created a session");
 if(overlay){log("Game created a new session before ending the old one; moving the wheel to the new one");overlay.reset();}
 {
  try{overlay=std::make_unique<Overlay>(*out,*ci);overlayInstance=i;}
  catch(const std::exception& e){log(e.what());}
  catch(...){log("Initialization failed");}
 }
 return result;
}
XrResult XRAPI_CALL destroySession(XrSession session){
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(overlayFor(session)){overlay.reset();log("Game ended its session; wheel removed");}
 return nextDestroySession(session);
}
XrResult XRAPI_CALL destroyInstance(XrInstance i){
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(targetProcess)log(i==instance?"Game closed its OpenXR instance":"Game closed an older OpenXR instance");
 if(i==overlayInstance){overlay.reset();overlayInstance=XR_NULL_HANDLE;}
 XrResult result=nextDestroyInstance(i);
 if(i==instance)instance=XR_NULL_HANDLE;
 return result;
}
XrResult XRAPI_CALL beginSession(XrSession session,const XrSessionBeginInfo* info){
 XrResult result=nextBeginSession(session,info);
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(auto* o=overlayFor(session))o->begin(*info);
 return result;
}
XrResult XRAPI_CALL waitFrame(XrSession session,const XrFrameWaitInfo* wait,XrFrameState* frame){
 XrResult result=nextWaitFrame(session,wait,frame);
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(auto* o=overlayFor(session))o->waited(XR_SUCCEEDED(result)&&frame->shouldRender);
 return result;
}
XrResult XRAPI_CALL endFrame(XrSession session,const XrFrameEndInfo* frame){
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(auto* o=overlayFor(session))return o->endFrame(*frame);
 return nextEndFrame(session,frame);
}
// Diagnostics only: session state changes (headset off and on) and runtime recentering.
XrResult XRAPI_CALL pollEvent(XrInstance i,XrEventDataBuffer* event){
 XrResult result=nextPollEvent(i,event);
 if(result!=XR_SUCCESS||!targetProcess||!event)return result;
 if(event->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
  logf("Session state %d",int(reinterpret_cast<const XrEventDataSessionStateChanged*>(event)->state));
 else if(event->type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
  const auto* change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(event);
  const auto& p=change->poseInPreviousSpace.position;
  logf("Runtime recentering reference space type=%d, pose valid=%d, moved=(%.3f,%.3f,%.3f)",int(change->referenceSpaceType),
   int(change->poseValid),p.x,p.y,p.z);
 }
 return result;
}

// Diagnostics only: shows in NeoXR.log how the game sets up and recenters its tracking spaces.
XrResult XRAPI_CALL createReferenceSpace(XrSession session,const XrReferenceSpaceCreateInfo* ci,XrSpace* out){
 XrResult result=nextCreateReferenceSpace(session,ci,out);
 static int logged=0;
 if(targetProcess&&ci&&XR_SUCCEEDED(result)&&logged++<maxLoggedSpaces){
  const auto& p=ci->poseInReferenceSpace;
  logf("Game created reference space type=%d offset=(%.3f,%.3f,%.3f) rotation=(%.3f,%.3f,%.3f,%.3f)",int(ci->referenceSpaceType),
   p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w);
 }
 return result;
}

XrResult XRAPI_CALL gipa(XrInstance i,const char* name,PFN_xrVoidFunction* function){
 if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;
 #define HOOK(n,fn) if(std::strcmp(name,"xr" #n)==0){*function=reinterpret_cast<PFN_xrVoidFunction>(fn);return XR_SUCCESS;}
 HOOK(GetInstanceProcAddr,gipa) HOOK(CreateSession,createSession) HOOK(DestroySession,destroySession)
 HOOK(DestroyInstance,destroyInstance) HOOK(EndFrame,endFrame) HOOK(WaitFrame,waitFrame) HOOK(BeginSession,beginSession)
 HOOK(CreateReferenceSpace,createReferenceSpace) HOOK(PollEvent,pollEvent)
 #undef HOOK
 return nextGipa(i,name,function);
}

std::vector<std::string> runtimeExtensions(PFN_xrGetInstanceProcAddr next){
 PFN_xrEnumerateInstanceExtensionProperties enumerate=nullptr;
 if(XR_FAILED(next(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)))||!enumerate)return {};
 uint32_t count=0;
 if(XR_FAILED(enumerate(nullptr,0,&count,nullptr)))return {};
 std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
 if(XR_FAILED(enumerate(nullptr,count,&count,extensions.data())))return {};
 std::vector<std::string> names;
 for(uint32_t e=0;e<count;e++)names.push_back(extensions[e].extensionName);
 return names;
}

bool contains(const std::vector<std::string>& names,const char* name){return std::find(names.begin(),names.end(),name)!=names.end();}

XrResult XRAPI_CALL createInstance(const XrInstanceCreateInfo* ci,const XrApiLayerCreateInfo* info,XrInstance* out){
 if(!info||!info->nextInfo||!ci)return XR_ERROR_INITIALIZATION_FAILED;
 std::lock_guard<std::recursive_mutex> guard(lock);
 wchar_t path[MAX_PATH]={};GetModuleFileNameW(module,path,MAX_PATH);folder=std::filesystem::path(path).parent_path();
 wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);
 std::wstring allowed=config().text(L"Process",L"iRacingSim64DX11.exe");
 targetProcess=_wcsicmp(std::filesystem::path(exe).filename().c_str(),allowed.c_str())==0;
 auto* next=info->nextInfo;
 auto chained=*info;chained.nextInfo=next->next;
 std::vector<std::string> offered;
 if(targetProcess){
  log("Layer loaded in configured process (NeoXR " NEOXR_VERSION ")");
  for(uint32_t e=0;e<ci->enabledExtensionCount;e++)logf("Game enabled extension %s",ci->enabledExtensionNames[e]);
  offered=runtimeExtensions(next->nextGetInstanceProcAddr);
  std::string line="Runtime offers "+std::to_string(offered.size())+" extensions:";
  for(const auto& name:offered)line+=" "+name;
  log(line);
 }

 // Passthrough needs an extension the game does not enable, so NeoXR adds it to its instance.
 XrInstanceCreateInfo withPassthrough=*ci;std::vector<const char*> names(ci->enabledExtensionNames,ci->enabledExtensionNames+ci->enabledExtensionCount);
 passthroughEnabled=false;
 if(targetProcess){
  passthroughEnabled=contains(offered,neo::Passthrough::extension);
  if(!passthroughEnabled)log("Passthrough: the runtime does not offer it");
  else if(std::none_of(names.begin(),names.end(),[](const char* n){return std::strcmp(n,neo::Passthrough::extension)==0;}))names.push_back(neo::Passthrough::extension);
  withPassthrough.enabledExtensionCount=uint32_t(names.size());withPassthrough.enabledExtensionNames=names.data();
 }
 bool extended=withPassthrough.enabledExtensionCount>ci->enabledExtensionCount;

 // A game may restart OpenXR (e.g. after the headset slept) before destroying its old instance.
 // Refusing would fail the game's whole OpenXR start, so NeoXR follows the newest instance; the
 // next functions dispatch on the handle, so the old one keeps working until it is destroyed.
 bool replacing=instance!=XR_NULL_HANDLE;
 XrResult result=next->nextCreateApiLayerInstance(extended?&withPassthrough:ci,&chained,out);
 if(XR_FAILED(result)&&extended){
  logf("The runtime refused the passthrough extension, XrResult=%d; starting without it",int(result));
  passthroughEnabled=false;result=next->nextCreateApiLayerInstance(ci,&chained,out);
 }
 if(XR_FAILED(result)){if(targetProcess)logf("Game's xrCreateInstance failed further down the chain, XrResult=%d",int(result));return result;}
 if(extended&&passthroughEnabled)log("Passthrough enabled; H in edit mode shows the room");
 nextGipa=next->nextGetInstanceProcAddr;instance=*out;
 if(replacing&&targetProcess)log("Game created a new OpenXR instance while the old one is still open; following the new one");
 #define LOAD_NEXT(name) nextGipa(instance,"xr" #name,reinterpret_cast<PFN_xrVoidFunction*>(&next##name));
 FN_LIST(LOAD_NEXT)
 #undef LOAD_NEXT
 return result;
}

}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char*,XrNegotiateApiLayerRequest* request){
 bool compatible=loader&&request&&
  loader->structType==XR_LOADER_INTERFACE_STRUCT_LOADER_INFO&&loader->structVersion==XR_LOADER_INFO_STRUCT_VERSION&&loader->structSize==sizeof(*loader)&&
  request->structType==XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST&&request->structVersion==XR_API_LAYER_INFO_STRUCT_VERSION&&request->structSize==sizeof(*request)&&
  loader->minInterfaceVersion<=1&&loader->maxInterfaceVersion>=1&&
  loader->maxApiVersion>=XR_MAKE_VERSION(1,0,0)&&loader->minApiVersion<=XR_MAKE_VERSION(1,0,34);
 if(!compatible)return XR_ERROR_INITIALIZATION_FAILED;
 request->layerInterfaceVersion=1;request->layerApiVersion=XR_MAKE_VERSION(1,0,34);
 request->getInstanceProcAddr=gipa;request->createApiLayerInstance=createInstance;
 return XR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE handle,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){module=handle;DisableThreadLibraryCalls(handle);}
 return TRUE;
}
