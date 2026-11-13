#include "../extern/lua/lua.h"
#include "../extern/lua/lauxlib.h"
#include "../extern/lua/lualib.h"
#include "../extern/lua/StarBootstrapBytecode.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <array>
#include <atomic>
#include <memory>
#include <utility>
void* alloc(void*,void* p,size_t,size_t n){if(!n){std::free(p);return nullptr;}return std::realloc(p,n);}
void run(lua_State* L,char const* code){if(luaL_loadbuffer(L,code,std::strlen(code),"test")!=LUA_OK||lua_pcall(L,0,0,0)!=LUA_OK){std::fprintf(stderr,"Lua test error: %s\n",lua_tostring(L,-1));std::abort();}}
lua_State* create(){static std::atomic<unsigned> seed{123};auto L=lua_newstate(alloc,nullptr,seed.fetch_add(1));assert(L);return L;}
void open(lua_State* L){
 for(auto lib:std::array<std::pair<char const*,lua_CFunction>,7>{{{"_G",luaopen_base},{"os",luaopen_os},{"coroutine",luaopen_coroutine},{"math",luaopen_math},{"string",luaopen_string},{"table",luaopen_table},{"utf8",luaopen_utf8}}}){luaL_requiref(L,lib.first,lib.second,1);lua_pop(L,1);}
 luaL_requiref(L,"pluto_assert",luaopen_assert,1);lua_pop(L,1);
 luaL_requiref(L,"scheduler",luaopen_scheduler,1);lua_pop(L,1);
 luaL_requiref(L,"vector3",luaopen_vector3,1);lua_pop(L,1);
}
char const* behavior=R"(
  pluto_assert.equal(7,7)
  local ok = pcall(function() pluto_assert.equal(7,8) end)
  assert(not ok)
  local v = vector3(1,2,3) + vector3(4,5,6)
  assert(v.x == 5 and v.y == 7 and v.z == 9 and v:sum() == 21)
  local s = new scheduler()
  s.yieldfunc = function() end
  local count = 0
  s:add(function() count = count + 1 end)
  s:run()
  assert(count == 1)
)";
int main(int argc,char**argv){
 assert(argc==2);
#ifdef STAR_PLUTO_BOOTSTRAP_CACHE
 Star::Switch2ExplorationProfile::start();Star::Switch2ExplorationProfile::beginFrame(1,3,true);
#endif
 auto a=create();open(a);run(a,behavior);
 run(a,"pluto_assert.marker='a'; pluto_assert.AssertionError.marker='a'; scheduler.marker='a'; vector3.marker='a'; getmetatable(vector3).marker='a'");
 auto b=create();open(b);run(b,behavior);
 run(b,"assert(pluto_assert.marker==nil and pluto_assert.AssertionError.marker==nil and scheduler.marker==nil and vector3.marker==nil and getmetatable(vector3).marker==nil)");
 lua_close(b);run(a,"assert(pluto_assert.marker=='a')");lua_close(a);
#ifdef STAR_PLUTO_BOOTSTRAP_CACHE
 for(auto& slot:StarPlutoBootstrap::slots){
#if STAR_PLUTO_BOOTSTRAP_BYTES > 32
  auto n=slot.publication.load();assert(n>=2&&n-2<=StarPlutoBootstrap::Capacity);std::printf("bootstrap bytes=%zu\n",n-2);
#else
  assert(slot.publication.load()==0);
#endif
  slot.publication.store(0); // Test-only reset after all readers have closed.
 }
#endif
 // Exercise simultaneous cold publication with independent actual Lua states.
 std::array<std::thread,4> threads;std::atomic<bool> start{false};
 for(auto& t:threads)t=std::thread([&]{while(!start.load())std::this_thread::yield();for(int i=0;i<8;++i){auto L=create();open(L);run(L,behavior);lua_close(L);}});
 start.store(true);for(auto& t:threads)t.join();
#ifdef STAR_PLUTO_BOOTSTRAP_CACHE
 auto bad=std::make_unique<StarPlutoBootstrap::Slot>();auto L=create();
 assert(StarPlutoBootstrap::load(L,*bad,"function (",10,"bad-bootstrap")==LUA_ERRSYNTAX);assert(bad->publication.load()==0);lua_pop(L,1);
 assert(StarPlutoBootstrap::load(L,*bad,"return {}",9,"good-bootstrap")==LUA_OK);assert(lua_pcall(L,0,1,0)==LUA_OK&&lua_istable(L,-1));lua_close(L);
 Star::Switch2ExplorationProfile::endFrame(3,1000);assert(Star::Switch2ExplorationProfile::dump(argv[1]));
#endif
 // Host-only supporting measurement, never projected to console FPS.
 auto begin=std::chrono::steady_clock::now();for(int i=0;i<32;++i){auto L=create();open(L);lua_close(L);}
 auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
 std::printf("bootstrap functional/isolation/concurrency tests passed; 32 host VM/library creations: %.3f ms\n",ms);
}
