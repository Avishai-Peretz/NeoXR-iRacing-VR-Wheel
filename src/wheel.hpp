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

namespace neo {

// Everything that turns controller input into an animated, rendered wheel. The VR layer and the
// setup wizard's preview both use it, so the preview shows exactly what the headset will.
class WheelRig {
public:
 // Loads the model selected in NeoXR.ini, creates its GPU resources and applies the other settings.
 void init(ID3D11Device* device,UINT width,UINT height,const Config& settings){
  std::wstring model=settings.text(L"Model",L"custom");
  std::filesystem::path assets;  // empty selects the procedural placeholder
  if(_wcsicmp(model.c_str(),L"custom")==0)assets=settings.path(L"AssetDirectory",L"assets");
  else if(_wcsicmp(model.c_str(),L"placeholder")!=0)throw std::runtime_error("Model must be custom or placeholder");
  renderer.init(device,width,height,assets);
  configure(settings);
 }

 // Everything that can change without reloading the model.
 void configure(const Config& settings){
  renderer.buttonTravel=settings.real(L"ButtonTravelMm",1.2f,0.f,5.f)/1000.f;
  renderer.paddleAngle=settings.radians(L"PaddleAngleDegrees",7,0.f,25.f);
  renderer.clutchAngle=settings.radians(L"ClutchAngleDegrees",12,0.f,40.f);
  renderer.stickAngle=settings.radians(L"StickAngleDegrees",12,0.f,30.f);
  renderer.flashStrength=settings.real(L"FlashStrength",.75f,0.f,1.f);
  brightness=settings.real(L"Brightness",.8f,.1f,2.f);
  steeringAxis_=settings.integer(L"SteeringAxis",0);invertSteering_=settings.flag(L"Invert",false);
  rotationDegrees_=settings.real(L"RotationDegrees",900,90.f,2520.f);
  mapping_.load(settings);
  knobStep_=settings.radians(L"KnobStepDegrees",15,0.f,90.f);
  // Thumb rollers turn about an axle across the wheel face; they nudge per detent and settle back.
  for(int c=firstKnob;c<firstStick;c++)animator_.recentre[c]=std::abs(renderer.axes[c][2])<.9f;
  click_.init(settings.real(L"ClickVolume",.5f,0.f,1.f),settings.path(L"ClickSoundFile"));
 }

 // Wheel rotation in radians for the configured steering axis.
 float steering(const DIJOYSTATE2& wheel) const{return neo::steering(axisValue(wheel,steeringAxis_),rotationDegrees_,invertSteering_);}

 // Advances the control animations by `dt` seconds and plays the click they produce, if any.
 std::array<ControlPose,controlCount> animate(const DIJOYSTATE2* buttons,float dt){
  Click clicked=animator_.update(mapping_.sample(buttons),dt,knobStep_);
  if(clicked!=Click::None)click_.play(clicked);
  std::array<ControlPose,controlCount> poses;
  for(int i=0;i<controlCount;i++)poses[i]=animator_.pose(i);
  return poses;
 }

 Renderer renderer;
 float brightness=.8f;

private:
 int steeringAxis_=0;bool invertSteering_=false;float rotationDegrees_=900;
 float knobStep_=.26f;
 ControlMapping mapping_;
 Animator animator_;
 ClickSound click_;
};

}
