#include "../core/StarCachedAlias.hpp"
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <cassert>
#include <stdexcept>
#include <cstdio>
struct Sheet {std::string path;std::array<int,4> pixels;};
struct Cache {
  using Key=std::string;using Value=std::shared_ptr<Sheet>;
  std::vector<std::pair<Key,Value>> entries;
  Value* ptr(Key const& key){for(auto& entry:entries)if(entry.first==key)return &entry.second;return nullptr;}
  template <typename F> Value& get(Key const& key,F factory){if(auto p=ptr(key))return *p;auto value=factory(key);entries.emplace_back(key,std::move(value));entries.shrink_to_fit();return entries.back().second;}
  void set(Key const& key,Value value){if(auto p=ptr(key))*p=std::move(value);else{entries.emplace_back(key,std::move(value));entries.shrink_to_fit();}}
};
int main(){
  Cache cache;int builds=0,paths=0;
  auto fetch=[&](char const* alias,char const* path){return Star::cachedAlias(cache,std::string(alias),[&]{++paths;return std::string(path);},[&](std::string const& key){++builds;if(key=="broken")throw std::runtime_error("image load failure");return std::make_shared<Sheet>(Sheet{key,{10,20,30,40}});});};
  auto a=fetch("material:piece1","sheet.png?");
  auto b=fetch("material:piece2","sheet.png?");
  assert(a==b&&builds==1&&paths==2);
  assert(a->pixels[0]==10&&b->pixels[3]==40); // UV subrectangles still select the full sheet.
  auto again=fetch("material:piece1","must-not-resolve");assert(again==a&&paths==2);
  auto hue=fetch("material:hue","sheet.png?hueshift=30?");assert(hue!=a&&builds==2);
  auto directive=fetch("material:background","sheet.png?brightness=-20");assert(directive!=a&&directive!=hue&&builds==3);
  assert(fetch("other-material:piece","sheet.png?")==a&&builds==3);
  try{fetch("bad-piece","broken");assert(false);}catch(std::runtime_error const&){}
  assert(!cache.ptr("bad-piece")&&!cache.ptr("broken"));
  cache.entries.clear();assert(a->pixels[2]==30); // Already queued geometry retains ownership.
  auto fresh=fetch("material:piece1","sheet.png?");assert(fresh!=a);
  std::puts("Cached alias passed: shared sheets, distinct hue/directives, lazy alias hits, insertion invalidation, load failure and live draw lifetime after flush.");
}
