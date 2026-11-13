// Standalone host build: STAR_SYSTEM_SWITCH2, plus StarSwitch2WaitProfile.cpp.
#include "StarSwitch2WaitProfile.hpp"
#include "StarSwitch2RandomWait.hpp"
#include "StarSwitch2HeapWait.hpp"
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
using namespace Star::Switch2WaitProfile;
#ifdef STAR_WAIT_PROFILE_TEST_CPU_CLOCK
namespace Star::Switch2WaitProfile {int64_t testCpuClock(){static int64_t value=0;return value+=10;}}
#endif
struct _reent;
static unsigned heapForwarded=0;
extern "C" void __real___malloc_lock(struct _reent*){++heapForwarded;}
extern "C" void __wrap___malloc_lock(struct _reent*);
void check(bool value, char const* label) { if (!value) throw std::runtime_error(label); }
int main(int argc, char** argv) {
  try {
    check(argc==2,"output path required"); std::remove(argv[1]);
    __wrap___malloc_lock(nullptr); // disabled startup path still forwards
    start(); scene(Scene::Menu);
    std::mutex mutex; std::promise<void> locked, attempted;
    std::thread worker([&] {
      Background work("host-worker-held");
      std::lock_guard<std::mutex> lock(mutex); locked.set_value();
      attempted.get_future().wait();
      // Must not enter the main-thread coarse timing even while a main frame is active.
      Scope ignored(Metric::Update);
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    });
    locked.get_future().wait();
    unsigned samples=0;
    struct Locker {
      std::mutex& mutex;
      std::promise<void>& attempted;
      bool tryLock() {
        bool acquired = mutex.try_lock();
        if (!acquired) attempted.set_value();
        return acquired;
      }
      void lock() { mutex.lock(); }
      ~Locker() { mutex.unlock(); }
    };
    for(unsigned i=0;i<16;++i) {
      Frame frame;
      if(detailed())++samples;
      for (unsigned call=0; call<64; ++call) {
        __wrap___malloc_lock(nullptr);
        auto token=beginRandomWait();
        endRandomWait(token, i==0);
      }
      count(Counter::StarsVisited,11);
      for(unsigned call=0;call<16;++call){Scope immediate(Metric::RendererImmediate);__wrap___malloc_lock(nullptr);__wrap___malloc_lock(nullptr);}
      {
        Scope update(Metric::Update);
        for(unsigned call=0;call<32;++call)__wrap___malloc_lock(nullptr);
        if(i==0) {
          Scope waiting(Metric::AssetLock);
          Locker lock{mutex, attempted};
          acquire(lock, Metric::AssetContended);
          waiting.stop(); waiting.stop(); // explicit stop is idempotent
        } else {
          Scope waiting(Metric::AssetLock);
          Scope nested(Metric::AssetLock); // same metric must not double-count
        }
      }
      Scope pacing(Metric::Pacing);
      if(i==15){Scope serverPause(Metric::LocalServerPause);std::this_thread::sleep_for(std::chrono::milliseconds(60));}
      else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.join(); finishFrames();
    check(samples==2,"one in eight detailed frames");
    check(heapForwarded==1+16*(64+32+32),"heap wrapper forwards every SDK lock call exactly once");
    std::ifstream premature(argv[1]);check(!premature.good(),"report must not be written during capture");
    std::promise<void> backgroundReady, teardown;
    std::thread stillAlive([&] {
      Background work("unfinished-at-freeze");
      backgroundReady.set_value();
      teardown.get_future().wait();
    });
    backgroundReady.get_future().wait();
    // A file cannot be used as a parent directory. Exercise a real open failure,
    // then save the same frozen capture before the background thread tears down.
    std::string badParent = std::string(argv[1]) + ".not-a-directory";
    { std::ofstream sentinel(badParent); sentinel << "not a directory"; }
    bool rejected = !dump((badParent + "/report.log").c_str());
    int openError = errno;
    bool saved = dump(argv[1]);
    teardown.set_value(); stillAlive.join();
    std::remove(badParent.c_str());
    check(rejected && openError != 0,"open error reported without discarding capture");
    check(saved,"fallback retry succeeds before teardown");
    std::ifstream input(argv[1]);std::string text((std::istreambuf_iterator<char>(input)),{});
    check(text.find("frames=16 detailed=2 scenes=0x2")!=std::string::npos,"frame and scene aggregation");
    auto pos=text.find("asset-initial-lock avg=");check(pos!=std::string::npos,"cache-hit lock timing recorded");
    double mean=std::stod(text.substr(pos+23));check(mean>5,"contended initial lock accounted for");
    check(text.find("asset-contended avg=")!=std::string::npos,"failed try-lock contention distinguished");
    check(text.find("rng-lock-sparse calls/detailed-frame=64.00 timed=4 contended=2")!=std::string::npos,"sparse RNG lock accounting and sample rate");
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
    check(text.find("[heap-lock] start=0.000s detailed=2 calls=256 timed=16")!=std::string::npos,"heap lock sampling stride and frame selection");
    check(text.find("metric=update calls=2 acquisitions=64 per-call=32.000")!=std::string::npos,"heap acquisition attribution to update scope");
    check(text.find("metric=renderer-immediate calls=32 acquisitions=64 per-call=2.000")!=std::string::npos,"heap acquisition attribution independent of CPU query cap");
#else
    check(text.find("allocation-profile=off heap-lock-profile=off")!=std::string::npos,"disabled allocator profiling explicitly reported");
    check(text.find("[heap-lock]")==std::string::npos&&text.find("[heap-scope]")==std::string::npos,"disabled heap instrumentation does not produce misleading rows");
#endif
    check(text.find("kind=stars-visited total=22 per-detailed-frame=11.000")!=std::string::npos,"work counters sampled consistently");
#ifdef STAR_WAIT_PROFILE_TEST_CPU_CLOCK
    check(text.find("metric=update calls=2")!=std::string::npos&&text.find("running-total-ms=0.020")!=std::string::npos,"CPU scope accumulation uses matching detailed calls");
    check(text.find("metric=renderer-immediate calls=16")!=std::string::npos,"CPU queries capped to eight per metric/detailed frame");
#endif
    check(text.find("host-worker-held")!=std::string::npos,"background work recorded separately");
    check(text.find("unfinished-at-freeze")==std::string::npos,"late background completions excluded after freeze");
    check(text.find("[hitch]")!=std::string::npos,"unsampled long frame retained");
    check(text.find(" | local-server-pause=")!=std::string::npos,"new server phase records even an unsampled hitch");
    check(text.find("128 worst frames>=50ms")!=std::string::npos,"shorter gameplay hitch threshold advertised");
    check(text.find("pacing-sleep avg=")!=std::string::npos,"deliberate sleep distinguished");
    check(text.find("overflow-frames=0 dropped-background-spans=0")!=std::string::npos,"capture capacity metadata");
    std::puts("Wait profile passed: sampling, contention, background exclusion, pacing/hitches, pre-teardown save and retry after real open failure.");
  } catch(std::exception const& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
