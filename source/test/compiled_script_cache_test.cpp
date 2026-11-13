#include "../core/StarCompiledScriptCache.hpp"
#include "../extern/lua/lua.h"
#include "../extern/lua/lauxlib.h"
#include "../extern/lua/lualib.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <map>
using Cache=Star::CompiledScriptCache<std::string,std::string>;
struct Fixture {Cache cache{8192,8};std::map<std::string,std::string> source;int compiles=0,hits=0;};
void* alloc(void*,void* p,size_t,size_t n){if(!n){std::free(p);return nullptr;}return std::realloc(p,n);}
std::string compile(lua_State* L,std::string const& source,std::string const& name){
 if(luaL_loadbuffer(L,source.data(),source.size(),name.c_str())!=LUA_OK){std::string e=lua_tostring(L,-1);lua_pop(L,1);throw std::runtime_error(e);}
 std::string result;lua_dump(L,[](lua_State*,void const* p,size_t n,void* out){static_cast<std::string*>(out)->append(static_cast<char const*>(p),n);return 0;},&result,0);lua_pop(L,1);return result;
}
void load(lua_State* L,Fixture& f,std::string const& path){
 auto result=f.cache.get(path,std::make_shared<std::string const>(f.source.at(path)),[&](auto const& source){++f.compiles;return compile(L,source,path);});
 if(result.hit)++f.hits;
 if(luaL_loadbuffer(L,result.code->data(),result.code->size(),path.c_str())!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));
 if(lua_pcall(L,0,0,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));
}
int requireModule(lua_State* L){
 auto& f=*static_cast<Fixture*>(lua_touserdata(L,lua_upvalueindex(1)));
 std::string path=luaL_checkstring(L,1);
 lua_getfield(L,LUA_REGISTRYINDEX,path.c_str());bool loaded=lua_toboolean(L,-1);lua_pop(L,1);
 if(!loaded){lua_pushboolean(L,1);lua_setfield(L,LUA_REGISTRYINDEX,path.c_str());load(L,f,path);}return 0;
}
lua_State* state(Fixture& f,int config){
 auto L=lua_newstate(alloc,nullptr,123);assert(L);
 luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
 lua_pushlightuserdata(L,&f);lua_pushcclosure(L,requireModule,1);lua_setglobal(L,"require");
 lua_pushinteger(L,config);lua_setglobal(L,"configValue");return L;
}
int invoke(lua_State* L){lua_getglobal(L,"event");assert(lua_pcall(L,0,1,0)==LUA_OK);int n=int(lua_tointeger(L,-1));lua_pop(L,1);return n;}
int main(){
 Fixture f;f.source["/module.lua"]="moduleCounter=(moduleCounter or 0)+1";
 f.source["/event.lua"]="require('/module.lua'); require('/module.lua'); globalCounter=(globalCounter or 0)+1; function event() return configValue+globalCounter+moduleCounter end";
 auto a=state(f,10);load(a,f,"/event.lua");assert(invoke(a)==12&&f.compiles==2);
 auto b=state(f,20);load(b,f,"/event.lua");assert(invoke(b)==22&&invoke(a)==12&&f.compiles==2&&f.hits==2);
 lua_pushinteger(b,999);lua_setglobal(b,"globalCounter");assert(invoke(a)==12); // No live globals cross states.
 f.source["/module.lua"]="moduleCounter=(moduleCounter or 0)+3";
 auto c=state(f,30);load(c,f,"/event.lua");assert(invoke(c)==34&&f.compiles==3); // Source changes invalidate.
 f.cache.clear();assert(invoke(a)==12); // Eviction cannot invalidate a live VM's functions.
 auto d=state(f,40);load(d,f,"/event.lua");assert(invoke(d)==44&&f.compiles==5);
 f.source["/broken.lua"]="function ( invalid";auto before=f.cache.size();
 try{load(d,f,"/broken.lua");assert(false);}catch(std::runtime_error const&){}assert(f.cache.size()==before);
 f.source["/broken.lua"]="fixed=true";load(d,f,"/broken.lua");
 Cache small(12,2);int builds=0;auto get=[&](std::string key,std::string source){return small.get(key,std::make_shared<std::string const>(source),[&](auto const& s){++builds;return s;});};
 get("a","aa");get("b","bb");assert(get("a","aa").hit);auto evicted=get("c","cc");assert(evicted.evicted==1&&small.size()==2&&small.bytes()<=12);
 assert(!get("b","bb").hit);auto size=small.size();assert(!get("huge",std::string(20,'x')).hit&&small.size()==size);
 small.clear();auto late=small.get("late",std::make_shared<std::string const>("x"),[&](auto const& s){small.clear();return s;});assert(*late.code=="x"&&small.size()==0);
 // An explicit unload prevents subsequent reuse.
 get("a","aa");small.erase("a");assert(!get("a","aa").hit);
 for(auto L:{a,b,c,d})lua_close(L);
 std::puts("Pluto bytecode cache passed: real compile/load, fresh-state globals/config isolation, require once per VM, source reload, live-code lifetime, syntax-error retry, payload/entry bounds, LRU, explicit unload and invalidation during compile.");
}
