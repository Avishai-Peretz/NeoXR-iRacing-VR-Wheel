#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include "click.hpp"
#include "config.hpp"
#include "controls.hpp"
#include "render.hpp"
#include "model_watch.hpp"

namespace neo {

// Everything that turns controller input into an animated, rendered wheel. The VR layer and the
// setup wizard's preview both use it, so the preview shows exactly what the headset will.
class WheelRig {
public:
 // Loads the model selected in NeoXR.ini, creates its GPU resources and applies the other settings.
 void init(ID3D11Device* device,UINT width,UINT height,const Config& settings){
  device_=device;width_=width;height_=height;
  renderer.init(device,width,height);
  configure(settings);
 }

 // Commit only complete GPU resources; import or upload failures retain the last good renderer.
 bool pollModel(std::string& message){
  std::string error;auto model=watch_.take(error);
  if(!error.empty())message="Model reload failed; keeping last working wheel: "+error;
  if(!model)return false;
  try{
   Renderer next;next.init(device_.Get(),width_,height_,model.get());
   next.background=renderer.background;next.buttonTravel=renderer.buttonTravel;next.paddleAngle=renderer.paddleAngle;
   next.clutchAngle=renderer.clutchAngle;next.stickAngle=renderer.stickAngle;next.flashStrength=renderer.flashStrength;
   next.opacity=renderer.opacity;next.editMode=renderer.editMode;
   renderer=std::move(next);ready=true;
   for(int c=firstKnob;c<firstStick;c++)animator_.recentre[c]=std::abs(renderer.axes[c][2])<.9f;
   message="Wheel model loaded/reloaded ("+std::to_string(model->parts.size())+" parts)";return true;
  }catch(const std::exception& ex){message=std::string("Model GPU upload failed; keeping last working wheel: ")+ex.what();return false;}
 }
 void resize(UINT width,UINT height){renderer.resize(width,height);width_=width;height_=height;}

 // Everything that can change without reloading the model.
 void configure(const Config& settings){
  std::wstring mode=settings.text(L"Model",L"custom");
  auto source=_wcsicmp(mode.c_str(),L"placeholder")==0?std::filesystem::path{}:settings.path(L"ModelFile",L"assets/neo-x.gltf");
  if(source!=source_||!selected_){
   source_=source;selected_=true;watch_.select(source);
   if(source.empty()){Renderer next;next.init(device_.Get(),width_,height_);next.background=renderer.background;renderer=std::move(next);ready=true;}
  }
  renderer.buttonTravel=settings.real(L"ButtonTravelMm",1.2f,0.f,5.f)/1000.f;
  renderer.paddleAngle=settings.radians(L"PaddleAngleDegrees",7,0.f,25.f);
  renderer.clutchAngle=settings.radians(L"ClutchAngleDegrees",12,0.f,40.f);
  renderer.stickAngle=settings.radians(L"StickAngleDegrees",12,0.f,30.f);
  renderer.flashStrength=settings.real(L"FlashStrength",.75f,0.f,1.f);
  brightness=settings.real(L"Brightness",.8f,.1f,2.f);
  steeringAxis_=settings.integer(L"SteeringAxis",0);invertSteering_=settings.flag(L"Invert",false);
  rotationDegrees_=settings.real(L"RotationDegrees",900,90.f,2520.f);
  steeringZeroOffset_=settings.real(L"SteeringZeroOffsetDegrees",0,-1260.f,1260.f);
  mapping_.load(settings);
  knobStep_=settings.radians(L"KnobStepDegrees",15,0.f,90.f);
  // Thumb rollers turn about an axle across the wheel face; they nudge per detent and settle back.
  for(int c=firstKnob;c<firstStick;c++)animator_.recentre[c]=std::abs(renderer.axes[c][2])<.9f;
  click_.init(settings.real(L"ClickVolume",.5f,0.f,1.f),settings.path(L"ClickSoundFile"));
 }

 // Wheel rotation in radians for the configured steering axis.
 float steering(const DIJOYSTATE2& wheel) const{return neo::steering(axisValue(wheel,steeringAxis_),rotationDegrees_,invertSteering_,steeringZeroOffset_);}

 // Advances the control animations by `dt` seconds and plays the click they produce, if any.
 std::array<ControlPose,controlCount> animate(const DIJOYSTATE2* buttons,float dt){
  Click clicked=animator_.update(mapping_.sample(buttons),dt,knobStep_);
  if(clicked!=Click::None)click_.play(clicked);
  std::array<ControlPose,controlCount> poses;
  for(int i=0;i<controlCount;i++)poses[i]=animator_.pose(i);
  return poses;
 }

 Renderer renderer;
 float brightness=.8f;bool ready=false;

private:
 int steeringAxis_=0;bool invertSteering_=false;float rotationDegrees_=900,steeringZeroOffset_=0;
 float knobStep_=.26f;
 ControlMapping mapping_;
 Animator animator_;
 ClickSound click_;
 ComPtr<ID3D11Device> device_;UINT width_=1,height_=1;
 std::filesystem::path source_;bool selected_=false;
 ModelWatch watch_;
};

}
