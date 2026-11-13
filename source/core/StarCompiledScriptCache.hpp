#pragma once
#include <list>
#include <memory>
#include <mutex>
#include <cstdint>

namespace Star {
// Serialized code and source bytes only. No live VM values/callbacks are stored.
// Payload and entry count are bounded; callers load a separate chunk per state.
template <typename Key, typename Bytes>
class CompiledScriptCache {
public:
  using Buffer = std::shared_ptr<Bytes const>;
  struct Result { Buffer code; bool hit = false; size_t evicted = 0; };
  CompiledScriptCache(size_t budget, size_t entries) : m_budget(budget), m_limit(entries) {}

  template <typename Compile>
  Result get(Key const& key, Buffer source, Compile compile) {
    uint64_t generation;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (auto i=m_entries.begin();i!=m_entries.end();++i) {
        if (i->key==key) {
          if (*i->source==*source) {
            auto code=i->code; m_entries.splice(m_entries.begin(),m_entries,i);
            return {std::move(code),true,0};
          }
          m_bytes-=i->charge; m_entries.erase(i); break;
        }
      }
      generation=m_generation;
    }
    // No cache mutex is held during parsing/compilation or exception handling.
    auto code=std::make_shared<Bytes const>(compile(*source));
    size_t bytes=code->size();
    if (source->size()>m_budget || bytes>m_budget-source->size() || !m_limit || key.size()>1024)
      return {std::move(code),false,0};
    bytes+=source->size();
    std::lock_guard<std::mutex> lock(m_mutex);
    if(generation!=m_generation)return {std::move(code),false,0};
    for(auto i=m_entries.begin();i!=m_entries.end();++i)if(i->key==key) {
      // A concurrent compiler may already have published this source.
      if(*i->source==*source) {auto existing=i->code;m_entries.splice(m_entries.begin(),m_entries,i);return {std::move(existing),false,0};}
      m_bytes-=i->charge;m_entries.erase(i);break;
    }
    size_t evicted=0;
    while(!m_entries.empty() && (m_entries.size()>=m_limit || bytes>m_budget-m_bytes)) {
      m_bytes-=m_entries.back().charge;m_entries.pop_back();++evicted;
    }
    m_entries.push_front({key,std::move(source),code,bytes});m_bytes+=bytes;
    return {std::move(code),false,evicted};
  }
  void clear() {std::lock_guard<std::mutex> lock(m_mutex);m_entries.clear();m_bytes=0;++m_generation;}
  void erase(Key const& key) {
    std::lock_guard<std::mutex> lock(m_mutex);++m_generation;
    for(auto i=m_entries.begin();i!=m_entries.end();++i)if(i->key==key){m_bytes-=i->charge;m_entries.erase(i);break;}
  }
  size_t bytes() const {std::lock_guard<std::mutex> lock(m_mutex);return m_bytes;}
  size_t size() const {std::lock_guard<std::mutex> lock(m_mutex);return m_entries.size();}
private:
  struct Entry {Key key;Buffer source,code;size_t charge;};
  mutable std::mutex m_mutex;
  std::list<Entry> m_entries;
  size_t m_budget,m_limit,m_bytes=0;
  uint64_t m_generation=0;
};
}
