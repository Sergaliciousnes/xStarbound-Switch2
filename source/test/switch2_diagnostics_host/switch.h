#pragma once
// Host-only clock/sleep shim for testing the watchdog, not a Horizon emulator.
#include <chrono>
#include <cstdint>
#include <thread>
using u64 = std::uint64_t;
inline u64 armGetSystemTick() {
  return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline u64 armTicksToNs(u64 ticks) { return ticks; }
inline void svcSleepThread(std::int64_t nanoseconds) {
  std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
}
