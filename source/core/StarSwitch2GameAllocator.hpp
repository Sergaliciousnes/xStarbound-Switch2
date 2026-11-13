#pragma once
#include <cstddef>
#include <cstdio>
namespace Star::Switch2GameAllocator {
bool initialize(size_t budget=size_t(4ULL*1024*1024*1024));
void* allocate(size_t size);
void* resize(void* pointer,size_t size);
void release(void* pointer,size_t alignment=0) noexcept;
void* cppAllocate(size_t size,size_t alignment=0);
void* cppAllocateNoThrow(size_t size,size_t alignment=0) noexcept;
void* sdkResize(void* pointer,size_t size);
bool owns(void const*) noexcept;
bool active() noexcept;
struct ThreadCounters {size_t registered=0,exited=0;};
ThreadCounters threads() noexcept;
bool retiringThread() noexcept;
void report(FILE* file,char const* phase);
void reportFile(char const* phase,bool append);
}
