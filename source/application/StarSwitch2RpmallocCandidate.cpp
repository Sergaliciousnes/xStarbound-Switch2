#include "StarSwitch2RpmallocCandidate.hpp"
#include "rpmalloc.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#ifdef _WIN32
#include <malloc.h>
#else
#include <malloc.h>
#endif
namespace Star::RpmallocCandidate {
namespace {
constexpr size_t Alignment=65536,Budget=128*1024*1024;
struct Mapping {void* base=nullptr;size_t size=0;};
std::array<Mapping,512> mappings{};
std::mutex mutex;
Backing stats;
bool reject=false,initialized=false;
void* map(size_t bytes,size_t* offset){
  // Charge an alignment allowance too. This is a bounded backing-request
  // budget, not an OS resident-memory measurement or the whole process heap.
  size_t charged=bytes+Alignment;
  size_t slot=mappings.size();
  {
    std::lock_guard<std::mutex> guard(mutex);
    if(reject||bytes>Budget||charged<bytes||charged>Budget-stats.current){++stats.rejected;errno=ENOMEM;return nullptr;}
    for(size_t i=0;i<mappings.size();++i)if(!mappings[i].size){slot=i;break;}
    if(slot==mappings.size()){++stats.rejected;errno=ENOMEM;return nullptr;}
    mappings[slot].size=bytes;stats.current+=charged;stats.peak=std::max(stats.peak,stats.current);
  }
#ifdef _WIN32
  void* p=_aligned_malloc(bytes,Alignment);
#else
  void* p=::memalign(Alignment,bytes);
#endif
  {
    std::lock_guard<std::mutex> guard(mutex);
    if(!p){mappings[slot]={};stats.current-=charged;++stats.rejected;errno=ENOMEM;return nullptr;}
    mappings[slot].base=p;++stats.maps;++stats.active;
  }
  *offset=0;return p;
}
void unmap(void* address,size_t,size_t offset,size_t bytes){
  if(!bytes){std::lock_guard<std::mutex> guard(mutex);++stats.partial;return;}
  // Partial decommit retains its SDK allocation. Only an entire original
  // mapping is returned; never pass a subspan or rpmalloc block to SDK free.
  void* p=nullptr;
  {
    std::lock_guard<std::mutex> guard(mutex);
    for(auto& m:mappings)if(m.base==address&&m.size==bytes&&offset==0){
      p=m.base;stats.current-=m.size+Alignment;m={};++stats.releases;--stats.active;break;
    }
    if(!p){++stats.errors;return;}
  }
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
}
bool initialize(){
  if(initialized)return true;
  rpmalloc_config_t config{};config.memory_map=map;config.memory_unmap=unmap;
  config.page_size=4096;config.span_size=Alignment;config.span_map_count=16;
  initialized=rpmalloc_initialize_config(&config)==0&&rpmalloc_is_thread_initialized();
  return initialized;
}
void shutdown(){if(initialized){rpmalloc_finalize();initialized=false;}}
void enter(){rpmalloc_thread_initialize();}
void leave(){rpmalloc_thread_finalize(1);}
void* allocate(size_t n){return rpmalloc(n);}
void* zero(size_t n,size_t size){return rpcalloc(n,size);}
void* resize(void* p,size_t n){return rprealloc(p,n);}
void* aligned(size_t alignment,size_t n){return rpaligned_alloc(alignment,n);}
void release(void* p){rpfree(p);}
Backing backing(){std::lock_guard<std::mutex> guard(mutex);return stats;}
void rejectNewBacking(bool value){std::lock_guard<std::mutex> guard(mutex);reject=value;}
}
