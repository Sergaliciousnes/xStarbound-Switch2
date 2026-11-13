#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

// Explicit allocator APIs only: never replaces the process allocator. Thread
// construction, initialization, logging and cleanup stay outside timed work.
namespace Star::AllocatorContentionBenchmark {
using Clock=std::chrono::steady_clock;
struct Backend {
  char const* name;
  void* (*allocate)(size_t);
  void* (*resize)(void*,size_t);
  void (*release)(void*);
  void (*enter)();
  void (*leave)();
};
inline void noThreadHook(){}
inline Backend sdk(){return {"default-libc",std::malloc,std::realloc,std::free,noThreadHook,noThreadHook};}
struct Result {uint64_t operations=0,checks=0;double milliseconds=0;bool ok=true;Clock::time_point finished{};};
inline uint32_t next(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
inline Result churn(Backend const& api,bool mixed,unsigned identity,Clock::time_point deadline,void(*timedEnd)(void*)=nullptr,void* timedEndData=nullptr){
  constexpr unsigned Slots=256;
  constexpr size_t Sizes[]={24,48,80,128,256,512,1024};
  std::array<unsigned char*,Slots> blocks{};
  std::array<size_t,Slots> sizes{};
  Result r;uint32_t random=0x345abc01u+identity*113u;
  auto signature=[identity](unsigned i){return static_cast<unsigned char>((i+identity*17u)%251u);};
  auto started=Clock::now();
  do {
    for(unsigned i=0;i<Slots;++i){
      auto& p=blocks[i];auto& n=sizes[i];
      if(p){
        if(p[0]!=signature(i)||p[n-1]!=signature(i)){r.ok=false;break;}
        ++r.checks;
      }
      size_t wanted=Sizes[next(random)%std::size(Sizes)];
      if(mixed&&p&&(next(random)&3u)==0){
        auto q=static_cast<unsigned char*>(api.resize(p,wanted));++r.operations;
        if(!q){r.ok=false;break;}
        p=q;
        if(p[0]!=signature(i)){r.ok=false;break;}
        ++r.checks;
      }else{
        if(p){api.release(p);++r.operations;p=nullptr;}
        p=static_cast<unsigned char*>(api.allocate(wanted));++r.operations;
        if(!p){r.ok=false;break;}
      }
      n=wanted;p[0]=p[n-1]=signature(i);
    }
  }while(r.ok&&Clock::now()<deadline);
  r.finished=Clock::now();
  r.milliseconds=std::chrono::duration<double,std::milli>(r.finished-started).count();
  if(timedEnd)timedEnd(timedEndData);
  // Cleanup is excluded from timing and runs on every failure path.
  for(auto p:blocks)if(p)api.release(p);
  return r;
}
inline bool handoff(Backend const& api){
  std::array<unsigned char*,256> pointers{};bool ok=true;
  for(unsigned i=0;i<pointers.size();++i){
    pointers[i]=static_cast<unsigned char*>(api.allocate(128));
    if(!pointers[i]){ok=false;break;}
    pointers[i][0]=static_cast<unsigned char>(i);
  }
  if(ok){
    try{
      std::thread other([&]{api.enter();for(unsigned i=0;i<pointers.size();++i){if(pointers[i][0]!=static_cast<unsigned char>(i))ok=false;api.release(pointers[i]);pointers[i]=nullptr;}api.leave();});
      other.join();
    }catch(...){ok=false;}
  }
  for(auto p:pointers)if(p)api.release(p);
  return ok;
}
template<class Log,class Continue>
bool run(Backend const& api,Log log,Continue proceed,double durationMs=200){
  char line[1024];
  auto emit=[&](char const* format,auto... args){std::snprintf(line,sizeof(line),format,args...);log(line);};
  api.enter();
  if(!handoff(api)){emit("[allocator-concurrency] version=1 allocator=%s handoff-verified=false",api.name);api.leave();return false;}
  emit("[allocator-concurrency] version=1 allocator=%s handoff-verified=true slots/thread=256 sizes=24/48/80/128/256/512/1024 max-live-payload=1MiB threads=1/2/4 duration-ms=%.0f trials=3; explicit APIs, no process allocator override",api.name,durationMs);
  bool ok=true;
  for(bool mixed:{false,true})for(unsigned count:{1u,2u,4u})for(unsigned trial=1;trial<=3;++trial){
    if(!proceed()){api.leave();return false;}
    std::atomic<unsigned> ready{0};std::atomic<bool> start{false},abort{false};
    std::array<Result,4> results{};std::array<std::thread,3> helpers;
    Clock::time_point deadline;unsigned created=0;
    try{
      for(unsigned i=1;i<count;++i){
        helpers[i-1]=std::thread([&,i]{
          api.enter();++ready;
          while(!start.load(std::memory_order_acquire))std::this_thread::yield();
          if(!abort.load())results[i]=churn(api,mixed,i,deadline);
          api.leave();
        });++created;
      }
    }catch(...){abort.store(true);ok=false;}
    while(ready.load()<created)std::this_thread::yield();
    auto begin=Clock::now();deadline=begin+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(durationMs));
    start.store(true,std::memory_order_release);
    if(ok)results[0]=churn(api,mixed,0,deadline);
    for(unsigned i=0;i<created;++i)helpers[i].join();
    auto finished=begin;
    for(unsigned i=0;i<count;++i)if(results[i].finished>finished)finished=results[i].finished;
    double wall=std::chrono::duration<double,std::milli>(finished-begin).count();
    uint64_t operations=0,checks=0;
    for(unsigned i=0;i<count;++i){ok&=results[i].ok;operations+=results[i].operations;checks+=results[i].checks;}
    emit("[allocator-concurrency] allocator=%s workload=%s threads=%u trial=%u operations=%llu checks=%llu wall-ms=%.3f aggregate-Mop/s=%.3f main-Mop/s=%.3f verified=%s",api.name,mixed?"mixed-realloc":"allocate-free",count,trial,(unsigned long long)operations,(unsigned long long)checks,wall,wall>0?operations/(wall*1000):0,results[0].milliseconds>0?results[0].operations/(results[0].milliseconds*1000):0,ok?"true":"false");
    if(!ok){api.leave();return false;}
  }
  api.leave();return true;
}
}
