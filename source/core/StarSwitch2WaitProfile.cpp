#include "StarSwitch2RandomWait.hpp"
#include "StarSwitch2HeapWait.hpp"
#include "StarSwitch2AllocationProfile.hpp"
#include "StarSwitch2IoProfile.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#include "StarSwitch2WaitProfile.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <mutex>
#ifdef __SWITCH2__
#include <switch.h>
#endif
namespace Star::Switch2WaitProfile {
#ifdef STAR_WAIT_PROFILE_TEST_CPU_CLOCK
int64_t testCpuClock(); // deterministic host-test provider, never in console builds
#endif
namespace {
constexpr unsigned N = unsigned(Metric::Count), WindowCount = 720, HitchCount = 128, EventCount = 2048;
constexpr int64_t WindowUs = 5000000;
struct Value { uint64_t sum = 0, peak = 0, calls = 0, cpu=0,cpuWall=0,cpuCalls=0,heapCalls=0,heapScopeCalls=0; };
struct Rng { uint64_t calls=0, timed=0, contended=0, total=0, peak=0; };
constexpr unsigned Counters=unsigned(Counter::Count);
struct FrameData { Rng rng,heap; std::array<uint64_t,Counters> counters{};std::array<Value, N> values{}; int64_t start = 0, elapsed = 0; uint64_t number = 0; Scene scene = Scene::Unknown; bool sampled = false; };
struct Window { Rng rng,heap;std::array<uint64_t,Counters> counters{};std::array<Value, N> values{}; uint64_t frames = 0, sampled = 0, elapsed = 0, peak = 0, scenes = 0; int64_t cpuUs = -1, schedulerWallUs = 0; };
struct Event { char const* label = nullptr; int64_t start = 0, elapsed = 0; };
std::array<Window, WindowCount> windows;
std::array<FrameData, HitchCount> hitches;
std::array<Event, EventCount> events;
std::atomic<bool> enabled{false};
std::mutex eventMutex;
unsigned eventSize = 0, eventDropped = 0, hitchSize = 0, highestWindow = 0;
uint64_t frameNumber = 0, overflowFrames = 0;
int64_t started = 0, lastCpu = -1, lastCpuWall = 0;
int64_t captureUnixMs = 0;
int mainPriority = -1; uint64_t coreMask = 0;
int64_t cpuQueryCalibrationUs=-1;
thread_local bool mainThread = false, inFrame = false;
thread_local FrameData current;
thread_local std::array<unsigned,N> depth{};
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
thread_local std::array<uint64_t,N> heapStarts{};
thread_local std::array<uint64_t,N> allocationSerial{};
thread_local std::array<int,N> allocationParents{};
thread_local uint64_t allocationSequence=0;
thread_local int allocationStage=-1;
#endif
Scene currentScene = Scene::Unknown;
bool cpuFinalized = false;
bool frozen = false;
#ifdef __SWITCH2__
Handle mainHandle = INVALID_HANDLE;
#endif
int64_t cpuUs() {
#if defined(STAR_WAIT_PROFILE_TEST_CPU_CLOCK)
  return testCpuClock();
#elif defined(__SWITCH2__)
  u64 ticks = 0;
  if (R_SUCCEEDED(svcGetInfo(&ticks, InfoType_ThreadTickCount, mainHandle, TickCountInfo_Total)))
    return armTicksToNs(ticks) / 1000;
#endif
  return -1;
}
void schedulerSample(unsigned previous, int64_t now) {
  auto cpu = cpuUs();
  if (previous < WindowCount && lastCpu >= 0 && cpu >= lastCpu) {
    windows[previous].cpuUs = cpu - lastCpu;
    windows[previous].schedulerWallUs = now - lastCpuWall;
  }
  lastCpu = cpu; lastCpuWall = now;
}
char const* names[] = {"events", "update", "render", "gather", "paint", "finish", "present", "pacing-sleep", "gpu-fence", "acquire",
  "asset-initial-lock", "asset-get-work", "asset-loader-wait", "root-member-lock", "metadata-initial-lock", "directives-lock", "sector-finish", "lighting-lock",
  "file-cursor-lock", "file-read", "file-write", "file-sync", "entity-delta",
  "asset-contended", "root-contended", "metadata-contended", "directives-contended",
  "world-update", "player-scripts", "interface-update", "mixer-update", "inventory-state", "armor-state",
  "sky-paint", "parallax-paint", "tile-setup", "lighting-paint", "entity-paint", "terrain-paint",
  "paint-cleanup", "particle-paint", "overlay-paint",
  "sky-stars", "sky-debris", "sky-back-orbiters", "sky-horizon", "sky-gradient", "sky-front-orbiters",
  "renderer-immediate", "renderer-cached", "world-entities", "world-damage", "world-sky", "world-weather",
  "world-particles", "world-audio", "world-sectors",
  "local-server-pause", "client-universe", "packet-receive", "packet-handle", "packet-send", "player-save", "system-update"};
char const* counterNames[]={"stars-visited","stars-drawn","star-resets","debris-visited","immediate-primitives","immediate-vertices","immediate-draws","cached-buffers","cached-rebuilds","cached-vertices","cached-draws","entities-updated"};
static_assert(std::size(names) == N);
static_assert(N<=Switch2AllocationProfile::OutsideStage);
static_assert(std::size(counterNames)==Counters);
bool cpuMetric(Metric m){
  return m==Metric::Update||m==Metric::Render||m==Metric::Gather||m==Metric::Paint
      ||m==Metric::Finish||m==Metric::Present||m==Metric::SkyPaint||m==Metric::ParallaxPaint
      ||m==Metric::WorldUpdate||m==Metric::InterfaceUpdate||m==Metric::EntityPaint
      ||unsigned(m)>=unsigned(Metric::SkyStars);
}
}
int64_t nowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void start() {
  // Called once on the main thread, before entering the event loop.
  started = nowUs(); mainThread = true; currentScene = Scene::Unknown;
  captureUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#ifdef __SWITCH2__
  mainHandle = threadGetCurHandle();
  s32 priority = -1;
  if (R_SUCCEEDED(svcGetThreadPriority(&priority, mainHandle))) mainPriority = priority;
  svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
#endif
  lastCpu = cpuUs(); lastCpuWall = started;
  if(lastCpu>=0){auto before=nowUs();for(unsigned i=0;i<32;++i)cpuUs();cpuQueryCalibrationUs=nowUs()-before;}
  enabled.store(true, std::memory_order_release);
  Switch2IoProfile::start();
  Switch2AllocationProfile::start();
  Switch2ExplorationProfile::start();
}
void scene(Scene value) { if (mainThread) { currentScene = value; if (inFrame) current.scene = value; } }
bool detailed() { return mainThread && inFrame && current.sampled; }
bool allocationContext(Switch2AllocationProfile::Context& context){
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
  if(!enabled.load(std::memory_order_relaxed)||!detailed())return false;
  context.frame=current.number;context.scene=unsigned(current.scene);
  context.window=unsigned((current.start-started)/WindowUs);
  context.stage=allocationStage<0?Switch2AllocationProfile::OutsideStage:unsigned(allocationStage);
  context.label=allocationStage<0?"outside-scope":names[allocationStage];return true;
#else
  (void)context;return false;
#endif
}
void count(Counter c,uint64_t amount){if(detailed())current.counters[unsigned(c)]+=amount;}
int64_t beginHeapWait(){
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
  // The SDK can allocate before TLS initialization. Do not touch any TLS until
  // start() enables capture on the initialized game main thread.
  if(!enabled.load(std::memory_order_relaxed)||!detailed())return 0;
  ++current.heap.calls;return current.heap.calls%16==0?nowUs():0;
#else
  return 0;
#endif
}
void endHeapWait(int64_t start){if(!start||!detailed())return;auto us=uint64_t(std::max<int64_t>(0,nowUs()-start));++current.heap.timed;current.heap.total+=us;current.heap.peak=std::max(current.heap.peak,us);}
int64_t beginRandomWait() {
  if (!detailed()) return 0;
  ++current.rng.calls;
  return (current.rng.calls % 32 == 0) ? nowUs() : 0;
}
void endRandomWait(int64_t start, bool contended) {
  if (!start || !detailed()) return;
  auto elapsed = uint64_t(std::max<int64_t>(0, nowUs() - start));
  ++current.rng.timed; current.rng.contended += contended;
  current.rng.total += elapsed; current.rng.peak = std::max(current.rng.peak, elapsed);
}

void beginFrame() {
  if (!mainThread) return;
  auto now = nowUs();
  unsigned window = unsigned((now - started) / WindowUs);
  if (window != highestWindow) { schedulerSample(highestWindow, now); highestWindow = window; }
  current = {}; current.start = now; current.number = frameNumber++;
  current.sampled = (current.number % 8 == 0); current.scene = currentScene;
  depth.fill(0); inFrame = true;
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
  allocationSerial.fill(0);allocationStage=-1;allocationSequence=0;
#endif
  Switch2IoProfile::beginFrame(current.number);
  Switch2AllocationProfile::frame(current.sampled,window,unsigned(current.scene));
  Switch2ExplorationProfile::beginFrame(current.number,unsigned(current.scene),current.sampled);
}
Scope::Scope(Metric metric) : m_metric(metric) {
  m_active = mainThread && inFrame && (unsigned(metric) < unsigned(Metric::AssetLock)
      || unsigned(metric)>=unsigned(Metric::LocalServerPause) || current.sampled);
  if (m_active) {
    m_outer = depth[unsigned(metric)]++ == 0;
    if (m_outer) {
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
      allocationParents[unsigned(metric)]=allocationStage;allocationStage=int(metric);allocationSerial[unsigned(metric)]=++allocationSequence;
      if(current.sampled)heapStarts[unsigned(metric)]=current.heap.calls;
#endif
      m_start = nowUs();
      if(current.sampled&&lastCpu>=0&&cpuMetric(metric)&&current.values[unsigned(metric)].cpuCalls<8)m_cpuStart=cpuUs();
    }
  }
}
void Scope::stop() {
  if (!m_active) return;
  if (m_outer) {
    auto endCpu=m_cpuStart>=0?cpuUs():-1;
    auto us = uint64_t(std::max<int64_t>(0, nowUs() - m_start)); auto& v = current.values[unsigned(m_metric)]; v.sum += us; v.peak = std::max(v.peak, us); ++v.calls;
    if(endCpu>=m_cpuStart&&m_cpuStart>=0){v.cpu+=uint64_t(endCpu-m_cpuStart);v.cpuWall+=us;++v.cpuCalls;}
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
    if(current.sampled){v.heapCalls+=current.heap.calls-heapStarts[unsigned(m_metric)];++v.heapScopeCalls;}
    allocationSerial[unsigned(m_metric)]=0;
    if(allocationStage==int(m_metric)){
      allocationStage=allocationParents[unsigned(m_metric)];
      if(allocationStage>=0&&!allocationSerial[unsigned(allocationStage)]){
        allocationStage=-1;uint64_t newest=0;
        for(unsigned i=0;i<N;++i)if(allocationSerial[i]>newest){newest=allocationSerial[i];allocationStage=int(i);}
      }
    }
#endif
  }
  --depth[unsigned(m_metric)]; m_active = false;
}
void endFrame() {
  if (!mainThread || !inFrame) return;
  current.elapsed = nowUs() - current.start; inFrame = false;
  Switch2IoProfile::endFrame(unsigned(current.scene), current.elapsed);
  Switch2ExplorationProfile::endFrame(unsigned(current.scene),current.elapsed);
  unsigned index = unsigned((current.start - started) / WindowUs);
  if (index < WindowCount) {
    auto& row = windows[index]; ++row.frames; row.sampled += current.sampled;
    row.rng.calls += current.rng.calls; row.rng.timed += current.rng.timed;
    row.rng.contended += current.rng.contended; row.rng.total += current.rng.total;
    row.rng.peak = std::max(row.rng.peak, current.rng.peak);
    row.heap.calls+=current.heap.calls;row.heap.timed+=current.heap.timed;row.heap.total+=current.heap.total;row.heap.peak=std::max(row.heap.peak,current.heap.peak);
    for(unsigned i=0;i<Counters;++i)row.counters[i]+=current.counters[i];
    row.elapsed += current.elapsed; row.peak = std::max(row.peak, uint64_t(current.elapsed)); row.scenes |= 1ULL << unsigned(current.scene);
    for (unsigned i=0; i<N; ++i) { auto& dst=row.values[i]; auto& src=current.values[i]; dst.sum+=src.sum; dst.calls+=src.calls; dst.peak=std::max(dst.peak,src.peak);dst.cpu+=src.cpu;dst.cpuWall+=src.cpuWall;dst.cpuCalls+=src.cpuCalls;dst.heapCalls+=src.heapCalls;dst.heapScopeCalls+=src.heapScopeCalls; }
  } else ++overflowFrames;
  if (current.elapsed >= 50000) {
    if (hitchSize < HitchCount) hitches[hitchSize++] = current;
    else { auto minimum=std::min_element(hitches.begin(),hitches.end(),[](auto const& a,auto const& b){return a.elapsed<b.elapsed;}); if(current.elapsed>minimum->elapsed)*minimum=current; }
  }
}
Background::Background(char const* label) : m_label(label) { if (enabled.load(std::memory_order_acquire)) m_start=nowUs(); }
void Background::stop() {
  if (!m_start) return;
  auto elapsed=nowUs()-m_start;
  std::lock_guard<std::mutex> lock(eventMutex);
  if (enabled.load(std::memory_order_relaxed)) {
    if (eventSize<EventCount) events[eventSize++]={m_label,m_start-started,elapsed}; else ++eventDropped;
  }
  m_start=0;
}
void finishFrames() {
  if (mainThread && !cpuFinalized) {
    schedulerSample(highestWindow, nowUs());
    cpuFinalized = true;
  }
}
bool dump(char const* path) {
  if (!mainThread && !frozen) { errno = EINVAL; return false; }
  if (!frozen) {
    finishFrames();
    enabled.store(false,std::memory_order_release); mainThread=false;
    frozen = true;
  }
  std::lock_guard<std::mutex> lock(eventMutex);
  FILE* f=std::fopen(path,"w"); if(!f)return false;
  errno = 0;
  char buffer[16384]; std::setvbuf(f,buffer,_IOFBF,sizeof(buffer));
  std::fprintf(f,"wait-profile version=1 | buffered until normal exit | detailed frames=1/8 | capacity=60min,128 worst frames>=50ms,2048 background spans\n");
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
  std::fprintf(f,"allocation-profile=on heap-lock-profile=on\n");
#else
  std::fprintf(f,"allocation-profile=off heap-lock-profile=off\n");
#endif
  std::fprintf(f,"capture-console-unix-ms=%lld start-monotonic-us=%lld\n",(long long)captureUnixMs,(long long)started);
  std::fprintf(f,"cpu-query-calibration calls=32 total-us=%lld | selected CPU scopes: detailed frames only, first 8 calls/metric/frame; nested times overlap; queries included in wall times\n",(long long)cpuQueryCalibrationUs);
  std::fprintf(f,"main-priority=%d allowed-core-mask=0x%llx | scenes bitmask: unknown=1 menu=2 ship=4 planet=8 other=16\n",mainPriority,(unsigned long long)coreMask);
  std::fprintf(f,"Times are wall time; nested scopes overlap. Sampled metrics are divided by detailed frames, never by all frames. Lock scopes measure acquisition only; asset-get-work includes loading and waits. CPU sample windows are approximate; not-running includes pacing and waits, NOT proven preemption. No CPU clock measurement.\n");
  for(unsigned j=0;j<WindowCount;++j) {auto const& w=windows[j]; if(!w.frames)continue;
    std::fprintf(f,"[window] start=%.3fs frames=%llu detailed=%llu scenes=0x%llx frame-avg=%.3fms frame-max=%.3fms cpu-running=%.3fms scheduler-wall=%.3fms",j*5.0,(unsigned long long)w.frames,(unsigned long long)w.sampled,(unsigned long long)w.scenes,w.elapsed/(1000.0*w.frames),w.peak/1000.0,w.cpuUs<0?-1:w.cpuUs/1000.0,w.schedulerWallUs/1000.0);
    for(unsigned i=0;i<N;++i){auto const& v=w.values[i]; auto denominator=i<unsigned(Metric::AssetLock)?w.frames:w.sampled; if(v.calls&&denominator)std::fprintf(f," | %s avg=%.3fms max-span=%.3fms calls/frame=%.2f",names[i],v.sum/(1000.0*denominator),v.peak/1000.0,v.calls/double(denominator));}
    if (w.sampled) std::fprintf(f," | rng-lock-sparse calls/detailed-frame=%.2f timed=%llu contended=%llu measured-total=%.3fms max-span=%.3fms (one-in-32 calls on detailed frames; not extrapolated)",w.rng.calls/double(w.sampled),(unsigned long long)w.rng.timed,(unsigned long long)w.rng.contended,w.rng.total/1000.0,w.rng.peak/1000.0);
    std::fputc('\n',f);
    for(unsigned i=0;i<N;++i){auto const& v=w.values[i];if(v.cpuCalls)std::fprintf(f,"[cpu-scope] start=%.3fs metric=%s calls=%llu wall-total-ms=%.3f running-total-ms=%.3f wall-per-call-ms=%.3f running-per-call-ms=%.3f\n",j*5.0,names[i],(unsigned long long)v.cpuCalls,v.cpuWall/1000.0,v.cpu/1000.0,v.cpuWall/(1000.0*v.cpuCalls),v.cpu/(1000.0*v.cpuCalls));}
    for(unsigned i=0;i<N;++i){auto const& v=w.values[i];if(v.heapScopeCalls)std::fprintf(f,"[heap-scope] start=%.3fs metric=%s calls=%llu acquisitions=%llu per-call=%.3f per-detailed-frame=%.3f\n",j*5.0,names[i],(unsigned long long)v.heapScopeCalls,(unsigned long long)v.heapCalls,v.heapCalls/double(v.heapScopeCalls),w.sampled?v.heapCalls/double(w.sampled):0.0);}
    if(w.sampled){
#ifdef STAR_SWITCH2_ALLOCATION_PROFILE
      std::fprintf(f,"[heap-lock] start=%.3fs detailed=%llu calls=%llu timed=%llu measured-total-ms=%.3f max-ms=%.3f stride=16\n",j*5.0,(unsigned long long)w.sampled,(unsigned long long)w.heap.calls,(unsigned long long)w.heap.timed,w.heap.total/1000.0,w.heap.peak/1000.0);
#endif
      for(unsigned i=0;i<Counters;++i)if(w.counters[i])std::fprintf(f,"[work-count] start=%.3fs detailed=%llu kind=%s total=%llu per-detailed-frame=%.3f\n",j*5.0,(unsigned long long)w.sampled,counterNames[i],(unsigned long long)w.counters[i],w.counters[i]/double(w.sampled));}
  }
  std::sort(hitches.begin(),hitches.begin()+hitchSize,[](auto const&a,auto const&b){return a.start<b.start;});
  for(unsigned j=0;j<hitchSize;++j){auto const& h=hitches[j];std::fprintf(f,"[hitch] at=%.3fs frame=%llu scene=%u detailed=%d elapsed=%.3fms",(h.start-started)/1e6,(unsigned long long)h.number,unsigned(h.scene),h.sampled,h.elapsed/1000.0);for(unsigned i=0;i<N;++i)if(h.values[i].calls)std::fprintf(f," | %s=%.3fms",names[i],h.values[i].sum/1000.0);std::fputc('\n',f);}
  for(unsigned j=0;j<eventSize;++j)std::fprintf(f,"[background] at=%.3fs duration=%.3fms label=%s\n",events[j].start/1e6,events[j].elapsed/1000.0,events[j].label);
  std::fprintf(f,"overflow-frames=%llu dropped-background-spans=%u\n",(unsigned long long)overflowFrames,eventDropped);
  bool ok=std::ferror(f)==0;
  int writeError = errno;
  int closeResult = std::fclose(f);
  if (!ok) { errno = writeError ? writeError : EIO; return false; }
  return closeResult == 0;
}
}
