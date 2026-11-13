// Tests the actual portable profiler with deterministic counts and concurrency.
#include "StarSwitch2IoProfile.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace Star::Switch2IoProfile;
std::string read(std::filesystem::path const& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  assert(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  auto pack=file("sdmc:/switch/xstarbound/assets/packed.pak"),world=file("sdmc:/world/test.world");
  assert(pack!=world&&pack==file("sdmc:/switch/xstarbound/assets/packed.pak"));
  start();beginFrame(42);
  {
    Asset asset("/scripts/test.lua");
    {Operation op(pack,Op::ReadAbsolute,4096,123);std::this_thread::sleep_for(std::chrono::milliseconds(1));op.acquired();std::this_thread::sleep_for(std::chrono::milliseconds(3));op.complete(256);}
    {Asset nested("/images/test.png");Operation op(pack,Op::ReadAbsolute,10,512);op.acquired();op.complete(10);}
    {Operation op(pack,Op::ReadAbsolute,5,900);op.acquired();op.complete(5);}
  }
  {Operation op(world,Op::Write,7);op.acquired();op.complete(7);}
  {Operation error(world,Op::Read,16);error.acquired();} // incomplete represents thrown API operation
  cache(Cache::ScriptHit);cache(Cache::ScriptMiss);
  endFrame(3,10000);
  std::vector<std::thread> workers;
  for(unsigned i=0;i<4;++i)workers.emplace_back([=](){threadName("worker");for(unsigned n=0;n<1000;++n){Operation op(world,Op::ReadAbsolute,4,n*4);op.acquired();op.complete(4);cache(Cache::BlockHit);}});
  for(auto& worker:workers)worker.join();
  std::promise<void> active,release;auto done=release.get_future();
  std::thread late([&](){threadName("in-flight");Operation op(world,Op::Read,4);active.set_value();done.wait();op.acquired();op.complete(4);});
  active.get_future().wait();
  assert(!dump((out/"missing/report.log").string().c_str()));
  assert(dump((out/"io.log").string().c_str()));
  release.set_value();late.join();
  assert(dump((out/"retry.log").string().c_str()));
  auto text=read(out/"io.log");assert(text==read(out/"retry.log"));
  assert(text.find("frame=42 scene=3")!=std::string::npos);
  assert(text.find("category=script calls=2 bytes=261")!=std::string::npos);
  assert(text.find("category=image calls=1 bytes=10")!=std::string::npos);
  assert(text.find("included=0 name=in-flight")!=std::string::npos);
  assert(text.find("op=read calls=1 requested=16 bytes=0")!=std::string::npos);
  assert(text.find("errors=1")!=std::string::npos);
  unsigned workerRows=0;std::istringstream lines(text);std::string line;
  while(std::getline(lines,line))if(line.find("[file-total]")==0&&line.find("op=pread calls=1000 requested=4000 bytes=4000")!=std::string::npos)++workerRows;
  assert(workerRows==4);
  assert(text.find("kind=block-hit calls=1000")!=std::string::npos);
  std::printf("PASS: exact main/background counts, partial/error bytes, logical asset restoration, cache counts, frame attribution, in-flight freeze omission, failed-open retry and stable report.\n");
}
