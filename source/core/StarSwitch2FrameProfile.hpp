#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>

namespace Star::Switch2FrameProfile {
enum class Phase { Frame, Update, Gather, Paint, Tiles, Sky, LightingWait, Finish,
  Fence, GeometryUpload, Textures, Acquire, Record, Present, AssetMiss, FileRead,
  Packets, WorldUpdate, PlayerScripts, InterfaceUpdate, MixerUpdate, StorageSave,
  PlayerSerialize, FileWrite, FileSync, EntityUpdates, UpdatePlayer, UpdateMonster,
  UpdateObject, UpdateOther, Damage, Weather, ParticleUpdate, WorldAudio, NetUpdates,
  SectorUnload, GatherLights, GatherLighting, GatherHighlights, GatherEntities,
  RenderPlayer, RenderMonster, RenderObject, RenderOther, GatherDirectives, GatherTiles,
  GatherEnvironment, PaintLighting, Parallax, EntityLayers, TerrainDraw, ParticleDraw,
  OverlayDraw, PaintCleanup, LuaCalls, LuaGc, UpdatePlant, RenderPlant, ReadEntityDelta, WriteEntityDelta, Count };
enum class Counter { Updates, Draws, Vertices, UploadBytes, NewImages, Descriptors,
  EntitiesUpdated, EntitiesGathered, DrawableSets, ParticleCount, Saves, Packets, PlantsDrawn, PlantsCulled, Count };

// Immediate completed-frame duration, with a one-second peak hold for hitches.
class FrameMeter {
public:
  void reset(int64_t now) { m_last = now; m_ms = m_peak = 0; m_peakUntil = now; }
  void complete(int64_t now) {
    if (now <= m_last) { m_last = now; return; }
    m_ms = (now - m_last) / 1000.0;
    m_last = now;
    if (m_ms >= m_peak || now >= m_peakUntil) {
      m_peak = m_ms;
      m_peakUntil = now + 1000000;
    }
  }
  double milliseconds() const { return m_ms; }
  double peakMilliseconds() const { return m_peak; }
  double fps() const { return m_ms > 0 ? 1000.0 / m_ms : 0; }
private:
  int64_t m_last = 0, m_peakUntil = 0;
  double m_ms = 0, m_peak = 0;
};

#ifdef STAR_SYSTEM_SWITCH2
inline int64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr unsigned PhaseCount = static_cast<unsigned>(Phase::Count);
constexpr unsigned CounterCount = static_cast<unsigned>(Counter::Count);
inline FrameMeter meter;
#endif

#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_DIAGNOSTICS)
inline thread_local bool tracking = false;
inline thread_local std::array<int64_t, PhaseCount> frameTimes{};
inline thread_local std::array<unsigned, PhaseCount> depths{};
inline thread_local std::array<uint64_t, CounterCount> frameCounts{};
inline std::array<std::atomic<int64_t>, PhaseCount> totals{}, maxima{};
inline std::array<std::atomic<uint64_t>, CounterCount> counts{};
inline std::atomic<uint64_t> frames{0};
inline void begin() { frameTimes.fill(0); frameCounts.fill(0); depths.fill(0); tracking = true; }
inline void count(Counter counter, uint64_t value) {
  if (tracking) frameCounts[static_cast<unsigned>(counter)] += value;
}
class Zone {
public:
  explicit Zone(Phase phase) : m_phase(phase), m_active(tracking) {
    if (m_active && depths[static_cast<unsigned>(m_phase)]++ == 0) m_start = nowUs();
  }
  Zone(Zone const&) = delete;
  Zone& operator=(Zone const&) = delete;
  ~Zone() { stop(); }
  void stop() {
    if (!m_active) return;
    if (m_start) frameTimes[static_cast<unsigned>(m_phase)] += nowUs() - m_start;
    --depths[static_cast<unsigned>(m_phase)];
    m_active = false;
  }
private:
  Phase m_phase;
  bool m_active;
  int64_t m_start = 0;
};
inline void complete() {
  meter.complete(nowUs());
  frameTimes[static_cast<unsigned>(Phase::Frame)] = int64_t(meter.milliseconds() * 1000);
  for (unsigned i = 0; i < PhaseCount; ++i) {
    totals[i].fetch_add(frameTimes[i], std::memory_order_relaxed);
    auto peak = maxima[i].load(std::memory_order_relaxed);
    while (peak < frameTimes[i] && !maxima[i].compare_exchange_weak(peak, frameTimes[i], std::memory_order_relaxed)) {}
  }
  for (unsigned i = 0; i < CounterCount; ++i) counts[i].fetch_add(frameCounts[i], std::memory_order_relaxed);
  frames.fetch_add(1, std::memory_order_relaxed);
  tracking = false;
}
// Called only by the independent watchdog. Windows are approximate because
// frame publication may overlap a snapshot. Times overlap for nested phases.
inline void report(FILE* output) {
  auto number = frames.exchange(0);
  if (!number) return;
  char const* names[] = {"frame", "update", "gather", "paint", "tiles", "sky", "light-wait", "finish",
      "fence", "geometry-upload", "textures", "acquire", "record", "present", "asset-miss", "file-read",
      "packets", "world-update", "player-scripts", "interface-update", "mixer-update", "storage-save",
      "player-serialize", "file-write", "file-sync", "entity-updates", "update-player", "update-monster",
      "update-object", "update-other", "damage", "weather", "particle-update", "world-audio", "net-updates",
      "sector-unload", "gather-lights", "gather-lighting", "gather-highlights", "gather-entities",
      "render-player", "render-monster", "render-object", "render-other", "gather-directives", "gather-tiles",
      "gather-environment", "paint-lighting", "parallax", "entity-layers", "terrain-draw", "particle-draw",
      "overlay-draw", "paint-cleanup", "lua-calls", "lua-gc", "update-plant", "render-plant", "read-entity-delta", "write-entity-delta"};
  char const* countNames[] = {"updates", "draws", "vertices", "upload-bytes", "new-images", "descriptors",
      "entities-updated", "entities-gathered", "drawable-sets", "particles", "saves", "packets", "plants-drawn", "plants-culled"};
  static_assert(std::size(names) == PhaseCount && std::size(countNames) == CounterCount);
  std::fprintf(output, "[performance] frames=%llu | milliseconds avg/max per frame (nested phases overlap)", (unsigned long long)number);
  for (unsigned i = 0; i < PhaseCount; ++i) {
    auto sum = totals[i].exchange(0), peak = maxima[i].exchange(0);
    std::fprintf(output, " | %s=%.2f/%.2f", names[i], sum / (1000.0 * number), peak / 1000.0);
  }
  for (unsigned i = 0; i < CounterCount; ++i)
    std::fprintf(output, " | %s/frame=%.1f", countNames[i], counts[i].exchange(0) / double(number));
  std::fputc('\n', output);
}
#else
class Zone { public: explicit Zone(Phase) {} void stop() {} };
inline void count(Counter, uint64_t) {}
#ifdef STAR_SYSTEM_SWITCH2
inline void begin() {}
inline void complete() { meter.complete(nowUs()); }
#endif
#endif
}
