#pragma once
#include <cstddef>
#include <cstdint>
namespace Star::RpmallocCandidate {
struct Backing {
  uint64_t current=0,peak=0,maps=0,releases=0,partial=0,rejected=0,errors=0,active=0;
};
// Single-threaded explicit lifecycle; all workers must exit before shutdown.
bool initialize();
void shutdown();
void enter();
void leave();
void* allocate(size_t);
void* zero(size_t,size_t);
void* resize(void*,size_t);
void* aligned(size_t,size_t);
void release(void*);
Backing backing();
void rejectNewBacking(bool);
// Game backend only: no arbitrary-pointer header reads or metadata mutex.
bool initializeForGame(size_t budget);
bool owns(void const*) noexcept;
}
