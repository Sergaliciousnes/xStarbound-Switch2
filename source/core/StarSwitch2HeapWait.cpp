#include "StarSwitch2HeapWait.hpp"
// Profile the existing newlib lock acquisition; allocation and lock semantics
// remain entirely in the SDK. No allocation, logger, try-lock or extra mutex.
struct _reent;
extern "C" void __real___malloc_lock(struct _reent*);
extern "C" void __wrap___malloc_lock(struct _reent* reent){
  auto start=Star::Switch2WaitProfile::beginHeapWait();
  __real___malloc_lock(reent);
  Star::Switch2WaitProfile::endHeapWait(start);
}
