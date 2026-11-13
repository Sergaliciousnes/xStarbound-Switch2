#include "StarSwitch2GameAllocator.hpp"
#include "../application/StarSwitch2RpmallocCandidate.hpp"
#include "../extern/rpmalloc-switch2/rpmalloc.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <malloc.h>
#include <new>
#include <algorithm>
#include <cstring>
#ifdef STAR_GAME_ALLOCATOR_HOST
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <switch.h>
#include <pthread.h>
extern "C" void* __aarch64_read_tp(void);
extern "C" void __real_free(void*);
extern "C" void* __real_realloc(void*,size_t);
#endif
namespace Star::Switch2GameAllocator {
namespace {
namespace RP=RpmallocCandidate;
std::atomic<bool> enabled{false};size_t backingBudget=0;
std::atomic<size_t> registered{0},exited{0};
// Trivial TLS stays available while later TLS destructors run. Once our exit
// hook fires, late allocations use SDK memory and cannot resurrect a heap
// after the platform's final destructor callback iteration.
thread_local bool retiring=false;
#ifdef STAR_GAME_ALLOCATOR_HOST
DWORD key=FLS_OUT_OF_INDEXES;
void NTAPI finalizeThread(void*){retiring=true;RP::leave();++exited;}
bool setThreadKey(){if(FlsGetValue(key))return true;if(!FlsSetValue(key,reinterpret_cast<void*>(1)))return false;++registered;return true;}
#else
pthread_key_t key;
void finalizeThread(void*){retiring=true;RP::leave();++exited;}
bool setThreadKey(){if(pthread_getspecific(key))return true;if(pthread_setspecific(key,reinterpret_cast<void*>(1))!=0)return false;++registered;return true;}
#endif
bool tlsReady(){
#ifdef STAR_GAME_ALLOCATOR_HOST
  return true;
#else
  return __aarch64_read_tp()!=nullptr;
#endif
}
bool route(){return enabled.load(std::memory_order_acquire)&&tlsReady()&&!retiring;}
bool heap(){
  if(rpmalloc_is_thread_initialized())return true;
  RP::enter();
  if(!rpmalloc_is_thread_initialized())return false;
  if(!setThreadKey()){RP::leave();return false;}return true;
}
void sdkFree(void* p,size_t alignment){
#ifdef STAR_GAME_ALLOCATOR_HOST
  if(alignment>alignof(std::max_align_t))_aligned_free(p);else std::free(p);
#else
  (void)alignment;__real_free(p);
#endif
}
void* alignedAllocate(size_t n,size_t alignment){
  if(!alignment||alignment<=alignof(std::max_align_t))return allocate(n);
  if(alignment&(alignment-1)){errno=EINVAL;return nullptr;}
  if(route()&&alignment<65536){if(!heap()){errno=ENOMEM;return nullptr;}return RP::aligned(alignment,n);}
#ifdef STAR_GAME_ALLOCATOR_HOST
  return _aligned_malloc(n,alignment);
#else
  return ::memalign(alignment,n);
#endif
}
}
bool active() noexcept{return enabled.load(std::memory_order_acquire);}
ThreadCounters threads() noexcept{return {registered.load(),exited.load()};}
bool retiringThread() noexcept{return active()&&(!tlsReady()||retiring);}
bool owns(void const* p) noexcept{return RP::owns(p);}
bool initialize(size_t budget){
  if(active())return true;
  if(!RP::initializeForGame(budget))return false;
#ifdef STAR_GAME_ALLOCATOR_HOST
  key=FlsAlloc(finalizeThread);if(key==FLS_OUT_OF_INDEXES)return false;
#else
  if(pthread_key_create(&key,finalizeThread)!=0)return false;
#endif
  if(!setThreadKey())return false;
  backingBudget=budget;enabled.store(true,std::memory_order_release);return true;
}
void* allocate(size_t n){if(!route())return std::malloc(n);if(!heap()){errno=ENOMEM;return nullptr;}return RP::allocate(n);}
void* sdkResize(void* p,size_t n){
#ifdef STAR_GAME_ALLOCATOR_HOST
  return std::realloc(p,n);
#else
  return __real_realloc(p,n);
#endif
}
void* resize(void* p,size_t n){
  if(!p)return allocate(n);
  if(!owns(p))return sdkResize(p,n);
  if(!route()){
    if(!n){RP::release(p);return nullptr;}
    auto q=std::malloc(n);if(!q)return nullptr;
    std::memcpy(q,p,std::min(n,rpmalloc_usable_size(p)));RP::release(p);return q;
  }
  if(!heap()){errno=ENOMEM;return nullptr;}return RP::resize(p,n);
}
void release(void* p,size_t alignment) noexcept {if(!p)return;if(owns(p))RP::release(p);else sdkFree(p,alignment);}
void* cppAllocate(size_t n,size_t alignment){if(!n)n=1;for(;;){if(void* p=alignedAllocate(n,alignment))return p;auto handler=std::get_new_handler();if(!handler)throw std::bad_alloc();handler();}}
void* cppAllocateNoThrow(size_t n,size_t alignment) noexcept {try{return cppAllocate(n,alignment);}catch(...){return nullptr;}}
#ifndef STAR_SWITCH2_TIMING_DISABLED
void report(FILE* f,char const* phase){auto s=RP::backing();auto t=threads();std::fprintf(f,"[game-allocator] phase=%s active=%d rpmalloc=1.4.5 C++/Star/Lua-routing=enabled C-malloc=SDK free/realloc=ownership-routed budget=%llu charged=%llu peak=%llu maps=%llu releases=%llu rejected=%llu ownership-errors=%llu thread-heaps=%llu exited-thread-heaps=%llu lifecycle=process-lifetime\n",phase,active(),(unsigned long long)backingBudget,(unsigned long long)s.current,(unsigned long long)s.peak,(unsigned long long)s.maps,(unsigned long long)s.releases,(unsigned long long)s.rejected,(unsigned long long)s.errors,(unsigned long long)t.registered,(unsigned long long)t.exited);
#ifndef STAR_GAME_ALLOCATOR_HOST
  u64 total=0,used=0;auto rt=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);auto ru=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
  std::fprintf(f,"[process-memory] total=%llu used=%llu total-result=0x%x used-result=0x%x (SDK heap reservation is included; not allocator live bytes)\n",(unsigned long long)total,(unsigned long long)used,rt,ru);
#endif
  std::fflush(f);}
void reportFile(char const* phase,bool append){if(FILE* f=std::fopen("sdmc:/xstarbound-rpmalloc.log",append?"a":"w")){report(f,phase);std::fclose(f);}}
#endif
}
