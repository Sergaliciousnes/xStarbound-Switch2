#include "StarReadBlockCache.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

void check(bool value, char const* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    Star::ReadBlockCache cache;
    char output[4]{};
    cache.store(1, "abcd", 4);
    check(!cache.read(1, 0, output, 4), "Disabled cache retained data");
    cache.setBudget(8);
    cache.store(1, "abcd", 4);
    cache.store(2, "efgh", 4);
    check(cache.read(1, 1, output, 2) && !std::memcmp(output, "bc", 2), "Partial block read");
    cache.store(3, "ijkl", 4);
    check(!cache.read(2, 0, output, 4) && cache.read(1, 0, output, 4), "LRU eviction");
    check(!cache.read(1, 3, output, 2), "Out-of-range cache read");
    cache.erase(1);
    check(!cache.read(1, 0, output, 4), "Mutation invalidation");
    cache.store(3, "wxyz", 4);
    check(cache.read(3, 0, output, 4) && !std::memcmp(output, "wxyz", 4), "Block replacement");
    cache.store(4, "123456789", 9);
    check(cache.usedBytes() == 4, "Oversized blocks bypass cache");
    cache.setBudget(2);
    check(cache.usedBytes() == 0, "Budget shrink");
    cache.setBudget(64);
    std::atomic<bool> valid{true};
    auto work = [&] {
      for (unsigned i = 0; i < 5000; ++i) {
        cache.store(i % 32, "abcd", 4);
        char result[4];
        if (cache.read(i % 32, 0, result, 4) && std::memcmp(result, "abcd", 4)) valid = false;
        if (i % 13 == 0) cache.erase(i % 32);
      }
    };
    std::thread first(work), second(work);
    first.join(); second.join();
    check(valid && cache.usedBytes() <= 64, "Concurrent contents and budget");
    cache.clear();
    check(cache.usedBytes() == 0 && !cache.read(3, 0, output, 4), "Lifecycle clear");
    std::puts("Read-block cache tests passed: LRU, partial reads, bounds, invalidation, replacement, budgets, clear and concurrent access.");
    return 0;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
