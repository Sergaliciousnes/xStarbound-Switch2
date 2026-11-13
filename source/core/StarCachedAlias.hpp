#pragma once
namespace Star {
// Keep the cheap alias lookup while sharing one value for a canonical key.
// Copy the value before inserting the alias: insertion can invalidate cache
// references. No outer producer remains active during the inner lookup.
template <typename Cache,typename CanonicalKey,typename Producer>
typename Cache::Value cachedAlias(Cache& cache,typename Cache::Key const& alias,
    CanonicalKey canonicalKey,Producer producer) {
  if(auto value=cache.ptr(alias))return *value;
  auto canonical=canonicalKey();
  typename Cache::Value value=cache.get(canonical,producer);
  cache.set(alias,value);
  return value;
}
}
