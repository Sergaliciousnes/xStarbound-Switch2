#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Star::PerformanceKernels {
constexpr char Version[] = "serial-cpu-v1";
constexpr uint64_t Seed = 0x123456789abcdef0ULL;
#if defined(_MSC_VER)
#define STAR_BENCH_NOINLINE __declspec(noinline)
#else
#define STAR_BENCH_NOINLINE __attribute__((noinline))
#endif
// Defined unsigned wraparound; each iteration depends on the previous one.
STAR_BENCH_NOINLINE inline uint64_t integer(uint64_t x, unsigned count) {
  for (unsigned i=0;i<count;++i) { x^=x>>12; x^=x<<25; x^=x>>27; x*=0x2545f4914f6cdd1dULL; }
  return x;
}
struct Floating { double a=1.25,b=0.75,c=1.75,d=0.25; };
STAR_BENCH_NOINLINE inline Floating floating(Floating x, unsigned count) {
  for (unsigned i=0;i<count;++i) {
    auto a=x.a*0.9999997+x.b*0.0000003;
    auto b=x.b*0.9999995+x.c*0.0000005;
    auto c=x.c*0.9999993+x.d*0.0000007;
    auto d=x.d*0.9999991+x.a*0.0000009;
    x={a,b,c,d};
  }
  return x;
}
inline uint64_t checksum(uint64_t x) { return x; }
inline uint64_t checksum(Floating x) {
  uint64_t bits[4]; double values[]={x.a,x.b,x.c,x.d}; std::memcpy(bits,values,sizeof(bits));
  return bits[0]^bits[1]^bits[2]^bits[3];
}
struct Trial { uint64_t iterations=0, checksum=0; double milliseconds=0; };
template <typename State, typename Kernel>
Trial measure(State state, Kernel kernel, unsigned targetMs=200) {
  using Clock=std::chrono::steady_clock;
  constexpr unsigned Chunk=4096, MaxChunks=65536;
  auto started=Clock::now(); Trial result;
  auto target=std::chrono::milliseconds(std::clamp(targetMs,1u,500u));
  for(unsigned i=0;i<MaxChunks;++i) {
    state=kernel(state,Chunk); result.iterations+=Chunk;
    if(Clock::now()-started>=target)break;
  }
  result.milliseconds=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
  result.checksum=checksum(state); return result;
}
inline double median(std::vector<double> values) {
  if(values.empty())return 0;
  std::sort(values.begin(),values.end());
  size_t middle=values.size()/2;
  return values.size()%2?values[middle]:(values[middle-1]+values[middle])*0.5;
}
#undef STAR_BENCH_NOINLINE
}
