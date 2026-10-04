#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <string>
#include "geometry.hpp"

namespace neo {

// The [Wheel] section of NeoXR.ini. Every read has a fallback and numeric reads can be clamped,
// so a typo in the file degrades to a default instead of an invalid layer state.
class Config {
public:
 explicit Config(std::filesystem::path folder):folder_(std::move(folder)),file_((folder_/L"NeoXR.ini").wstring()) {}
 // Reads `file` but still resolves relative paths against `folder`, e.g. for a draft copy of the settings.
 Config(std::filesystem::path folder,const std::filesystem::path& file):folder_(std::move(folder)),file_(file.wstring()) {}

 int integer(const std::wstring& key,int fallback) const {
  return int(GetPrivateProfileIntW(section,key.c_str(),fallback,file_.c_str()));
 }
 bool flag(const std::wstring& key,bool fallback) const {return integer(key,fallback?1:0)!=0;}

 float real(const std::wstring& key,float fallback) const {
  try {
   float value=std::stof(text(key,std::to_wstring(fallback)));
   return std::isfinite(value)?value:fallback;
  } catch(...) {return fallback;}
 }
 float real(const std::wstring& key,float fallback,float low,float high) const {
  return std::clamp(real(key,fallback),low,high);
 }
 // Stored in degrees for people, returned in radians for the math.
 float radians(const std::wstring& key,float fallbackDegrees,float lowDegrees,float highDegrees) const {
  return real(key,fallbackDegrees,lowDegrees,highDegrees)*pi/180;
 }

 std::wstring text(const std::wstring& key,const std::wstring& fallback=L"") const {
  wchar_t buffer[MAX_PATH]={};
  GetPrivateProfileStringW(section,key.c_str(),fallback.c_str(),buffer,MAX_PATH,file_.c_str());
  return buffer;
 }
 // Relative paths are resolved against the layer folder, so the package can be moved as a whole.
 std::filesystem::path path(const std::wstring& key,const std::wstring& fallback=L"") const {
  std::filesystem::path value(text(key,fallback));
  return value.empty()||value.is_absolute()?value:folder_/value;
 }

 void write(const std::wstring& key,const std::wstring& value) const {
  WritePrivateProfileStringW(section,key.c_str(),value.c_str(),file_.c_str());
 }

private:
 static constexpr const wchar_t* section=L"Wheel";
 std::filesystem::path folder_;
 std::wstring file_;
};

// Parses a key setting: F1-F24, one of the names below, a single letter or digit, or a decimal
// virtual-key code. Empty or 0 means "no key".
inline int parseKey(const std::wstring& text){
 if(text.empty())return 0;
 struct Name {const wchar_t* name;int key;};
 static constexpr Name names[]={{L"Tab",VK_TAB},{L"Insert",VK_INSERT},{L"Delete",VK_DELETE},{L"Home",VK_HOME},
  {L"End",VK_END},{L"Pause",VK_PAUSE},{L"ScrollLock",VK_SCROLL},{L"Backspace",VK_BACK}};
 for(const auto& n:names)if(_wcsicmp(text.c_str(),n.name)==0)return n.key;
 if(std::towupper(text[0])==L'F'&&text.size()>1){
  int number=_wtoi(text.c_str()+1);
  if(number>=1&&number<=24)return VK_F1+number-1;
 }
 if(text.size()==1&&std::iswalnum(text[0]))return int(std::towupper(text[0]));
 return _wtoi(text.c_str());
}

}
