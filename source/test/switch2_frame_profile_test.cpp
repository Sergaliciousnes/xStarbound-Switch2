// Standalone host build: define STAR_SYSTEM_SWITCH2 and STAR_SWITCH2_DIAGNOSTICS.
#include "StarSwitch2FrameProfile.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

using namespace Star::Switch2FrameProfile;
void check(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    FrameMeter sample;
    sample.reset(0);
    sample.complete(16667);
    check(std::abs(sample.fps() - 60) < 0.01, "Normal frame FPS");
    sample.complete(116667);
    check(sample.fps() == 10 && sample.milliseconds() == 100, "Spike must appear immediately");
    sample.complete(133334);
    check(std::abs(sample.fps() - 60) < 0.01 && sample.peakMilliseconds() == 100, "Immediate recovery and retained hitch");
    int64_t time = 133334;
    for (unsigned i = 0; i < 61; ++i) sample.complete(time += 16667);
    check(sample.peakMilliseconds() < 17, "Peak hold must expire");
    sample.reset(0);
    check(sample.fps() == 0 && sample.peakMilliseconds() == 0, "Reset clears stale samples");

    meter.reset(nowUs());
    begin();
    {
      Zone zone(Phase::Update);
      std::thread background([] { count(Counter::Draws, 1000); });
      background.join();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
      Zone outer(Phase::LuaCalls);
      {
        Zone inner(Phase::LuaCalls);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      check(frameTimes[static_cast<unsigned>(Phase::LuaCalls)] == 0, "Nested Lua scopes must not double count");
      outer.stop();
      auto recorded = frameTimes[static_cast<unsigned>(Phase::LuaCalls)];
      outer.stop();
      check(recorded > 0 && frameTimes[static_cast<unsigned>(Phase::LuaCalls)] == recorded,
          "Explicit stop records once");
    }
    try {
      Zone unwind(Phase::PlayerSerialize);
      throw std::runtime_error("expected unwind");
    } catch (std::runtime_error const&) {}
    for (auto depth : depths) check(depth == 0, "Scope depths must unwind to zero");
    count(Counter::Draws, 7);
    complete();
    FILE* output = std::tmpfile();
    check(output != nullptr, "Temporary profile output");
    report(output);
    std::rewind(output);
    char text[4096]{};
    std::fread(text, 1, sizeof(text) - 1, output);
    std::fclose(output);
    check(std::strstr(text, "frames=1") && std::strstr(text, "draws/frame=7.0"), "Profile aggregation and background exclusion");
    check(std::strstr(text, "lua-calls=") && std::strstr(text, "update-plant="), "Detailed phases must be emitted");
    check(totals[static_cast<unsigned>(Phase::Update)].load() == 0 && frames.load() == 0, "Snapshots consume window counters");
    std::puts("Frame profile tests passed: immediate spike/recovery, hitch hold/expiry, snapshots, thread exclusion, nested scope deduplication, explicit stop and exception unwinding.");
    return 0;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
