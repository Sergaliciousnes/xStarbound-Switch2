#pragma once
#ifdef STAR_PLUTO_BOOTSTRAP_CACHE
#include "lua.h"
#include "lauxlib.h"
#include "../../core/StarSwitch2ExplorationProfile.hpp"
#include <array>
#include <atomic>
#include <cstring>

namespace StarPlutoBootstrap {
#ifndef STAR_PLUTO_BOOTSTRAP_BYTES
#define STAR_PLUTO_BOOTSTRAP_BYTES (128 * 1024)
#endif
constexpr size_t Capacity = STAR_PLUTO_BOOTSTRAP_BYTES;
enum class Library : size_t { Assert, Vector3, Scheduler, Count };
struct Slot {
  // 0: empty; 1: publishing; n+2: immutable n-byte chunk ready.
  std::atomic<size_t> publication{0};
  std::array<char, Capacity> bytes{};
};
// Fixed BSS storage; no allocator, mutex, shutdown hook or live VM ownership.
inline std::array<Slot, size_t(Library::Count)> slots{};
struct Writer {char* bytes;size_t used=0;};
inline int write(lua_State*, void const* bytes, size_t size, void* context) {
  auto& out=*static_cast<Writer*>(context);
  if(size>Capacity-out.used)return 1;
  std::memcpy(out.bytes+out.used,bytes,size);out.used+=size;return 0;
}
struct PublicationGuard {
  Slot& slot;bool ready=false;
  ~PublicationGuard(){if(!ready)slot.publication.store(0,std::memory_order_release);}
};
// Each slot is reserved for one immutable source literal/chunk name for this
// executable. Asset reload cannot change these built-in library sources.
inline int load(lua_State* L, Slot& slot, char const* source, size_t size, char const* name) {
  size_t published=slot.publication.load(std::memory_order_acquire);
  if(published>=2) {
    STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapHit);
    STAR_EXPLORATION_SCOPE(loadTrace,Star::Switch2ExplorationProfile::Kind::BootstrapLoad,name,published-2);
    int status=luaL_loadbufferx(L,slot.bytes.data(),published-2,name,"b");
    if(status!=LUA_OK)STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapError);
    return status;
  }
  STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapMiss);
  int status;
  {
    STAR_EXPLORATION_SCOPE(compileTrace,Star::Switch2ExplorationProfile::Kind::BootstrapCompile,name,size);
    status=luaL_loadbufferx(L,source,size,name,"t");
  }
  if(status!=LUA_OK) {
    STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapError);
    return status;
  }
  size_t empty=0;
  if(slot.publication.compare_exchange_strong(empty,1,std::memory_order_acq_rel)) {
    PublicationGuard claim{slot};Writer out{slot.bytes.data()};
    STAR_EXPLORATION_SCOPE(dumpTrace,Star::Switch2ExplorationProfile::Kind::BootstrapDump,name);
    // The bounded writer does not allocate or raise a Lua error. The original
    // compiled closure stays on this VM's stack even if the chunk won't fit.
    int dumped=lua_dump(L,write,&out,0);
    if(dumped==0 && out.used) {
      slot.publication.store(out.used+2,std::memory_order_release);claim.ready=true;
      STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapPublished);
      STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapBytes,out.used);
    } else {
      STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::BootstrapBypass);
    }
  }
  // No waiting for another publisher: a racing miss uses its own closure.
  return LUA_OK;
}
inline int open(lua_State* L, Library library, char const* source, size_t size, char const* name) {
  int status=load(L,slots[size_t(library)],source,size,name);
  if(status!=LUA_OK)return lua_error(L);
  STAR_EXPLORATION_SCOPE(executeTrace,Star::Switch2ExplorationProfile::Kind::BootstrapExecute,name);
  lua_call(L,0,1); // Fresh tables, functions, metatables and state on every VM.
  return 1;
}
}
#endif
