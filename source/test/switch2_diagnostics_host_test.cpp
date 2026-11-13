// Standalone host build: define STAR_SYSTEM_SWITCH2 and STAR_SWITCH2_DIAGNOSTICS.
#include "StarSwitch2Diagnostics.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

int main(int argc, char** argv) {
  if (argc != 2 || !std::freopen(argv[1], "w", stderr)) return 1;
  using namespace Star::Switch2Diagnostics;
  startWorldEntryTrace();
  startWorldEntryTrace(); // Must not launch a second watchdog or truncate.
  stage(Channel::Main, "host main deliberately blocked");
  count(Counter::Frames);
  std::thread background([] {
    for (unsigned i = 0; i < 60; ++i) {
      stage(Channel::Server, "host background advancing");
      count(Counter::ServerTicks);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  std::this_thread::sleep_for(std::chrono::seconds(6));
  background.join();
  std::puts("Watchdog host scenario complete: main blocked for 6 seconds while background advanced.");
  return 0;
}
