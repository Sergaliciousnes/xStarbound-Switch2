#pragma once
#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <utility>

namespace Star {
// Single-process ownership for console storage. Leases have no persistent
// sentinel; process termination cannot leave a lock behind on the SD card.
class ProcessLock {
  struct State { std::mutex mutex; std::set<std::string> keys; };
  struct Lease {
    std::shared_ptr<State> state;
    std::string key;
    bool registered = false;
    Lease(std::shared_ptr<State> s, std::string k) : state(std::move(s)), key(std::move(k)) {}
    ~Lease() {
      if (registered) {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->keys.erase(key);
      }
    }
  };
public:
  static std::shared_ptr<void> tryAcquire(std::string key) {
    static auto state = std::make_shared<State>();
    // Allocate before taking the registry mutex. Unpublished leases do not
    // unregister, so allocation/insertion failure cannot deadlock cleanup.
    auto lease = std::make_shared<Lease>(state, std::move(key));
    std::lock_guard<std::mutex> guard(state->mutex);
    if (!state->keys.insert(lease->key).second) return {};
    lease->registered = true;
    return lease;
  }

  // Lexical normalization: unlike realpath, it works for a marker that does
  // not exist. SD paths are case-insensitive; fold ASCII case consistently.
  static std::string pathKey(std::string path, std::string cwd) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::replace(cwd.begin(), cwd.end(), '\\', '/');
    if (path.empty()) path = ".";
    if (path.find(":/") == std::string::npos) {
      if (path.front() != '/') path = cwd + '/' + path;
      else if (auto mount = cwd.find(":/"); mount != std::string::npos)
        path = cwd.substr(0, mount + 1) + path;
    }
    std::string prefix;
    size_t begin = 0;
    if (auto mount = path.find(":/"); mount != std::string::npos) {
      prefix = path.substr(0, mount + 2); begin = mount + 2;
    } else if (path.front() == '/') { prefix = "/"; begin = 1; }
    std::vector<std::string> parts;
    while (begin <= path.size()) {
      auto end = path.find('/', begin);
      if (end == std::string::npos) end = path.size();
      auto part = path.substr(begin, end - begin);
      if (part == "..") { if (!parts.empty()) parts.pop_back(); }
      else if (!part.empty() && part != ".") parts.push_back(std::move(part));
      if (end == path.size()) break;
      begin = end + 1;
    }
    for (auto const& part : parts) {
      if (!prefix.empty() && prefix.back() != '/') prefix += '/';
      prefix += part;
    }
    for (char& c : prefix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return prefix;
  }
};
}
