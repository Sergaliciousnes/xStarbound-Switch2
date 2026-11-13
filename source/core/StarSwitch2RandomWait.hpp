#pragma once
#include <cstdint>
namespace Star::Switch2WaitProfile {
#if defined(STAR_SYSTEM_SWITCH2) && !defined(STAR_SWITCH2_TIMING_DISABLED)
// One call in 32, and only on a detailed main-thread frame. Zero means untimed.
int64_t beginRandomWait();
void endRandomWait(int64_t started, bool contended);
#else
inline int64_t beginRandomWait() { return 0; }
inline void endRandomWait(int64_t, bool) {}
#endif
}
