#include "../application/StarSwitch2RpmallocCandidate.hpp"
#include "../extern/rpmalloc-switch2/rpmalloc.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <new>
#include <malloc.h>
namespace Star::RpmallocCandidate {
namespace {
constexpr size_t Alignment=65536,OwnerSlots=262144;
struct Mapping {void* base=nullptr;size_t requested=0,rounded=0;};
struct State {
  std::array<Mapping,8192> mappings{};
  std::array<std::atomic<uintptr_t>,OwnerSlots> owners{};
  std::mutex mutex;Backing stats;size_t budget=0;bool reject=false,initialized=false;
};
// Intentionally process-lifetime SDK storage, also during static destruction.
std::atomic<State*> state{nullptr};
size_t hash(uintptr_t key){return ((key>>16)*uintptr_t(11400714819323198485ULL))&(OwnerSlots-1);}
bool insert(State& s,uintptr_t key){
  size_t tomb=OwnerSlots;
  for(size_t n=0;n<OwnerSlots;++n){auto index=(hash(key)+n)&(OwnerSlots-1);auto k=s.owners[index].load(std::memory_order_relaxed);
    if(k==key)return false;
    if(k==1&&tomb==OwnerSlots)tomb=index;
    if(!k){s.owners[tomb==OwnerSlots?index:tomb].store(key,std::memory_order_release);return true;}}
  if(tomb<OwnerSlots){s.owners[tomb].store(key,std::memory_order_release);return true;}return false;
}
bool erase(State& s,uintptr_t key){
  for(size_t n=0;n<OwnerSlots;++n){auto& slot=s.owners[(hash(key)+n)&(OwnerSlots-1)];auto k=slot.load(std::memory_order_relaxed);if(!k)return false;if(k==key){slot.store(1,std::memory_order_release);return true;}}return false;
}
void backingFree(void* p){
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void* map(size_t bytes,size_t* offset){
  auto& s=*state.load(std::memory_order_acquire);
  if(bytes>SIZE_MAX-(Alignment-1)){errno=ENOMEM;return nullptr;}
  size_t rounded=(bytes+Alignment-1)&~(Alignment-1),charged=rounded+Alignment,slot=s.mappings.size();
  {std::lock_guard<std::mutex> guard(s.mutex);
    if(s.reject||charged<rounded||charged>s.budget-s.stats.current){++s.stats.rejected;errno=ENOMEM;return nullptr;}
    for(size_t i=0;i<s.mappings.size();++i)if(!s.mappings[i].requested){slot=i;break;}
    if(slot==s.mappings.size()){++s.stats.rejected;errno=ENOMEM;return nullptr;}
    s.mappings[slot].requested=bytes;s.mappings[slot].rounded=rounded;s.stats.current+=charged;s.stats.peak=std::max(s.stats.peak,s.stats.current);}
  // The entire SDK allocation is rounded to ownership-cell boundaries, so its
  // last cell cannot also contain an unrelated live SDK allocation.
#ifdef _WIN32
  void* p=_aligned_malloc(rounded,Alignment);
#else
  void* p=::memalign(Alignment,rounded);
#endif
  bool registered=p!=nullptr;
  {std::lock_guard<std::mutex> guard(s.mutex);size_t done=0;
    if(p)for(;done<rounded;done+=Alignment)if(!insert(s,reinterpret_cast<uintptr_t>(p)+done)){registered=false;++s.stats.errors;break;}
    if(!registered){for(size_t n=0;n<done;n+=Alignment)erase(s,reinterpret_cast<uintptr_t>(p)+n);s.mappings[slot]={};s.stats.current-=charged;++s.stats.rejected;}
    else{s.mappings[slot].base=p;++s.stats.maps;++s.stats.active;}}
  if(!registered){if(p)backingFree(p);errno=ENOMEM;return nullptr;}*offset=0;return p;
}
void unmap(void* address,size_t,size_t offset,size_t bytes){
  auto& s=*state.load(std::memory_order_acquire);
  if(!bytes){std::lock_guard<std::mutex> guard(s.mutex);++s.stats.partial;return;}void* p=nullptr;
  {std::lock_guard<std::mutex> guard(s.mutex);
    for(auto& m:s.mappings)if(m.base==address&&m.requested==bytes&&offset==0){p=m.base;for(size_t n=0;n<m.rounded;n+=Alignment)if(!erase(s,reinterpret_cast<uintptr_t>(p)+n))++s.stats.errors;s.stats.current-=m.rounded+Alignment;m={};++s.stats.releases;--s.stats.active;break;}
    if(!p){++s.stats.errors;return;}}
  // Publish ownership removal before SDK free makes this range reusable.
  backingFree(p);
}
}
bool owns(void const* p) noexcept {
  if(!p)return false;
  auto s=state.load(std::memory_order_acquire);if(!s)return false;auto key=reinterpret_cast<uintptr_t>(p)&~uintptr_t(Alignment-1);
  for(size_t n=0;n<OwnerSlots;++n){auto k=s->owners[(hash(key)+n)&(OwnerSlots-1)].load(std::memory_order_acquire);if(!k)return false;if(k==key)return true;}return false;
}
bool initializeForGame(size_t budget){
  auto s=state.load(std::memory_order_acquire);
  if(!s){void* raw=std::malloc(sizeof(State));if(!raw)return false;s=new(raw) State();state.store(s,std::memory_order_release);}
  if(s->initialized)return true;
  s->budget=budget;
  rpmalloc_config_t config{};config.memory_map=map;config.memory_unmap=unmap;config.page_size=4096;config.span_size=Alignment;config.span_map_count=16;
  s->initialized=rpmalloc_initialize_config(&config)==0&&rpmalloc_is_thread_initialized();return s->initialized;
}
bool initialize(){return initializeForGame(128*1024*1024);}
void shutdown(){auto s=state.load();if(s&&s->initialized){rpmalloc_finalize();s->initialized=false;}}
void enter(){rpmalloc_thread_initialize();}
void leave(){rpmalloc_thread_finalize(1);}
void* allocate(size_t n){return rpmalloc(n);}
void* zero(size_t n,size_t size){return rpcalloc(n,size);}
void* resize(void* p,size_t n){return rprealloc(p,n);}
void* aligned(size_t alignment,size_t n){return rpaligned_alloc(alignment,n);}
void release(void* p){rpfree(p);}
Backing backing(){auto s=state.load();if(!s)return {};std::lock_guard<std::mutex> guard(s->mutex);return s->stats;}
void rejectNewBacking(bool value){auto s=state.load();std::lock_guard<std::mutex> guard(s->mutex);s->reject=value;}
}
