#include "StarSwitch2IoProfile.hpp"
#ifdef STAR_SWITCH2_IO_PROFILE
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace Star::Switch2IoProfile {
namespace {
constexpr unsigned Threads=32,Files=256,Windows=720,Ops=unsigned(Op::Count),Caches=unsigned(Cache::Count),Categories=7,Slow=32;
constexpr int64_t WindowUs=5000000;
char const* names[]={"open","close","read","pread","write","pwrite","sync","seek","tell","size","eof","resize"};
char const* cacheNames[]={"block-hit","block-miss","block-pending","block-disabled","script-hit","script-miss"};
char const* categoryNames[]={"unclassified","script","image","audio","font","json","other-asset"};
static_assert(std::size(names)==Ops&&std::size(cacheNames)==Caches);
struct Stats {uint64_t calls=0,requested=0,bytes=0,total=0,lock=0,work=0,peak=0,errors=0;};
// Keep the large static slot array zero-initialized (BSS, not NRO file data).
// Sentinel frame/offset values are assigned when an event is recorded.
struct Event {int64_t at=0,offset=0;uint64_t frame=0;unsigned scene=0,op=0,file=0;Stats stats;char asset[144]{};};
struct AssetCounts {uint64_t calls=0,bytes=0,time=0;};
struct Row {std::array<Stats,Ops> ops{};std::array<uint64_t,Caches> cache{};std::array<AssetCounts,Categories> assets{};std::array<uint64_t,5> readSizes{};};
struct Slot {
  std::atomic<bool> active{false};char name[64]{};
  std::array<Row,Windows> windows{};
  std::array<std::array<Stats,Ops>,Files> files{};
  std::array<Stats,Categories> assets{};
  std::array<Event,Slow> slow{};unsigned slowSize=0;
  uint64_t overflow=0;
};
struct Traffic {uint64_t reads=0,readBytes=0,readUs=0,writes=0,writeBytes=0,writeUs=0,syncs=0,syncUs=0;};
struct Frame {uint64_t number=0;int64_t at=0,elapsed=0;unsigned scene=0;Stats stats;Traffic traffic;};
struct FrameWindow {uint64_t frames=0,ioFrames=0,elapsed=0,scenes=0;Stats stats;Traffic traffic;};
std::array<Slot,Threads> slots;
std::array<std::array<char,240>,Files> paths{};
std::array<FrameWindow,Windows> frameWindows{};
std::array<Frame,128> frames;
unsigned slotCount=0,fileCount=1,frameCount=0;
std::atomic<uint64_t> droppedThreads{0},droppedFiles{0};
std::atomic<bool> recording{false},closed{false};
std::mutex registration;
int64_t started=0;
bool frozen=false;
std::array<bool,Threads> included{};
thread_local int slotIndex=-1;
thread_local unsigned activeDepth=0;
thread_local char pendingName[64]="unnamed";
thread_local char const* assetPath=nullptr;
thread_local bool frameActive=false;
thread_local Frame currentFrame;
int64_t now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void add(Stats& to,Stats const& from){to.calls+=from.calls;to.requested+=from.requested;to.bytes+=from.bytes;to.total+=from.total;to.lock+=from.lock;to.work+=from.work;to.peak=std::max(to.peak,from.peak);to.errors+=from.errors;}
void add(Traffic& to,Traffic const& from){to.reads+=from.reads;to.readBytes+=from.readBytes;to.readUs+=from.readUs;to.writes+=from.writes;to.writeBytes+=from.writeBytes;to.writeUs+=from.writeUs;to.syncs+=from.syncs;to.syncUs+=from.syncUs;}
Slot* enter(){
  if(!recording.load(std::memory_order_relaxed))return nullptr;
  if(slotIndex==-1){
    std::lock_guard<std::mutex> guard(registration);
    if(!recording.load())return nullptr;
    if(slotCount==Threads){slotIndex=-2;++droppedThreads;return nullptr;}
    slotIndex=int(slotCount++);std::snprintf(slots[slotIndex].name,sizeof(pendingName),"%s",pendingName);
  }
  if(slotIndex<0)return nullptr;
  Slot* slot=&slots[slotIndex];
  if(!activeDepth)slot->active.store(true); // seq_cst handshake with freeze
  ++activeDepth;
  if(!recording.load()){if(!--activeDepth)slot->active.store(false);return nullptr;}
  return slot;
}
void leave(Slot* slot){if(!--activeDepth)slot->active.store(false);}
unsigned category(char const* path){
  if(!path)return 0;
  auto dot=std::strrchr(path,'.');if(!dot)return 6;
  if(!std::strcmp(dot,".lua")||!std::strcmp(dot,".pluto"))return 1;
  if(!std::strcmp(dot,".png"))return 2;
  if(!std::strcmp(dot,".ogg")||!std::strcmp(dot,".wav")||!std::strcmp(dot,".mp3"))return 3;
  if(!std::strcmp(dot,".ttf")||!std::strcmp(dot,".otf"))return 4;
  if(!std::strcmp(dot,".json"))return 5;
  return 6;
}
void stats(FILE* f,Stats const& s){std::fprintf(f," calls=%llu requested=%llu bytes=%llu total-ms=%.3f cursor-ms=%.3f work-ms=%.3f max-ms=%.3f errors=%llu",(unsigned long long)s.calls,(unsigned long long)s.requested,(unsigned long long)s.bytes,s.total/1000.0,s.lock/1000.0,s.work/1000.0,s.peak/1000.0,(unsigned long long)s.errors);}
void traffic(FILE* f,Traffic const& s){std::fprintf(f," read-calls=%llu read-bytes=%llu read-ms=%.3f write-calls=%llu write-bytes=%llu write-ms=%.3f sync-calls=%llu sync-ms=%.3f",(unsigned long long)s.reads,(unsigned long long)s.readBytes,s.readUs/1000.0,(unsigned long long)s.writes,(unsigned long long)s.writeBytes,s.writeUs/1000.0,(unsigned long long)s.syncs,s.syncUs/1000.0);}
}
uint16_t file(char const* path){
  if(closed.load(std::memory_order_relaxed))return 0;
  std::lock_guard<std::mutex> guard(registration);
  if(closed.load())return 0;
  if(!path||!*path)path="<unnamed File>";
  // File names are copied only on open, never hashed/formatted for each read.
  for(unsigned i=1;i<fileCount;++i)if(!std::strcmp(path,paths[i].data()))return uint16_t(i);
  if(fileCount==Files){++droppedFiles;return 0;}
  auto index=fileCount++;std::snprintf(paths[index].data(),paths[index].size(),"%s",path);return uint16_t(index);
}
void threadName(char const* name){std::snprintf(pendingName,sizeof(pendingName),"%s",name?name:"unnamed");}
void start(){started=now();threadName("Main");recording.store(true);}
void beginFrame(uint64_t number){if(!recording.load(std::memory_order_relaxed))return;currentFrame={};currentFrame.number=number;currentFrame.at=now()-started;frameActive=true;}
void endFrame(unsigned scene,int64_t elapsed){
  if(!frameActive)return;
  frameActive=false;
  currentFrame.scene=scene;currentFrame.elapsed=elapsed;
  auto index=unsigned(currentFrame.at/WindowUs);
  if(index<Windows){auto& w=frameWindows[index];++w.frames;w.ioFrames+=currentFrame.stats.calls!=0;w.elapsed+=elapsed;w.scenes|=uint64_t(1)<<scene;add(w.stats,currentFrame.stats);add(w.traffic,currentFrame.traffic);}
  if(!currentFrame.stats.calls)return;
  if(frameCount<frames.size())frames[frameCount++]=currentFrame;
  else {auto least=std::min_element(frames.begin(),frames.end(),[](auto const& a,auto const& b){return a.stats.total<b.stats.total;});if(least->stats.total<currentFrame.stats.total)*least=currentFrame;}
}
Operation::Operation(uint16_t fileId,Op op,uint64_t requested,int64_t offset):m_file(fileId),m_op(op),m_requested(requested),m_offset(offset){m_slot=enter();if(m_slot)m_start=now();}
void Operation::acquired(){if(m_slot)m_acquired=now();}
Operation::~Operation(){
  if(!m_slot)return;
  auto* slot=static_cast<Slot*>(m_slot);auto end=now();
  Stats s{1,m_requested,m_bytes,uint64_t(std::max<int64_t>(0,end-m_start)),uint64_t(m_acquired?std::max<int64_t>(0,m_acquired-m_start):0),uint64_t(std::max<int64_t>(0,end-(m_acquired?m_acquired:m_start))),0,uint64_t(!m_complete)};s.peak=s.total;
  auto index=unsigned(std::max<int64_t>(0,m_start-started)/WindowUs);
  if(index<Windows)add(slot->windows[index].ops[unsigned(m_op)],s);else ++slot->overflow;
  add(slot->files[m_file<Files?m_file:0][unsigned(m_op)],s);
  if(m_op==Op::Read||m_op==Op::ReadAbsolute){
    auto c=category(assetPath?assetPath:paths[m_file<Files?m_file:0].data());
    if(!assetPath&&c==6)c=0; // Physical world/pack reads are not logical assets.
    add(slot->assets[c],s);
    if(index<Windows){auto& row=slot->windows[index];auto& asset=row.assets[c];++asset.calls;asset.bytes+=s.bytes;asset.time+=s.total;++row.readSizes[m_requested==0?0:m_requested<=256?1:m_requested<=4096?2:m_requested<=65536?3:4];}
  }
  if(frameActive){
    add(currentFrame.stats,s);auto& t=currentFrame.traffic;
    if(m_op==Op::Read||m_op==Op::ReadAbsolute){++t.reads;t.readBytes+=s.bytes;t.readUs+=s.total;}
    else if(m_op==Op::Write||m_op==Op::WriteAbsolute){++t.writes;t.writeBytes+=s.bytes;t.writeUs+=s.total;}
    else if(m_op==Op::Sync){++t.syncs;t.syncUs+=s.total;}
  }
  if(s.total>=2000){
    Event* event=nullptr;
    if(slot->slowSize<Slow)event=&slot->slow[slot->slowSize++];
    else{auto smallest=std::min_element(slot->slow.begin(),slot->slow.end(),[](auto const& a,auto const& b){return a.stats.total<b.stats.total;});if(s.total>smallest->stats.total)event=&*smallest;}
    if(event){*event={};event->at=m_start-started;event->offset=m_offset;event->frame=frameActive?currentFrame.number:UINT64_MAX;event->file=m_file;event->op=unsigned(m_op);event->stats=s;if(assetPath)std::snprintf(event->asset,sizeof(event->asset),"%s",assetPath);}
  }
  leave(slot);
}
void cache(Cache event){auto* slot=enter();if(!slot)return;auto index=unsigned(std::max<int64_t>(0,now()-started)/WindowUs);if(index<Windows)++slot->windows[index].cache[unsigned(event)];else ++slot->overflow;leave(slot);}
Asset::Asset(char const* path):m_previous(assetPath){assetPath=path;}
Asset::~Asset(){assetPath=m_previous;}
bool dump(char const* path){
  if(!frozen){
    recording.store(false);
    {std::lock_guard<std::mutex> guard(registration);closed.store(true);}
    auto deadline=now()+100000;
    for(unsigned i=0;i<slotCount;++i){while(slots[i].active.load()&&now()<deadline)std::this_thread::yield();included[i]=!slots[i].active.load();}
    frozen=true;
  }
  FILE* f=std::fopen(path,"w");if(!f)return false;
  char buffer[16384];std::setvbuf(f,buffer,_IOFBF,sizeof(buffer));errno=0;
  std::fprintf(f,"io-profile version=1 | all engine File operations, all registered threads | buffered until exit\nstart-monotonic-us=%lld static-capacity-bytes=%zu\n",(long long)started,sizeof(slots)+sizeof(paths)+sizeof(frameWindows)+sizeof(frames));
  std::fprintf(f,"Completed API calls; total includes cursor acquisition, work includes seek/read/restore and scheduling. Nested open/close during resize overlap. No claim of physical disk/cache misses or IPC counts. Asset categories classify actual File reads, not Lua executions. Raw stdio/fstream/log writes and RAM Buffer I/O are outside coverage. Frame sums are main thread only; thread sums overlap in wall time. Frozen in-flight slots omitted.\n");
  for(unsigned i=0;i<fileCount;++i)std::fprintf(f,"[path] file=%u name=%s\n",i,i?paths[i].data():"<unregistered/overflow>");
  for(unsigned w=0;w<Windows;++w){auto const& row=frameWindows[w];if(!row.frames)continue;std::fprintf(f,"[frame-window] start=%.3fs frames=%llu io-frames=%llu scenes=0x%llx frame-average-ms=%.3f",w*5.0,(unsigned long long)row.frames,(unsigned long long)row.ioFrames,(unsigned long long)row.scenes,row.elapsed/(1000.0*row.frames));stats(f,row.stats);traffic(f,row.traffic);std::fputc('\n',f);}
  for(unsigned t=0;t<slotCount;++t){
    auto const& slot=slots[t];std::fprintf(f,"[thread] id=%u included=%d name=%s overflow=%llu\n",t,included[t],slot.name,(unsigned long long)(included[t]?slot.overflow:0));if(!included[t])continue;
    for(unsigned w=0;w<Windows;++w){auto const& row=slot.windows[w];
      for(unsigned op=0;op<Ops;++op)if(row.ops[op].calls){std::fprintf(f,"[io-window] thread=%u start=%.3fs op=%s",t,w*5.0,names[op]);stats(f,row.ops[op]);std::fputc('\n',f);}
      for(unsigned c=0;c<Caches;++c)if(row.cache[c])std::fprintf(f,"[cache-window] thread=%u start=%.3fs kind=%s calls=%llu\n",t,w*5.0,cacheNames[c],(unsigned long long)row.cache[c]);
      for(unsigned c=0;c<Categories;++c)if(row.assets[c].calls)std::fprintf(f,"[asset-window] thread=%u start=%.3fs category=%s calls=%llu bytes=%llu total-ms=%.3f\n",t,w*5.0,categoryNames[c],(unsigned long long)row.assets[c].calls,(unsigned long long)row.assets[c].bytes,row.assets[c].time/1000.0);
      if(std::any_of(row.readSizes.begin(),row.readSizes.end(),[](auto n){return n!=0;}))std::fprintf(f,"[read-sizes] thread=%u start=%.3fs zero=%llu le256=%llu le4096=%llu le65536=%llu larger=%llu\n",t,w*5.0,(unsigned long long)row.readSizes[0],(unsigned long long)row.readSizes[1],(unsigned long long)row.readSizes[2],(unsigned long long)row.readSizes[3],(unsigned long long)row.readSizes[4]);
    }
    for(unsigned id=0;id<fileCount;++id)for(unsigned op=0;op<Ops;++op)if(slot.files[id][op].calls){std::fprintf(f,"[file-total] thread=%u file=%u op=%s",t,id,names[op]);stats(f,slot.files[id][op]);std::fputc('\n',f);}
    for(unsigned c=0;c<Categories;++c)if(slot.assets[c].calls){std::fprintf(f,"[asset-total] thread=%u category=%s",t,categoryNames[c]);stats(f,slot.assets[c]);std::fputc('\n',f);}
    for(unsigned i=0;i<slot.slowSize;++i){auto const& e=slot.slow[i];std::fprintf(f,"[slow-io] thread=%u at=%.6fs frame=%llu file=%u op=%s offset=%lld",t,e.at/1e6,(unsigned long long)e.frame,e.file,names[e.op],(long long)e.offset);stats(f,e.stats);std::fprintf(f," asset=%s\n",e.asset);}
  }
  for(unsigned i=0;i<frameCount;++i){auto const& row=frames[i];std::fprintf(f,"[io-frame] at=%.6fs frame=%llu scene=%u elapsed-ms=%.3f",row.at/1e6,(unsigned long long)row.number,row.scene,row.elapsed/1000.0);stats(f,row.stats);traffic(f,row.traffic);std::fputc('\n',f);}
  std::fprintf(f,"dropped-threads=%llu dropped-file-names=%llu\n",(unsigned long long)droppedThreads.load(),(unsigned long long)droppedFiles.load());
  bool ok=!std::ferror(f);int error=errno;int result=std::fclose(f);if(!ok){errno=error?error:EIO;return false;}return result==0;
}
}
#endif
