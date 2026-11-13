#include "../core/StarBoundedResourceCache.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>
#include <stdexcept>
#include <cstdio>
std::atomic<int> live{0},peak{0},opens{0};
struct Resource {
  int id;
  explicit Resource(int key):id(key){int n=++live;++opens;int old=peak.load();while(old<n&&!peak.compare_exchange_weak(old,n)){}assert(n<=8);}
  ~Resource(){--live;}
};
int main(){
  {
    Star::BoundedResourceCache<int,std::unique_ptr<Resource>,8> cache;
    auto read=[&](int key){cache.with(key,[&]{return std::make_unique<Resource>(key);},[&](auto const& p){assert(p&&p->id==key);std::this_thread::yield();assert(p->id==key);});};
    for(int i=0;i<64;++i)read(i);
    assert(live==8&&peak==8&&opens==64);
    int before=opens;read(63);assert(opens==before);
    // A failed factory must not leave the victim's old key attached to a
    // different resource, and a callback failure must release its lock.
    try{cache.with(100,[]{throw std::runtime_error("open failure");return std::unique_ptr<Resource>{};},[](auto const&){});assert(false);}catch(std::runtime_error const&){}
    read(100);
    try{cache.with(100,[]{return std::make_unique<Resource>(100);},[](auto const&){throw std::runtime_error("read failure");});assert(false);}catch(std::runtime_error const&){}
    read(100);
    std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.emplace_back([&,t]{for(int i=0;i<2000;++i)read((i+t*17)%64);});
    for(auto& worker:workers)worker.join();
    assert(peak==8);
  }
  assert(live==0);
  {
    Star::BoundedResourceCache<int,std::unique_ptr<Resource>,1> cache;
    std::vector<Star::ResourceCacheEvent> events;
    auto run=[&](int key){cache.with(key,[&]{return std::make_unique<Resource>(key);},[&](auto const& p){assert(p->id==key);},[&](auto event){events.push_back(event);});};
    using E=Star::ResourceCacheEvent;
    run(1);assert((events==std::vector<E>{E::Locked,E::Miss}));events.clear();
    run(1);assert((events==std::vector<E>{E::Locked,E::Hit}));events.clear();
    run(2);assert((events==std::vector<E>{E::Locked,E::Miss,E::Evict}));
  }
  assert(live==0);
  std::puts("bounded-resource-cache: verified 64 keys, 8-resource ceiling, concurrency, factory/read errors and cleanup");
}
