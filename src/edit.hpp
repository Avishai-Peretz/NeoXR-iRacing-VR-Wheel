#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdlib>
#include <thread>
// Mouse placement. While editing, low-level hooks on a private thread swallow the mouse and the edit
// keys so the game does not see them. Hooks only react while a window of this process has focus.
struct EditMode {
 enum Mode {Off,Move,Adjust,Confirm};
 enum Result {None,Save,Cancel,Forget};
 std::atomic<int> mode{Off},result{None};
 std::atomic<bool> passthrough{false};  // H pressed while editing: switch between the room and the game
 std::atomic<long> leftX{0},leftY{0},rightX{0},rightY{0},scroll{0};
 std::atomic<int> toggleKey{VK_TAB};
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
 bool leftHeld=false,rightHeld=false,disabled=false;int escapes=0;ULONGLONG lastEscape=0;
 std::array<bool,256> keyDown{};
 // Adjust mode: a right-drag turns or pitches, whichever way it starts, until the button is released.
 enum Axis {Free,Horizontal,Vertical};Axis rightAxis=Free;long rightStartX=0,rightStartY=0;
 static constexpr long axisLockPixels=6,maxEventPixels=200;
 static EditMode*& active(){static EditMode* p=nullptr;return p;}
 static bool focused(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
 void finish(Result r){result=r;mode=Off;leftHeld=rightHeld=false;}
 // Moves are swallowed, so the cursor stays put; parking it mid-way in its allowed area (the game may
 // clip it to its window) leaves room to measure motion in every direction.
 static void parkCursor(){RECT clip;if(!GetClipCursor(&clip))return;SetPhysicalCursorPos((clip.left+clip.right)/2,(clip.top+clip.bottom)/2);}
public:
 // Handles an individual keyboard event; repeats keep being consumed without changing modes.
 bool keyEvent(int vk,bool down,bool hasFocus){
  int m=mode;
  if(vk<0||vk>=int(keyDown.size()))return false;
  bool pressed=down&&!keyDown[vk];keyDown[vk]=down;
  // Track releases even outside the game, but only consume events while it has focus.
  if(!hasFocus)return false;
  if(m==Off){if(vk!=toggleKey)return false;
   if(pressed){leftX=leftY=rightX=rightY=scroll=0;escapes=0;mode=Move;}return true;}
  if(vk==VK_ESCAPE&&pressed){ULONGLONG now=GetTickCount64();escapes=now-lastEscape<600?escapes+1:1;lastEscape=now;
   // Emergency exit: five quick presses release the hooks for the rest of the session.
   if(escapes>=5){disabled=true;finish(Cancel);return true;}}
  if(m==Confirm){if(pressed&&vk==VK_RETURN)finish(Save);else if(pressed&&vk==VK_ESCAPE)finish(Cancel);else if(pressed&&vk==VK_DELETE)finish(Forget);
   return vk==VK_RETURN||vk==VK_ESCAPE||vk==VK_DELETE||vk==toggleKey?true:false;}
  if(vk=='M'){if(pressed)mode=m==Move?Adjust:Move;return true;}
  // Held keys repeat; each repeat would switch the view again.
  if(vk=='H'){if(pressed)passthrough=true;return true;}
  if(vk==VK_ESCAPE||vk==toggleKey){if(pressed)mode=Confirm;return true;}
  return false;
 }
private:
 static LRESULT CALLBACK keyboard(int code,WPARAM w,LPARAM l){
  auto* e=active();if(code!=HC_ACTION||!e||e->disabled)return CallNextHookEx(nullptr,code,w,l);
  auto* k=reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
  bool wasOff=e->mode==Off;
  if(!e->keyEvent(int(k->vkCode),w==WM_KEYDOWN||w==WM_SYSKEYDOWN,focused()))return CallNextHookEx(nullptr,code,w,l);
  if(wasOff&&e->mode==Move)parkCursor();
  return 1;
 }
 static LRESULT CALLBACK mouse(int code,WPARAM w,LPARAM l){
  auto* e=active();if(code!=HC_ACTION||!e||e->disabled||e->mode==Off||!focused())return CallNextHookEx(nullptr,code,w,l);
  auto* m=reinterpret_cast<MSLLHOOKSTRUCT*>(l);
  switch(w){
   case WM_LBUTTONDOWN:e->leftHeld=true;break;case WM_LBUTTONUP:e->leftHeld=false;break;
   case WM_RBUTTONDOWN:e->rightHeld=true;e->rightAxis=Free;e->rightStartX=e->rightStartY=0;break;
   case WM_RBUTTONUP:e->rightHeld=false;break;
   case WM_MOUSEWHEEL:e->scroll+=GET_WHEEL_DELTA_WPARAM(m->mouseData);break;
   case WM_MOUSEMOVE:{
    // pt is where this move would put the cursor; measured from where the cursor really is, so a game
    // moving the cursor meanwhile cannot bias the drag.
    if(m->flags&LLMHF_INJECTED)break;POINT now;if(!GetPhysicalCursorPos(&now))break;
    long dx=m->pt.x-now.x,dy=m->pt.y-now.y;
    if(e->mode==Confirm)break;
    // No hand moves a mouse this far in one event; it is the cursor being repositioned (e.g. by the game).
    if(std::abs(dx)>maxEventPixels||std::abs(dy)>maxEventPixels){parkCursor();break;}
    if(e->leftHeld){e->leftX+=dx;e->leftY+=dy;}
    if(e->rightHeld){
     if(e->mode==Adjust){
      if(e->rightAxis==Free){e->rightStartX+=dx;e->rightStartY+=dy;
       if(std::abs(e->rightStartX)<axisLockPixels&&std::abs(e->rightStartY)<axisLockPixels)break;
       e->rightAxis=std::abs(e->rightStartX)>=std::abs(e->rightStartY)?Horizontal:Vertical;
       dx=e->rightStartX;dy=e->rightStartY;}
      if(e->rightAxis==Horizontal)dy=0;else dx=0;
     }
     e->rightX+=dx;e->rightY+=dy;}
    if(now.x==m->pt.x&&now.y==m->pt.y)break;
    RECT clip;if(GetClipCursor(&clip)&&(now.x<=clip.left+1||now.x>=clip.right-2||now.y<=clip.top+1||now.y>=clip.bottom-2))parkCursor();
    break;}
  }
  return 1;
 }
};
