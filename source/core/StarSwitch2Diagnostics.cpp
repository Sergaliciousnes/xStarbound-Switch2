#include "StarSwitch2Diagnostics.hpp"
#include "StarSwitch2FrameProfile.hpp"
#include <switch.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

namespace Star::Switch2Diagnostics {
namespace {
struct Stage {
  std::atomic<char const*> label{"not observed"};
  std::atomic<u64> tick{0};
};
struct Trace {
  std::atomic<bool> armed{false}, running{false};
  std::array<Stage, static_cast<unsigned>(Channel::Count)> stages;
  std::array<std::atomic<u64>, static_cast<unsigned>(Counter::Count)> counters{};
  std::thread worker;
  FILE* output = nullptr;
  u64 started = 0;
#ifdef __SWITCH2__
  bool clocksAttempted = false, clockServiceReady = false, cpuClockOpen = false, gpuClockOpen = false;
  ClkrstSession cpuClock{}, gpuClock{};
  Result cpuClockResult = 0, gpuClockResult = 0;
  Handle mainThread = INVALID_HANDLE;
  u64 lastThreadTicks = 0, lastCpuSampleTick = 0;
  bool haveCpuSample = false;

  void reportClocks() {
    if (!clocksAttempted) {
      clocksAttempted = true;
      auto result = clkrstInitialize();
      clockServiceReady = R_SUCCEEDED(result);
      cpuClockResult = gpuClockResult = result;
      if (clockServiceReady) {
        cpuClockResult = clkrstOpenSession(&cpuClock, PcvModuleId_CpuBus, 3);
        gpuClockResult = clkrstOpenSession(&gpuClock, PcvModuleId_GPU, 3);
        cpuClockOpen = R_SUCCEEDED(cpuClockResult);
        gpuClockOpen = R_SUCCEEDED(gpuClockResult);
      }
    }
    u32 cpuHz = 0, gpuHz = 0;
    auto cpuResult = cpuClockOpen ? clkrstGetClockRate(&cpuClock, &cpuHz) : cpuClockResult;
    auto gpuResult = gpuClockOpen ? clkrstGetClockRate(&gpuClock, &gpuHz) : gpuClockResult;
    std::fprintf(output, "[clocks] query-stage=%s | ", clockServiceReady ? "module-session/rate" : "service-init");
    if (R_SUCCEEDED(cpuResult)) std::fprintf(output, "cpu-hz=%u", cpuHz);
    else std::fprintf(output, "cpu=unavailable result=0x%08x module=%u description=%u", cpuResult, R_MODULE(cpuResult), R_DESCRIPTION(cpuResult));
    std::fprintf(output, " | ");
    if (R_SUCCEEDED(gpuResult)) std::fprintf(output, "gpu-hz=%u", gpuHz);
    else std::fprintf(output, "gpu=unavailable result=0x%08x module=%u description=%u", gpuResult, R_MODULE(gpuResult), R_DESCRIPTION(gpuResult));
    std::fprintf(output, " (read-only)\n");

    u32 configuration = 0;
    auto profileResult = appletGetCurrentPerformanceConfiguration(&configuration);
    std::fprintf(output, "[power-context] operation-mode=%d performance-mode=%d | profile-id=0x%08x result=0x%08x (profile is not measured MHz)\n",
        (int)appletGetOperationMode(), (int)appletGetPerformanceMode(), configuration, profileResult);

    u64 threadTicks = 0;
    auto threadResult = svcGetInfo(&threadTicks, InfoType_ThreadTickCount, mainThread, TickCountInfo_Total);
    u64 sampleTick = armGetSystemTick();
    if (R_SUCCEEDED(threadResult)) {
      if (haveCpuSample && sampleTick > lastCpuSampleTick && threadTicks >= lastThreadTicks) {
        double elapsedMs = armTicksToNs(sampleTick - lastCpuSampleTick) / 1e6;
        double runningMs = armTicksToNs(threadTicks - lastThreadTicks) / 1e6;
        std::fprintf(output, "[main-thread] running-ms=%.2f elapsed-ms=%.2f scheduled-percent=%.1f (not CPU frequency)\n",
            runningMs, elapsedMs, elapsedMs > 0 ? 100.0 * runningMs / elapsedMs : 0);
      }
      lastThreadTicks = threadTicks;
      lastCpuSampleTick = sampleTick;
      haveCpuSample = true;
    } else {
      haveCpuSample = false;
      std::fprintf(output, "[main-thread] scheduled-time=unavailable result=0x%08x\n", threadResult);
    }
  }
#endif

  Trace() { for (auto& value : counters) value.store(0); }

  ~Trace() {
    running.store(false);
    if (worker.joinable()) worker.join();
#ifdef __SWITCH2__
    if (cpuClockOpen) clkrstCloseSession(&cpuClock);
    if (gpuClockOpen) clkrstCloseSession(&gpuClock);
    if (clockServiceReady) clkrstExit();
#endif
    if (output && output != stderr) std::fclose(output);
  }
  void report() {
    u64 now = armGetSystemTick();
    std::fprintf(output, "[+%.1fs]", armTicksToNs(now - started) / 1e9);
    char const* names[] = {"main", "vulkan", "server", "client-world", "celestial", "lighting", "render", "texture"};
    static_assert(std::size(names) == static_cast<unsigned>(Channel::Count));
    for (unsigned i = 0; i < stages.size(); ++i) {
      auto label = stages[i].label.load(std::memory_order_relaxed);
      auto tick = stages[i].tick.load(std::memory_order_relaxed);
      double age = tick && tick <= now ? armTicksToNs(now - tick) / 1e9 : 0;
      std::fprintf(output, " | %s=%s (%.1fs)", names[i], label, age);
    }
    char const* counts[] = {"frames", "server-ticks", "world-starts", "search-attempts", "chunks", "reads", "writes"};
    for (unsigned i = 0; i < counters.size(); ++i)
      std::fprintf(output, " | %s=%llu", counts[i], static_cast<unsigned long long>(counters[i].load(std::memory_order_relaxed)));
    std::fputc('\n', output);
    Switch2FrameProfile::report(output);
#ifdef __SWITCH2__
    reportClocks();
#endif
    std::fflush(output);
  }
};
Trace& trace() { static Trace state; return state; }
}

void startWorldEntryTrace() {
  auto& state = trace();
  if (state.armed.exchange(true)) return;
  state.output = std::fopen("sdmc:/xstarbound-world-trace.log", "w");
  if (!state.output) state.output = stderr;
  // Build each snapshot in stdio's buffer and flush once in report(), rather
  // than issuing an SD write for every formatted field during gameplay.
  if (state.output != stderr) std::setvbuf(state.output, nullptr, _IOFBF, 8192);
  state.started = armGetSystemTick();
#ifdef __SWITCH2__
  // Capture the real main-thread handle here; CUR_THREAD_HANDLE in report()
  // would instead refer to the watchdog whose scheduled time is mostly sleep.
  state.mainThread = threadGetCurHandle();
#endif
  std::fprintf(state.output, "World-entry watchdog: independent 5-second snapshots; stage ages are approximate, counters count completed work. No engine logger/mutex is used.\n");
  state.running.store(true);
  try {
    state.worker = std::thread([&state]() {
      unsigned periods = 0;
      while (state.running.load()) {
        if (periods++ % 10 == 0) state.report();
        svcSleepThread(500'000'000);
      }
      state.report();
    });
  } catch (...) {
    state.running.store(false);
    std::fprintf(state.output, "Could not start watchdog thread\n");
  }
}

void stage(Channel channel, char const* label) {
  auto& state = trace();
  if (!state.armed.load(std::memory_order_relaxed)) return;
  auto& value = state.stages[static_cast<unsigned>(channel)];
  value.tick.store(armGetSystemTick(), std::memory_order_relaxed);
  value.label.store(label, std::memory_order_relaxed);
}
void count(Counter counter) {
  auto& state = trace();
  if (state.armed.load(std::memory_order_relaxed))
    state.counters[static_cast<unsigned>(counter)].fetch_add(1, std::memory_order_relaxed);
}
}
