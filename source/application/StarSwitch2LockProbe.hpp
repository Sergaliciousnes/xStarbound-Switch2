#pragma once
#include <cstdint>
namespace Star::LockProbe {
struct Counters {
  uint64_t heapCalls=0,heapSamples=0,heapTicks=0,heapPeak=0;
  uint64_t waitCalls=0,waitSamples=0,waitTicks=0,waitPeak=0,waitErrors=0;
  uint64_t wakeCalls=0,wakeSamples=0,wakeTicks=0,wakePeak=0,wakeErrors=0;
};
// Enable only after main/TLS initialization. No allocation or logging in probes.
void enable(bool value);
void attach(Counters* counters);
}
