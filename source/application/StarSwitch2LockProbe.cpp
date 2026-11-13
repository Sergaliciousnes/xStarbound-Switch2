#include "StarSwitch2LockProbe.hpp"
#include <algorithm>
#include <atomic>
#ifdef STAR_LOCK_PROBE_HOST
#include <chrono>
using Result=uint32_t;using Handle=uint32_t;
static uint64_t ticks(){return std::chrono::steady_clock::now().time_since_epoch().count();}
#else
#include <switch.h>
static uint64_t ticks(){return armGetSystemTick();}
#endif
struct _reent;
namespace Star::LockProbe {
namespace {
std::atomic<bool> enabled{false};
thread_local Counters* active=nullptr;
}
void enable(bool value){enabled.store(value,std::memory_order_release);}
void attach(Counters* counters){active=counters;}
Counters* current(){
  // A wrapped SDK call can happen before initialized TLS. Global guard first.
  if(!enabled.load(std::memory_order_relaxed))return nullptr;
  return active;
}
}
extern "C" {
void __real___malloc_lock(_reent*);
Result __real_svcArbitrateLock(Handle,uint32_t*,uint32_t);
Result __real_svcArbitrateUnlock(uint32_t*);
void __wrap___malloc_lock(_reent* r){
  auto s=Star::LockProbe::current();
  if(!s){__real___malloc_lock(r);return;}
  if(++s->heapCalls%16){__real___malloc_lock(r);return;}
  auto start=ticks();__real___malloc_lock(r);auto elapsed=ticks()-start;
  ++s->heapSamples;s->heapTicks+=elapsed;s->heapPeak=std::max(s->heapPeak,elapsed);
}
Result __wrap_svcArbitrateLock(Handle owner,uint32_t* mutex,uint32_t requester){
  auto s=Star::LockProbe::current();
  if(!s)return __real_svcArbitrateLock(owner,mutex,requester);
  ++s->waitCalls;bool sample=s->waitCalls%16==0;auto start=sample?ticks():0;
  auto result=__real_svcArbitrateLock(owner,mutex,requester);
  if(sample){auto elapsed=ticks()-start;++s->waitSamples;s->waitTicks+=elapsed;s->waitPeak=std::max(s->waitPeak,elapsed);}
  s->waitErrors+=result!=0;return result;
}
Result __wrap_svcArbitrateUnlock(uint32_t* mutex){
  auto s=Star::LockProbe::current();
  if(!s)return __real_svcArbitrateUnlock(mutex);
  ++s->wakeCalls;bool sample=s->wakeCalls%16==0;auto start=sample?ticks():0;
  auto result=__real_svcArbitrateUnlock(mutex);
  if(sample){auto elapsed=ticks()-start;++s->wakeSamples;s->wakeTicks+=elapsed;s->wakePeak=std::max(s->wakePeak,elapsed);}
  s->wakeErrors+=result!=0;return result;
}
}
