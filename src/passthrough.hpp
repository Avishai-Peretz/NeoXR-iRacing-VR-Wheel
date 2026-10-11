#pragma once
#include <openxr/openxr.h>
#include <functional>
#include <string>

namespace neo {

// The headset cameras' view of the room (XR_FB_passthrough), shown while editing so the wheel can be
// lined up with the real one. Nothing is created until first shown, and a refusal turns it off for good.
class Passthrough {
public:
 static constexpr const char* extension=XR_FB_PASSTHROUGH_EXTENSION_NAME;
 using Log=std::function<void(const std::string&)>;
 void init(PFN_xrGetInstanceProcAddr gipa,XrInstance instance,XrSession session,Log log){
  session_=session;log_=std::move(log);
  auto load=[&](const char* name,auto& fn){
   if(XR_FAILED(gipa(instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&fn))))fn=nullptr;
  };
  load("xrCreatePassthroughFB",create_);load("xrDestroyPassthroughFB",destroy_);
  load("xrPassthroughStartFB",start_);load("xrPassthroughPauseFB",pause_);
  load("xrCreatePassthroughLayerFB",createLayer_);load("xrDestroyPassthroughLayerFB",destroyLayer_);
  load("xrPassthroughLayerResumeFB",resumeLayer_);load("xrPassthroughLayerPauseFB",pauseLayer_);
  usable_=create_&&destroy_&&start_&&pause_&&createLayer_&&destroyLayer_&&resumeLayer_&&pauseLayer_;
 }
 Passthrough()=default;Passthrough(const Passthrough&)=delete;Passthrough& operator=(const Passthrough&)=delete;
 ~Passthrough(){
  if(layer_)destroyLayer_(layer_);
  if(passthrough_)destroy_(passthrough_);
 }

 bool showing() const{return showing_;}
 // Shows or hides the room; false if it cannot be shown.
 bool show(bool on){
  if(on==showing_)return true;
  if(!on){pauseLayer_(layer_);pause_(passthrough_);showing_=false;return true;}
  if(!usable_)return false;
  if(!passthrough_){
   XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
   if(XrResult r=create_(session_,&info,&passthrough_);XR_FAILED(r))return fail("xrCreatePassthroughFB",r);
   XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
   layer.passthrough=passthrough_;layer.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
   if(XrResult r=createLayer_(session_,&layer,&layer_);XR_FAILED(r))return fail("xrCreatePassthroughLayerFB",r);
  }
  if(XrResult r=start_(passthrough_);XR_FAILED(r))return fail("xrPassthroughStartFB",r);
  if(XrResult r=resumeLayer_(layer_);XR_FAILED(r)){pause_(passthrough_);return fail("xrPassthroughLayerResumeFB",r);}
  showing_=true;return true;
 }
 // The composition layer that shows the room, or null while it is hidden.
 const XrCompositionLayerBaseHeader* layer(){
  if(!showing_)return nullptr;
  composition_={XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
  composition_.flags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;composition_.layerHandle=layer_;
  return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&composition_);
 }

private:
 bool fail(const char* call,XrResult result){
  usable_=false;
  if(log_)log_(std::string("Passthrough: ")+call+" failed, XrResult="+std::to_string(result));
  return false;
 }

 XrSession session_=XR_NULL_HANDLE;Log log_;
 PFN_xrCreatePassthroughFB create_=nullptr;PFN_xrDestroyPassthroughFB destroy_=nullptr;
 PFN_xrPassthroughStartFB start_=nullptr;PFN_xrPassthroughPauseFB pause_=nullptr;
 PFN_xrCreatePassthroughLayerFB createLayer_=nullptr;PFN_xrDestroyPassthroughLayerFB destroyLayer_=nullptr;
 PFN_xrPassthroughLayerResumeFB resumeLayer_=nullptr;PFN_xrPassthroughLayerPauseFB pauseLayer_=nullptr;
 bool usable_=false,showing_=false;
 XrPassthroughFB passthrough_=XR_NULL_HANDLE;XrPassthroughLayerFB layer_=XR_NULL_HANDLE;
 XrCompositionLayerPassthroughFB composition_{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
};

}
