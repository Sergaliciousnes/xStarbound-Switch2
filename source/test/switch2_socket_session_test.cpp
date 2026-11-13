#include "StarSwitch2SocketSession.hpp"
#include <cassert>
#include <cstdio>
#include <type_traits>
namespace SocketTest { int starts=0, stops=0; Result nextResult=0; }
static_assert(!std::is_copy_constructible_v<Star::Switch2SocketSession>);
static_assert(!std::is_copy_assignable_v<Star::Switch2SocketSession>);
int main() {
  { Star::Switch2SocketSession session; assert(session.result()==0); assert(SocketTest::starts==1 && SocketTest::stops==0); }
  assert(SocketTest::stops==1);
  SocketTest::nextResult=0x1234;
  { Star::Switch2SocketSession session; assert(session.result()==0x1234); }
  assert(SocketTest::starts==2 && SocketTest::stops==1);
  SocketTest::nextResult=0;
  try { Star::Switch2SocketSession session; throw 1; } catch(int) {}
  assert(SocketTest::starts==3 && SocketTest::stops==2);
  std::puts("Socket session tests passed: initialization, balanced cleanup, failure and exception unwinding.");
}
