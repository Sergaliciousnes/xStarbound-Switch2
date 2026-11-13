#include "StarSwitch2ExplorationProfile.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cerrno>
#ifdef __SWITCH2__
#include <switch.h>
#endif
namespace Star::Switch2ExplorationProfile {
#ifdef STAR_EXPLORATION_TEST_CLOCK
int64_t testNow();
#endif
namespace {
constexpr size_t K=size_t(Kind::Count),C=size_t(Counter::Count),Windows=720,Slow=512,Hitches=128;
struct Stats {uint64_t calls=0,total=0,peak=0;};
struct Frame {uint64_t number=0;unsigned scene=0;int64_t start=0,elapsed=0;std::array<Stats,K> phases{};std::array<uint64_t,C> counts{};};
struct Window {uint64_t frames=0,scenes=0;std::array<Stats,K> phases{};std::array<uint64_t,C> counts{};};
struct Event {uint64_t frame=0,detail=0;unsigned scene=0,kind=0;int64_t at=0,elapsed=0;char label[192]{};};
std::array<Window,Windows> windows{};
std::array<Event,Slow> slow{};std::array<Frame,Hitches> hitches{};
struct NamedStat {Stats stats; unsigned kind=0; char label[192]{};};
std::array<NamedStat,512> namedStats{};
uint64_t droppedNamedSamples=0;
void recordNamed(Kind kind,char const* label,uint64_t elapsed) {
  switch(kind) {
    case Kind::StatsEvent:case Kind::StatsAchievement:case Kind::StatsScript:
    case Kind::VmLibrary:case Kind::UiPaneBuild:case Kind::UiWidgetBuild:
    case Kind::UiPaneUpdate:case Kind::UiPaneRender:case Kind::UiTooltip:
    case Kind::ScriptComponentInit:case Kind::BootstrapCompile:
    case Kind::BootstrapLoad:case Kind::BootstrapExecute:
    case Kind::ObjectRender:case Kind::ObjectEffects:case Kind::ObjectTags:case Kind::ObjectAnimator:case Kind::ObjectOrientation:case Kind::ObjectScripted:break;
    default:return;
  }
  // Bounded, allocation-free table also captures many short calls whose total
  // matters even when none reaches the 2ms individual slow-record threshold.
  char key[192]{};size_t length=0;uint32_t hash=2166136261u ^ unsigned(kind);
  while(label[length] && length<sizeof(key)-1) {key[length]=label[length];hash=(hash^uint8_t(key[length]))*16777619u;++length;}
  for(size_t n=0;n<namedStats.size();++n) {
    auto& row=namedStats[(hash+n)%namedStats.size()];
    if(!row.stats.calls) {row.kind=unsigned(kind);std::copy(std::begin(key),std::end(key),std::begin(row.label));}
    if(row.kind==unsigned(kind) && std::equal(std::begin(key),std::end(key),std::begin(row.label))) {
      ++row.stats.calls;row.stats.total+=elapsed;row.stats.peak=std::max(row.stats.peak,elapsed);return;
    }
  }
  ++droppedNamedSamples;
}
size_t slowSize=0,hitchSize=0;
uint64_t overflow=0,orphaned=0;int64_t started=0;
std::atomic<bool> enabled{false};
thread_local bool main=false,inFrame=false,detailed=false;
thread_local Frame current{};
char const* kinds[]={"packet","entity-load","entity-state","entity-init","world-start","tile-apply","entity-delta",
  "tile-hash","terrain-build","liquid-build","terrain-buffer","tile-image","tile-texture","asset-miss","asset-wait",
  "asset-load","image-load","texture-miss","texture-create","atlas-snapshot","gpu-texture",
  "archive-acquire","archive-wait","archive-open","archive-read","universe-statistics","world-step","universe-scripts","stats-event","stats-achievement","stats-lookup","stats-config","stats-vm-setup","stats-component-setup","stats-init","stats-invoke","stats-uninit","stats-destroy","stats-script","stats-bytecode-lookup","stats-compile","stats-bytecode-load","vm-create","vm-state","vm-registry","vm-base","vm-os","vm-library","vm-finalize","vm-root-config","vm-root-bookkeeping","gather-lights","gather-lighting","gather-highlights","gather-entities","gather-tiles","gather-environment","gather-npc-render","gather-object-render","gather-other-render","gather-npc-lights","gather-object-lights","gather-other-lights","gather-npc-directives","gather-object-directives","gather-other-directives","ui-input","ui-update","ui-render","ui-pane-update","ui-pane-render","ui-inventory-update","ui-interact","ui-pane-build","ui-widget-tree","ui-widget-build","ui-tooltip","script-component-init","script-context","script-init-function","bootstrap-compile","bootstrap-dump","bootstrap-load","bootstrap-execute","client-npc-update","client-object-update","client-other-update","object-render","object-effects","object-tags","object-animator","object-orientation","object-scripted"};
char const* counters[]={"entity-creates","tile-packets","tile-cells","terrain-hit","terrain-miss","liquid-hit",
  "liquid-miss","tile-texture-miss","asset-misses","archive-hit","archive-miss","archive-evict","texture-bytes","tile-texture-reuse","stats-events","stats-achievement-checks","stats-scripts","stats-script-files","stats-failures","stats-already-unlocked","stats-bytecode-hit","stats-bytecode-miss","stats-bytecode-evict","metadata-written","metadata-skipped","chat-written","chat-skipped","gather-npcs","gather-objects","gather-other","gather-light-calls","gather-light-sources","gather-drawables","ui-widgets","bootstrap-hit","bootstrap-miss","bootstrap-published","bootstrap-bypass","bootstrap-bytes","bootstrap-error"};
static_assert(std::size(kinds)==K&&std::size(counters)==C);
int64_t now(){
#ifdef STAR_EXPLORATION_TEST_CLOCK
  return testNow();
#elif defined(__SWITCH2__)
  return armTicksToNs(armGetSystemTick())/1000;
#else
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
bool active(){return enabled.load(std::memory_order_relaxed)&&main&&inFrame;}
template <typename T,size_t N> void keep(std::array<T,N>& values,size_t& size,T const& value){
  auto compare=[](T const& a,T const& b){return a.elapsed>b.elapsed;};
  if(size<N){values[size++]=value;std::push_heap(values.begin(),values.begin()+size,compare);}
  else if(value.elapsed>values[0].elapsed){std::pop_heap(values.begin(),values.end(),compare);values.back()=value;std::push_heap(values.begin(),values.end(),compare);}
}
}
void start(){started=now();main=true;enabled.store(true,std::memory_order_release);}
void beginFrame(uint64_t number,unsigned scene,bool detail){
  if(!enabled.load(std::memory_order_relaxed)||!main)return;
  current={};current.number=number;current.scene=scene;current.start=now();detailed=detail;inFrame=true;
}
void count(Counter counter,uint64_t amount){if(active())current.counts[size_t(counter)]+=amount;}
Scope::Scope(Kind kind,char const* label,uint64_t detail,bool sampledOnly) noexcept
  :m_kind(kind),m_label(label?label:""),m_detail(detail){
  if(active()&&(!sampledOnly||detailed)){int saved=errno;m_frame=current.number;m_start=now();errno=saved;}
}
void Scope::stop() noexcept {
  if(!m_start)return;
  auto before=m_start;m_start=0;
  if(!active())return;
  if(m_frame!=current.number){++orphaned;return;}
  int saved=errno;
  auto elapsed=std::max<int64_t>(0,now()-before);auto& stats=current.phases[size_t(m_kind)];
  ++stats.calls;stats.total+=elapsed;stats.peak=std::max(stats.peak,uint64_t(elapsed));
  recordNamed(m_kind,m_label,uint64_t(elapsed));
  if(elapsed>=2000){Event event{};event.frame=m_frame;event.scene=current.scene;event.kind=unsigned(m_kind);event.detail=m_detail;event.at=before-started;event.elapsed=elapsed;std::snprintf(event.label,sizeof(event.label),"%s",m_label);keep(slow,slowSize,event);}
  errno=saved;
}
void endFrame(unsigned scene,int64_t elapsed){
  if(!active())return;
  inFrame=false;current.scene=scene;current.elapsed=elapsed;
  auto index=uint64_t(std::max<int64_t>(0,current.start-started))/5000000;
  if(index<Windows){auto& row=windows[index];++row.frames;row.scenes|=uint64_t(1)<<std::min(scene,31u);
    for(size_t k=0;k<K;++k){auto& a=row.phases[k];auto const& b=current.phases[k];a.calls+=b.calls;a.total+=b.total;a.peak=std::max(a.peak,b.peak);}
    for(size_t c=0;c<C;++c)row.counts[c]+=current.counts[c];
  }else ++overflow;
  if(elapsed>=50000)keep(hitches,hitchSize,current);
}
bool dump(char const* path){
  if(!main){errno=EINVAL;return false;}enabled.store(false,std::memory_order_release);inFrame=false;
  FILE* file=std::fopen(path,"w");if(!file)return false;
  char buffer[16384];std::setvbuf(file,buffer,_IOFBF,sizeof(buffer));
  std::fprintf(file,"exploration-profile version=1 main-thread-only=1 slow-threshold-ms=2 worst-calls=512 worst-frames=128 hitch-threshold-ms=50\n");
  std::fprintf(file,"start-monotonic-us=%lld static-bytes=%zu overflow-frames=%llu orphaned-scopes=%llu\n",(long long)started,sizeof(windows)+sizeof(slow)+sizeof(hitches)+sizeof(namedStats),(unsigned long long)overflow,(unsigned long long)orphaned);
  std::fprintf(file,"Nested phase times overlap. Main-thread API wall time, not CPU-only/GPU time or physical media latency. Tile hash is sampled one frame in eight; other tagged operations cover all frames. Labels are truncated. Archive counters cover main-thread lookups only. detail is entity ID for entity phases, tile-cell/byte work size for relevant phases; zero otherwise. No packet reordering, prefetching, allocator instrumentation or hot-path logging.\n");
  for(size_t w=0;w<Windows;++w){auto const& row=windows[w];if(!row.frames)continue;
    for(size_t k=0;k<K;++k){auto const& s=row.phases[k];if(s.calls)std::fprintf(file,"[phase] start=%.3fs frames=%llu scenes=0x%llx kind=%s calls=%llu total-ms=%.3f max-ms=%.3f\n",w*5.0,(unsigned long long)row.frames,(unsigned long long)row.scenes,kinds[k],(unsigned long long)s.calls,s.total/1000.0,s.peak/1000.0);}
    for(size_t c=0;c<C;++c)if(row.counts[c])std::fprintf(file,"[counter] start=%.3fs kind=%s total=%llu\n",w*5.0,counters[c],(unsigned long long)row.counts[c]);
  }
  std::fprintf(file,"stats-name-capacity=512 dropped-named-samples=%llu\n",(unsigned long long)droppedNamedSamples);
  for(auto const& row:namedStats)if(row.stats.calls)std::fprintf(file,"[stats-name] kind=%s calls=%llu total-ms=%.3f max-ms=%.3f label=%s\n",kinds[row.kind],(unsigned long long)row.stats.calls,row.stats.total/1000.0,row.stats.peak/1000.0,row.label);
  std::sort(slow.begin(),slow.begin()+slowSize,[](auto const&a,auto const&b){return a.elapsed>b.elapsed;});
  for(size_t n=0;n<slowSize;++n){auto const& e=slow[n];std::fprintf(file,"[slow] at=%.6fs frame=%llu scene=%u kind=%s elapsed-ms=%.3f detail=%llu label=%s\n",e.at/1e6,(unsigned long long)e.frame,e.scene,kinds[e.kind],e.elapsed/1000.0,(unsigned long long)e.detail,e.label);}
  std::sort(hitches.begin(),hitches.begin()+hitchSize,[](auto const&a,auto const&b){return a.start<b.start;});
  for(size_t n=0;n<hitchSize;++n){auto const& f=hitches[n];std::fprintf(file,"[frame] at=%.6fs frame=%llu scene=%u elapsed-ms=%.3f",(f.start-started)/1e6,(unsigned long long)f.number,f.scene,f.elapsed/1000.0);for(size_t k=0;k<K;++k)if(f.phases[k].calls)std::fprintf(file," | %s=%.3fms/%llu",kinds[k],f.phases[k].total/1000.0,(unsigned long long)f.phases[k].calls);std::fputc('\n',file);}
  bool okay=std::ferror(file)==0;int saved=errno;int close=std::fclose(file);if(!okay){errno=saved?saved:EIO;return false;}return close==0;
}
}
