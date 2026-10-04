#pragma once
#include <array>
#include <cmath>
#include <string>
#include "animation.hpp"
#include "config.hpp"
#include "input.hpp"

namespace neo {

// Turns a held signal into one event per press. Every hotkey, knob detent and recenter button uses it.
class EdgeTrigger {
public:
 bool rising(bool held){bool hit=held&&!held_;held_=held;return hit;}
private:
 bool held_=false;
};

// Maps DirectInput buttons, axes and POV hats to the wheel's animated controls, as set in NeoXR.ini.
// Button and axis numbers are DirectInput indices (SimPro's numbering minus one); -1 means unmapped.
class ControlMapping {
public:
 struct Knob {int clockwise,counterClockwise;};
 struct Stick {int up=-1,down=-1,left=-1,right=-1,push=-1,pov=-1;};
 struct Clutch {int axis=-1;float rest=-10000,full=10000;};

 // Slots 0-7 push buttons, 8-9 shift paddles, 10-11 clutch levers.
 std::array<int,12> buttons{0,1,2,3,4,5,6,7,-1,-1,-1,-1};
 std::array<Knob,4> knobs{{{8,9},{10,11},{36,37},{38,39}}};
 std::array<Stick,2> sticks{};
 std::array<Clutch,2> clutches{};

 void load(const Config& config){
  for(int i=0;i<8;i++)buttons[i]=config.integer(L"Button"+std::to_wstring(i),i);
  const wchar_t* levers[]={L"LeftPaddleButton",L"RightPaddleButton",L"LeftClutchButton",L"RightClutchButton"};
  for(int i=0;i<4;i++)buttons[8+i]=config.integer(levers[i],-1);
  const wchar_t* clutchNames[]={L"LeftClutch",L"RightClutch"};
  for(int j=0;j<2;j++){
   std::wstring name=clutchNames[j];auto& c=clutches[j];
   c.axis=config.integer(name+L"Axis",-1);c.rest=config.real(name+L"Rest",-10000);c.full=config.real(name+L"Full",10000);
  }
  const wchar_t* knobNames[]={L"B9B10Knob",L"B11B12Knob",L"B37B38Knob",L"B39B40Knob"};
  for(int k=0;k<4;k++){
   std::wstring name=knobNames[k];auto& knob=knobs[k];
   knob.clockwise=config.integer(name+L"CW",knob.clockwise);
   knob.counterClockwise=config.integer(name+L"CCW",knob.counterClockwise);
  }
  const wchar_t* stickNames[]={L"LeftStick",L"RightStick"};
  for(int j=0;j<2;j++){
   std::wstring name=stickNames[j];auto& s=sticks[j];
   s.up=config.integer(name+L"Up",-1);s.down=config.integer(name+L"Down",-1);
   s.left=config.integer(name+L"Left",-1);s.right=config.integer(name+L"Right",-1);
   s.push=config.integer(name+L"Push",-1);s.pov=config.integer(name+L"POV",-1);
  }
 }

 // state is null when the device could not be read; every control then reads as released.
 // Stateful: knob detents are counted on the press edge of their buttons.
 std::array<ControlInput,controlCount> sample(const DIJOYSTATE2* state){
  std::array<ControlInput,controlCount> out{};
  auto held=[&](int button){return state&&buttonHeld(*state,button);};
  for(int i=0;i<12;i++)out[i].down=held(buttons[i]);
  for(int j=0;j<2;j++){
   const auto& c=clutches[j];float span=c.full-c.rest;
   if(state&&c.axis>=0&&c.axis<8&&std::abs(span)>1)
    out[10+j].analog=std::clamp((float(axisValue(*state,c.axis))-c.rest)/span,0.f,1.f);
  }
  for(int k=0;k<4;k++){
   auto& turn=out[firstKnob+k].turn;
   if(knobEdges_[k][0].rising(held(knobs[k].clockwise)))turn-=1;
   if(knobEdges_[k][1].rising(held(knobs[k].counterClockwise)))turn+=1;
  }
  for(int j=0;j<2;j++){
   const auto& s=sticks[j];auto& c=out[firstStick+j];
   c.x=float(held(s.right))-float(held(s.left));c.y=float(held(s.up))-float(held(s.down));c.down=held(s.push);
   // A centred hat reports 0xFFFF in the low word; otherwise hundredths of a degree clockwise from up.
   if(state&&s.pov>=0&&s.pov<4&&LOWORD(state->rgdwPOV[s.pov])!=0xFFFF){
    float angle=float(state->rgdwPOV[s.pov])/100*pi/180;
    c.x=std::round(std::sin(angle));c.y=std::round(std::cos(angle));
   }
  }
  return out;
 }

private:
 std::array<std::array<EdgeTrigger,2>,4> knobEdges_{};
};

}
