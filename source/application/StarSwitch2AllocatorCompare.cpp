#include "StarSwitch2RpmallocCandidate.hpp"
#include "../test/StarAllocatorContentionBenchmark.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <malloc.h>
#ifdef __SWITCH2__
#include <switch.h>
#include "StarSwitch2AllocatorBuild.hpp"
#ifdef STAR_SWITCH2_COMPARE_MIMALLOC
extern "C" int mi_version(void);
#endif
#endif
namespace {
namespace RP=Star::RpmallocCandidate;
namespace Bench=Star::AllocatorContentionBenchmark;
FILE* output=nullptr;
void emitLine(char const* line){std::fprintf(output,"%s\n",line);std::fflush(output);}
void metadata(char const* allocator){
#ifdef __SWITCH2__
  u64 id=0,mask=0;s32 priority=-1,preferred=-1;auto handle=threadGetCurHandle();
  auto ri=svcGetThreadId(&id,handle);auto rp=svcGetThreadPriority(&priority,handle);auto rm=svcGetThreadCoreMask(&preferred,&mask,handle);
  std::fprintf(output,"[allocator-thread] allocator=%s id=%llu handle=0x%x priority=%d mask=0x%llx preferred=%d id-result=0x%x priority-result=0x%x mask-result=0x%x\n",allocator,(unsigned long long)id,handle,priority,(unsigned long long)mask,preferred,ri,rp,rm);
#else
  (void)allocator;
#endif
}
void sdkEnter(){metadata("sdk-libc");}
void rpEnter(){RP::enter();metadata("rpmalloc-1.4.5-sdk-backed");}
bool proceed(){
#ifdef __SWITCH2__
  return appletMainLoop();
#else
  return true;
#endif
}
struct Api {Bench::Backend basic;void*(*zero)(size_t,size_t);void*(*aligned)(size_t,size_t);void(*alignedFree)(void*);};
void* sdkAligned(size_t a,size_t n){
#ifdef _WIN32
  return _aligned_malloc(n,a);
#else
  return ::memalign(a,n);
#endif
}
void sdkAlignedFree(void* p){
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
bool bytes(void* p,size_t n,unsigned char value){auto c=static_cast<unsigned char*>(p);for(size_t i=0;i<n;++i)if(c[i]!=value)return false;return true;}
bool remoteStress(Bench::Backend const& a){
  struct Packet {void* p=nullptr;size_t n=0;unsigned char tag=0;};
  std::array<std::array<Packet,128>,4> boxes{};std::array<std::thread,4> workers;
  std::atomic<unsigned> published{0},consumed{0};std::atomic<bool> start{false},abort{false},failed{false};
  unsigned created=0;
  try{
    for(unsigned i=0;i<4;++i){workers[i]=std::thread([&,i]{
      a.enter();while(!start.load(std::memory_order_acquire))std::this_thread::yield();
      if(!abort.load())for(unsigned round=0;round<64;++round){
        for(unsigned j=0;j<128;++j){auto& p=boxes[i][j];p.n=1+(j*37+round*109+i*11)%8192;p.tag=static_cast<unsigned char>((i+round+j)%251);p.p=failed.load()?nullptr:a.allocate(p.n);if(!p.p)failed.store(true);else std::memset(p.p,p.tag,p.n);}
        ++published;while(published.load()<(round+1)*4)std::this_thread::yield();
        for(auto& p:boxes[(i+3)%4])if(p.p){if(!bytes(p.p,p.n,p.tag))failed.store(true);a.release(p.p);p.p=nullptr;}
        ++consumed;while(consumed.load()<(round+1)*4)std::this_thread::yield();
      }
      a.leave();
    });++created;}
  }catch(...){abort.store(true);}
  start.store(true,std::memory_order_release);for(unsigned i=0;i<created;++i)workers[i].join();
  // All participants finish both barriers, including on allocation failure.
  for(auto& box:boxes)for(auto& p:box)if(p.p)a.release(p.p);
  bool ok=!abort.load()&&!failed.load()&&consumed.load()==256;
  std::fprintf(output,"[remote-stress] allocator=%s workers=4 rounds=64 transferred-blocks=32768 verified=%s\n",a.name,ok?"true":"false");std::fflush(output);return ok;
}
bool correctness(Api const& api){
  auto const& a=api.basic;bool ok=true;a.enter();uint64_t checks=0;
  for(size_t n:{size_t(0),size_t(1),size_t(15),size_t(16),size_t(17),size_t(4096),size_t(32769),size_t(1048576)}){
    auto p=a.allocate(n);if(n&&!p){ok=false;break;}if(p){std::memset(p,0x59,n);ok&=bytes(p,n,0x59);++checks;a.release(p);}
  }
  auto z=api.zero(128,32);if(!z)ok=false;else{ok&=bytes(z,4096,0);++checks;a.release(z);}
  size_t huge=std::numeric_limits<size_t>::max();auto overflow=api.zero(huge,2);ok&=overflow==nullptr;if(overflow)a.release(overflow);
  auto p=static_cast<unsigned char*>(a.resize(nullptr,128));
  if(!p)ok=false;else{
    std::memset(p,0x6a,128);
    for(size_t n:{size_t(32),size_t(8192),size_t(64),size_t(1048576)}){
      auto q=static_cast<unsigned char*>(a.resize(p,n));if(!q){ok=false;break;}
      p=q;ok&=bytes(p,32,0x6a);std::memset(p,0x6a,n);++checks;
    }
    a.release(p);
  }
  for(size_t align:{size_t(16),size_t(64),size_t(256),size_t(4096),size_t(32768)}){
    auto q=api.aligned(align,align+17);if(!q){ok=false;break;}
    ok&=reinterpret_cast<uintptr_t>(q)%align==0;std::memset(q,0x73,align+17);ok&=bytes(q,align+17,0x73);++checks;api.alignedFree(q);
  }
  struct Slot {void* p=nullptr;size_t size=0;unsigned char tag=0;};
  std::array<Slot,64> slots{};uint32_t random=0x918abc21;
  for(unsigned round=0;round<10000&&ok;++round){
    auto& s=slots[Bench::next(random)%slots.size()];
    if(s.p){ok&=bytes(s.p,s.size,s.tag);++checks;if(!ok)break;}
    size_t n=1+Bench::next(random)%8192;
    if(s.p&&(Bench::next(random)&3)==0){a.release(s.p);s={};continue;}
    auto q=a.resize(s.p,n);if(!q){ok=false;break;}
    if(s.p){ok&=bytes(q,std::min(s.size,n),s.tag);++checks;}
    s.p=q;s.size=n;s.tag=static_cast<unsigned char>(round%251);std::memset(q,s.tag,n);
  }
  for(auto& s:slots)if(s.p){ok&=bytes(s.p,s.size,s.tag);a.release(s.p);}
  // Owner exits and returns its heap while live blocks remain. Main then frees
  // those blocks, followed by another owner to exercise orphan-heap reuse.
  for(unsigned pass=0;pass<3&&ok;++pass){
    std::array<Slot,256> remote{};bool producerOk=true;
    try{
      std::thread producer([&]{a.enter();for(unsigned i=0;i<remote.size();++i){auto& s=remote[i];s.size=32+i*7;s.tag=static_cast<unsigned char>(i);s.p=a.allocate(s.size);if(!s.p){producerOk=false;break;}std::memset(s.p,s.tag,s.size);}a.leave();});
      producer.join();
    }catch(...){producerOk=false;}
    ok&=producerOk;
    for(auto& s:remote)if(s.p){ok&=bytes(s.p,s.size,s.tag);++checks;a.release(s.p);}
  }
  ok&=Bench::handoff(a);if(ok)ok=remoteStress(a);a.release(nullptr);
  std::fprintf(output,"[correctness] allocator=%s random-operations=10000 checks=%llu owner-exit-passes=3 overflow/calloc/realloc/alignment/handoff=%s\n",a.name,(unsigned long long)checks,ok?"verified":"FAILED");std::fflush(output);
  a.leave();return ok;
}
void backing(char const* phase){auto s=RP::backing();std::fprintf(output,"[backing] phase=%s charged-bytes=%llu peak-charged-bytes=%llu maps=%llu releases=%llu partial-retained=%llu rejected=%llu errors=%llu active=%llu budget-bytes=134217728\n",phase,(unsigned long long)s.current,(unsigned long long)s.peak,(unsigned long long)s.maps,(unsigned long long)s.releases,(unsigned long long)s.partial,(unsigned long long)s.rejected,(unsigned long long)s.errors,(unsigned long long)s.active);std::fflush(output);}
bool failureTests(){
  RP::enter();bool ok=true;auto p=RP::allocate(128);
  if(!p){RP::leave();return false;}std::memset(p,0x31,128);
  RP::rejectNewBacking(true);
  auto q=RP::resize(p,64*1024*1024);ok&=q==nullptr;
  if(q){RP::release(q);p=nullptr;}else ok&=bytes(p,128,0x31);
  auto large=RP::allocate(64*1024*1024);ok&=large==nullptr;if(large)RP::release(large);
  RP::rejectNewBacking(false);RP::release(p);
  std::array<void*,40> blocks{};unsigned allocated=0;bool exhausted=false;
  for(;allocated<blocks.size();++allocated){
    blocks[allocated]=RP::allocate(4*1024*1024);
    if(!blocks[allocated]){exhausted=true;break;}
    auto b=static_cast<unsigned char*>(blocks[allocated]);b[0]=0x52;b[4*1024*1024-1]=0x71;
  }
  ok&=exhausted;
  for(auto b:blocks)if(b){auto c=static_cast<unsigned char*>(b);ok&=c[0]==0x52&&c[4*1024*1024-1]==0x71;RP::release(b);}
  auto recovered=RP::allocate(2*1024*1024);ok&=recovered!=nullptr;if(recovered){std::memset(recovered,0x35,2*1024*1024);ok&=bytes(recovered,2*1024*1024,0x35);RP::release(recovered);}
  auto s=RP::backing();ok&=s.errors==0&&s.peak<=128*1024*1024;
  std::fprintf(output,"[failure-tests] forced-backing-failure/realloc-preservation/cap-exhaustion/recovery=%s live-4MiB-blocks-at-cap=%u\n",ok?"verified":"FAILED",allocated);std::fflush(output);backing("after-failure-tests");RP::leave();return ok;
}
}
int main(int argc,char** argv){
#ifdef __SWITCH2__
  (void)argc;(void)argv;
#ifdef STAR_SWITCH2_COMPARE_MIMALLOC
  char const* path="sdmc:/xstarbound-allocator-compare-mimalloc.log";
  char const* before="sdk-mimalloc-before";char const* after="sdk-mimalloc-after";
#elif defined(STAR_SWITCH2_COMPARE_NEWLIB)
  char const* path="sdmc:/xstarbound-allocator-compare-newlib.log";
  char const* before="newlib-dlmalloc-before";char const* after="newlib-dlmalloc-after";
#else
  char const* path="sdmc:/xstarbound-allocator-compare.log";
  char const* before="sdk-libc-before";char const* after="sdk-libc-after";
#endif
#else
  char const* path=argc>1?argv[1]:"allocator-compare-host.log";char const* before="host-crt-before";char const* after="host-crt-after";
#endif
  output=std::fopen(path,"w");if(!output)return 1;char buffer[16384];std::setvbuf(output,buffer,_IOFBF,sizeof(buffer));
  emitLine("allocator-compare version=1 rpmalloc=1.4.5 e4393ff85585d91400bcbad2e7266c011075b673; isolated explicit APIs; overrides/preload OFF; assertions/argument-validation ON; 64KiB spans; 1MiB map batches; SDK-backed blocks; 128MiB charged backing cap; no VM syscalls or clock/priority/affinity changes");
#ifdef __SWITCH2__
  std::fprintf(output,"[allocator-build] sdk=%s cpu=%s baseline=%s\n",STAR_COMPARE_SDK,STAR_COMPARE_CPU,before);
#ifdef STAR_SWITCH2_COMPARE_MIMALLOC
  std::fprintf(output,"[allocator-runtime] mimalloc-version=%d\n",mi_version());
#endif
  std::fflush(output);
#endif
  Api sdk{{before,std::malloc,std::realloc,std::free,sdkEnter,Bench::noThreadHook},std::calloc,sdkAligned,sdkAlignedFree};
  bool ok=correctness(sdk)&&Bench::run(sdk.basic,emitLine,proceed);
  bool initialized=false;
  if(ok){initialized=RP::initialize();ok=initialized;}
  if(ok){
    Api rp{{"rpmalloc-1.4.5-sdk-backed",RP::allocate,RP::resize,RP::release,rpEnter,RP::leave},RP::zero,RP::aligned,RP::release};
    backing("initialized");ok=correctness(rp)&&failureTests();
    if(ok){backing("before-rpmalloc-benchmark");ok=Bench::run(rp.basic,emitLine,proceed);backing("after-rpmalloc-benchmark");}
  }
  if(initialized){RP::shutdown();backing("finalized");auto s=RP::backing();ok&=s.current==0&&s.active==0&&s.errors==0;}
  if(ok){sdk.basic.name=after;ok=Bench::run(sdk.basic,emitLine,proceed);}
  std::fprintf(output,"finished status=%d\n",ok?0:1);bool written=!std::ferror(output);if(std::fclose(output)!=0)written=false;return ok&&written?0:1;
}
