// Linux-only, asset-free baseline. See scripts/switch2/build-linux-benchmark.sh.
#include "StarPerformanceKernels.hpp"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace K=Star::PerformanceKernels;
using Timer=std::chrono::steady_clock;
constexpr size_t MiB=1024*1024;
volatile uint64_t sink=0;
FILE* report=nullptr;
void log(char const* format,...) {
  va_list args; va_start(args,format);va_list copy;va_copy(copy,args);
  std::vprintf(format,args);std::putchar('\n');std::fflush(stdout);
  if(report){std::vfprintf(report,format,copy);std::fputc('\n',report);std::fflush(report);}
  va_end(copy);va_end(args);
}
double ms(Timer::time_point begin){return std::chrono::duration<double,std::milli>(Timer::now()-begin).count();}
double threadMs(){timespec value{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value))return -1;return value.tv_sec*1000.0+value.tv_nsec/1e6;}
std::string readLine(std::string const& path){std::ifstream stream(path);std::string line;std::getline(stream,line);return line;}
void environment(char const* when) {
  utsname info{};if(uname(&info)==0)log("[environment] phase=%s system=%s release=%s machine=%s",when,info.sysname,info.release,info.machine);
  for(unsigned i=0;i<16;++i){
    std::string path="/sys/devices/system/cpu/cpufreq/policy"+std::to_string(i)+"/";
    auto current=readLine(path+"scaling_cur_freq");if(current.empty())continue;
    log("[cpufreq] phase=%s policy=%u current-kHz=%s min-kHz=%s max-kHz=%s governor=%s",when,i,current.c_str(),readLine(path+"scaling_min_freq").c_str(),readLine(path+"scaling_max_freq").c_str(),readLine(path+"scaling_governor").c_str());
  }
  for(unsigned i=0;i<16;++i){
    std::string path="/sys/class/thermal/thermal_zone"+std::to_string(i)+"/";
    auto value=readLine(path+"temp");if(!value.empty())log("[thermal] phase=%s zone=%u type=%s raw-temp=%s",when,i,readLine(path+"type").c_str(),value.c_str());
  }
}
template<class State,class Kernel>void cpu(char const* name,State initial,Kernel kernel){
  std::vector<double> rates;sink=K::checksum(kernel(initial,4096));
  for(unsigned i=0;i<3;++i){
    double before=threadMs();auto trial=K::measure(initial,kernel);double after=threadMs();sink=trial.checksum;
    double rate=trial.iterations/(trial.milliseconds*1000);rates.push_back(rate);
    log("[cpu] kernel=%s trial=%u iterations=%llu wall-ms=%.3f Miter/s=%.3f main-running-ms=%.3f checksum=0x%016llx",name,i+1,(unsigned long long)trial.iterations,trial.milliseconds,rate,before>=0&&after>=before?after-before:-1,(unsigned long long)trial.checksum);
  }
  log("[cpu-summary] kernel=%s median-Miter/s=%.3f min=%.3f max=%.3f",name,K::median(rates),*std::min_element(rates.begin(),rates.end()),*std::max_element(rates.begin(),rates.end()));
}
// Keep libc copies observable and outside compiler replacement/elimination.
using Copy=void*(*)(void*,void const*,size_t);
Copy volatile copyBytes=std::memcpy;
void memory(){
  constexpr size_t Size=32*MiB;
  std::vector<unsigned char> source(Size,0x5a),destination(Size,0);
  std::vector<double> rates;
  for(unsigned trial=0;trial<3;++trial){
    uint64_t count=0;auto start=Timer::now();
    do {copyBytes(destination.data(),source.data(),Size);++count;}while(ms(start)<200&&count<16384);
    auto elapsed=ms(start);bool valid=std::memcmp(source.data(),destination.data(),Size)==0;
    if(!valid)throw std::runtime_error("memory copy verification failed");
    double rate=count*double(Size)/(elapsed/1000)/(1024*MiB);rates.push_back(rate);
    log("[memory-copy] version=memcopy-v1 trial=%u size-MiB=32 copies=%llu milliseconds=%.3f payload-GiB/s=%.3f verified=true",trial+1,(unsigned long long)count,elapsed,rate);
  }
  log("[memory-copy-summary] median-payload-GiB/s=%.3f (not total bus traffic)",K::median(rates));
  // One random permutation cycle prevents independent loads/prefetch hiding latency.
  for(size_t bytes:std::array<size_t,3>{256*1024,8*MiB,32*MiB}){
    size_t count=bytes/sizeof(uint32_t);std::vector<uint32_t> order(count),next(count);std::iota(order.begin(),order.end(),0);
    uint64_t rng=K::Seed;
    for(size_t i=count-1;i>0;--i){rng=K::integer(rng,1);std::swap(order[i],order[rng%(i+1)]);}
    for(size_t i=0;i<count;++i)next[order[i]]=order[(i+1)%count];
    order.clear();order.shrink_to_fit();
    volatile uint32_t const* pointer=next.data();uint32_t index=0;
    for(size_t i=0;i<count;++i)index=pointer[index];
    uint64_t steps=0;auto start=Timer::now();
    do {for(unsigned i=0;i<4096;++i)index=pointer[index];steps+=4096;}while(ms(start)<150&&steps<268435456);
    double elapsed=ms(start);sink=index;
    log("[pointer-chase] version=dependent-load-v1 working-KiB=%zu steps=%llu milliseconds=%.3f ns-per-load=%.3f",bytes/1024,(unsigned long long)steps,elapsed,elapsed*1e6/steps);
  }
}
void allocation(){
  constexpr size_t Count=1024;
  std::vector<void*> blocks(Count);std::vector<double> rates;
  for(unsigned trial=0;trial<3;++trial){
    uint64_t pairs=0;auto start=Timer::now();
    do {
      for(size_t i=0;i<Count;++i){
        size_t size=(i%3==0?32:i%3==1?128:512);blocks[i]=std::malloc(size);
        if(!blocks[i]){for(size_t j=0;j<i;++j)std::free(blocks[j]);throw std::bad_alloc();}
        // Volatile writes keep real allocations/accesses observable under optimization.
        auto* pointer=static_cast<volatile unsigned char*>(blocks[i]);pointer[0]=0x5a;pointer[size-1]=0xa5;
      }
      for(auto block:blocks)std::free(block);
      pairs+=Count;
    }while(ms(start)<200&&pairs<268435456);
    double elapsed=ms(start),rate=pairs/(elapsed*1000);rates.push_back(rate);
    log("[allocation] version=malloc-batch-v1 trial=%u batch=1024 sizes=32/128/512 malloc-free-pairs=%llu milliseconds=%.3f million-pairs/s=%.3f",trial+1,(unsigned long long)pairs,elapsed,rate);
  }
  log("[allocation-summary] median-million-pairs/s=%.3f (libc allocator, single thread)",K::median(rates));
}
struct Scratch{
  int fd=-1;std::string path;
  ~Scratch(){if(fd>=0)::close(fd);if(!path.empty()&&::unlink(path.c_str()))log("[sd] cleanup failed path=%s errno=%d",path.c_str(),errno);}
};
bool transfer(int fd,char* data,size_t size,bool writing){
  size_t done=0;while(done<size){ssize_t count=writing ? ::write(fd,data+done,size-done) : ::read(fd,data+done,size-done);if(count<0&&errno==EINTR)continue;if(count<=0)return false;done+=size_t(count);}return true;
}
void disk(std::string const& directory){
  constexpr size_t Size=8*MiB,Chunk=64*1024,Block=4096;
  struct statvfs space{};
  if(::statvfs(directory.c_str(),&space)||double(space.f_bavail)*space.f_frsize<32*MiB){log("[sd] skipped: free space unavailable or below 32MiB");return;}
  Scratch scratch;std::string path=directory+"/xsb-bench-scratch-XXXXXX";std::vector<char> name(path.begin(),path.end());name.push_back(0);
  scratch.fd=::mkstemp(name.data());if(scratch.fd<0)throw std::runtime_error("cannot create unique SD scratch file");scratch.path=name.data();
  log("[sd] version=sd-posix-v1 scratch=%s maximum-file=8MiB buffers=16MiB",scratch.path.c_str());
  std::vector<char> pattern(Size),readback(Size);for(size_t i=0;i<Size;++i)pattern[i]=char((i*131u)^(i>>8)^(i>>16));
  auto deadline=Timer::now()+std::chrono::seconds(20);
  auto check=[&](){if(Timer::now()>=deadline)throw std::runtime_error("SD budget exceeded between operations");};
  auto start=Timer::now();
  for(size_t offset=0;offset<Size;offset+=Chunk){check();if(!transfer(scratch.fd,pattern.data()+offset,Chunk,true))throw std::runtime_error("SD write failed");}
  double writeMs=ms(start);start=Timer::now();int synced=::fsync(scratch.fd),syncErr=synced?errno:0;double syncMs=ms(start);
  log("[sd] sequential-write bytes=%zu chunk=%zu write-ms=%.3f write-MiB/s=%.3f final-fsync-ms=%.3f fsync-result=%d errno=%d",Size,Chunk,writeMs,8000/writeMs,syncMs,synced,syncErr);
  if(!synced)log("[sd] write-including-final-sync-MiB/s=%.3f",8000/(writeMs+syncMs));
  int fd=scratch.fd;scratch.fd=-1;if(::close(fd))throw std::runtime_error("SD close failed");
  scratch.fd=::open(scratch.path.c_str(),O_RDONLY);if(scratch.fd<0)throw std::runtime_error("SD reopen failed");
  for(unsigned pass=0;pass<2;++pass){
    if(::lseek(scratch.fd,0,SEEK_SET)!=0)throw std::runtime_error("SD seek failed");
    start=Timer::now();
    for(size_t offset=0;offset<Size;offset+=Chunk){check();if(!transfer(scratch.fd,readback.data()+offset,Chunk,false))throw std::runtime_error("SD sequential read failed");}
    double elapsed=ms(start);bool verified=std::memcmp(pattern.data(),readback.data(),Size)==0;
    log("[sd] sequential-read pass=%u bytes=%zu milliseconds=%.3f MiB/s=%.3f verified=%s",pass+1,Size,elapsed,8000/elapsed,verified?"true":"false");
    if(!verified)throw std::runtime_error("SD sequential verification failed");
  }
  uint64_t rng=K::Seed;std::vector<double> times;char buffer[Block];
  for(unsigned i=0;i<256;++i){
    check();rng=K::integer(rng,1);size_t offset=(rng%(Size/Block))*Block;start=Timer::now();
    if(::lseek(scratch.fd,off_t(offset),SEEK_SET)!=off_t(offset)||!transfer(scratch.fd,buffer,Block,false))throw std::runtime_error("SD random read failed");
    times.push_back(ms(start));if(std::memcmp(buffer,pattern.data()+offset,Block))throw std::runtime_error("SD random verification failed");
  }
  std::sort(times.begin(),times.end());double total=std::accumulate(times.begin(),times.end(),0.0);
  log("[sd] random-read block=4096 operations=256 median-ms=%.4f p95-ms=%.4f max-ms=%.4f IOPS=%.1f all-verified=true",K::median(times),times[243],times.back(),256000/total);
  log("[sd] Reads may be cached; no cache eviction/raw-device access. Linux write() is buffered; use including-sync write result.");
}
int main(int argc,char** argv){
  std::string directory=".";bool skipSd=false;
  for(int i=1;i<argc;++i){
    if(std::strcmp(argv[i],"--no-sd")==0)skipSd=true;
    else if(std::strcmp(argv[i],"--directory")==0&&i+1<argc)directory=argv[++i];
    else{std::printf("Usage: %s [--directory EXISTING_DIRECTORY] [--no-sd]\n",argv[0]);return std::strcmp(argv[i],"--help")==0?0:2;}
  }
  std::string path=directory+"/xsb-benchmark-results-XXXXXX.log";std::vector<char> name(path.begin(),path.end());name.push_back(0);
  int logFd=::mkstemps(name.data(),4);if(logFd<0){std::fprintf(stderr,"Cannot create unique report in %s: %s\n",directory.c_str(),std::strerror(errno));return 1;}
  report=::fdopen(logFd,"w");if(!report){::close(logFd);::unlink(name.data());return 1;}
  int result=0;
  try{
    log("[suite] switch-linux-baseline-v1 report=%s compiler=%s CPU-version=%s",name.data(),__VERSION__,K::Version);
    log("[suite] No clocks/priorities/affinity changed; no game assets/saves used; single-thread tests");
    environment("before");
    if(K::integer(K::Seed,1)!=0xb7fb0288c5ee4339ULL||K::integer(K::Seed,4096)!=0xd7e50bbb28fdbe5cULL)throw std::runtime_error("CPU known-result check failed");
    log("[checks] fixed integer checksums passed");
    cpu("uint64-dependent-mix",K::Seed,K::integer);cpu("binary64-four-chains",K::Floating{},K::floating);
    memory();allocation();if(!skipSd)disk(directory);else log("[sd] skipped by --no-sd");
    environment("after");log("[suite] complete status=0");
  }catch(std::exception const& error){log("[suite] FAILED: %s errno=%d (%s)",error.what(),errno,std::strerror(errno));result=1;}
  std::fclose(report);report=nullptr;std::printf("Saved results: %s\n",name.data());return result;
}
