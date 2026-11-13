#include "../core/StarSwitch2SimulationProfile.hpp"
#include <cassert>
#include <cstdio>
int main(){int touched=0;STAR_SIM_TICK(t,(++touched,"world"),++touched);STAR_SIM_SCOPE(s,Star::Switch2SimulationProfile::Kind::NpcUpdate,(++touched,"label"));STAR_SIM_ADVANCE(++touched);STAR_SIM_ENTITY(++touched);STAR_SIM_FIDELITY(++touched);assert(touched==0);std::puts("Disabled simulation probes evaluate no label/work arguments.");}
