#include <cstdint>
struct _reent;
extern "C" {
void __real___malloc_lock(_reent*){}
uint32_t __real_svcArbitrateLock(uint32_t,uint32_t*,uint32_t){return 0;}
uint32_t __real_svcArbitrateUnlock(uint32_t*){return 0;}
}
