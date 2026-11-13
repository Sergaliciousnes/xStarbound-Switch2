#include "StarAllocatorContentionBenchmark.hpp"
#ifdef TEST_RPMALLOC
#include "rpmalloc.h"
#endif
int main(){
  using namespace Star::AllocatorContentionBenchmark;
  auto log=[](char const* text){std::puts(text);};auto proceed=[](){return true;};
  if(!run(sdk(),log,proceed))return 1;
#ifdef TEST_RPMALLOC
  if(rpmalloc_initialize(nullptr)!=0)return 2;
  Backend rp{"bundled-rpmalloc",rpmalloc,rprealloc,rpfree,rpmalloc_thread_initialize,rpmalloc_thread_finalize};
  bool ok=run(rp,log,proceed);
  rpmalloc_finalize();
  if(!ok)return 3;
#endif
  return 0;
}
