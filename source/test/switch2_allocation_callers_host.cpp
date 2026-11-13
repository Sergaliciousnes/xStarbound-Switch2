#include "StarSwitch2AllocationProfile.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <thread>
using namespace Star::Switch2AllocationProfile;
std::thread::id owner;
bool detailed=false;
namespace Star::Switch2WaitProfile {
bool allocationContext(Context& c){if(std::this_thread::get_id()!=owner||!detailed)return false;c={20,3,2,"render"};return true;}
}
extern "C" {
void* __wrap_malloc(size_t);void __wrap_free(void*);
void* __wrap_calloc(size_t,size_t);void* __wrap_realloc(void*,size_t);
void* __wrap__Znwm(size_t) noexcept(false);void* __wrap__Znam(size_t) noexcept(false);
void __wrap__ZdlPv(void*) noexcept;void __wrap__ZdaPv(void*) noexcept;
void __wrap__ZdlPvm(void*,size_t) noexcept;void __wrap__ZdaPvm(void*,size_t) noexcept;
void* __real_malloc(size_t n){if(n==SIZE_MAX){errno=ENOMEM;return nullptr;}if(n==777)std::this_thread::sleep_for(std::chrono::milliseconds(2));auto p=std::malloc(n);errno=EDOM;return p;}
void __real_free(void* p){std::free(p);errno=ERANGE;}
void* __real_calloc(size_t n,size_t size){return std::calloc(n,size);}
void* __real_realloc(void* p,size_t n){return std::realloc(p,n);}
void* __real__Znwm(size_t n) noexcept(false){auto p=__wrap_malloc(n);if(!p)throw std::bad_alloc();return p;}
void* __real__Znam(size_t n) noexcept(false){return __real__Znwm(n);}
void __real__ZdlPv(void* p) noexcept{__wrap_free(p);}
void __real__ZdaPv(void* p) noexcept{__wrap_free(p);}
void __real__ZdlPvm(void* p,size_t) noexcept{__wrap_free(p);}
void __real__ZdaPvm(void* p,size_t) noexcept{__wrap_free(p);}
}
std::string read(std::filesystem::path const& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  assert(argc==2);owner=std::this_thread::get_id();std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  auto early=__wrap_malloc(8);__wrap_free(early); // disabled startup path
  start();frame(false);
  auto outside=__wrap_malloc(8);__wrap_free(outside);
  detailed=true;frame(true);
  for(unsigned i=0;i<10000;++i){
    auto p=__wrap__Znwm(32+(i%3)*128);assert(p&&errno==EDOM);std::memset(p,0x5a,32);__wrap__ZdlPv(p);assert(errno==ERANGE);
    p=__wrap_malloc(64);assert(p&&errno==EDOM);__wrap_free(p);assert(errno==ERANGE);
  }
  auto p=__wrap__Znam(256);__wrap__ZdaPvm(p,256);
  p=__wrap__Znam(256);__wrap__ZdaPv(p);
  p=__wrap__Znwm(128);__wrap__ZdlPvm(p,128);
  p=__wrap_calloc(16,4);for(unsigned i=0;i<64;++i)assert(static_cast<unsigned char*>(p)[i]==0);
  std::memset(p,0x5a,64);p=__wrap_realloc(p,128);for(unsigned i=0;i<64;++i)assert(static_cast<unsigned char*>(p)[i]==0x5a);__wrap_free(p);
  try{__wrap__Znwm(SIZE_MAX);assert(false);}catch(std::bad_alloc const&){}
  p=__wrap_malloc(64);__wrap_free(p); // verifies nesting restored after throw
  p=__wrap_malloc(777);__wrap_free(p); // every eligible API is timed, including unsampled sites
  __wrap_free(nullptr);
  std::thread background([](){thread("worker metadata");auto p=__wrap_malloc(16);__wrap_free(p);});background.join();
  assert(!dump((out/"missing/report.log").string().c_str()));
  assert(dump((out/"allocations.log").string().c_str()));
  p=__wrap_malloc(8);__wrap_free(p);assert(dump((out/"retry.log").string().c_str()));
  auto text=read(out/"allocations.log");assert(text==read(out/"retry.log"));
  assert(text.find("frames=2 detailed=1")!=std::string::npos);
  assert(text.find("kind=new calls=10002")!=std::string::npos);
  assert(text.find("kind=delete calls=10000")!=std::string::npos);
  assert(text.find("kind=malloc calls=10002")!=std::string::npos);
  assert(text.find("kind=free calls=10003")!=std::string::npos);
  assert(text.find("kind=new-array calls=2")!=std::string::npos);
  assert(text.find("kind=delete-array-sized calls=1")!=std::string::npos);
  assert(text.find("[caller]")!=std::string::npos);
  assert(text.find("[slow]")!=std::string::npos);
  assert(text.find("name=worker metadata")!=std::string::npos);
  assert(text.find("dropped-samples=0")!=std::string::npos);
  std::puts("PASS: original API forwarding, data/errno preservation, nested-call suppression, failure unwind, background/disabled exclusion, bounded caller samples and dump retry.");
}
