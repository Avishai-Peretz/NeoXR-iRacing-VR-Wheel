#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace neo {
// Control slots: 0-7 push buttons, 8-11 shifters/clutches, 12-15 rotary knobs, 16-17 joysticks.
constexpr int controlCount=18,firstKnob=12,firstStick=16;
enum class Click {None,Detent,Button,Paddle};
struct Spring {
 float x=0,v=0;
 // Fixed 1 ms substeps keep the stiff spring stable at any headset frame rate.
 void step(float target,float dt,float omega,float zeta){
  for(float left=dt;left>0;left-=.001f){float h=std::min(left,.001f);v+=(omega*omega*(target-x)-2*zeta*omega*v)*h;x+=v*h;}
 }
};
struct ControlInput {bool down=false;int turn=0;float x=0,y=0;};
struct ControlPose {float press=0,flash=0,a=0,b=0;};
struct Animator {
 std::array<Spring,controlCount> press,tiltA,tiltB;std::array<float,controlCount> flash{},knob{};
 std::array<ControlInput,controlCount> previous{};
 // Knobs whose mesh is only detailed on the visible arc nudge per detent and settle back.
 std::array<bool,controlCount> recentre{};
 Click update(const std::array<ControlInput,controlCount>& in,float dt,float knobStep){
  dt=std::clamp(dt,0.f,.1f);Click click=Click::None;
  auto event=[&](int i,Click kind){flash[i]=1;click=std::max(click,kind);};
  for(int i=0;i<controlCount;i++){
   const auto& c=in[i];const auto& p=previous[i];
   if(c.down&&!p.down)event(i,i>=8&&i<firstKnob?Click::Paddle:Click::Button);
   if(c.turn){knob[i]+=float(c.turn)*knobStep;event(i,Click::Detent);}
   if((c.x||c.y)&&(c.x!=p.x||c.y!=p.y))event(i,Click::Button);
   previous[i]=c;previous[i].turn=0;
   flash[i]*=std::exp(-dt/.12f);
   press[i].step(c.down?1.f:0.f,dt,90,.35f);
   if(i>=firstStick){tiltA[i].step(c.x,dt,70,.4f);tiltB[i].step(c.y,dt,70,.4f);}
   else if(i>=firstKnob){tiltA[i].step(knob[i],dt,110,.45f);if(recentre[i])knob[i]*=std::exp(-dt/.06f);}
  }
  return click;
 }
 ControlPose pose(int i)const{return {press[i].x,flash[i],tiltA[i].x,tiltB[i].x};}
};
}
