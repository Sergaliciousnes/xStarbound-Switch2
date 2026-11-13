#pragma once

#include <cstdint>
#include <atomic>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Star {

// Copies complete disk blocks into a bounded LRU. The database owns coherence:
// pending writes take precedence, and mutation/lifecycle paths invalidate it.
class ReadBlockCache {
public:
  void setBudget(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budget = bytes;
    trim();
    m_enabled.store(bytes != 0, std::memory_order_relaxed);
  }
  bool enabled() const {
    return m_enabled.load(std::memory_order_relaxed);
  }
  bool read(uint64_t key, size_t offset, char* output, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_lookup.find(key);
    if (found == m_lookup.end()) return false;
    auto entry = found->second;
    if (offset > entry->data.size() || size > entry->data.size() - offset) return false;
    if (size) std::memcpy(output, entry->data.data() + offset, size);
    m_entries.splice(m_entries.begin(), m_entries, entry);
    return true;
  }
  void store(uint64_t key, char const* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    eraseLocked(key);
    if (!size || size > m_budget) return;
    m_entries.push_front({key, std::vector<char>(data, data + size)});
    try {
      m_lookup.emplace(key, m_entries.begin());
    } catch (...) {
      m_entries.pop_front();
      throw;
    }
    m_used += size;
    trim();
  }
  void erase(uint64_t key) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    eraseLocked(key);
  }
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lookup.clear();
    m_entries.clear();
    m_used = 0;
  }
  size_t usedBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_used;
  }

private:
  struct Entry { uint64_t key; std::vector<char> data; };
  void eraseLocked(uint64_t key) {
    auto found = m_lookup.find(key);
    if (found == m_lookup.end()) return;
    m_used -= found->second->data.size();
    m_entries.erase(found->second);
    m_lookup.erase(found);
  }
  void trim() {
    while (m_used > m_budget) eraseLocked(m_entries.back().key);
  }
  mutable std::mutex m_mutex;
  std::atomic<bool> m_enabled{false};
  size_t m_budget = 0, m_used = 0;
  std::list<Entry> m_entries;
  std::unordered_map<uint64_t, std::list<Entry>::iterator> m_lookup;
};
}
