#ifdef NDEBUG
#undef NDEBUG
#endif
#include "model.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
#include <limits>
int main(int argc,char** argv){
 assert(argc==2);auto m=neo::loadModel(argv[1]);assert(m.indices.size()/3==206786);assert(m.width>.30f&&m.width<.31f);
 bool controls[neo::controlCount]{};for(const auto& v:m.vertices)if(v.control>=0)controls[int(v.control)]=true;
 for(bool present:controls)assert(present);
 // Fitted knob/joystick centres lie inside each part, within 3 mm of the parts' measured hubs.
 const float hubs[6][2]={{.0839f,-.0448f},{-.0853f,-.0417f},{-.0462f,-.0504f},{.0449f,-.0512f},{-.046f,.0462f},{.047f,.045f}};
 for(int c=neo::firstKnob;c<neo::controlCount;c++){const auto& p=m.pivots[c];assert(p[3]==1);
  assert(std::hypot(p[0]-hubs[c-neo::firstKnob][0],p[1]-hubs[c-neo::firstKnob][1])<.003f);}
 auto bytes=std::ifstream(argv[1],std::ios::binary);std::string data((std::istreambuf_iterator<char>(bytes)),{});
 auto temp=std::filesystem::temp_directory_path()/"neoxr-model-invalid.neo";
 auto rejects=[&](const std::string& b){std::ofstream out(temp,std::ios::binary);out.write(b.data(),b.size());out.close();bool caught=false;try{neo::loadModel(temp);}catch(const std::runtime_error&){caught=true;}assert(caught);};
 rejects(data.substr(0,100));auto corrupt=data;corrupt[0]='X';rejects(corrupt);
 corrupt=data;uint32_t bad=uint32_t(m.vertices.size());std::memcpy(corrupt.data()+88+m.vertices.size()*sizeof(neo::ModelVertex),&bad,4);rejects(corrupt);
 corrupt=data;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(corrupt.data()+88,&nan,4);rejects(corrupt);
 std::filesystem::remove(temp);std::cout<<"Model geometry, controls, pivots, truncation, indices and NaN checks passed\n";
}
