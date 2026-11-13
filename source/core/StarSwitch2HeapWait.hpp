#pragma once
#include <cstdint>
namespace Star::Switch2WaitProfile {
#if defined(STAR_SYSTEM_SWITCH2) && !defined(STAR_SWITCH2_TIMING_DISABLED)
int64_t beginHeapWait();
void endHeapWait(int64_t start);
#else
inline int64_t beginHeapWait(){return 0;}
inline void endHeapWait(int64_t){}
#endif
}
