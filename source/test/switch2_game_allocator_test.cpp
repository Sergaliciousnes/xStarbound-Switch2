#include "../core/StarSwitch2GameAllocator.hpp"
#include "../application/StarSwitch2RpmallocCandidate.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#ifdef STAR_SWITCH2_SHARED_BOUNDARY_TEST
#include <stdexcept>
extern "C" void* boundarySdkAllocate();
extern "C" void boundarySdkFree(void*);
extern "C" char* boundaryCppAllocate();
extern "C" void boundaryCppFree(char*);
extern "C" void boundaryThrow();
#endif
#ifdef __SWITCH2__
#include <switch.h>
#endif
namespace G=Star::Switch2GameAllocator;
extern "C" void __wrap_free(void*);
extern "C" void* __wrap_realloc(void*,size_t);
namespace {
void* earlyCpp=nullptr;void* earlyC=nullptr;char const* reportPath=nullptr;
struct Lifetime {
  Lifetime(){earlyCpp=::operator new(87);earlyC=std::malloc(91);if(earlyC)std::memset(earlyC,0x31,91);}
  ~Lifetime(){if(reportPath){auto p=new(std::nothrow) char[128];bool ok=p&&G::owns(p);delete[] p;if(FILE* f=std::fopen(reportPath,"a")){std::fprintf(f,"late-static-destructor=%s\n",ok?"verified":"FAILED");std::fclose(f);}}}
} lifetime;
bool check(bool value,FILE* f,char const* name){if(!value)std::fprintf(f,"FAILED: %s\n",name);return value;}
struct Tail {
  unsigned char* saved=static_cast<unsigned char*>(G::allocate(99));
  Tail(){if(!saved)std::abort();std::memset(saved,0x3d,99);}
  ~Tail(){bool retired=G::retiringThread();auto q=static_cast<unsigned char*>(G::resize(saved,1024));if(!q)std::abort();for(unsigned i=0;i<99;++i)if(q[i]!=0x3d)std::abort();if(retired&&G::owns(q))std::abort();__wrap_free(q);auto p=new(std::nothrow) int(17);if(!p||*p!=17||(retired&&G::owns(p)))std::abort();delete p;}
};
thread_local Tail tail;
struct alignas(256) Aligned {unsigned char bytes[513];};
struct Throws {Throws(){throw 3;}};
struct alignas(256) ThrowsAligned {ThrowsAligned(){throw 3;}};
std::atomic<unsigned> handlerCalls{0};
}
int main(int argc,char** argv){
#ifdef __SWITCH2__
  (void)argc;(void)argv;reportPath="sdmc:/xstarbound-rpmalloc-routing.log";
#ifdef STAR_SWITCH2_SHARED_BOUNDARY_TEST
  reportPath="sdmc:/xstarbound-shared-routing.log";
#endif
#else
  reportPath=argc>1?argv[1]:"game-allocator-host.log";
#endif
  FILE* f=std::fopen(reportPath,"w");if(!f)return 1;std::setvbuf(f,nullptr,_IONBF,0);
  bool ok=check(earlyCpp&&earlyC&&!G::owns(earlyCpp)&&!G::owns(earlyC),f,"early SDK ownership");
  ok&=check(G::initialize(64*1024*1024),f,"initialize");if(!ok){std::fclose(f);return 1;}
  ::operator delete(earlyCpp);earlyCpp=nullptr;
#ifdef STAR_SWITCH2_SHARED_BOUNDARY_TEST
  auto boundaryCheck=[&]{
    auto sdk=boundarySdkAllocate();
    bool passed=sdk&&!G::owns(sdk);
    __wrap_free(sdk);
    sdk=std::malloc(256);
    passed&=sdk&&!G::owns(sdk);
    boundarySdkFree(sdk);
    auto cpp=boundaryCppAllocate();
    passed&=cpp&&G::owns(cpp);
    delete[] cpp;
    cpp=new char[256];
    passed&=G::owns(cpp);
    boundaryCppFree(cpp);
    bool caught=false;
    try{boundaryThrow();}catch(std::runtime_error const& e){caught=std::strlen(e.what())==256;}
    return passed&&caught;
  };
  ok&=check(boundaryCheck(),f,"shared-library C/C++ allocation ownership and exceptions");
  bool boundaryWorkerOk=false;
  std::thread boundaryWorker([&]{boundaryWorkerOk=boundaryCheck();});boundaryWorker.join();
  ok&=check(boundaryWorkerOk,f,"shared-library ownership and exceptions on worker thread");
  std::fprintf(f,"shared-boundary-tests=%s\n",ok?"verified":"FAILED");
#endif
  auto resized=static_cast<unsigned char*>(__wrap_realloc(earlyC,8192));ok&=check(resized&&!G::owns(resized),f,"foreign SDK realloc");
  if(resized){for(unsigned i=0;i<91;++i)ok&=resized[i]==0x31;__wrap_free(resized);}else __wrap_free(earlyC);earlyC=nullptr;
  for(size_t n:{size_t(0),size_t(1),size_t(128),size_t(65537),size_t(2097171)}){auto p=::operator new(n);ok&=check(p&&G::owns(p),f,"C++ ownership");::operator delete(p);}
  for(size_t a:{size_t(16),size_t(256),size_t(32768),size_t(65536),size_t(131072)}){
    auto p=::operator new(527,std::align_val_t(a));ok&=check(p&&reinterpret_cast<uintptr_t>(p)%a==0,f,"alignment");if(a>=65536)ok&=!G::owns(p);::operator delete(p,std::align_val_t(a));
  }
  auto object=new Aligned;std::memset(object->bytes,0x71,sizeof(object->bytes));ok&=G::owns(object);delete object;
  try{auto p=new Throws;delete p;ok=false;}catch(int){}
  try{auto p=new ThrowsAligned;delete p;ok=false;}catch(int){}
  auto p=static_cast<unsigned char*>(G::allocate(99));ok&=check(p&&G::owns(p),f,"Star allocation");
  if(p){std::memset(p,0x69,99);auto q=static_cast<unsigned char*>(__wrap_realloc(p,4096));ok&=q&&G::owns(q);if(q){for(unsigned i=0;i<99;++i)ok&=q[i]==0x69;__wrap_free(q);}else G::release(p);}
  for(unsigned i=0;i<4096;++i){auto sdk=std::malloc(1+i%8192);ok&=check(sdk&&!G::owns(sdk),f,"SDK provenance beside partial mappings");__wrap_free(sdk);}
  std::atomic<bool> workersOk{true};std::array<std::thread,4> workers;
  for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&]{(void)&tail;for(unsigned j=0;j<64;++j){auto x=static_cast<unsigned char*>(G::allocate(4194321));if(!x||!G::owns(x)){workersOk=false;break;}x[0]=0x32;x[4194320]=0x41;auto sdk=std::malloc(113);if(!sdk||G::owns(sdk))workersOk=false;__wrap_free(sdk);if(x[0]!=0x32||x[4194320]!=0x41)workersOk=false;__wrap_free(x);}});
  for(auto& worker:workers)worker.join();
  ok&=check(workersOk.load(),f,"concurrent map/provenance/free and TLS cleanup");
  std::array<unsigned char*,256> remote{};
  std::thread producer([&]{for(unsigned i=0;i<remote.size();++i){remote[i]=new unsigned char[128];std::memset(remote[i],int(i),128);}});producer.join();
  for(unsigned i=0;i<remote.size();++i){ok&=G::owns(remote[i]);for(unsigned j=0;j<128;++j)ok&=remote[i][j]==static_cast<unsigned char>(i);delete[] remote[i];}
  Star::RpmallocCandidate::rejectNewBacking(true);
  volatile size_t huge=128*1024*1024;auto np=::operator new(huge,std::nothrow);ok&=check(!np,f,"nothrow failure");if(np)::operator delete(np);
  bool threw=false;try{auto q=::operator new(huge);::operator delete(q);}catch(std::bad_alloc const&){threw=true;}ok&=check(threw,f,"throwing new failure");
  auto oldHandler=std::set_new_handler([]{++handlerCalls;throw std::bad_alloc();});
  threw=false;try{auto q=::operator new(huge);::operator delete(q);}catch(std::bad_alloc const&){threw=true;}
  std::set_new_handler(oldHandler);ok&=check(threw&&handlerCalls.load()==1,f,"new handler contract");
  Star::RpmallocCandidate::rejectNewBacking(false);
  auto recovered=new int(9);ok&=*recovered==9&&G::owns(recovered);delete recovered;
  auto stats=Star::RpmallocCandidate::backing();ok&=check(stats.errors==0&&stats.peak<=64*1024*1024,f,"backing accounting");
  auto threads=G::threads();ok&=check(threads.registered==threads.exited+1&&threads.exited>=5,f,"worker heap cleanup callbacks");
  G::report(f,"routing-tests-complete");std::fprintf(f,"routing-tests=%s; allocator deliberately alive for late static destruction\n",ok?"verified":"FAILED");std::fclose(f);return ok?0:1;
}
