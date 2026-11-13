#include "StarSwitch2GameAllocator.hpp"
extern "C" {
void __wrap_free(void* p){Star::Switch2GameAllocator::release(p);}
void* __wrap_realloc(void* p,size_t n){return p&&Star::Switch2GameAllocator::owns(p)?Star::Switch2GameAllocator::resize(p,n):Star::Switch2GameAllocator::sdkResize(p,n);}
}
