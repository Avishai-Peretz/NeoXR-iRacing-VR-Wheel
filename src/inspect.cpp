#include <windows.h>
#include "input.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){WheelInput in;
 if(argc<2){if(!in.list())return 1;for(size_t i=0;i<in.devices.size();i++)
 std::wcout<<i<<L": "<<in.devices[i].tszInstanceName<<L"\n";
 std::wcout<<L"Run NeoXR-Input.exe INDEX to inspect axes/buttons. Ctrl+C to exit.\n";return 0;}
 if(!in.open(_wtoi(argv[1]))){std::cerr<<"Device open failed\n";return 1;}
 for(;;){if(in.poll()){std::cout<<"axes:";for(int a=0;a<8;a++)std::cout<<' '<<in.axis(a);
 std::cout<<" buttons:";for(int i=0;i<128;i++)if(in.state.rgbButtons[i]&128)std::cout<<' '<<i;
 std::cout<<" pov:";for(int i=0;i<4;i++)if(LOWORD(in.state.rgdwPOV[i])!=0xFFFF)std::cout<<' '<<i<<'@'<<in.state.rgdwPOV[i]/100;
 std::cout<<"\n";}else std::cout<<"Input unavailable\n";Sleep(250);}
}
