#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include "animation.hpp"
#include "geometry.hpp"
// PlaySound keeps reading the buffer while it plays, so buffers live as long as this object.
struct ClickSound {
 std::array<std::vector<char>,4> sounds;
 static std::vector<char> wav(const std::vector<int16_t>& samples){
  const uint32_t rate=44100,data=uint32_t(samples.size()*2),riff=36+data,fmt=16,byteRate=rate*2;const uint16_t pcm=1,mono=1,align=2,bits=16;
  std::vector<char> out(44+data);char* p=out.data();
  auto put=[&](const void* v,size_t n){std::memcpy(p,v,n);p+=n;};
  put("RIFF",4);put(&riff,4);put("WAVE",4);put("fmt ",4);put(&fmt,4);put(&pcm,2);put(&mono,2);put(&rate,4);put(&byteRate,4);put(&align,2);put(&bits,2);
  put("data",4);put(&data,4);put(samples.data(),data);return out;
 }
 // Decaying tone for the body of the click plus a very short noise burst for the contact transient.
 static std::vector<char> synth(float tone,float decay,float noise,float volume){
  const int rate=44100,n=rate/25;std::vector<int16_t> s(n);uint32_t seed=0x2545F491u;
  for(int i=0;i<n;i++){float t=float(i)/rate;seed=seed*1664525u+1013904223u;float white=float(int32_t(seed))/2147483648.f;
   float v=std::sin(2*neo::pi*tone*t)*std::exp(-t/decay)+white*noise*std::exp(-t/.0012f);
   s[i]=int16_t(std::clamp(v*volume,-1.f,1.f)*32767);}
  return wav(s);
 }
 // Can be called again to change the sound; playback is stopped before the old buffers go away.
 void init(float volume,const std::filesystem::path& file){
  PlaySoundW(nullptr,nullptr,0);for(auto& s:sounds)s.clear();
  if(volume<=0)return;
  if(!file.empty()){std::ifstream in(file,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
   if(bytes.size()<44||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4))throw std::runtime_error("ClickSoundFile must be a WAV file");
   for(size_t i=1;i<sounds.size();i++)sounds[i]=bytes;return;}
  sounds[size_t(neo::Click::Detent)]=synth(4200,.0025f,.3f,volume*.6f);
  sounds[size_t(neo::Click::Button)]=synth(2600,.005f,.55f,volume);
  sounds[size_t(neo::Click::Paddle)]=synth(1300,.011f,.8f,volume);
 }
 void play(neo::Click c)const{const auto& s=sounds[size_t(c)];
  if(!s.empty())PlaySoundW(reinterpret_cast<LPCWSTR>(s.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);}
 ~ClickSound(){PlaySoundW(nullptr,nullptr,0);}
};
