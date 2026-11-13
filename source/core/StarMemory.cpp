#include "StarMemory.hpp"
#ifdef STAR_SWITCH2_GAME_RPMALLOC
#include "StarSwitch2GameAllocator.hpp"
#ifdef STAR_SWITCH2_GAME_ALLOC_PROFILE
#include "StarSwitch2AllocationProfile.hpp"
#endif
#endif

#if defined STAR_USE_MIMALLOC
#include "mimalloc/mimalloc.h"
// #elifdef apparently isn't compatible with MSVC.
#elif defined STAR_USE_JEMALLOC
#include "jemalloc/jemalloc.h"
#elif defined STAR_USE_RPMALLOC
  #if !defined _WIN32
    #include "rpmalloc.h"
  #else // Windows needs manual function overriding.
    #include "rpnew.h"

    // bool rpmalloc_linker_dummy() {
    //   rpmalloc_linker_reference();
    //   return true;
    // }

    // static bool _rpmalloc_dummy = rpmalloc_linker_dummy();
  #endif
#endif

namespace Star {

#if defined STAR_SWITCH2_GAME_RPMALLOC
#ifdef STAR_SWITCH2_GAME_ALLOC_PROFILE
  void* malloc(size_t size){
    if(!Switch2AllocationProfile::captureRequested())return Switch2GameAllocator::allocate(size);
    Switch2AllocationProfile::Call call(Switch2AllocationProfile::Kind::Malloc,size,__builtin_return_address(0));
    auto p=Switch2GameAllocator::allocate(size);call.result(p);return p;
  }
  void* realloc(void* ptr,size_t size){
    if(!Switch2AllocationProfile::captureRequested())return Switch2GameAllocator::resize(ptr,size);
    Switch2AllocationProfile::Call call(Switch2AllocationProfile::Kind::Realloc,size,__builtin_return_address(0));
    auto p=Switch2GameAllocator::resize(ptr,size);call.result(p);return p;
  }
  void free(void* ptr){
    if(!ptr||!Switch2AllocationProfile::captureRequested())return Switch2GameAllocator::release(ptr);
    Switch2AllocationProfile::Call call(Switch2AllocationProfile::Kind::Free,0,__builtin_return_address(0));
    Switch2GameAllocator::release(ptr);call.success();
  }
  void free(void* ptr,size_t){free(ptr);}
#else
  void* malloc(size_t size){return Switch2GameAllocator::allocate(size);}
  void* realloc(void* ptr,size_t size){return Switch2GameAllocator::resize(ptr,size);}
  void free(void* ptr){Switch2GameAllocator::release(ptr);}
  void free(void* ptr,size_t){Switch2GameAllocator::release(ptr);}
#endif
#elif defined STAR_USE_RPMALLOC
  void* malloc(size_t size) {
    return ::rpmalloc(size);
  }

  void* realloc(void* ptr, size_t size) {
    return ::rprealloc(ptr, size);
  }

  void free(void* ptr) {
    ::rpfree(ptr);
  }

  void free(void* ptr, size_t) {
    ::rpfree(ptr);
  }
#elif defined STAR_USE_MIMALLOC
  void* malloc(size_t size) {
    return mi_malloc(size);
  }

  void* realloc(void* ptr, size_t size) {
    return mi_realloc(ptr, size);
  }

  void free(void* ptr) {
    mi_free(ptr);
  }

  void free(void* ptr, size_t size) {
    mi_free(ptr);
  }
#elif defined STAR_USE_JEMALLOC
  #if defined STAR_CROSS_COMPILE
  // FezzedOne: `jemalloc`'s stuff isn't renamed by default in the MinGW version off the AUR.
  void* malloc(size_t size) {
    return ::malloc(size);
  }

  void* realloc(void* ptr, size_t size) {
    return ::realloc(ptr, size);
  }

  void free(void* ptr) {
    ::free(ptr);
  }

  void free(void* ptr, size_t size) {
    if (ptr)
      ::sdallocx(ptr, size, 0);
  }
  #else
  // FezzedOne: ... or in the VCPKG package.
  void* malloc(size_t size) {
    return ::malloc(size);
  }

  void* realloc(void* ptr, size_t size) {
    return ::realloc(ptr, size);
  }

  void free(void* ptr) {
    ::free(ptr);
  }

  void free(void* ptr, size_t size) {
    if (ptr)
      ::sdallocx(ptr, size, 0);
  }
  #endif
#else
  void* malloc(size_t size) {
    return ::malloc(size);
  }

  void* realloc(void* ptr, size_t size) {
    return ::realloc(ptr, size);
  }

  void free(void* ptr) {
    return ::free(ptr);
  }

  void free(void* ptr, size_t) {
    return ::free(ptr);
  }
#endif

}

#if !defined(STAR_USE_RPMALLOC) && !defined(STAR_SYSTEM_SWITCH2)

void* operator new(std::size_t size) {
  auto ptr = Star::malloc(size);
  if (!ptr)
    throw std::bad_alloc();
  return ptr;
}

void* operator new[](std::size_t size) {
  auto ptr = Star::malloc(size);
  if (!ptr)
    throw std::bad_alloc();
  return ptr;
}

// Globally override new and delete.  As per the standard, new and delete must
// be defined in global scope, and must not be inline.

void* operator new(std::size_t size, std::nothrow_t const&) noexcept {
  return Star::malloc(size);
}

void* operator new[](std::size_t size, std::nothrow_t const&) noexcept {
  return Star::malloc(size);
}

void operator delete(void* ptr) noexcept {
  Star::free(ptr);
}

void operator delete[](void* ptr) noexcept {
  Star::free(ptr);
}

void operator delete(void* ptr, std::nothrow_t const&) noexcept {
  Star::free(ptr);
}

void operator delete[](void* ptr, std::nothrow_t const&) noexcept {
  Star::free(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept {
  Star::free(ptr, size);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
  Star::free(ptr, size);
}

#endif // !defined STAR_USE_RPMALLOC
