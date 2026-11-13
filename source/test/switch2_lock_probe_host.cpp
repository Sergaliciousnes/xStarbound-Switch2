#include "../application/StarSwitch2LockProbe.hpp"
#include <cassert>
#include <cstdint>
#include <thread>
struct _reent;
namespace {
unsigned heap=0,wait=0,wake=0;
uint32_t* expected=reinterpret_cast<uint32_t*>(uintptr_t(0x1000));
}
extern "C" {
void __wrap___malloc_lock(_reent*);
uint32_t __wrap_svcArbitrateLock(uint32_t,uint32_t*,uint32_t);
uint32_t __wrap_svcArbitrateUnlock(uint32_t*);
void __real___malloc_lock(_reent* p){assert(p==nullptr);++heap;}
uint32_t __real_svcArbitrateLock(uint32_t a,uint32_t* p,uint32_t b){assert(a==5&&p==expected&&b==7);++wait;return 123;}
uint32_t __real_svcArbitrateUnlock(uint32_t* p){assert(p==expected);++wake;return 456;}
}
int main(){
  namespace P=Star::LockProbe;
  P::Counters c;P::attach(&c);
  __wrap___malloc_lock(nullptr);assert(c.heapCalls==0&&heap==1);
  P::enable(true);
  for(unsigned i=0;i<32;++i){__wrap___malloc_lock(nullptr);assert(__wrap_svcArbitrateLock(5,expected,7)==123);assert(__wrap_svcArbitrateUnlock(expected)==456);}
  assert(c.heapCalls==32&&c.heapSamples==2&&heap==33);
  assert(c.waitCalls==32&&c.waitSamples==2&&c.waitErrors==32&&wait==32);
  assert(c.wakeCalls==32&&c.wakeSamples==2&&c.wakeErrors==32&&wake==32);
  std::thread background([]{__wrap___malloc_lock(nullptr);});background.join();assert(c.heapCalls==32&&heap==34);
  P::attach(nullptr);__wrap___malloc_lock(nullptr);assert(c.heapCalls==32&&heap==35);
  P::enable(false);P::attach(&c);__wrap___malloc_lock(nullptr);assert(c.heapCalls==32&&heap==36);
}
