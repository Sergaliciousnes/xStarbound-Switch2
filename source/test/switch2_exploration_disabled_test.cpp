#include "StarSwitch2ExplorationProfile.hpp"
#include <cassert>
#include <cstdio>
int calls=0;
char const* label(){++calls;return "must not evaluate";}
uint64_t work(){++calls;return 999;}
int main(){
  STAR_EXPLORATION_SCOPE(scope,Star::Switch2ExplorationProfile::Kind::Packet,label(),work());
  scope.stop();
  STAR_EXPLORATION_COUNT(Star::Switch2ExplorationProfile::Counter::TileCells,work());
  assert(calls==0);
  std::puts("Disabled exploration probes: no label/work argument evaluation.");
}
