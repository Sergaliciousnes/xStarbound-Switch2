#include "StarSwitch2ExplorationProfile.hpp"
#include <atomic>
#include <thread>
#include <fstream>
#include <string>
#include <cassert>
#include <cerrno>
#include <cstdio>
using namespace Star::Switch2ExplorationProfile;
static std::atomic<int64_t> clockUs{1000000};
namespace Star::Switch2ExplorationProfile {int64_t testNow(){return clockUs.load();}}
int main(int argc,char**argv){
  assert(argc==2);char const* path=argv[1];std::remove(path);
  {Scope disabled(Kind::Packet,"before-start");clockUs+=10000;}
  start();beginFrame(42,3,false);
  errno=EDOM;
  char label[]="/monsters/example.png";
  {STAR_EXPLORATION_SCOPE(packet,Kind::Packet,"EntityCreate");
    {Scope load(Kind::EntityLoad,label,123);clockUs+=3000;load.stop();load.stop();}
    {Scope hash(Kind::TileHash,"unsampled",0,true);clockUs+=3000;}
    clockUs+=4000;
  }
  assert(errno==EDOM);label[0]='!';STAR_EXPLORATION_COUNT(Counter::EntityCreates,2);
  std::thread worker([]{Scope skipped(Kind::ImageLoad,"background");count(Counter::AssetMisses,99);});worker.join();
  for(int n=0;n<50;++n){Scope event(Kind::StatsEvent,"short-event");clockUs+=100;}
  {Scope event(Kind::StatsEvent,"achievement-trigger");Scope check(Kind::StatsAchievement,"achievement-name");clockUs+=3500;}
  for(int n=0;n<30;++n){Scope library(Kind::VmLibrary,"crypto");clockUs+=100;}
  {Scope widget(Kind::UiWidgetBuild,"image");clockUs+=2500;}
  {Scope npc(Kind::GatherNpcRender,"npc",1,true);clockUs+=5000;} // Unsampled: excluded.
  STAR_EXPLORATION_COUNT(Counter::GatherNpcs,3);
  endFrame(3,60000);
  for(unsigned n=0;n<600;++n){beginFrame(100+n,3,true);{Scope load(Kind::ImageLoad,"capacity",n);clockUs+=2000+n;}char name[48];std::snprintf(name,sizeof(name),"achievement-%u",n);{Scope named(Kind::StatsAchievement,name);clockUs+=10;}endFrame(3,51000+n);}
  // Leave a final labelled record that must survive bounded worst-event eviction.
  beginFrame(999,3,true);{Scope wait(Kind::ArchiveWait,"last",8);clockUs+=90000;}endFrame(3,90000);
  std::string bad=std::string(path)+".parent";{std::ofstream f(bad);f<<"file";}
  assert(!dump((bad+"/log").c_str()));assert(dump(path));std::remove(bad.c_str());
  std::ifstream f(path);std::string text((std::istreambuf_iterator<char>(f)),{});
  assert(text.find("before-start")==std::string::npos&&text.find("background")==std::string::npos);
  assert(text.find("kind=tile-hash")==std::string::npos);
  assert(text.find("kind=entity-load calls=1 total-ms=3.000")!=std::string::npos);
  assert(text.find("kind=packet calls=1 total-ms=10.000")!=std::string::npos);
  assert(text.find("/monsters/example.png")!=std::string::npos&&text.find("!monsters")==std::string::npos);
  assert(text.find("kind=entity-creates total=2")!=std::string::npos);
  assert(text.find("frame=999 scene=3 kind=archive-wait elapsed-ms=90.000 detail=8 label=last")!=std::string::npos);
  assert(text.find("[stats-name] kind=stats-event calls=50 total-ms=5.000 max-ms=0.100 label=short-event")!=std::string::npos);
  assert(text.find("[stats-name] kind=stats-achievement calls=1 total-ms=3.500 max-ms=3.500 label=achievement-name")!=std::string::npos);
  assert(text.find("dropped-named-samples=93")!=std::string::npos);
  size_t names=0;for(size_t p=0;(p=text.find("[stats-name]",p))!=std::string::npos;++p)++names;assert(names==512);
  assert(text.find("[stats-name] kind=vm-library calls=30 total-ms=3.000 max-ms=0.100 label=crypto")!=std::string::npos);
  assert(text.find("[stats-name] kind=ui-widget-build calls=1 total-ms=2.500 max-ms=2.500 label=image")!=std::string::npos);
  assert(text.find("kind=gather-npc-render")==std::string::npos);
  assert(text.find("kind=gather-npcs total=3")!=std::string::npos);
  size_t slow=0,frames=0;for(size_t p=0;(p=text.find("[slow]",p))!=std::string::npos;++p)++slow;
  for(size_t p=0;(p=text.find("[frame]",p))!=std::string::npos;++p)++frames;
  assert(slow==512&&frames==128);
  std::puts("Exploration profile passed: exact phase/counter accounting, sampled exclusion, background/disabled exclusion, errno, label lifetime, bounded worst events/frames/named totals, short-event accumulation, and failed-write retry.");
}
