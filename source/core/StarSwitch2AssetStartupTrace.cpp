#include "StarSwitch2AssetStartupTrace.hpp"
#include "../application/StarSwitch2RpmallocCandidate.hpp"
#include <switch.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <cerrno>
#include <algorithm>
namespace Star::Switch2AssetStartupTrace {
// Cold startup boundaries only. Use a separate, synced file: normal logger
// buffers and deferred exit profiles cannot diagnose a startup hard freeze.
void checkpoint(char const* phase,char const* path,uint64_t progress,uint64_t total) noexcept {
  try {
    static std::mutex mutex;
    static bool first=true;
    std::lock_guard<std::mutex> guard(mutex);
    int fd=::open("sdmc:/xstarbound-asset-startup.log",O_WRONLY|O_CREAT|(first?O_TRUNC:O_APPEND),0666);
    if(fd<0)return;
    first=false;
    char line[1200];
    uint64_t charged=0,peak=0,maps=0,active=0,rejected=0,errors=0;
#ifdef STAR_SWITCH2_GAME_RPMALLOC
    auto s=RpmallocCandidate::backing();charged=s.current;peak=s.peak;maps=s.maps;active=s.active;rejected=s.rejected;errors=s.errors;
#endif
    auto n=std::snprintf(line,sizeof(line),"ticks=%llu phase=%s path=%.500s trace-fd=%d progress=%llu total=%llu charged=%llu peak=%llu maps=%llu active=%llu rejected=%llu ownership-errors=%llu\n",
      (unsigned long long)armGetSystemTick(),phase,path,fd,(unsigned long long)progress,(unsigned long long)total,
      (unsigned long long)charged,(unsigned long long)peak,(unsigned long long)maps,(unsigned long long)active,(unsigned long long)rejected,(unsigned long long)errors);
    size_t remaining=n>0?std::min(size_t(n),sizeof(line)-1):0,offset=0;
    while(remaining){auto r=::write(fd,line+offset,remaining);if(r<0&&errno==EINTR)continue;if(r<=0)break;offset+=r;remaining-=r;}
    ::fsync(fd);::close(fd);
  } catch (...) {} // Diagnostic failure must not replace the original failure.
}
}
