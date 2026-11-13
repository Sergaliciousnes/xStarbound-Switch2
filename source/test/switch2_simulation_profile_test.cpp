#include "../core/StarSwitch2SimulationProfile.hpp"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <array>
using namespace Star::Switch2SimulationProfile;
std::atomic<int64_t> clockUs{100};
namespace Star::Switch2SimulationProfile {int64_t testNow(){errno=EIO;return clockUs.load();}}
void add(int64_t n){clockUs+=n;}
std::string read(char const* p){std::ifstream f(p);return std::string(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){
 assert(argc==2);errno=EDOM;start();assert(errno==EDOM);
 {Scope excluded(Kind::NpcScript,"outside");add(500);}
 for(int i=0;i<8;++i){Tick tick("test-world");
   {Scope work(Kind::Work,"",false);
     entity(0);entity(1);entity(2);fidelity(2);advance(0.01);
     {Scope npc(Kind::NpcUpdate,"guard");{Scope script(Kind::NpcScript);add(2000);}{Scope path(Kind::PathExplore);add(1000);}}
   }
   {Scope sleep(Kind::Sleep,"",false);add(17000);}
 }
 clockUs=5000100;
 // First tick in each eight is detailed. Named labels are copied at scope end.
 {Tick tick("test-world");char label[]="mutable";{Scope scope(Kind::NpcUpdate,label);add(100);}label[0]='!';}
 for(int i=0;i<7;++i){Tick tick("test-world");add(100);}
 for(int group=0;group<3;++group){
   {Tick tick("test-world");for(unsigned n=0;n<100;++n){char label[64];std::snprintf(label,sizeof(label),"type-%d-%u",group,n);Scope scope(Kind::NpcUpdate,label);add(10);}}
   for(int i=0;i<7;++i){Tick tick("test-world");add(100);}
 }
 std::array<std::thread,4> workers;
 for(auto& thread:workers)thread=std::thread([]{for(int i=0;i<16;++i){Tick tick("parallel-world");Scope work(Kind::Work,"",false);advance(0.01);entity(0);add(100);}});
 for(auto& t:workers)t.join();
 // Dump must not wait for a live tick or publish partially recorded TLS data.
 std::atomic<bool> ready{false},release{false};
 std::thread pending([&]{Tick tick("pending-world");ready=true;while(!release.load())std::this_thread::yield();});
 while(!ready.load())std::this_thread::yield();
 for(int i=0;i<20;++i){std::thread t([]{Tick tick("bounded-world");add(100);});t.join();}
 add(3600000000LL);{Tick overflow("test-world");add(200);}
 assert(!dump("?:/missing/simulation.log"));assert(dump(argv[1]));release=true;pending.join();
 auto text=read(argv[1]);assert(text.find("inflight-omitted=1")!=std::string::npos);assert(text.find("observed-hz=50.000 simulated-rate=0.5000")!=std::string::npos);assert(text.find("advanced-ticks=8")!=std::string::npos);assert(text.find("fidelity-medium=8")!=std::string::npos);assert(text.find("outside")==std::string::npos);
 assert(text.find("kind=npc-script calls=1 total-ms=2.000")!=std::string::npos);
 assert(text.find("kind=path-explore calls=1 total-ms=1.000")!=std::string::npos);
 assert(text.find("kind=work calls=8 total-ms=24.000")!=std::string::npos);
 assert(text.find("kind=sleep calls=8 total-ms=136.000")!=std::string::npos);
 assert(text.find("npc-updates=8 object-updates=8 other-updates=8")!=std::string::npos);
 assert(text.find("label=mutable")!=std::string::npos&&text.find("label=!utable")==std::string::npos);
 assert(text.find("overflow-ticks=1")!=std::string::npos);
 assert(text.find("dropped-worlds=0")==std::string::npos);
 assert(text.find("dropped-named-samples=0 label=test-world")==std::string::npos);
 // No background caller may dump another thread's capture.
 std::thread foreign([&]{assert(!dump(argv[1]));assert(errno==EINVAL);});foreign.join();
 std::puts("Simulation recorder passed: sampled/coarse accounting, actor labels, paused ticks, concurrent publishers, bounds, freeze and write retry.");
}
