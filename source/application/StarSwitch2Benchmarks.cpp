#include "StarSwitch2Benchmarks.hpp"
#include "StarPerformanceKernels.hpp"
#include "StarPerformanceMemory.hpp"
#include "../test/StarAllocatorContentionBenchmark.hpp"
#include <cstdarg>
#include "StarLogging.hpp"
#include <switch.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Star {
namespace {
using BenchmarkClock=std::chrono::steady_clock;
double elapsed(BenchmarkClock::time_point start) { return std::chrono::duration<double,std::milli>(BenchmarkClock::now()-start).count(); }
volatile uint64_t consumed=0;
int64_t cpuTimeUs() {
  u64 ticks=0;
  return R_SUCCEEDED(svcGetInfo(&ticks,InfoType_ThreadTickCount,threadGetCurHandle(),TickCountInfo_Total))?int64_t(armTicksToNs(ticks)/1000):-1;
}
template <typename State, typename Kernel>
bool cpuSeries(char const* name,State initial,Kernel kernel) {
  std::vector<double> rates;
  consumed=PerformanceKernels::checksum(kernel(initial,4096)); // untimed warm-up
  for(unsigned i=0;i<3;++i) {
    if(!appletMainLoop())return false;
    auto cpuStart=cpuTimeUs();
    auto result=PerformanceKernels::measure(initial,kernel);
    auto cpuEnd=cpuTimeUs();
    consumed=result.checksum;
    double rate=result.milliseconds>0?result.iterations/(result.milliseconds*1000):0;
    rates.push_back(rate);
    Logger::info("[cpu] kernel={} trial={} iterations={} wall-ms={:.3f} Miter/s={:.3f} main-running-ms={:.3f} checksum=0x{:016x}",name,i+1,result.iterations,result.milliseconds,rate,cpuStart>=0&&cpuEnd>=cpuStart?(cpuEnd-cpuStart)/1000.0:-1,result.checksum);
  }
  Logger::info("[cpu-summary] kernel={} median-Miter/s={:.3f} min={:.3f} max={:.3f}",name,PerformanceKernels::median(rates),*std::min_element(rates.begin(),rates.end()),*std::max_element(rates.begin(),rates.end()));
  return true;
}
struct Scratch {
  int fd=-1; std::string path; bool owned=false;
  ~Scratch() {
    if(fd>=0)::close(fd);
    if(owned && ::unlink(path.c_str())!=0)
      Logger::warn("[sd] scratch cleanup failed: {} errno={} ({})",path,errno,std::strerror(errno));
  }
};
bool transfer(int fd,char* bytes,size_t size,bool writing) {
  size_t done=0;
  while(done<size) {
    ssize_t count=writing ? ::write(fd,bytes+done,size-done) : ::read(fd,bytes+done,size-done);
    if(count<0 && errno==EINTR)continue;
    if(count<=0)return false;
    done+=size_t(count);
  }
  return true;
}
}
bool runSwitch2CpuBenchmarks() {
  u64 mask=0; s32 priority=-1;
  svcGetInfo(&mask,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
  svcGetThreadPriority(&priority,threadGetCurHandle());
  Logger::info("[benchmark] cpu-version={} compiler={} single-thread priority={} allowed-process-mask=0x{:x} operation-mode={} performance-mode={}",PerformanceKernels::Version,__VERSION__,priority,mask,int(appletGetOperationMode()),int(appletGetPerformanceMode()));
  Logger::info("[cpu] Native AArch64 throughput, NOT measured MHz. 3 x 200ms per kernel; integer wraparound and binary64 arithmetic, FP contraction disabled.");
  return cpuSeries("uint64-dependent-mix",PerformanceKernels::Seed,PerformanceKernels::integer)
      &&cpuSeries("binary64-four-chains",PerformanceKernels::Floating{},PerformanceKernels::floating);
}
namespace {
void memoryLog(char const* format,...){char line[768];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);Logger::info("{}",line);}
bool memoryContinue(){return appletMainLoop();}
}
bool runSwitch2MemoryBenchmarks(){
  Logger::info("[memory] suite-v2: matching Switch 1 Linux memory/allocation workloads, peak buffers about 128MiB, no allocator replacement or clock changes");
  return PerformanceMemory::memory(memoryLog,memoryContinue)&&PerformanceMemory::allocation(memoryLog,memoryContinue)
      &&AllocatorContentionBenchmark::run(AllocatorContentionBenchmark::sdk(),[](char const* line){Logger::info("{}",line);},memoryContinue);
}
bool runSwitch2SdBenchmarks() {
  constexpr size_t MiB=1024*1024, Size=8*MiB, Chunk=64*1024, Block=4096;
  auto* fs=fsdevGetDeviceFileSystem("sdmc"); s64 available=0;
  Result spaceResult=fs?fsFsGetFreeSpace(fs,"/",&available):Result(-1);
  if(R_FAILED(spaceResult)||available<s64(32*MiB)) {
    Logger::warn("[sd] skipped: free-space query failed or less than 32 MiB available; result=0x{:x} free-bytes={}",spaceResult,available);return true;
  }
  char const* directory="sdmc:/switch/xstarbound/benchmarks";
  if(::mkdir(directory,0777)!=0 && errno!=EEXIST) { Logger::warn("[sd] skipped: cannot create scratch directory errno={}",errno);return true; }
  struct stat info{};
  if(::stat(directory,&info)!=0||!S_ISDIR(info.st_mode)) { Logger::warn("[sd] skipped: scratch directory is not a directory");return true; }
  Scratch scratch;
  for(unsigned attempt=0;attempt<16;++attempt) {
    char name[192];std::snprintf(name,sizeof(name),"%s/bench-%016llx-%u.tmp",directory,(unsigned long long)armGetSystemTick(),attempt);
    scratch.path=name;
    scratch.fd=::open(name,O_RDWR|O_CREAT|O_EXCL,0600);
    if(scratch.fd>=0) {scratch.owned=true;break;}
    if(errno!=EEXIST)break;
  }
  if(!scratch.owned) {Logger::warn("[sd] skipped: cannot exclusively create scratch file errno={}",errno);return true;}
  Logger::info("[sd] version=sd-posix-v1 scratch={} maximum-file=8MiB buffers=16MiB free-MiB={:.1f}",scratch.path,available/double(MiB));
  std::vector<char> pattern(Size),readback(Size);
  for(size_t i=0;i<Size;++i)pattern[i]=char((i*131u)^(i>>8)^(i>>16));
  auto deadline=BenchmarkClock::now()+std::chrono::seconds(20);
  unsigned budgetChecks=0;
  auto checkBudget=[&]() {return BenchmarkClock::now()<deadline&&((++budgetChecks%16)!=0||appletMainLoop());};
  auto start=BenchmarkClock::now();
  for(size_t offset=0;offset<Size;offset+=Chunk) {
    if(!checkBudget()) {Logger::warn("[sd] cancelled/time budget exceeded while writing");return false;}
    if(!transfer(scratch.fd,pattern.data()+offset,Chunk,true)) {Logger::error("[sd] write failed errno={} ({})",errno,std::strerror(errno));return false;}
  }
  auto writeMs=elapsed(start);start=BenchmarkClock::now();
  int syncResult=::fsync(scratch.fd), syncError=syncResult?errno:0;auto syncMs=elapsed(start);
  Logger::info("[sd] sequential-write bytes={} chunk={} write-ms={:.3f} write-MiB/s={:.3f} final-fsync-ms={:.3f} fsync-result={} errno={}",Size,Chunk,writeMs,8*1000.0/writeMs,syncMs,syncResult,syncError);
  if(syncResult==0)Logger::info("[sd] write-including-final-sync-MiB/s={:.3f}",8*1000.0/(writeMs+syncMs));
  int old=scratch.fd;scratch.fd=-1;
  if(::close(old)!=0) {Logger::error("[sd] close failed errno={}",errno);return false;}
  scratch.fd=::open(scratch.path.c_str(),O_RDONLY);
  if(scratch.fd<0) {Logger::error("[sd] reopen failed errno={}",errno);return false;}
  for(unsigned pass=0;pass<2;++pass) {
    if(::lseek(scratch.fd,0,SEEK_SET)!=0)return false;
    start=BenchmarkClock::now();
    for(size_t offset=0;offset<Size;offset+=Chunk) {
      if(!checkBudget()) {Logger::warn("[sd] cancelled/time budget exceeded while reading");return false;}
      if(!transfer(scratch.fd,readback.data()+offset,Chunk,false)) {Logger::error("[sd] read failed errno={}",errno);return false;}
    }
    auto ms=elapsed(start);
    bool verified=std::memcmp(pattern.data(),readback.data(),Size)==0;
    Logger::info("[sd] sequential-read pass={} bytes={} milliseconds={:.3f} MiB/s={:.3f} verified={}",pass+1,Size,ms,8*1000.0/ms,verified);
    if(!verified)return false;
  }
  uint64_t rng=PerformanceKernels::Seed;std::vector<double> latencies;
  std::array<char,Block> buffer{};
  for(unsigned i=0;i<256;++i) {
    if(!checkBudget()) {Logger::warn("[sd] cancelled/time budget exceeded during random reads");return false;}
    rng=PerformanceKernels::integer(rng,1);size_t offset=(rng%(Size/Block))*Block;
    start=BenchmarkClock::now();
    if(::lseek(scratch.fd,off_t(offset),SEEK_SET)!=off_t(offset)||!transfer(scratch.fd,buffer.data(),Block,false))return false;
    latencies.push_back(elapsed(start));
    if(std::memcmp(buffer.data(),pattern.data()+offset,Block)!=0)return false;
  }
  std::sort(latencies.begin(),latencies.end());double total=0;for(auto ms:latencies)total+=ms;
  Logger::info("[sd] random-read block=4096 operations=256 median-ms={:.4f} p95-ms={:.4f} max-ms={:.4f} IOPS={:.1f} all-verified=true",PerformanceKernels::median(latencies),latencies[243],latencies.back(),256*1000.0/total);
  Logger::info("[sd] Reads may be cached; no cache flush/raw-device access. Per-write platform flushes may already be included. Scratch is closed/unlinked on return.");
  return true;
}
}
