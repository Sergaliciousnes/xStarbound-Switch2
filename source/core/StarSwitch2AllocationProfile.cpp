#include "StarSwitch2AllocationProfile.hpp"
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_ALLOCATION_PROFILE)
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <limits>
#include <mutex>
#ifdef __SWITCH2__
#include <switch.h>
#endif

// The signed return-address delta is independent of the NRO's load address.
extern "C" void starAllocationProfileAnchor() {}
namespace Star::Switch2AllocationProfile {
namespace {
constexpr unsigned Stages=OutsideStage+1,Kinds=unsigned(Kind::Count),Sites=2048,Slow=32,Scenes=5,Windows=720;
char const* names[]={"new","new-array","delete","delete-array","delete-sized","delete-array-sized","malloc","calloc","realloc","free"};
struct Totals {uint64_t calls=0,bytes=0,time=0,peak=0,nulls=0;};
struct Site {int64_t offset=0;unsigned stage=0,kind=0,scene=0;uint64_t samples=0,time=0,peak=0,bytes=0,nulls=0;};
struct Window {uint64_t frames=0,details=0,scenes=0;std::array<Totals,Scenes> sums{};};
struct Event {Context context;int64_t offset=0;unsigned kind=0;uint64_t bytes=0,time=0;bool result=false;};
struct ThreadInfo {char name[64]{};uint64_t id=0,mask=0;int priority=-1,preferred=-1;uint32_t affinityResult=UINT32_MAX,priorityResult=UINT32_MAX;};
std::array<std::array<std::array<Totals,Kinds>,Stages>,Scenes> totals{};
std::array<Window,Windows> windows{};
std::array<char const*,Stages> labels{};
std::array<Site,Sites> sites{};
std::array<Event,Slow> slow{};
std::atomic<bool> enabled{false};
std::atomic<bool> metadataFrozen{false};
std::array<ThreadInfo,64> threads{};
unsigned threadCount=0,threadDropped=0;
std::mutex threadMutex;
uint64_t frames=0,details=0,selected=0,dropped=0;
uint64_t overflow=0;
unsigned slowSize=0;
uint64_t clockPairTotal=0,clockPairPeak=0;
thread_local unsigned nesting=0;
thread_local uint32_t random=0x6912ab37;
int64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void addSaturated(uint64_t& value,uint64_t amount){value=amount>UINT64_MAX-value?UINT64_MAX:value+amount;}
}
void start(){
  thread("Main");
  for(unsigned i=0;i<1024;++i){auto before=now();auto elapsed=uint64_t(std::max<int64_t>(0,now()-before));clockPairTotal+=elapsed;clockPairPeak=std::max(clockPairPeak,elapsed);}
  enabled.store(true,std::memory_order_release);
}
void thread(char const* name){
  if(metadataFrozen.load(std::memory_order_relaxed))return;
  ThreadInfo info;std::snprintf(info.name,sizeof(info.name),"%s",name?name:"unnamed");
#ifdef __SWITCH2__
  auto handle=threadGetCurHandle();s32 priority=-1,preferred=-1;u64 mask=0,id=0;
  info.affinityResult=svcGetThreadCoreMask(&preferred,&mask,handle);
  info.priorityResult=svcGetThreadPriority(&priority,handle);
  svcGetThreadId(&id,handle);info.id=id;info.mask=mask;info.preferred=preferred;info.priority=priority;
#endif
  std::lock_guard<std::mutex> guard(threadMutex);
  if(metadataFrozen.load())return;
  if(threadCount<threads.size())threads[threadCount++]=info;else ++threadDropped;
}
void frame(bool detailed,unsigned window,unsigned scene){if(enabled.load(std::memory_order_relaxed)){++frames;details+=detailed;if(window<Windows){auto& w=windows[window];++w.frames;w.details+=detailed;w.scenes|=uint64_t(1)<<std::min(scene,Scenes-1);}}}
bool captureRequested() noexcept {
  if(!enabled.load(std::memory_order_relaxed))return false;
  if(nesting)return false;
  Context context;return Switch2WaitProfile::allocationContext(context);
}
Call::Call(Kind kind,size_t bytes,void const* caller,bool meaningful) noexcept:m_kind(kind),m_bytes(bytes){
  // Must precede ALL TLS accesses: SDK initialization can allocate before TLS.
  if(!enabled.load(std::memory_order_relaxed)||!meaningful)return;
  if(nesting){++nesting;m_entered=true;return;}
  if(!Switch2WaitProfile::allocationContext(m_context))return;
  ++nesting;m_entered=true;
  m_context.stage=std::min(m_context.stage,Stages-1);labels[m_context.stage]=m_context.label;
  m_context.scene=std::min(m_context.scene,Scenes-1);
  auto& t=totals[m_context.scene][m_context.stage][unsigned(kind)];++t.calls;addSaturated(t.bytes,bytes);
  if(m_context.window<Windows){auto& w=windows[m_context.window].sums[m_context.scene];++w.calls;addSaturated(w.bytes,bytes);}else ++overflow;
  // Independent deterministic PRNG, never the game's RNG. Randomize sampling
  // to avoid always selecting one position in repeated fixed-size loops.
  random^=random<<13;random^=random>>17;random^=random<<5;
  int saved=errno;
  m_sample=(random&63)==0;selected+=m_sample;
  m_offset=int64_t(reinterpret_cast<uintptr_t>(caller))-int64_t(reinterpret_cast<uintptr_t>(&starAllocationProfileAnchor));
  m_start=now();errno=saved;
}
Call::~Call() noexcept {
  if(!m_entered)return;
  if(m_start){
    int saved=errno;uint64_t duration=uint64_t(std::max<int64_t>(0,now()-m_start));
    auto& total=totals[m_context.scene][m_context.stage][unsigned(m_kind)];total.time+=duration;total.peak=std::max(total.peak,duration);total.nulls+=!m_result;
    if(m_context.window<Windows){auto& w=windows[m_context.window].sums[m_context.scene];w.time+=duration;w.peak=std::max(w.peak,duration);w.nulls+=!m_result;}
    if(m_sample){
    uint64_t hash=uint64_t(m_offset)^uint64_t(m_context.stage*131+unsigned(m_kind)+m_context.scene*8191);hash^=hash>>33;hash*=0xff51afd7ed558ccdULL;hash^=hash>>33;
    Site* destination=nullptr;
    for(unsigned probe=0;probe<32;++probe){auto& s=sites[(hash+probe)%Sites];if(!s.samples||(s.offset==m_offset&&s.stage==m_context.stage&&s.kind==unsigned(m_kind)&&s.scene==m_context.scene)){destination=&s;break;}}
    if(destination){auto& s=*destination;s.offset=m_offset;s.stage=m_context.stage;s.kind=unsigned(m_kind);s.scene=m_context.scene;++s.samples;s.time+=duration;s.peak=std::max(s.peak,duration);addSaturated(s.bytes,m_bytes);s.nulls+=!m_result;}else ++dropped;
    }
    if(duration>=1000000){
      Event* e=nullptr;if(slowSize<Slow)e=&slow[slowSize++];
      else{auto least=std::min_element(slow.begin(),slow.end(),[](auto const& a,auto const& b){return a.time<b.time;});if(duration>least->time)e=&*least;}
      if(e)*e={m_context,m_offset,unsigned(m_kind),m_bytes,duration,m_result};
    }
    errno=saved;
  }
  --nesting;
}
bool dump(char const* path){
  enabled.store(false,std::memory_order_release);
  {std::lock_guard<std::mutex> guard(threadMutex);metadataFrozen.store(true);}
  FILE* f=std::fopen(path,"w");if(!f)return false;
  char buffer[16384];std::setvbuf(f,buffer,_IOFBF,sizeof(buffer));errno=0;
  std::fprintf(f,"allocation-callers version=1 anchor=starAllocationProfileAnchor\nframes=%llu detailed=%llu selected=%llu dropped-samples=%llu static-bytes=%zu overflow-operations=%llu\n",(unsigned long long)frames,(unsigned long long)details,(unsigned long long)selected,(unsigned long long)dropped,sizeof(totals)+sizeof(sites)+sizeof(slow)+sizeof(windows)+sizeof(threads),(unsigned long long)overflow);
  std::fprintf(f,"clock-pair-calibration calls=1024 total-ns=%llu max-ns=%llu\n",(unsigned long long)clockPairTotal,(unsigned long long)clockPairPeak);
  std::fprintf(f,"Thread affinity/priority are read-only snapshots at engine thread entry or main capture start, not CPU utilization; SDK-internal threads may be absent. thread-metadata-dropped=%u\n",threadDropped);
  for(unsigned i=0;i<threadCount;++i){auto const& t=threads[i];std::fprintf(f,"[thread-affinity] id=%llu mask=0x%llx preferred=%d priority=%d affinity-result=0x%x priority-result=0x%x name=%s\n",(unsigned long long)t.id,(unsigned long long)t.mask,t.preferred,t.priority,t.affinityResult,t.priorityResult,t.name);}
  std::fprintf(f,"Main detailed frames only; root API calls suppress nested new-to-malloc/delete-to-free double counting. Every eligible root operation is timed; caller summaries randomly sample about 1/64 operations. Slow rows retain the worst eligible operations >=1ms, independently of sampling. Times include original allocator/SDK work, waits and preemption; not CPU-only or proven contention. Bytes are size arguments, not live/resident memory. Delete/free zero-size arguments mean unknown size; null realloc result is not necessarily failure. Aligned/nothrow C++ variants and direct _r/memalign APIs are not exhaustively covered. No stack walking, thread suspension or allocation during recording. API rows use the nearest active scope exclusively; the separate heap-scope report remains inclusive. Timing adds overhead; clock-pair calibration does not include all instrumentation costs.\n");
  for(unsigned j=0;j<Windows;++j){auto const& w=windows[j];if(!w.frames)continue;for(unsigned scene=0;scene<Scenes;++scene){auto const& t=w.sums[scene];if(t.calls)std::fprintf(f,"[allocation-window] start=%.3fs frames=%llu detailed=%llu scenes=0x%llx scene=%u calls=%llu total-ms=%.6f max-ms=%.6f\n",j*5.0,(unsigned long long)w.frames,(unsigned long long)w.details,(unsigned long long)w.scenes,scene,(unsigned long long)t.calls,t.time/1e6,t.peak/1e6);}}
  for(unsigned scene=0;scene<Scenes;++scene)for(unsigned stage=0;stage<Stages;++stage)for(unsigned kind=0;kind<Kinds;++kind){auto const& t=totals[scene][stage][kind];if(t.calls)std::fprintf(f,"[api] scene=%u stage=%s kind=%s calls=%llu size-arguments=%llu total-ms=%.6f max-ms=%.6f null-results=%llu\n",scene,labels[stage]?labels[stage]:"unknown",names[kind],(unsigned long long)t.calls,(unsigned long long)t.bytes,t.time/1e6,t.peak/1e6,(unsigned long long)t.nulls);}
  for(auto const& s:sites)if(s.samples)std::fprintf(f,"[caller] delta=%lld scene=%u stage=%s kind=%s samples=%llu total-ms=%.6f max-ms=%.6f size-arguments=%llu null-results=%llu\n",(long long)s.offset,s.scene,labels[s.stage]?labels[s.stage]:"unknown",names[s.kind],(unsigned long long)s.samples,s.time/1e6,s.peak/1e6,(unsigned long long)s.bytes,(unsigned long long)s.nulls);
  for(unsigned i=0;i<slowSize;++i){auto const& e=slow[i];std::fprintf(f,"[slow] frame=%llu scene=%u delta=%lld stage=%s kind=%s milliseconds=%.6f size-argument=%llu result-nonnull-or-void-success=%d\n",(unsigned long long)e.context.frame,e.context.scene,(long long)e.offset,e.context.label,names[e.kind],e.time/1e6,(unsigned long long)e.bytes,e.result);}
  bool ok=!std::ferror(f);int error=errno;int result=std::fclose(f);if(!ok){errno=error?error:EIO;return false;}return result==0;
}
}
#endif
