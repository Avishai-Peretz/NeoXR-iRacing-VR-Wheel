#include <windows.h>
#include "input.hpp"
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){WheelInput in;bool prompted=argc<2;int index=0;
 auto fail=[&](const char* message){std::cerr<<message<<"\n";if(prompted){std::cout<<"Press Enter to close.";std::string l;std::getline(std::cin,l);}return 1;};
 if(prompted){if(!in.list())return fail("DirectInput unavailable");
  for(size_t i=0;i<in.devices.size();i++)std::wcout<<i<<L": "<<in.devices[i].tszInstanceName<<L"\n";
  std::cout<<"Device to watch [0]: ";std::string line;if(!std::getline(std::cin,line))return 0;
  index=line.empty()?0:std::atoi(line.c_str());std::cout<<"Press controls to see their numbers. Close the window or Ctrl+C to exit.\n";}
 else index=_wtoi(argv[1]);
 if(!in.open(index))return fail("Device open failed");
 for(;;){if(in.poll()){std::cout<<"axes:";for(int a=0;a<8;a++)std::cout<<' '<<in.axis(a);
 std::cout<<" buttons:";for(int i=0;i<128;i++)if(in.state.rgbButtons[i]&128)std::cout<<' '<<i;
 std::cout<<" pov:";for(int i=0;i<4;i++)if(LOWORD(in.state.rgdwPOV[i])!=0xFFFF)std::cout<<' '<<i<<'@'<<in.state.rgdwPOV[i]/100;
 std::cout<<std::endl;}else std::cout<<"Input unavailable"<<std::endl;Sleep(250);}
}
