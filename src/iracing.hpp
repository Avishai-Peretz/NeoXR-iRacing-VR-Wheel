#pragma once
#include <windows.h>
#include <cctype>
#include <cstdlib>
#include <string>

namespace neo {

// The car folder name ("CarPath") of the player's car, from the session info YAML in iRacing's
// telemetry. Empty when the text has no player car.
inline std::string playerCarPath(const std::string& yaml){
 auto number=[&](size_t at){size_t end=at;while(end<yaml.size()&&std::isdigit(static_cast<unsigned char>(yaml[end])))end++;
  return end>at?std::atoi(yaml.substr(at,end-at).c_str()):-1;};
 auto value=[&](size_t at){while(at<yaml.size()&&yaml[at]==' ')at++;size_t end=yaml.find_first_of("\r\n",at);
  std::string v=yaml.substr(at,end==std::string::npos?std::string::npos:end-at);
  while(!v.empty()&&v.back()==' ')v.pop_back();return v;};
 const std::string driverKey="DriverCarIdx: ",entryKey="- CarIdx: ",pathKey="CarPath:";
 auto driver=yaml.find(driverKey);if(driver==std::string::npos)return {};
 int player=number(driver+driverKey.size());if(player<0)return {};
 for(auto entry=yaml.find(entryKey);entry!=std::string::npos;entry=yaml.find(entryKey,entry+1)){
  if(number(entry+entryKey.size())!=player)continue;
  auto next=yaml.find(entryKey,entry+1),path=yaml.find(pathKey,entry);
  if(path==std::string::npos||(next!=std::string::npos&&path>next))return {};
  return value(path+pathKey.size());
 }
 return {};
}

// Car folder names are short lowercase identifiers; anything else is replaced so the name is a safe
// INI key.
inline std::wstring carKey(const std::string& path){
 std::wstring key;
 for(char c:path)key+=std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-'||c=='.'?wchar_t(c):L'_';
 return key;
}

// Which car the player is in, read from iRacing's telemetry shared memory: the documented, read-only
// SDK interface. Nothing is written to the game.
class IracingTelemetry {
public:
 IracingTelemetry()=default;
 IracingTelemetry(const IracingTelemetry&)=delete;IracingTelemetry& operator=(const IracingTelemetry&)=delete;
 ~IracingTelemetry(){close();}

 // The player's car key, or empty while iRacing is not connected. Rereads the session info only when
 // iRacing changes it.
 const std::wstring& car(){
  if(!view_&&!open()){car_.clear();return car_;}
  const auto* header=static_cast<const Header*>(view_);
  if(!(header->status&connected)){car_.clear();update_=-1;return car_;}
  int update=header->sessionInfoUpdate;
  if(update==update_)return car_;
  int length=header->sessionInfoLength,offset=header->sessionInfoOffset;
  if(length<=0||offset<=0||size_t(offset)+size_t(length)>size_)return car_;
  std::string text(static_cast<const char*>(view_)+offset,strnlen(static_cast<const char*>(view_)+offset,size_t(length)));
  if(header->sessionInfoUpdate!=update)return car_;  // changed while copying; read again next time
  update_=update;car_=carKey(playerCarPath(text));
  return car_;
 }

private:
 // The start of irsdk_header.
 struct Header {int version,status,tickRate,sessionInfoUpdate,sessionInfoLength,sessionInfoOffset;};
 static constexpr int connected=1;

 bool open(){
  map_=OpenFileMappingW(FILE_MAP_READ,FALSE,L"Local\\IRSDKMemMapFileName");if(!map_)return false;
  view_=MapViewOfFile(map_,FILE_MAP_READ,0,0,0);
  MEMORY_BASIC_INFORMATION info{};
  if(!view_||!VirtualQuery(view_,&info,sizeof info)||info.RegionSize<sizeof(Header)){close();return false;}
  size_=info.RegionSize;return true;
 }
 void close(){if(view_)UnmapViewOfFile(view_);if(map_)CloseHandle(map_);view_=nullptr;map_=nullptr;size_=0;update_=-1;}

 HANDLE map_=nullptr;const void* view_=nullptr;size_t size_=0;int update_=-1;std::wstring car_;
};

}
