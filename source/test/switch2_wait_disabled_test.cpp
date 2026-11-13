#include "../core/StarSwitch2WaitProfile.hpp"
#include "../core/StarSwitch2RandomWait.hpp"
#include "../core/StarSwitch2HeapWait.hpp"
#include <cassert>
#include <cstdio>
namespace P=Star::Switch2WaitProfile;
struct Locker {int locks=0,tries=0;void lock(){++locks;}bool tryLock(){++tries;return false;}};
int main(){assert(P::beginRandomWait()==0);P::endRandomWait(0,false);assert(P::beginHeapWait()==0);P::endHeapWait(0);P::start();P::beginFrame();{P::Frame frame;P::Scope scope(P::Metric::WorldUpdate);P::Background background("disabled");scope.stop();background.stop();}P::scene(P::Scene::Planet);P::count(P::Counter::EntitiesUpdated,99);P::endFrame();P::finishFrames();assert(!P::detailed());Locker locker;P::acquire(locker,P::Metric::AssetContended);assert(locker.locks==1&&locker.tries==0);std::puts("Timing-disabled scopes require no recorder linkage and preserve ordinary locking.");}
