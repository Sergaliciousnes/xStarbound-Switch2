// Standalone SDK-only diagnostic: no Star engine, SDL, Vulkan or assets linked.
#include "StarSwitch2LockProbe.hpp"
#include "../test/StarAllocatorContentionBenchmark.hpp"
#include <switch.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <thread>
namespace {
using Clock=Star::AllocatorContentionBenchmark::Clock;
namespace Probe=Star::LockProbe;
enum class Work {Cpu,Private,Shared,Allocate};
enum class Mode {Default,Equal,Separate,Same};
#ifdef STAR_LOCK_PATH_HOST
constexpr unsigned TrialMs=5;
#else
constexpr unsigned TrialMs=200;
#endif
char const* workName(Work w){switch(w){case Work::Cpu:return "cpu-only";case Work::Private:return "private-rmutex";case Work::Shared:return "shared-rmutex";default:return "sdk-allocation";}}
char const* modeName(Mode m){switch(m){case Mode::Default:return "default";case Mode::Equal:return "equal-priority";case Mode::Separate:return "equal-separate-cores";default:return "equal-same-core";}}
struct alignas(64) Protected {RMutex mutex{};uint64_t count=0,value=0x91d0a3651bdc6743ULL;};
struct Settings {
  Handle handle=0;u64 id=0,mask=0;s32 priority=-1,preferred=-1;
  Result idResult=0,priorityResult=0,maskResult=0;
};
Settings settings(){Settings s;s.handle=threadGetCurHandle();s.idResult=svcGetThreadId(&s.id,s.handle);s.priorityResult=svcGetThreadPriority(&s.priority,s.handle);s.maskResult=svcGetThreadCoreMask(&s.preferred,&s.mask,s.handle);return s;}
int firstCore(uint64_t mask){for(int i=0;i<32;++i)if(mask&(uint64_t(1)<<i))return i;return -1;}
struct alignas(64) Record {
  Settings before,actual,restored;Result setPriority=0,setMask=0,restorePriority=0,restoreMask=0,cpuStartResult=0,cpuEndResult=0;
  uint64_t operations=0,checks=0,checksum=0,cpuStart=0,cpuEnd=0;
  u32 firstCpu=UINT32_MAX,lastCpu=UINT32_MAX;
  bool ok=true,changedPriority=false,changedMask=false,restoreVerified=true;
  Clock::time_point started{},finished{};Probe::Counters probe;Protected local;
};
void configure(Record& r,unsigned identity,Mode mode,s32 mainPriority,int mainCore){
  r.before=settings();
  r.ok=r.before.idResult==0&&r.before.priorityResult==0&&r.before.maskResult==0;
  if(r.ok&&identity&&mode!=Mode::Default){
    r.setPriority=svcSetThreadPriority(r.before.handle,mainPriority);r.changedPriority=r.setPriority==0;r.ok&=r.setPriority==0;
  }
  int target=-1;
  if(r.ok&&(mode==Mode::Separate||mode==Mode::Same)){
    target=identity&&mode==Mode::Separate?firstCore(r.before.mask&~(uint64_t(1)<<mainCore)):mainCore;
    if(target<0)r.setMask=UINT32_MAX;
    else r.setMask=svcSetThreadCoreMask(r.before.handle,target,uint32_t(1)<<target);
    r.changedMask=r.setMask==0;r.ok&=r.setMask==0;
  }
  r.actual=settings();
  r.ok&=r.actual.idResult==0&&r.actual.priorityResult==0&&r.actual.maskResult==0;
  if(r.changedPriority)r.ok&=r.actual.priority==mainPriority;
  if(r.changedMask)r.ok&=r.actual.mask==(uint64_t(1)<<target);
}
void restore(Record& r){
  if(r.changedMask)r.restoreMask=svcSetThreadCoreMask(r.before.handle,r.before.preferred,uint32_t(r.before.mask));
  if(r.changedPriority)r.restorePriority=svcSetThreadPriority(r.before.handle,r.before.priority);
  r.ok&=r.restoreMask==0&&r.restorePriority==0;
  r.restored=settings();
  if(r.changedMask||r.changedPriority){
    r.restoreVerified=r.restored.priorityResult==0&&r.restored.maskResult==0&&r.restored.priority==r.before.priority&&r.restored.mask==r.before.mask&&r.restored.preferred==r.before.preferred;
    r.ok&=r.restoreVerified;
  }
}
uint64_t mix(uint64_t v){v^=v>>27;v*=0x2545f4914f6cdd1dULL;return v^(v>>31);}
void endAllocationTiming(void* data){
  Probe::attach(nullptr);auto& r=*static_cast<Record*>(data);
  r.cpuEndResult=svcGetInfo(&r.cpuEnd,InfoType_ThreadTickCount,r.before.handle,TickCountInfo_Total);
}
void perform(Record& r,Protected& shared,Work work,unsigned identity,Clock::time_point deadline,bool trace){
  r.firstCpu=svcGetCurrentProcessorNumber();
  r.cpuStartResult=svcGetInfo(&r.cpuStart,InfoType_ThreadTickCount,r.before.handle,TickCountInfo_Total);
  Probe::attach(trace?&r.probe:nullptr);
  r.started=Clock::now();
  if(work==Work::Allocate){
    auto result=Star::AllocatorContentionBenchmark::churn(Star::AllocatorContentionBenchmark::sdk(),false,identity,deadline,endAllocationTiming,&r);
    r.operations=result.operations;r.checks=result.checks;r.ok&=result.ok;r.finished=result.finished;
  }else{
    Protected& p=work==Work::Shared?shared:r.local;
    uint64_t value=0x37dca649b1ULL+identity;
    do{
      for(unsigned j=0;j<256;++j){
        if(work==Work::Cpu)value=mix(value);
        else{rmutexLock(&p.mutex);p.value=mix(p.value);++p.count;rmutexUnlock(&p.mutex);}
        ++r.operations;
      }
    }while(Clock::now()<deadline);
    r.checksum=value;r.finished=Clock::now();
  }
  Probe::attach(nullptr);
  if(work!=Work::Allocate)r.cpuEndResult=svcGetInfo(&r.cpuEnd,InfoType_ThreadTickCount,r.before.handle,TickCountInfo_Total);
  r.lastCpu=svcGetCurrentProcessorNumber();
}
bool trial(FILE* log,Work work,Mode mode,unsigned count,bool trace,unsigned number){
  std::array<Record,4> rows;Protected shared;std::array<std::thread,3> helpers;
  std::atomic<unsigned> ready{0};std::atomic<bool> start{false},abort{false};Clock::time_point deadline;
  unsigned created=0;bool creationFailed=false;
  auto main=settings();int mainCore=firstCore(main.mask);
  if(mainCore<0){std::fprintf(log,"[case] status=skipped reason=no-main-core\n");return true;}
  configure(rows[0],0,mode,main.priority,mainCore);if(!rows[0].ok)abort.store(true);
  try{
    for(unsigned i=1;i<count;++i){
      helpers[i-1]=std::thread([&,i]{
        configure(rows[i],i,mode,main.priority,mainCore);if(!rows[i].ok)abort.store(true);
        ++ready;while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        if(!abort.load())perform(rows[i],shared,work,i,deadline,trace);
        restore(rows[i]);
      });++created;
    }
  }catch(...){abort.store(true);creationFailed=true;}
  while(ready.load()<created)std::this_thread::yield();
  auto begin=Clock::now();deadline=begin+std::chrono::milliseconds(TrialMs);
  Probe::enable(trace);start.store(true,std::memory_order_release);
  if(!abort.load())perform(rows[0],shared,work,0,deadline,trace);
  for(unsigned i=0;i<created;++i)helpers[i].join();
  Probe::enable(false);restore(rows[0]);
  auto finished=begin;uint64_t total=0;bool valid=!creationFailed&&!abort.load();
  for(unsigned i=0;i<count;++i){total+=rows[i].operations;finished=std::max(finished,rows[i].finished);valid&=rows[i].ok;}
  if(work==Work::Shared&&!abort.load())valid&=shared.count==total;
  if(work==Work::Private&&!abort.load())for(unsigned i=0;i<count;++i)valid&=rows[i].local.count==rows[i].operations;
  double wall=std::chrono::duration<double,std::milli>(finished-begin).count();
  std::fprintf(log,"[case] work=%s mode=%s threads=%u trace=%u trial=%u status=%s wall-ms=%.3f operations=%llu aggregate-Mop/s=%.6f shared-count=%llu\n",workName(work),modeName(mode),count,trace,number,abort.load()?"skipped":valid?"verified":"FAILED",wall,(unsigned long long)total,wall>0?total/(wall*1000):0,(unsigned long long)shared.count);
  for(unsigned i=0;i<count;++i){auto const& r=rows[i];auto const& s=r.probe;
    double elapsed=std::chrono::duration<double,std::milli>(r.finished-r.started).count();
    auto ms=[](uint64_t ticks){return armTicksToNs(ticks)/1e6;};
    std::fprintf(log,"[thread] index=%u handle=0x%x id=%llu priority-before=%d priority-actual=%d mask-before=0x%llx mask-actual=0x%llx preferred=%d cpu-first=%u cpu-last=%u set-priority=0x%x set-mask=0x%x restore-priority=0x%x restore-mask=0x%x query-id=0x%x query-priority=0x%x query-mask=0x%x elapsed-ms=%.3f running-ms=%.3f cpu-start-result=0x%x cpu-end-result=0x%x operations=%llu checks=%llu Mop/s=%.6f checksum=0x%llx\n",i,r.before.handle,(unsigned long long)r.actual.id,r.before.priority,r.actual.priority,(unsigned long long)r.before.mask,(unsigned long long)r.actual.mask,r.actual.preferred,r.firstCpu,r.lastCpu,r.setPriority,r.setMask,r.restorePriority,r.restoreMask,r.actual.idResult,r.actual.priorityResult,r.actual.maskResult,elapsed,r.cpuStartResult==0&&r.cpuEndResult==0&&r.cpuEnd>=r.cpuStart?ms(r.cpuEnd-r.cpuStart):-1,r.cpuStartResult,r.cpuEndResult,(unsigned long long)r.operations,(unsigned long long)r.checks,elapsed>0?r.operations/(elapsed*1000):0,(unsigned long long)r.checksum);
    std::fprintf(log,"[locks] index=%u heap-calls=%llu heap-samples=%llu heap-sampled-ms=%.6f heap-max-ms=%.6f arbitrate-lock-calls=%llu lock-samples=%llu lock-sampled-ms=%.6f lock-max-ms=%.6f lock-errors=%llu arbitrate-unlock-calls=%llu unlock-samples=%llu unlock-sampled-ms=%.6f unlock-max-ms=%.6f unlock-errors=%llu\n",i,(unsigned long long)s.heapCalls,(unsigned long long)s.heapSamples,ms(s.heapTicks),ms(s.heapPeak),(unsigned long long)s.waitCalls,(unsigned long long)s.waitSamples,ms(s.waitTicks),ms(s.waitPeak),(unsigned long long)s.waitErrors,(unsigned long long)s.wakeCalls,(unsigned long long)s.wakeSamples,ms(s.wakeTicks),ms(s.wakePeak),(unsigned long long)s.wakeErrors);
    std::fprintf(log,"[restored] index=%u priority=%d mask=0x%llx preferred=%d query-priority=0x%x query-mask=0x%x verified=%s\n",i,r.restored.priority,(unsigned long long)r.restored.mask,r.restored.preferred,r.restored.priorityResult,r.restored.maskResult,r.restoreVerified?"true":"false");
    if(r.restoreMask||r.restorePriority||!r.restoreVerified){std::fprintf(log,"[error] thread settings restoration failed; stopping tests\n");std::fflush(log);return false;}
  }
  std::fflush(log);return abort.load()?!creationFailed:valid;
}
}
int main(int argc,char** argv){
#ifdef STAR_LOCK_PATH_HOST
  char const* path=argc>1?argv[1]:"lock-path-host.log";
#else
  (void)argc;(void)argv;
  char const* path="sdmc:/xstarbound-lock-path.log";
#endif
  FILE* log=std::fopen(path,"w");if(!log)return 1;
  char buffer[16384];std::setvbuf(log,buffer,_IOFBF,sizeof(buffer));
  std::fprintf(log,"lock-path version=1 compiler=%s tick-hz=%llu duration-ms=%u trials=3\nStandalone SDK newlib/libounce; no engine, SDL, Vulkan, assets or allocation replacement. Settings variants affect test threads only and restore their original priority/core masks. Trace samples one in 16 heap acquisitions and arbitration calls; nested times overlap; no extrapolation. Untraced cases still have forwarding wrapper overhead. No locks are disabled or replaced.\n",__VERSION__,(unsigned long long)armGetSystemTickFreq(),TrialMs);std::fflush(log);
  bool ok=true;
  for(Work work:{Work::Cpu,Work::Private,Work::Shared,Work::Allocate})for(unsigned count:{1u,2u,4u})for(unsigned n=1;n<=3&&ok;++n){
    if(!appletMainLoop()){std::fprintf(log,"cancelled\n");std::fclose(log);return 0;}
    ok=trial(log,work,Mode::Default,count,false,n);
  }
  for(Mode mode:{Mode::Default,Mode::Equal,Mode::Separate,Mode::Same})for(Work work:{Work::Shared,Work::Allocate})for(unsigned n=1;n<=3&&ok;++n){
    if(!appletMainLoop()){std::fprintf(log,"cancelled\n");std::fclose(log);return 0;}
    ok=trial(log,work,mode,2,true,n);
  }
  std::fprintf(log,"finished status=%d\n",ok?0:1);bool written=!std::ferror(log);if(std::fclose(log)!=0)written=false;return ok&&written?0:1;
}
