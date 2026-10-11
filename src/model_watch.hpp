#pragma once
#include "model.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

namespace neo {
// File IO and Assimp parsing happen off the game thread. Only a completed, stable generation is published.
class ModelWatch {
public:
 ModelWatch():worker_([this]{run();}){}
 ~ModelWatch(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}
 ModelWatch(const ModelWatch&)=delete;ModelWatch& operator=(const ModelWatch&)=delete;
 void select(const std::filesystem::path& path){
  std::lock_guard<std::mutex> lock(mutex_);if(path==path_)return;
  path_=path;++generation_;pending_.reset();error_.clear();wake_.notify_one();
 }
 std::shared_ptr<Model> take(std::string& error){
  std::lock_guard<std::mutex> lock(mutex_);error=std::move(error_);error_.clear();auto result=std::move(pending_);return result;
 }
private:
 std::mutex mutex_;std::condition_variable wake_;bool stop_=false;
 std::filesystem::path path_;uint64_t generation_=0;
 std::shared_ptr<Model> pending_;std::string error_;std::thread worker_;
 void run(){
  uint64_t loaded=~uint64_t(0);std::vector<Dependency> dependencies;bool retry=false;std::string lastError;
  while(true){
   std::filesystem::path path;uint64_t generation;
   {std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(retry?1500:500),[&]{return stop_||generation_!=loaded;});
    if(stop_)return;path=path_;generation=generation_;}
   if(path.empty()){loaded=generation;dependencies.clear();retry=false;continue;}
   bool changed=retry||loaded!=generation;
   for(const auto& dependency:dependencies)if(fileStamp(dependency.path)!=dependency.stamp){changed=true;break;}
   if(!changed)continue;
   // Editors often truncate then rewrite. Debounce the source before trying the import.
   auto before=fileStamp(path);
   {std::unique_lock<std::mutex> lock(mutex_);if(wake_.wait_for(lock,std::chrono::milliseconds(200),[&]{return stop_||generation_!=generation;})){if(stop_)return;continue;}}
   loaded=generation;
   if(fileStamp(path)!=before){retry=true;continue;}
   try{
    auto model=std::make_shared<Model>(loadModel(path));
    std::lock_guard<std::mutex> lock(mutex_);if(generation_!=generation)continue;
    dependencies=model->dependencies;pending_=std::move(model);retry=false;lastError.clear();error_.clear();
   }catch(const std::exception& ex){
    retry=true;std::lock_guard<std::mutex> lock(mutex_);
    if(generation_==generation&&lastError!=ex.what()){lastError=ex.what();error_=lastError;}
   }catch(...){retry=true;std::lock_guard<std::mutex> lock(mutex_);error_="Model import failed";}
  }
 }
};
}
