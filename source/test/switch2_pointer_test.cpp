#include "StarSwitch2Pointer.hpp"
#include <cassert>
#include <cstdio>
#include <type_traits>
namespace PointerTest { int starts=0, stops=0, updates=0, width=0, height=0; Result nextResult=0; PadPointer next{}; }
static_assert(!std::is_copy_constructible_v<Star::Switch2Pointer>);
int main() {
  PadState pad;
  { Star::Switch2Pointer pointer;
    pointer.update(pad,true); assert(PointerTest::updates==0 && !pointer.active());
    assert(pointer.initialize()==0 && PointerTest::starts==1);
    assert(PointerTest::width==1280 && PointerTest::height==720);
    assert(pointer.initialize()==0 && PointerTest::starts==1);
    PointerTest::next={true,10,20};
    pointer.update(pad,true); assert(!pointer.active() && pointer.deltaX()==0);
    pointer.update(pad,true); assert(pointer.active() && pointer.deltaX()==10 && pointer.deltaY()==20);
    pointer.update(pad,false); assert(!pointer.active() && pointer.deltaX()==0);
    pointer.update(pad,true); assert(!pointer.active());
    pointer.update(pad,true); assert(pointer.active());
    pointer.suspend(); pointer.update(pad,true); assert(!pointer.active());
    pointer.update(pad,true); assert(pointer.active());
    PointerTest::next={false,123,456}; pointer.update(pad,true); assert(!pointer.active() && pointer.deltaX()==0);
    pointer.close(); assert(PointerTest::stops==1);
  }
  assert(PointerTest::stops==1);
  PointerTest::nextResult=0x1234;
  { Star::Switch2Pointer pointer; assert(pointer.initialize()==0x1234); pointer.update(pad,true); assert(!pointer.active()); }
  assert(PointerTest::stops==1);
  PointerTest::nextResult=0;
  try { Star::Switch2Pointer pointer; pointer.initialize(); throw 1; } catch(int) {}
  assert(PointerTest::stops==2);
  std::puts("Joy-Con pointer lifecycle tests passed: init/failure, focus/keyboard suppression, disconnect and cleanup.");
}
