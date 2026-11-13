#pragma once
#include <array>
#include <mutex>
#include <cstdint>
#include <cstddef>
namespace Star {
enum class ResourceCacheEvent { Locked,Hit,Miss,Evict };
struct IgnoreResourceCacheEvent {void operator()(ResourceCacheEvent) const noexcept {}};
// A fixed number of owned resources. A callback pins its resource by holding
// the cache lock; eviction always closes the old resource before opening a new
// one. No leases or handles escape the callback. Exceptions release the lock.
template <typename Key,typename Resource,size_t Capacity> class BoundedResourceCache {
  static_assert(Capacity>0,"Cache must have at least one slot");
public:
  template <typename Factory,typename Function,typename Observer=IgnoreResourceCacheEvent>
  decltype(auto) with(Key const& key,Factory&& factory,Function&& function,Observer observer={}) {
    std::lock_guard<std::mutex> guard(m_mutex);
    observer(ResourceCacheEvent::Locked);
    Entry* chosen=nullptr;
    for(auto& e:m_entries){
      if(e.resource&&e.key==key){observer(ResourceCacheEvent::Hit);e.used=++m_clock;return function(e.resource);}
      if(!chosen||(!e.resource&&chosen->resource)||(bool(e.resource)==bool(chosen->resource)&&e.used<chosen->used))chosen=&e;
    }
    observer(ResourceCacheEvent::Miss);
    if(chosen->resource)observer(ResourceCacheEvent::Evict);
    chosen->resource=Resource{};
    chosen->key=key;
    chosen->resource=factory();
    chosen->used=++m_clock;
    return function(chosen->resource);
  }
private:
  struct Entry {Key key{};Resource resource{};uint64_t used=0;};
  std::array<Entry,Capacity> m_entries{};
  uint64_t m_clock=0;
  std::mutex m_mutex;
};
}
