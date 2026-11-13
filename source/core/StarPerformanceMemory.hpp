#pragma once
#include "StarPerformanceKernels.hpp"
#include <cstdlib>
#include <stdexcept>

namespace Star::PerformanceMemory {
using Clock=std::chrono::steady_clock;
using Log=void(*)(char const*,...);
using Continue=bool(*)();
constexpr size_t MiB=1024*1024;
using Copy=void*(*)(void*,void const*,size_t);
inline Copy volatile copyBytes=std::memcpy;
inline volatile uint64_t sink=0;
inline double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}

// Matches switch_linux_benchmark.cpp memory/allocation v1 workloads, including
// work sizes, shuffle/seed, timing chunks and result verification.
inline bool memory(Log log,Continue next){
  constexpr size_t Size=32*MiB;
  std::vector<unsigned char> source(Size,0x5a),destination(Size,0);
  std::vector<double> rates;
  for(unsigned trial=0;trial<3;++trial){
    if(!next())return false;
    uint64_t count=0;auto start=Clock::now();
    do {copyBytes(destination.data(),source.data(),Size);++count;}while(ms(start)<200&&count<16384);
    auto elapsed=ms(start);
    if(std::memcmp(source.data(),destination.data(),Size))throw std::runtime_error("memory copy verification failed");
    double rate=count*double(Size)/(elapsed/1000)/(1024*MiB);rates.push_back(rate);
    log("[memory-copy] version=memcopy-v1 trial=%u size-MiB=32 copies=%llu milliseconds=%.3f payload-GiB/s=%.3f verified=true",trial+1,(unsigned long long)count,elapsed,rate);
  }
  log("[memory-copy-summary] median-payload-GiB/s=%.3f (not total bus traffic)",PerformanceKernels::median(rates));
  for(size_t bytes:std::array<size_t,3>{256*1024,8*MiB,32*MiB}){
    if(!next())return false;
    size_t count=bytes/sizeof(uint32_t);std::vector<uint32_t> order(count),nextIndex(count);
    for(size_t i=0;i<count;++i)order[i]=uint32_t(i);
    uint64_t rng=PerformanceKernels::Seed;
    for(size_t i=count-1;i>0;--i){rng=PerformanceKernels::integer(rng,1);std::swap(order[i],order[rng%(i+1)]);}
    for(size_t i=0;i<count;++i)nextIndex[order[i]]=order[(i+1)%count];
    order.clear();order.shrink_to_fit();
    volatile uint32_t const* pointer=nextIndex.data();uint32_t index=0;
    for(size_t i=0;i<count;++i)index=pointer[index];
    uint64_t steps=0;auto start=Clock::now();
    do {for(unsigned i=0;i<4096;++i)index=pointer[index];steps+=4096;}while(ms(start)<150&&steps<268435456);
    double elapsed=ms(start);sink=index;
    log("[pointer-chase] version=dependent-load-v1 working-KiB=%zu steps=%llu milliseconds=%.3f ns-per-load=%.3f",bytes/1024,(unsigned long long)steps,elapsed,elapsed*1e6/steps);
  }
  return true;
}
inline bool allocation(Log log,Continue next){
  constexpr size_t Count=1024;
  std::vector<void*> blocks(Count);std::vector<double> rates;
  for(unsigned trial=0;trial<3;++trial){
    if(!next())return false;
    uint64_t pairs=0;auto start=Clock::now();
    do {
      for(size_t i=0;i<Count;++i){
        size_t size=(i%3==0?32:i%3==1?128:512);blocks[i]=std::malloc(size);
        if(!blocks[i]){for(size_t j=0;j<i;++j)std::free(blocks[j]);throw std::bad_alloc();}
        auto* pointer=static_cast<volatile unsigned char*>(blocks[i]);pointer[0]=0x5a;pointer[size-1]=0xa5;
      }
      for(auto block:blocks)std::free(block);
      pairs+=Count;
    }while(ms(start)<200&&pairs<268435456);
    double elapsed=ms(start),rate=pairs/(elapsed*1000);rates.push_back(rate);
    log("[allocation] version=malloc-batch-v1 trial=%u batch=1024 sizes=32/128/512 malloc-free-pairs=%llu milliseconds=%.3f million-pairs/s=%.3f",trial+1,(unsigned long long)pairs,elapsed,rate);
  }
  log("[allocation-summary] median-million-pairs/s=%.3f (platform libc allocator, single thread)",PerformanceKernels::median(rates));
  return true;
}
}
