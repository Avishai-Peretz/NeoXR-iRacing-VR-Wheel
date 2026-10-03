#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
// Mouse placement. While editing, low-level hooks on a private thread swallow the mouse and the edit
// keys so the game does not see them. Hooks only react while a window of this process has focus.
struct EditMode {
 enum Mode {Off,Move,Adjust,Confirm};
 enum Result {None,Save,Cancel};
 std::atomic<int> mode{Off},result{None};
 std::atomic<long> leftX{0},leftY{0},rightX{0},rightY{0},scroll{0};
 int toggleKey=VK_TAB;
 void start(HMODULE module,int key){
  toggleKey=key;active()=this;
  worker=std::thread([this,module]{MSG msg;PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE);threadId=GetCurrentThreadId();
   HHOOK k=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,module,0),m=SetWindowsHookExW(WH_MOUSE_LL,mouse,module,0);
   while(GetMessageW(&msg,nullptr,0,0)>0){}
   if(k)UnhookWindowsHookEx(k);if(m)UnhookWindowsHookEx(m);});
 }
 void stop(){if(!worker.joinable())return;while(!threadId)Sleep(1);PostThreadMessageW(threadId,WM_QUIT,0,0);worker.join();active()=nullptr;}
 ~EditMode(){stop();}
private:
 std::thread worker;std::atomic<DWORD> threadId{0};
 bool leftHeld=false,rightHeld=false,disabled=false;POINT origin{};int escapes=0;ULONGLONG lastEscape=0;
 static EditMode*& active(){static EditMode* p=nullptr;return p;}
 static bool focused(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
 void finish(Result r){result=r;mode=Off;leftHeld=rightHeld=false;}
 static LRESULT CALLBACK keyboard(int code,WPARAM w,LPARAM l){
  auto* e=active();if(code!=HC_ACTION||!e||e->disabled||!focused())return CallNextHookEx(nullptr,code,w,l);
  auto* k=reinterpret_cast<KBDLLHOOKSTRUCT*>(l);bool down=w==WM_KEYDOWN||w==WM_SYSKEYDOWN;int vk=int(k->vkCode);int m=e->mode;
  if(m==Off){if(vk!=e->toggleKey)return CallNextHookEx(nullptr,code,w,l);
   if(down){GetCursorPos(&e->origin);e->leftX=e->leftY=e->rightX=e->rightY=e->scroll=0;e->escapes=0;e->mode=Move;}return 1;}
  if(vk==VK_ESCAPE&&down){ULONGLONG now=GetTickCount64();e->escapes=now-e->lastEscape<600?e->escapes+1:1;e->lastEscape=now;
   // Emergency exit: five quick presses release the hooks for the rest of the session.
   if(e->escapes>=5){e->disabled=true;e->finish(Cancel);return 1;}}
  if(m==Confirm){if(down&&vk==VK_RETURN)e->finish(Save);else if(down&&vk==VK_ESCAPE)e->finish(Cancel);
   return vk==VK_RETURN||vk==VK_ESCAPE||vk==e->toggleKey?1:CallNextHookEx(nullptr,code,w,l);}
  if(vk=='M'){if(down)e->mode=m==Move?Adjust:Move;return 1;}
  if(vk==VK_ESCAPE||vk==e->toggleKey){if(down)e->mode=Confirm;return 1;}
  return CallNextHookEx(nullptr,code,w,l);
 }
 static LRESULT CALLBACK mouse(int code,WPARAM w,LPARAM l){
  auto* e=active();if(code!=HC_ACTION||!e||e->disabled||e->mode==Off||!focused())return CallNextHookEx(nullptr,code,w,l);
  auto* m=reinterpret_cast<MSLLHOOKSTRUCT*>(l);
  switch(w){
   case WM_LBUTTONDOWN:e->leftHeld=true;break;case WM_LBUTTONUP:e->leftHeld=false;break;
   case WM_RBUTTONDOWN:e->rightHeld=true;break;case WM_RBUTTONUP:e->rightHeld=false;break;
   case WM_MOUSEWHEEL:e->scroll+=GET_WHEEL_DELTA_WPARAM(m->mouseData);break;
   case WM_MOUSEMOVE:{
    // The move is swallowed, so the cursor stays at origin and pt-origin is this event's motion.
    if(m->flags&LLMHF_INJECTED)break;long dx=m->pt.x-e->origin.x,dy=m->pt.y-e->origin.y;
    if(e->mode!=Confirm){if(e->leftHeld){e->leftX+=dx;e->leftY+=dy;}if(e->rightHeld){e->rightX+=dx;e->rightY+=dy;}}break;}
  }
  return 1;
 }
};
