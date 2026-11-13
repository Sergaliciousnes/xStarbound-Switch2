#include "StarSwitch2SimulationProfile.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cerrno>
#ifdef __SWITCH2__
#include <switch.h>
#endif
namespace Star::Switch2SimulationProfile {
#ifdef STAR_SIMULATION_TEST_CLOCK
int64_t testNow();
#endif
namespace {
constexpr size_t K=size_t(Kind::Count),Windows=720,Slots=16,Names=128,TickNames=64;
struct Stats {uint64_t calls=0,total=0,peak=0;};
struct Named {Stats stats;unsigned kind=0;char label[96]{};};
struct Window {
  uint64_t ticks=0,detailed=0,elapsed=0,simulated=0,advanced=0,peak=0,first=0,last=0,entities[3]{},fidelities[4]{};
  std::array<Stats,K> phases{};
};
struct Slot {
  std::mutex mutex;char world[192]{};double targetHz=0;uint64_t threadId=0,coreMask=0;int priority=0,preferred=0;unsigned queried=0,queryErrors=0;uint64_t sequence=0,overflow=0,droppedNames=0;
  std::array<Window,Windows> windows{};std::array<Named,Names> names{};
};
std::array<Slot,Slots> slots{};
std::mutex registration;size_t used=0;
std::atomic<bool> enabled{false};std::atomic<uint64_t> droppedWorlds{0},inflight{0};
int64_t started=0;thread_local bool owner=false;
struct Current {
  Window data;std::array<Named,TickNames> names{};uint64_t dropped=0;
  int64_t start=0;bool active=false,detail=false;char const* actor="";
};
thread_local Current current;
thread_local int slotIndex=-1;
char const* kinds[]={"work","lock","world-update","inbound","messages","outbound","action","sync","sleep",
  "entity-npc","entity-object","entity-other","npc-update","npc-script","npc-movement","npc-status","npc-shared","path-find","path-explore"};
static_assert(std::size(kinds)==K);
int64_t now(){
#ifdef STAR_SIMULATION_TEST_CLOCK
  return testNow();
#elif defined(__SWITCH2__)
  return armTicksToNs(armGetSystemTick())/1000;
#else
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
void merge(Stats& a,Stats const& b){a.calls+=b.calls;a.total+=b.total;a.peak=std::max(a.peak,b.peak);}
template<size_t N> bool named(std::array<Named,N>& table,unsigned kind,char const* label,Stats const& stats){
  char key[96]{};uint32_t hash=2166136261u^kind;
  for(size_t i=0;label[i]&&i<sizeof(key)-1;++i){key[i]=label[i];hash=(hash^uint8_t(key[i]))*16777619u;}
  for(size_t i=0;i<N;++i){auto& row=table[(hash+i)%N];
    if(!row.stats.calls){row.kind=kind;std::memcpy(row.label,key,sizeof(key));}
    if(row.kind==kind&&std::memcmp(row.label,key,sizeof(key))==0){merge(row.stats,stats);return true;}
  }return false;
}
}
void start(){int saved=errno;owner=true;started=now();enabled.store(true,std::memory_order_release);errno=saved;}
Tick::Tick(char const* world,double step) noexcept {
  if(!enabled.load(std::memory_order_acquire))return;
  int saved=errno;
  if(slotIndex==-1){std::lock_guard<std::mutex> guard(registration);
    if(!enabled.load(std::memory_order_acquire)){errno=saved;return;}
    if(used==Slots){slotIndex=-2;++droppedWorlds;errno=saved;return;}
    slotIndex=int(used++);slots[size_t(slotIndex)].priority=-1;slots[size_t(slotIndex)].preferred=-1;slots[size_t(slotIndex)].targetHz=step>0?1.0/step:0;std::snprintf(slots[size_t(slotIndex)].world,192,"%s",world?world:"");
#ifdef __SWITCH2__
    auto& info=slots[size_t(slotIndex)];auto handle=threadGetCurHandle();u64 id=0,mask=0;s32 priority=-1,preferred=-1;
    info.queried=1;
    info.queryErrors=(R_FAILED(svcGetThreadId(&id,handle))?1u:0u) | (R_FAILED(svcGetThreadPriority(&priority,handle))?2u:0u) | (R_FAILED(svcGetThreadCoreMask(&preferred,&mask,handle))?4u:0u);
    info.threadId=id;info.coreMask=mask;info.priority=priority;info.preferred=preferred;
#endif
  }
  if(slotIndex<0){errno=saved;return;}
  auto& slot=slots[size_t(slotIndex)];current={};current.start=now();current.active=true;
  current.detail=(slot.sequence++%8)==0;current.data.ticks=1;current.data.detailed=current.detail;
  m_active=true;++inflight;errno=saved;
}
Tick::~Tick(){
  if(!m_active)return;
  int saved=errno;current.active=false;--inflight;
  if(enabled.load(std::memory_order_acquire)){
    auto ended=now();auto elapsed=uint64_t(std::max<int64_t>(0,ended-current.start));auto index=uint64_t(std::max<int64_t>(0,current.start-started))/5000000;
    auto& slot=slots[size_t(slotIndex)];std::lock_guard<std::mutex> guard(slot.mutex);
    if(enabled.load(std::memory_order_acquire)){
      if(index>=Windows)++slot.overflow;
      else {auto& w=slot.windows[index];++w.ticks;w.detailed+=current.detail;w.elapsed+=elapsed;w.simulated+=current.data.simulated;w.advanced+=current.data.advanced;if(w.ticks==1)w.first=uint64_t(current.start);w.last=uint64_t(ended);w.peak=std::max(w.peak,elapsed);
        for(size_t i=0;i<3;++i)w.entities[i]+=current.data.entities[i];
        for(size_t i=0;i<4;++i)w.fidelities[i]+=current.data.fidelities[i];
        for(size_t i=0;i<K;++i)merge(w.phases[i],current.data.phases[i]);
        slot.droppedNames+=current.dropped;
        for(auto const& n:current.names)if(n.stats.calls&&!named(slot.names,n.kind,n.label,n.stats))slot.droppedNames+=n.stats.calls;
      }
    }
  }errno=saved;
}
void advance(double dt){if(current.active&&enabled.load(std::memory_order_relaxed)&&dt>0){current.data.simulated+=uint64_t(dt*1e6+0.5);++current.data.advanced;}}
void fidelity(unsigned level){if(current.active&&enabled.load(std::memory_order_relaxed))++current.data.fidelities[std::min(level,3u)];}
void entity(unsigned type){if(current.active&&enabled.load(std::memory_order_relaxed))++current.data.entities[std::min(type,2u)];}
Scope::Scope(Kind kind,char const* label,bool sampled) noexcept:m_kind(kind),m_label(label?label:""){
  if(!current.active||!enabled.load(std::memory_order_relaxed)||(sampled&&!current.detail))return;
  int saved=errno;
  if(kind==Kind::NpcUpdate){m_previous=current.actor;current.actor=m_label;m_actor=true;}
  if(!*m_label)m_label=current.actor;
  m_start=now();errno=saved;
}
void Scope::stop() noexcept {
  if(!m_start)return;
  int saved=errno;auto start=m_start;m_start=0;
  if(current.active&&enabled.load(std::memory_order_relaxed)){
    auto elapsed=uint64_t(std::max<int64_t>(0,now()-start));Stats s{1,elapsed,elapsed};merge(current.data.phases[size_t(m_kind)],s);
    if(*m_label&&!named(current.names,unsigned(m_kind),m_label,s))++current.dropped;
  }
  if(m_actor)current.actor=m_previous;
  errno=saved;
}
bool dump(char const* path){
  if(!owner){errno=EINVAL;return false;}
  enabled.store(false,std::memory_order_release);
  {std::lock_guard<std::mutex> barrier(registration);}
  // Publishers recheck enabled under their slot mutex. Once each barrier has
  // completed, published data is immutable even if a server tick is in flight.
  for(size_t n=0;n<used;++n){std::lock_guard<std::mutex> barrier(slots[n].mutex);}
  FILE* f=std::fopen(path,"w");if(!f)return false;char buffer[16384];std::setvbuf(f,buffer,_IOFBF,sizeof(buffer));
  std::fprintf(f,"simulation-profile version=1 sample-stride=8 windows=720 world-capacity=16 named-capacity=128\n");
  std::fprintf(f,"start-monotonic-us=%lld static-bytes=%zu dropped-worlds=%llu inflight-omitted=%llu\n",(long long)started,sizeof(slots),(unsigned long long)droppedWorlds.load(),(unsigned long long)inflight.load());
  std::fprintf(f,"Server cycles include work, sync and sleep. Observed Hz=ticks/window active span seconds; simulated-rate=advanced simulation seconds/active span seconds (pause/timescale included). Boundary cycles charged to start window. Detail phases are one tick in eight; coarse phases all ticks. Nested times overlap. No CPU-only timing, main-frame attribution or hot-path disk writes. Named labels truncated; actor labels classify owning NPC, not individual Lua functions.\n");
  for(size_t n=0;n<used;++n){auto const& slot=slots[n];std::fprintf(f,"[world] slot=%zu overflow-ticks=%llu dropped-named-samples=%llu target-hz=%.3f thread-queried=%u thread-query-errors=%u thread-id=%llu priority=%d core-mask=0x%llx preferred=%d label=%s\n",n,(unsigned long long)slot.overflow,(unsigned long long)slot.droppedNames,slot.targetHz,slot.queried,slot.queryErrors,(unsigned long long)slot.threadId,slot.priority,(unsigned long long)slot.coreMask,slot.preferred,slot.world);
    for(size_t i=0;i<Windows;++i){auto const& w=slot.windows[i];if(!w.ticks)continue;
      std::fprintf(f,"[server-window] slot=%zu start=%.3fs ticks=%llu detailed=%llu cycle-ms=%.3f peak-ms=%.3f simulated-ms=%.3f span-ms=%.3f advanced-ticks=%llu observed-hz=%.3f simulated-rate=%.4f npc-updates=%llu object-updates=%llu other-updates=%llu fidelity-min=%llu fidelity-low=%llu fidelity-medium=%llu fidelity-high=%llu\n",n,i*5.0,(unsigned long long)w.ticks,(unsigned long long)w.detailed,w.elapsed/1000.0,w.peak/1000.0,w.simulated/1000.0,(w.last-w.first)/1000.0,(unsigned long long)w.advanced,w.last>w.first?w.ticks*1e6/(w.last-w.first):0.0,w.last>w.first?double(w.simulated)/(w.last-w.first):0.0,(unsigned long long)w.entities[0],(unsigned long long)w.entities[1],(unsigned long long)w.entities[2],(unsigned long long)w.fidelities[0],(unsigned long long)w.fidelities[1],(unsigned long long)w.fidelities[2],(unsigned long long)w.fidelities[3]);
      for(size_t k=0;k<K;++k){auto const& s=w.phases[k];if(s.calls)std::fprintf(f,"[server-phase] slot=%zu start=%.3fs kind=%s calls=%llu total-ms=%.3f max-ms=%.3f\n",n,i*5.0,kinds[k],(unsigned long long)s.calls,s.total/1000.0,s.peak/1000.0);}
    }
    for(auto const& s:slot.names)if(s.stats.calls)std::fprintf(f,"[server-name] slot=%zu kind=%s calls=%llu total-ms=%.3f max-ms=%.3f label=%s\n",n,kinds[s.kind],(unsigned long long)s.stats.calls,s.stats.total/1000.0,s.stats.peak/1000.0,s.label);
  }
  bool ok=!std::ferror(f);int saved=errno;int closed=std::fclose(f);if(!ok){errno=saved?saved:EIO;return false;}return closed==0;
}
}
