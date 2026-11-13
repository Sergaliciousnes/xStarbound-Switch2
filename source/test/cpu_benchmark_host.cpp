// Portable baseline: g++ -O2 -std=c++17 -ffp-contract=off -Isource/core source/test/cpu_benchmark_host.cpp -o cpu-bench
#include "StarPerformanceKernels.hpp"
#include <cstdio>
#include <stdexcept>
using namespace Star::PerformanceKernels;
volatile uint64_t resultSink=0;
template <typename State,typename Kernel>
void run(char const* name,State initial,Kernel kernel) {
  std::vector<double> rates; resultSink=checksum(kernel(initial,4096));
  for(unsigned trial=0;trial<3;++trial) {
    auto value=measure(initial,kernel); resultSink=value.checksum;
    double rate=value.iterations/(value.milliseconds*1000);
    rates.push_back(rate);
    std::printf("[cpu] kernel=%s trial=%u iterations=%llu wall-ms=%.3f Miter/s=%.3f checksum=0x%016llx\n",name,trial+1,(unsigned long long)value.iterations,value.milliseconds,rate,(unsigned long long)value.checksum);
  }
  std::printf("[cpu-summary] kernel=%s median-Miter/s=%.3f min=%.3f max=%.3f\n",name,median(rates),*std::min_element(rates.begin(),rates.end()),*std::max_element(rates.begin(),rates.end()));
}
int main() {
  if(integer(Seed,0)!=Seed || integer(Seed,1)!=0xb7fb0288c5ee4339ULL || integer(Seed,4096)!=0xd7e50bbb28fdbe5cULL)throw std::runtime_error("integer kernel checksum changed");
  auto fp=floating(Floating{},100000);
  if(!std::isfinite(fp.a)||!std::isfinite(fp.b)||!std::isfinite(fp.c)||!std::isfinite(fp.d)||fp.a<0.2||fp.a>1.8)throw std::runtime_error("floating kernel not bounded");
  auto timing=measure(Seed,integer,2);
  if(timing.iterations%4096 || timing.iterations==0 || timing.milliseconds<=0 || timing.checksum!=integer(Seed,unsigned(timing.iterations)))throw std::runtime_error("timed work was lost");
  std::printf("[cpu] version=%s native host reference; same kernels, not a MHz estimate\n",Version);
  run("uint64-dependent-mix",Seed,integer);
  run("binary64-four-chains",Floating{},floating);
}
