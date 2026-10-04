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
XrInstance instance=XR_NULL_HANDLE;

#define FN_LIST(X) X(CreateSession) X(DestroySession) X(DestroyInstance) X(EndFrame) X(WaitFrame) X(BeginSession) \
 X(EnumerateSwapchainFormats) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
 X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) \
 X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) X(GetSystemProperties)
#define DECLARE_NEXT(name) PFN_xr##name next##name=nullptr;
FN_LIST(DECLARE_NEXT)

neo::Config config(){return neo::Config(folder);}

void log(const std::string& line){
 OutputDebugStringA(("NeoXR: "+line+"\n").c_str());
 try{std::ofstream(folder/L"NeoXR.log",std::ios::app)<<line<<'\n';}catch(...){}
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
constexpr uint32_t maxEyeResolution=1024;
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

 // One frame, in order.
 bool readRecenterAndToggleKeys();
 void trackGameSpace(XrSpace space);
 void applyNudges();
 void applyEdit();
 std::optional<std::array<XrView,2>> locateViews(XrSpace space,XrTime time);
 void calibrate(const std::array<XrView,2>& views,XrSpace space,XrTime time,bool save);
 XMMATRIX wheelToGame() const;
 XrCompositionLayerProjectionView renderEye(int eye,const XrView& view,FXMMATRIX model,const std::array<neo::ControlPose,neo::controlCount>& poses);
 XrResult submit(const XrFrameEndInfo& frame,XrSpace space,const std::array<XrCompositionLayerProjectionView,2>& views);

 void saveSetting(const wchar_t* key,const wchar_t* format,float value);
 void savePlacement();
 const WheelInput& buttonSource() const{return separateButtons_?buttons_:wheel_;}

 XrSession session_;
 std::array<Eye,2> eyes_;
 Owned<XrSpace> levelSpace_{nextDestroySpace};  // gravity-aligned reference for levelling calibration
 XrSpace gameSpace_=XR_NULL_HANDLE;
 UINT width_=maxEyeResolution,height_=maxEyeResolution;
 uint32_t maxLayers_=0;
 bool stereo_=false,shouldRender_=false,failed_=false,visible_=true;

 neo::WheelRig rig_;
 WheelInput wheel_,buttons_;
 bool separateButtons_=false;
 FrameClock clock_;

 neo::Placement placement_,placementBeforeEdit_;
 XMFLOAT4X4 head_{};
 bool anchored_=false,hasSavedCalibration_=false;

 struct Hotkeys {neo::EdgeTrigger recenter,toggle,recenterKey,recenterButton,closer,farther,tiltUp,tiltDown;} keys_;
 int recenterKey_=0,recenterButton_=-1;

 EditMode edit_;
 int previousEditMode_=EditMode::Off;
 float editSensitivity_=1;
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
 if(editKey)edit_.start(module,editKey);
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
 log(rig_.renderer.detailed?"Custom wheel loaded: 23 parts with baked material textures":"Procedural placeholder selected");
}

void Overlay::loadPlacement(const neo::Config& settings){
 placement_.x=settings.real(L"X",0);placement_.y=settings.real(L"Y",-.3f);placement_.z=settings.real(L"Z",-.5f);
 placement_.tiltDegrees=settings.real(L"TiltDegrees",0);placement_.yawDegrees=settings.real(L"YawDegrees",0);
 placement_.width=settings.real(L"WidthMm",310)/1000.f;
 placement_=placement_.clamped();
}

void Overlay::openInput(const neo::Config& settings){
 if(!wheel_.open(settings.integer(L"SteeringDevice",-1)))throw std::runtime_error("Configure SteeringDevice using NeoXR-Input first");
 int buttonDevice=settings.integer(L"ButtonDevice",-1);separateButtons_=buttonDevice>=0;
 if(separateButtons_&&!buttons_.open(buttonDevice))throw std::runtime_error("ButtonDevice cannot be opened");
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
 if(failed_||!stereo_||!shouldRender_||!frame.layerCount||frame.layerCount>=maxLayers_)return passThrough();

 bool recenter=readRecenterAndToggleKeys();
 XrSpace space=findGameSpace(frame);
 if(!space)return passThrough();
 trackGameSpace(space);
 applyNudges();
 applyEdit();
 if(!visible_)return passThrough();

 try{
  if(!wheel_.poll())return passThrough();
  if(separateButtons_)buttons_.poll();
  const WheelInput& buttons=buttonSource();
  recenter|=keys_.recenterButton.rising(buttons.valid&&buttonHeld(buttons.state,recenterButton_));

  auto views=locateViews(space,frame.displayTime);
  if(!views)return passThrough();
  if(!anchored_||recenter)calibrate(*views,space,frame.displayTime,recenter);

  XMMATRIX model=wheelToGame();
  auto poses=rig_.animate(buttons.valid?&buttons.state:nullptr,clock_.tick());
  std::array<XrCompositionLayerProjectionView,2> projected{};
  for(int eye=0;eye<2;eye++)projected[eye]=renderEye(eye,(*views)[eye],model,poses);
  return submit(frame,space,projected);
 }catch(const std::exception& e){log(e.what());failed_=true;}
 catch(...){log("Render failed; overlay disabled");failed_=true;}
 return passThrough();
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
 if(closer||farther){
  placement_.z+=closer?nudgeMetres:-nudgeMetres;placement_=placement_.clamped();
  saveSetting(L"Z",L"%.2f",placement_.z);
 }
 bool up=pressed(keys_.tiltUp,VK_PRIOR),down=pressed(keys_.tiltDown,VK_NEXT);
 if(up||down){
  placement_.tiltDegrees+=up?nudgeDegrees:-nudgeDegrees;placement_=placement_.clamped();
  saveSetting(L"TiltDegrees",L"%.0f",placement_.tiltDegrees);
 }
}

// Consumes the mouse motion gathered by the edit-mode hooks since the last frame.
void Overlay::applyEdit(){
 int mode=edit_.mode;
 if(mode!=EditMode::Off&&previousEditMode_==EditMode::Off){
  placementBeforeEdit_=placement_;
  log("Edit mode: left-drag moves, right-drag sets distance, M switches to size/rotate, Esc then Enter saves");
 }
 neo::Drag drag{float(edit_.leftX.exchange(0)),float(edit_.leftY.exchange(0)),float(edit_.rightX.exchange(0)),
  float(edit_.rightY.exchange(0)),float(edit_.scroll.exchange(0))/WHEEL_DELTA};
 if(mode==EditMode::Move)placement_=neo::dragged(placement_,neo::DragMode::Move,drag,editSensitivity_);
 else if(mode==EditMode::Adjust)placement_=neo::dragged(placement_,neo::DragMode::Adjust,drag,editSensitivity_);
 switch(edit_.result.exchange(EditMode::None)){
  case EditMode::Save:savePlacement();log("Edit mode: placement saved");break;
  case EditMode::Cancel:placement_=placementBeforeEdit_;log("Edit mode: cancelled, placement restored");break;
 }
 previousEditMode_=mode;rig_.renderer.editMode=mode;
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
 XMStoreFloat4x4(&head_,head);anchored_=true;
 if(save){config().write(L"Calibration",neo::formatCalibration(head));hasSavedCalibration_=true;log("Calibration saved");}
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

void Overlay::savePlacement(){
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
 if(XR_SUCCEEDED(result)&&targetProcess&&!overlay&&config().flag(L"Enabled",false)){
  try{overlay=std::make_unique<Overlay>(*out,*ci);}
  catch(const std::exception& e){log(e.what());}
  catch(...){log("Initialization failed");}
 }
 return result;
}
XrResult XRAPI_CALL destroySession(XrSession session){
 std::lock_guard<std::recursive_mutex> guard(lock);
 if(overlayFor(session))overlay.reset();
 return nextDestroySession(session);
}
XrResult XRAPI_CALL destroyInstance(XrInstance i){
 std::lock_guard<std::recursive_mutex> guard(lock);
 overlay.reset();XrResult result=nextDestroyInstance(i);instance=XR_NULL_HANDLE;
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
 HOOK(CreateReferenceSpace,createReferenceSpace)
 #undef HOOK
 return nextGipa(i,name,function);
}

XrResult XRAPI_CALL createInstance(const XrInstanceCreateInfo* ci,const XrApiLayerCreateInfo* info,XrInstance* out){
 if(!info||!info->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
 if(instance!=XR_NULL_HANDLE)return XR_ERROR_LIMIT_REACHED;
 auto* next=info->nextInfo;nextGipa=next->nextGetInstanceProcAddr;
 auto chained=*info;chained.nextInfo=next->next;
 XrResult result=next->nextCreateApiLayerInstance(ci,&chained,out);
 if(XR_FAILED(result))return result;
 instance=*out;
 #define LOAD_NEXT(name) nextGipa(instance,"xr" #name,reinterpret_cast<PFN_xrVoidFunction*>(&next##name));
 FN_LIST(LOAD_NEXT)
 #undef LOAD_NEXT
 wchar_t path[MAX_PATH]={};GetModuleFileNameW(module,path,MAX_PATH);folder=std::filesystem::path(path).parent_path();
 wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);
 std::wstring allowed=config().text(L"Process",L"iRacingSim64DX11.exe");
 targetProcess=_wcsicmp(std::filesystem::path(exe).filename().c_str(),allowed.c_str())==0;
 if(targetProcess)log("Layer loaded in configured process (NeoXR " NEOXR_VERSION ")");
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
