#include <windows.h>
#include <cwchar>
#include <memory>
#include "bridge.hpp"

// C API over WheelInput for the setup wizard, so it sees devices, axes and buttons exactly as the layer does.

extern "C" {

// Returns the number of attached game controllers and copies the name of device `index` into `name`.
__declspec(dllexport) int neo_device_name(int index,wchar_t* name,int size){
 WheelInput input;
 if(!input.list())return -1;
 if(name&&size>0){
  name[0]=0;
  if(index>=0&&size_t(index)<input.devices.size())wcsncpy_s(name,size_t(size),input.devices[index].tszInstanceName,_TRUNCATE);
 }
 return int(input.devices.size());
}

__declspec(dllexport) void* neo_open(int index){
 auto input=std::make_unique<WheelInput>();
 return input->open(index)?input.release():nullptr;
}

__declspec(dllexport) int neo_poll(void* device,NeoInputState* out){
 auto* input=static_cast<WheelInput*>(device);
 if(!input||!out||!input->poll())return 0;
 *out=toBridge(input->state);
 return 1;
}

__declspec(dllexport) void neo_close(void* device){delete static_cast<WheelInput*>(device);}

}
