#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <optional>
#include <string>
#include "geometry.hpp"

namespace neo {

// Where the wheel sits relative to the calibrated head: metres, degrees, and rim width in metres.
struct Placement {
 float x=0,y=-.3f,z=-.5f;
 float tiltDegrees=0,yawDegrees=0;
 float width=.31f;

 // The one place that defines what a valid placement is.
 Placement clamped() const {
  Placement p=*this;
  p.x=std::clamp(p.x,-1.f,1.f);p.y=std::clamp(p.y,-1.5f,1.f);p.z=std::clamp(p.z,-1.5f,-.1f);
  p.tiltDegrees=std::clamp(p.tiltDegrees,-90.f,90.f);p.yawDegrees=std::clamp(p.yawDegrees,-90.f,90.f);
  p.width=std::clamp(p.width,.1f,.6f);
  return p;
 }
 // Pitch about the wheel's own axis first, then heading, then the offset from the head.
 DirectX::XMMATRIX relativeToHead() const {
  using namespace DirectX;
  return XMMatrixRotationX(tiltDegrees*pi/180)*XMMatrixRotationY(yawDegrees*pi/180)*XMMatrixTranslation(x,y,z);
 }
};

// Mouse motion collected since the last frame, in screen pixels and wheel notches.
struct Drag {float leftX=0,leftY=0,rightX=0,rightY=0,scrollNotches=0;};
enum class DragMode {Move,Adjust};

// Mouse-to-world rates at EditSensitivity=1.
constexpr float metresPerPixel=.0005f,metresPerNotch=.01f;
constexpr float widthPerPixel=.0002f,widthPerNotch=.005f,degreesPerPixel=.1f;

// Move: left-drag slides in the head plane, right-drag or scroll changes distance.
// Adjust: left-drag resizes, right-drag pitches (vertical) and turns (horizontal).
inline Placement dragged(Placement p,DragMode mode,const Drag& d,float sensitivity){
 if(mode==DragMode::Move){
  p.x+=d.leftX*metresPerPixel*sensitivity;
  p.y-=d.leftY*metresPerPixel*sensitivity; // screen Y grows downwards
  p.z+=d.rightY*metresPerPixel*sensitivity-d.scrollNotches*metresPerNotch;
 }else{
  p.width+=(d.leftX-d.leftY)*widthPerPixel*sensitivity+d.scrollNotches*widthPerNotch;
  p.tiltDegrees-=d.rightY*degreesPerPixel*sensitivity;
  p.yawDegrees+=d.rightX*degreesPerPixel*sensitivity;
 }
 return p.clamped();
}

// Keeps only the head's heading about gravity, so looking down while recentering does not tilt the
// wheel. head is in the game's space; levelToGame maps a gravity-aligned space into it, which matters
// because the game's space can be arbitrarily rotated (iRacing uses STAGE rolled 180 degrees).
inline DirectX::XMMATRIX levelHead(DirectX::FXMMATRIX head,DirectX::CXMMATRIX levelToGame){
 using namespace DirectX;
 XMFLOAT4X4 m;XMStoreFloat4x4(&m,head*XMMatrixInverse(nullptr,levelToGame));
 float yaw=std::atan2(m._31,m._33); // row-vector convention: row 2 is the head's +Z (backward) axis
 return XMMatrixRotationY(yaw)*XMMatrixTranslation(m._41,m._42,m._43)*levelToGame;
}

// Calibration=x,y,z,qx,qy,qz,qw: the levelled head pose in the game's tracking space.
inline std::wstring formatCalibration(DirectX::FXMMATRIX head){
 using namespace DirectX;
 XMVECTOR scale,rotation,translation;XMMatrixDecompose(&scale,&rotation,&translation,head);
 XMFLOAT4 q;XMFLOAT3 t;XMStoreFloat4(&q,rotation);XMStoreFloat3(&t,translation);
 wchar_t text[96];swprintf_s(text,L"%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f",t.x,t.y,t.z,q.x,q.y,q.z,q.w);
 return text;
}
// Rejects anything that is not seven finite numbers with a unit quaternion, including the
// pre-0.2.1 four-value format, so a bad line falls back to calibrating at startup.
inline std::optional<DirectX::XMFLOAT4X4> parseCalibration(const std::wstring& text){
 using namespace DirectX;
 XMFLOAT3 t;XMFLOAT4 q;
 if(swscanf_s(text.c_str(),L"%f,%f,%f,%f,%f,%f,%f",&t.x,&t.y,&t.z,&q.x,&q.y,&q.z,&q.w)!=7)return std::nullopt;
 float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
 if(!std::isfinite(t.x+t.y+t.z+norm)||std::abs(norm-1)>=.01f)return std::nullopt;
 XMFLOAT4X4 head;XMStoreFloat4x4(&head,XMMatrixRotationQuaternion(XMLoadFloat4(&q))*XMMatrixTranslation(t.x,t.y,t.z));
 return head;
}

}
