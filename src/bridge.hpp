#pragma once
#include <windows.h>
#include "input.hpp"

// Device state as the setup wizard marshals it between its native calls.
// Axis order matches axisValue(): X, Y, Z, Rx, Ry, Rz, Slider0, Slider1.
struct NeoInputState {LONG axes[8];DWORD pov[4];BYTE buttons[128];};

inline NeoInputState toBridge(const DIJOYSTATE2& s){
 NeoInputState out{};
 for(int a=0;a<8;a++)out.axes[a]=axisValue(s,a);
 for(int p=0;p<4;p++)out.pov[p]=s.rgdwPOV[p];
 for(int b=0;b<128;b++)out.buttons[b]=s.rgbButtons[b];
 return out;
}

inline DIJOYSTATE2 fromBridge(const NeoInputState& s){
 DIJOYSTATE2 out{};
 out.lX=s.axes[0];out.lY=s.axes[1];out.lZ=s.axes[2];out.lRx=s.axes[3];out.lRy=s.axes[4];out.lRz=s.axes[5];
 out.rglSlider[0]=s.axes[6];out.rglSlider[1]=s.axes[7];
 for(int p=0;p<4;p++)out.rgdwPOV[p]=s.pov[p];
 for(int b=0;b<128;b++)out.rgbButtons[b]=s.buttons[b];
 return out;
}
