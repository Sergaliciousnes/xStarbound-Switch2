#ifndef STAR_RENDER_PROFILING_HPP
#define STAR_RENDER_PROFILING_HPP

// CPU wall-time zones and per-frame counters; these are not GPU timestamps.
#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#include <cstdint>
namespace Star {
struct RenderProfileCounters {
  uint64_t atlasSnapshots = 0;
  uint64_t atlasSnapshotBytes = 0;
  uint64_t atlasWriteBytes = 0;
  uint64_t textureCacheMisses = 0;
  uint64_t retiredTextures = 0;
  uint64_t batchTextureLimitSplits = 0;
  uint64_t immediateBatches = 0;
  uint64_t cachedReplayBatches = 0;
  uint64_t cachedPreparedBatches = 0;
  uint64_t mergedDraws = 0;
  uint64_t mergeRangeBreaks = 0, mergeStateBreaks = 0, mergeTextureBreaks = 0;
};
inline thread_local RenderProfileCounters renderProfileCounters;
}
#define STAR_RENDER_ZONE(name) ZoneScopedN(name)
#define STAR_RENDER_NAMED_ZONE(variable, name) ZoneNamedN(variable, name, true)
#define STAR_RENDER_TEXT(...) ZoneTextF(__VA_ARGS__)
#define STAR_RENDER_PLOT(name, value) TracyPlot(name, (int64_t)(value))
#define STAR_RENDER_COUNT(field, value) (::Star::renderProfileCounters.field += (value))
#define STAR_RENDER_RESET() (::Star::renderProfileCounters = {})
#define STAR_RENDER_COUNTER(name, field) STAR_RENDER_PLOT(name, ::Star::renderProfileCounters.field)
#else
#define STAR_RENDER_ZONE(name) ((void)0)
#define STAR_RENDER_NAMED_ZONE(variable, name) ((void)0)
#define STAR_RENDER_TEXT(...) ((void)0)
#define STAR_RENDER_PLOT(name, value) ((void)0)
#define STAR_RENDER_COUNT(field, value) ((void)0)
#define STAR_RENDER_RESET() ((void)0)
#define STAR_RENDER_COUNTER(name, field) ((void)0)
#endif

#endif
