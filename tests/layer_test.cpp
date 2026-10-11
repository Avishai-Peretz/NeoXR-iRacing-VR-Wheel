#include <windows.h>
#include <DirectXMath.h>
#include <filesystem>
#include <iostream>
#include "config.hpp"
#include "controls.hpp"
#include "edit.hpp"
#include "iracing.hpp"
#include "placement.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace DirectX;

bool approx(float a,float b,float tolerance=1e-3f){return std::abs(a-b)<tolerance;}

void configReadsClampsAndResolvesPaths(){
 auto folder=std::filesystem::temp_directory_path()/L"NeoXR-LayerTest";
 std::filesystem::create_directories(folder);std::filesystem::remove(folder/L"NeoXR.ini");
 neo::Config config(folder);
 assert(config.integer(L"Missing",7)==7&&approx(config.real(L"Missing",.5f),.5f));
 config.write(L"Z",L"-0.39");config.write(L"Tilt",L"500");config.write(L"Bad",L"abc");config.write(L"Sound",L"click.wav");
 assert(approx(config.real(L"Z",0),-.39f));
 assert(approx(config.real(L"Tilt",0,-90,90),90));
 assert(approx(config.real(L"Bad",1.5f),1.5f));
 assert(approx(config.radians(L"Tilt",0,-90,90),neo::pi/2));
 assert(config.path(L"Sound")==folder/L"click.wav");
 assert(config.path(L"Missing").empty());
 std::filesystem::remove_all(folder);
}

void keysParseNamesFunctionKeysAndLetters(){
 assert(neo::parseKey(L"")==0);
 assert(neo::parseKey(L"Tab")==VK_TAB&&neo::parseKey(L"scrolllock")==VK_SCROLL);
 assert(neo::parseKey(L"F8")==VK_F8&&neo::parseKey(L"f12")==VK_F12);
 assert(neo::parseKey(L"m")=='M'&&neo::parseKey(L"7")=='7');
 assert(neo::parseKey(L"123")==123);
}

void edgeTriggerFiresOncePerPress(){
 neo::EdgeTrigger edge;
 assert(edge.rising(true)&&!edge.rising(true)&&!edge.rising(false)&&edge.rising(true));
}

void mappingReadsButtonsKnobsSticksAndClutch(){
 neo::ControlMapping mapping;
 mapping.buttons[8]=13;mapping.sticks[0]={21,19,16,22,17,-1};mapping.clutches[0]={7,-10000,-800};
 DIJOYSTATE2 state{};state.rgbButtons[13]=0x80;state.rgbButtons[21]=0x80;state.rgbButtons[8]=0x80;state.rglSlider[1]=-5400;
 auto first=mapping.sample(&state);
 assert(first[8].down&&!first[0].down);
 assert(first[neo::firstStick].y==1&&first[neo::firstStick].x==0);
 assert(first[neo::firstKnob].turn==-1);                 // B9 held = one clockwise detent
 assert(approx(first[10].analog,.5f));                    // halfway between rest and full
 assert(mapping.sample(&state)[neo::firstKnob].turn==0); // still held: no second detent
 state.rgdwPOV[0]=9000;mapping.sticks[1].pov=0;
 auto hat=mapping.sample(&state)[neo::firstStick+1];
 assert(hat.x==1&&hat.y==0);                             // 90 degrees = right
 auto released=mapping.sample(nullptr);
 assert(!released[8].down&&released[10].analog<0);       // unreadable device: everything released
}

void dragMovesAdjustsAndClamps(){
 neo::Placement start;
 auto moved=neo::dragged(start,neo::DragMode::Move,{100,100,0,-200,1},1);
 assert(approx(moved.x,.05f)&&approx(moved.y,-.35f));        // right and down on screen = right and down
 assert(approx(moved.z,-.5f-.1f-.01f));                    // drag up and scroll up = farther
 auto adjusted=neo::dragged(start,neo::DragMode::Adjust,{0,0,50,-100,0},2);
 assert(approx(adjusted.tiltDegrees,20)&&approx(adjusted.yawDegrees,10));
 auto limited=neo::dragged(start,neo::DragMode::Move,{100000,0,0,100000,0},1);
 assert(limited.x==1&&limited.z==-.1f);
}

void calibrationRoundTripsAndRejectsBadInput(){
 XMMATRIX head=XMMatrixRotationY(.5f)*XMMatrixTranslation(.1f,1.2f,-.3f);
 auto parsed=neo::parseCalibration(neo::formatCalibration(head));
 assert(parsed);
 XMFLOAT4X4 expected;XMStoreFloat4x4(&expected,head);
 for(int r=0;r<4;r++)for(int c=0;c<4;c++)assert(approx(parsed->m[r][c],expected.m[r][c]));
 assert(!neo::parseCalibration(L""));
 assert(!neo::parseCalibration(L"0.0821,0.1935,-0.2674,0.8195")); // pre-0.2.1 format
 assert(!neo::parseCalibration(L"0,0,0,0,0,0,2"));                // not a unit quaternion
}

// iRacing's space is STAGE rolled 180 degrees. Levelling must use real gravity, not that space's +Y.
void levellingKeepsHeadingInARolledGameSpace(){
 XMMATRIX levelToGame=XMMatrixRotationZ(neo::pi);
 XMMATRIX headInLevel=XMMatrixRotationX(-.35f)*XMMatrixRotationY(.5f)*XMMatrixTranslation(0,1.2f,0); // looking down
 XMMATRIX levelled=neo::levelHead(headInLevel*levelToGame,levelToGame)*XMMatrixInverse(nullptr,levelToGame);
 XMFLOAT4X4 got,want;XMStoreFloat4x4(&got,levelled);XMStoreFloat4x4(&want,XMMatrixRotationY(.5f)*XMMatrixTranslation(0,1.2f,0));
 for(int r=0;r<4;r++)for(int c=0;c<4;c++)assert(approx(got.m[r][c],want.m[r][c]));
}

void configSectionsAreSeparate(){
 auto folder=std::filesystem::temp_directory_path()/L"NeoXR-LayerTest-Sections";
 std::filesystem::create_directories(folder);std::filesystem::remove(folder/L"NeoXR.ini");
 neo::Config wheel(folder);auto cars=wheel.section(L"Cars");
 wheel.write(L"X",L"1");cars.write(L"X",L"2");
 assert(wheel.text(L"X")==L"1"&&cars.text(L"X")==L"2");
 cars.erase(L"X");assert(cars.text(L"X").empty()&&wheel.text(L"X")==L"1");
 std::filesystem::remove_all(folder);
}

void sessionInfoNamesThePlayersCar(){
 const std::string yaml=
  "WeekendInfo:\n TrackName: spa\nDriverInfo:\n DriverCarIdx: 2\n DriverUserID: 7\n Drivers:\n"
  " - CarIdx: 0\n   UserName: Pace Car\n   CarPath: safety pcfordmustang\n"
  " - CarIdx: 12\n   CarPath: wrong\n"
  " - CarIdx: 2\n   UserName: Driver\n   CarPath: dallarap217 \r\n   CarID: 128\n";
 assert(neo::playerCarPath(yaml)=="dallarap217");                  // not CarIdx 12, trailing space trimmed
 assert(neo::playerCarPath("DriverInfo:\n Drivers:\n")=="");
 assert(neo::playerCarPath(" DriverCarIdx: 5\n - CarIdx: 5\n - CarIdx: 6\n   CarPath: x\n")==""); // CarPath belongs to car 6
 assert(neo::carKey("safety pcfordmustang")==L"safety_pcfordmustang");
}

void carAlignmentRoundTripsAndRejectsBadInput(){
 neo::Placement p;p.x=.012f;p.y=-.28f;p.z=-.47f;p.tiltDegrees=12.5f;p.yawDegrees=-3;p.width=.3f;
 XMMATRIX head=XMMatrixRotationY(.4f)*XMMatrixTranslation(.1f,1.1f,-.2f);
 auto parsed=neo::parseAlignment(neo::formatAlignment(p,head));
 assert(parsed&&approx(parsed->placement.x,.012f)&&approx(parsed->placement.tiltDegrees,12.5f)&&approx(parsed->placement.width,.3f));
 XMFLOAT4X4 expected;XMStoreFloat4x4(&expected,head);
 for(int r=0;r<4;r++)for(int c=0;c<4;c++)assert(approx(parsed->head.m[r][c],expected.m[r][c]));
 assert(!neo::parseAlignment(L""));
 assert(!neo::parseAlignment(L"0,0,0,0,0,310"));                       // no calibration
 assert(!neo::parseAlignment(L"0,0,0,0,0|0,0,0,0,0,0,1"));             // five placement values
 assert(approx(neo::parseAlignment(L"0,0,-9,0,0,310|0,0,0,0,0,0,1")->placement.z,-1.5f)); // clamped
}

void editKeysIgnoreRepeatsAndTrackUnfocusedReleases(){
 EditMode edit;
 assert(edit.keyEvent(VK_TAB,true,true)&&edit.mode==EditMode::Move);
 for(int i=0;i<20;i++)assert(edit.keyEvent(VK_TAB,true,true)&&edit.mode==EditMode::Move);
 edit.keyEvent(VK_TAB,false,true);
 edit.keyEvent('M',true,true);
 assert(edit.mode==EditMode::Adjust);
 for(int i=0;i<20;i++)assert(edit.keyEvent('M',true,true)&&edit.mode==EditMode::Adjust);
 assert(!edit.keyEvent('M',false,false)); // release while another app has focus
 edit.keyEvent('M',true,true);
 assert(edit.mode==EditMode::Move);
 edit.keyEvent('H',true,true);assert(edit.passthrough.exchange(false));
 edit.keyEvent('H',true,true);assert(!edit.passthrough);
 edit.keyEvent(VK_ESCAPE,true,true);
 assert(edit.mode==EditMode::Confirm);
 for(int i=0;i<20;i++){edit.keyEvent(VK_ESCAPE,true,true);assert(edit.mode==EditMode::Confirm);}
 edit.keyEvent(VK_ESCAPE,false,true);edit.keyEvent(VK_ESCAPE,true,true);
 assert(edit.mode==EditMode::Off&&edit.result==EditMode::Cancel);
}

int main(){
 {
  neo::Placement before,after=before,car;car.x=.162f;car.tiltDegrees=.9f;car.yawDegrees=-16.1f;car.width=.331f;
  after.tiltDegrees=15;
  assert(neo::applyPlacementChanges(car,before,after));assert(car.tiltDegrees==15);
  assert(approx(car.x,.162f)&&approx(car.yawDegrees,-16.1f)&&approx(car.width,.331f));
  assert(!neo::applyPlacementChanges(car,after,after)); // unrelated settings reload keeps saved car tilt
  neo::Placement rotation;rotation.tiltDegrees=90;
  XMFLOAT3 normal;XMStoreFloat3(&normal,XMVector3TransformNormal(XMVectorSet(0,0,1,0),rotation.rotation()));
  assert(approx(normal.y,-1)&&approx(normal.z,0));
 }
 editKeysIgnoreRepeatsAndTrackUnfocusedReleases();
 configReadsClampsAndResolvesPaths();
 configSectionsAreSeparate();
 sessionInfoNamesThePlayersCar();
 carAlignmentRoundTripsAndRejectsBadInput();
 keysParseNamesFunctionKeysAndLetters();
 edgeTriggerFiresOncePerPress();
 mappingReadsButtonsKnobsSticksAndClutch();
 dragMovesAdjustsAndClamps();
 calibrationRoundTripsAndRejectsBadInput();
 levellingKeepsHeadingInARolledGameSpace();
 std::cout<<"PASS: config, keys, edge triggers, control mapping, mouse placement, calibration, levelling and per-car alignment\n";
}
