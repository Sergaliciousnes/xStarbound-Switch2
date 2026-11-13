#include "StarSwitch2AllocationProfile.hpp"
#include <cstddef>
#include <cstdint>
#ifdef STAR_SWITCH2_GAME_RPMALLOC
#include "StarSwitch2GameAllocator.hpp"
#endif
#ifdef _MSC_VER
#include <intrin.h>
#define STAR_ALLOC_CALLER _ReturnAddress()
#else
#define STAR_ALLOC_CALLER __builtin_extract_return_addr(__builtin_return_address(0))
#endif
using Star::Switch2AllocationProfile::Call;
using Star::Switch2AllocationProfile::Kind;
using Star::Switch2AllocationProfile::captureRequested;
extern "C" {
void* __real__Znwm(size_t) noexcept(false);void* __real__Znam(size_t) noexcept(false);
void __real__ZdlPv(void*) noexcept;void __real__ZdaPv(void*) noexcept;
void __real__ZdlPvm(void*,size_t) noexcept;void __real__ZdaPvm(void*,size_t) noexcept;
void* __real_malloc(size_t);void* __real_calloc(size_t,size_t);void* __real_realloc(void*,size_t);void __real_free(void*);
void* __wrap__Znwm(size_t n) noexcept(false){if(!captureRequested())return __real__Znwm(n);Call c(Kind::New,n,STAR_ALLOC_CALLER);auto p=__real__Znwm(n);c.result(p);return p;}
void* __wrap__Znam(size_t n) noexcept(false){if(!captureRequested())return __real__Znam(n);Call c(Kind::NewArray,n,STAR_ALLOC_CALLER);auto p=__real__Znam(n);c.result(p);return p;}
void __wrap__ZdlPv(void* p) noexcept{if(!p||!captureRequested())return __real__ZdlPv(p);Call c(Kind::Delete,0,STAR_ALLOC_CALLER);__real__ZdlPv(p);c.success();}
void __wrap__ZdaPv(void* p) noexcept{if(!p||!captureRequested())return __real__ZdaPv(p);Call c(Kind::DeleteArray,0,STAR_ALLOC_CALLER);__real__ZdaPv(p);c.success();}
void __wrap__ZdlPvm(void* p,size_t n) noexcept{if(!p||!captureRequested())return __real__ZdlPvm(p,n);Call c(Kind::DeleteSized,n,STAR_ALLOC_CALLER);__real__ZdlPvm(p,n);c.success();}
void __wrap__ZdaPvm(void* p,size_t n) noexcept{if(!p||!captureRequested())return __real__ZdaPvm(p,n);Call c(Kind::DeleteArraySized,n,STAR_ALLOC_CALLER);__real__ZdaPvm(p,n);c.success();}
void* __wrap_malloc(size_t n){if(!captureRequested())return __real_malloc(n);Call c(Kind::Malloc,n,STAR_ALLOC_CALLER);auto p=__real_malloc(n);c.result(p);return p;}
void* __wrap_calloc(size_t n,size_t size){if(!captureRequested())return __real_calloc(n,size);auto bytes=n&&size>SIZE_MAX/n?SIZE_MAX:n*size;Call c(Kind::Calloc,bytes,STAR_ALLOC_CALLER);auto p=__real_calloc(n,size);c.result(p);return p;}
#ifdef STAR_SWITCH2_GAME_RPMALLOC
void* __wrap_realloc(void* old,size_t n){auto invoke=[&]{return old&&Star::Switch2GameAllocator::owns(old)?Star::Switch2GameAllocator::resize(old,n):Star::Switch2GameAllocator::sdkResize(old,n);};if(!captureRequested())return invoke();Call c(Kind::Realloc,n,STAR_ALLOC_CALLER);auto p=invoke();c.result(p);return p;}
void __wrap_free(void* p){if(!p||!captureRequested())return Star::Switch2GameAllocator::release(p);Call c(Kind::Free,0,STAR_ALLOC_CALLER);Star::Switch2GameAllocator::release(p);c.success();}
#else
void* __wrap_realloc(void* old,size_t n){if(!captureRequested())return __real_realloc(old,n);Call c(Kind::Realloc,n,STAR_ALLOC_CALLER);auto p=__real_realloc(old,n);c.result(p);return p;}
void __wrap_free(void* p){if(!p||!captureRequested())return __real_free(p);Call c(Kind::Free,0,STAR_ALLOC_CALLER);__real_free(p);c.success();}
#endif
}
