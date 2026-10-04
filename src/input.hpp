#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wrl/client.h>
#include <vector>
#include <string>
using Microsoft::WRL::ComPtr;
// Axis numbers as listed by NeoXR-Input: 0 X, 1 Y, 2 Z, 3 Rx, 4 Ry, 5 Rz, 6-7 sliders.
inline long axisValue(const DIJOYSTATE2& s,int n){switch(n){case 1:return s.lY;case 2:return s.lZ;case 3:return s.lRx;
 case 4:return s.lRy;case 5:return s.lRz;case 6:return s.rglSlider[0];case 7:return s.rglSlider[1];default:return s.lX;}}
inline bool buttonHeld(const DIJOYSTATE2& s,int n){return n>=0&&n<128&&(s.rgbButtons[n]&128);}
struct WheelInput {
 ComPtr<IDirectInput8W> api; ComPtr<IDirectInputDevice8W> device;
 HWND window=nullptr;
 std::vector<DIDEVICEINSTANCEW> devices; DIJOYSTATE2 state{}; bool valid=false;
 static BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* d,void* p){
  static_cast<WheelInput*>(p)->devices.push_back(*d); return DIENUM_CONTINUE;
 }
 static BOOL CALLBACK range(const DIDEVICEOBJECTINSTANCEW* o,void* p){
  if(o->dwType&DIDFT_AXIS){DIPROPRANGE r{};r.diph.dwSize=sizeof(r);r.diph.dwHeaderSize=sizeof(r.diph);
   r.diph.dwHow=DIPH_BYID;r.diph.dwObj=o->dwType;r.lMin=-10000;r.lMax=10000;
   static_cast<IDirectInputDevice8W*>(p)->SetProperty(DIPROP_RANGE,&r.diph);}
  return DIENUM_CONTINUE;
 }
 bool list(){ devices.clear();
  if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,
     reinterpret_cast<void**>(api.GetAddressOf()),nullptr)))return false;
  return SUCCEEDED(api->EnumDevices(DI8DEVCLASS_GAMECTRL,enumerate,this,DIEDFL_ATTACHEDONLY));
 }
 bool open(int index){if(!list()||index<0||size_t(index)>=devices.size())return false;
  if(FAILED(api->CreateDevice(devices[index].guidInstance,&device,nullptr)))return false;
  if(FAILED(device->SetDataFormat(&c_dfDIJoystick2)))return false;
  window=CreateWindowExW(0,L"STATIC",L"NeoXR Input",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!window||FAILED(device->SetCooperativeLevel(window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)))return false;
  device->EnumObjects(range,device.Get(),DIDFT_AXIS);device->Acquire(); return true;
 }
 bool poll(){valid=false;if(!device)return false;
  HRESULT h=device->Poll();if(FAILED(h)){device->Acquire();device->Poll();}
  DIJOYSTATE2 next{};if(FAILED(device->GetDeviceState(sizeof(next),&next))){state={};return false;}
  state=next;valid=true;return true;
 }
 long axis(int n)const {return axisValue(state,n);}
 ~WheelInput(){if(device)device->Unacquire();device.Reset();if(window)DestroyWindow(window);}
};
