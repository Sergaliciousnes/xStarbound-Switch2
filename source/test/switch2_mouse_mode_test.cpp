#include "StarSwitch2MouseMode.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using Mode=Star::Switch2MouseMode;
void arm(Mode& mode) {
  for(int i=0;i<50;++i) assert(!mode.update(true,true,true,0,0.02f));
  assert(mode.update(true,true,true,0,0.02f));
}
int main() {
  Mode mode;
  for(int n=0;n<10;++n) {
    for(int i=0;i<5;++i) assert(!mode.update(true,true,true,0,0.02f));
    assert(!mode.update(true,true,false,0,0.2f));
  }
  for(int n=0;n<100;++n) {
    assert(!mode.update(true,true,true,0,0.05f));
    assert(!mode.update(true,true,false,0,0.05f));
    assert(!mode.update(true,true,false,0,0.05f));
  }
  mode=Mode{};arm(mode);
  for(int i=0;i<100;++i) assert(mode.update(true,true,false,0.2f,0.02f));
  assert(mode.update(true,false,false,1,60));
  mode.pause();assert(mode.update(true,true,false,0,0.02f));
  for(int i=0;i<15;++i) assert(mode.update(true,true,false,0.8f,0.02f));
  assert(mode.update(true,true,false,0,0.02f));
  for(int i=0;i<19;++i) assert(mode.update(true,true,true,0.8f,0.02f));
  assert(!mode.update(true,true,true,0.8f,0.02f));
  for(int i=0;i<100;++i) assert(!mode.update(true,true,true,0.8f,0.02f));
  arm(mode);
  for(int i=0;i<29;++i) assert(mode.update(true,true,false,0,1));
  assert(!mode.update(true,true,false,0,1));
  assert(!mode.update(true,true,true,0,5));
  assert(!mode.update(true,true,true,0,std::numeric_limits<float>::quiet_NaN()));
  arm(mode);assert(!mode.update(false,true,true,0,0.02f));
  mode=Mode{};
  for(int i=0;i<25;++i) assert(!mode.update(true,true,true,0,0.02f));
  assert(!mode.update(true,true,false,0,0.05f));assert(!mode.update(true,true,false,0,0.05f));
  bool active=false;for(int i=0;i<30;++i) active=mode.update(true,true,true,0,0.02f);
  assert(active);
  std::puts("Mouse mode tests passed: brief/sparse brushes, arming, gaps, handoff, idle, pause, disconnect and stalls.");
}
