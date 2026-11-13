#pragma once
// Host control-flow fixture, never added to native include directories.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#define __VERSION__ "host SDK fixture (not hardware performance)"
using u64=uint64_t;using u32=uint32_t;using s32=int32_t;using Handle=u32;using Result=u32;
constexpr unsigned InfoType_ThreadTickCount=25;constexpr uint64_t TickCountInfo_Total=UINT64_MAX;
inline auto fixtureMain=std::this_thread::get_id();
inline std::atomic<u32> fixtureNext{100};
inline thread_local u32 fixtureHandle=fixtureNext.fetch_add(1);
inline thread_local s32 fixturePriority=std::this_thread::get_id()==fixtureMain?44:59;
inline thread_local u64 fixtureMask=std::this_thread::get_id()==fixtureMain?1:63;
inline thread_local s32 fixturePreferred=std::this_thread::get_id()==fixtureMain?0:-1;
struct RMutex {std::recursive_mutex lock;};
inline void rmutexLock(RMutex* m){m->lock.lock();}
inline void rmutexUnlock(RMutex* m){m->lock.unlock();}
inline uint64_t armGetSystemTick(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/32;}
inline uint64_t armGetSystemTickFreq(){return 31250000;}
inline uint64_t armTicksToNs(uint64_t n){return n*32;}
inline Handle threadGetCurHandle(){return fixtureHandle;}
inline Result svcGetThreadId(u64* out,Handle){*out=fixtureHandle;return 0;}
inline Result svcGetThreadPriority(s32* out,Handle){*out=fixturePriority;return 0;}
inline Result svcGetThreadCoreMask(s32* ideal,u64* mask,Handle){*ideal=fixturePreferred;*mask=fixtureMask;return 0;}
inline Result svcSetThreadPriority(Handle,u32 priority){
  if(std::getenv("STAR_FAKE_DENY_PRIORITY")&&priority==44&&fixturePriority==59)return 1;
  if(std::getenv("STAR_FAKE_RESTORE_FAIL")&&priority==59&&fixturePriority==44)return 2;
  fixturePriority=s32(priority);return 0;
}
inline Result svcSetThreadCoreMask(Handle,s32 ideal,u32 mask){fixturePreferred=ideal;fixtureMask=mask;return 0;}
inline u32 svcGetCurrentProcessorNumber(){for(u32 i=0;i<32;++i)if(fixtureMask&(u64(1)<<i))return i;return UINT32_MAX;}
inline Result svcGetInfo(u64* out,unsigned,Handle,u64){*out=armGetSystemTick();return 0;}
inline bool appletMainLoop(){return true;}
