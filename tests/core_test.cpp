#include "geometry.hpp"
#include "animation.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 assert(std::abs(neo::steering(10000,900,false)-2.5f*neo::pi)<1e-5f);
 assert(neo::steering(0,900,false)==0);
 assert(neo::steering(-20000,900,true)==neo::steering(10000,900,false));
 auto v=neo::wheel(); assert(v.size()%3==0); float lo=1,hi=-1;bool buttons[8]={};
 for(auto a:v){assert(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z));
 lo=std::min(lo,a.x); hi=std::max(hi,a.x);if(a.button>=0) buttons[int(a.button)]=true;}
 assert(std::abs(hi-lo-.310f)<1e-6f);for(bool b:buttons)assert(b);
 neo::Animator anim;std::array<neo::ControlInput,neo::controlCount> in{};auto run=[&](float seconds){for(float t=0;t<seconds;t+=1/90.f)anim.update(in,1/90.f,.25f);};
 in[0].down=true;in[9].down=true;
 auto first=anim.update(in,1/90.f,.25f);assert(first==neo::Click::Paddle);assert(anim.flash[0]>.9f&&anim.flash[9]>.9f);
 assert(anim.update(in,1/90.f,.25f)==neo::Click::None);
 run(.3f);assert(std::abs(anim.press[0].x-1)<.02f);assert(anim.flash[0]<.1f);
 float peak=0;in[0].down=false;for(int i=0;i<30;i++){anim.update(in,1/90.f,.25f);peak=std::min(peak,anim.press[0].x);}
 assert(peak<-.05f);run(.4f);assert(std::abs(anim.press[0].x)<.02f);
 in[neo::firstKnob].turn=-1;assert(anim.update(in,1/90.f,.25f)==neo::Click::Detent);in[neo::firstKnob].turn=0;
 run(.4f);assert(std::abs(anim.tiltA[neo::firstKnob].x+.25f)<.01f);
 anim.recentre[neo::firstKnob+1]=true;in[neo::firstKnob+1].turn=1;anim.update(in,1/90.f,.25f);in[neo::firstKnob+1].turn=0;
 float swing=0;for(int i=0;i<20;i++){anim.update(in,1/90.f,.25f);swing=std::max(swing,anim.tiltA[neo::firstKnob+1].x);}
 assert(swing>.08f);run(.6f);assert(std::abs(anim.tiltA[neo::firstKnob+1].x)<.01f);
 in[neo::firstStick].y=1;assert(anim.update(in,1/90.f,.25f)==neo::Click::Button);run(.4f);assert(std::abs(anim.tiltB[neo::firstStick].x-1)<.02f);
 anim.update(in,.5f,.25f);assert(std::isfinite(anim.press[0].x));
 std::cout<<"PASS: steering endpoints, clamping, inversion, 310mm geometry, eight button regions and control animation\n";
}
