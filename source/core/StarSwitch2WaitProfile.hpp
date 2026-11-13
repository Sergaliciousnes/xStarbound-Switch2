#pragma once
#include <cstdint>
namespace Star::Switch2WaitProfile {
enum class Metric { Events, Update, Render, Gather, Paint, Finish, Present, Pacing, Fence, Acquire,
  AssetLock, AssetWork, AssetLoaderWait, RootLock, MetadataLock, DirectivesLock, SectorWait, LightingWait,
  FileCursor, FileRead, FileWrite, FileSync, EntityDelta,
  AssetContended, RootContended, MetadataContended, DirectivesContended,
  WorldUpdate, PlayerScripts, InterfaceUpdate, MixerUpdate, InventoryState, ArmorState,
  SkyPaint, ParallaxPaint, TileSetup, LightingPaint, EntityPaint, TerrainPaint,
  PaintCleanup, ParticlePaint, OverlayPaint,
  SkyStars, SkyDebris, SkyBackOrbiters, SkyHorizon, SkyGradient, SkyFrontOrbiters,
  RendererImmediate, RendererCached, WorldEntities, WorldDamage, WorldSky, WorldWeather,
  WorldParticles, WorldAudio, WorldSectors,
  LocalServerPause, ClientUniverse, PacketReceive, PacketHandle, PacketSend, PlayerSave, SystemUpdate, Count };
enum class Counter { StarsVisited, StarsDrawn, StarResets, DebrisVisited,
  ImmediatePrimitives, ImmediateVertices, ImmediateDraws, CachedBuffers, CachedRebuilds,
  CachedVertices, CachedDraws, EntitiesUpdated, Count };
enum class Scene { Unknown, Menu, Ship, Planet, Other };
#if defined(STAR_SYSTEM_SWITCH2) && !defined(STAR_SWITCH2_TIMING_DISABLED)
int64_t nowUs();
void start();
void beginFrame();
void endFrame();
void finishFrames();
void scene(Scene value);
bool detailed();
void count(Counter counter,uint64_t amount=1);
bool dump(char const* path);
// Coarse stages run each frame; hot lock/IO/state scopes run one frame in eight.
class Scope {
public:
  explicit Scope(Metric metric);
  ~Scope() { stop(); }
  void stop();
  Scope(Scope const&) = delete;
  Scope& operator=(Scope const&) = delete;
private:
  Metric m_metric;
  int64_t m_start = 0;
  int64_t m_cpuStart = -1;
  bool m_active = false, m_outer = false;
};
class Background {
public:
  explicit Background(char const* literalLabel);
  ~Background() { stop(); }
  void stop();
  Background(Background const&) = delete;
private:
  char const* m_label;
  int64_t m_start = 0;
};
#else
inline void start() {} inline void beginFrame() {} inline void endFrame() {}
inline void finishFrames() {}
inline void scene(Scene) {} inline bool detailed() { return false; }
inline void count(Counter,uint64_t=1) {}
inline bool dump(char const*) { return true; }
class Scope { public: explicit Scope(Metric) {} void stop() {} };
class Background { public: explicit Background(char const*) {} void stop() {} };
#endif
// Only sampled frames try the lock first. A failed try distinguishes observed
// contention from a long wall interval caused by preemption around a free lock.
template <typename Locker>
void acquire(Locker& locker, Metric contentionMetric) {
  if (detailed()) {
    if (!locker.tryLock()) {
      Scope blocked(contentionMetric);
      locker.lock();
    }
  } else {
    locker.lock();
  }
}
struct Frame { Frame() { beginFrame(); } ~Frame() { endFrame(); } };
}
