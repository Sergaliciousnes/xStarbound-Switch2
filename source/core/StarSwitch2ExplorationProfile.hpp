#pragma once
#include <cstdint>
namespace Star::Switch2ExplorationProfile {
enum class Kind { Packet,EntityLoad,EntityState,EntityInit,WorldStart,TileApply,EntityDelta,
  TileHash,TerrainBuild,LiquidBuild,TerrainBuffer,TileImage,TileTexture,AssetMiss,AssetWait,
  AssetLoad,ImageLoad,TextureMiss,TextureCreate,AtlasSnapshot,GpuTexture,
  ArchiveAcquire,ArchiveWait,ArchiveOpen,ArchiveRead,UniverseStatistics,WorldStep,UniverseScripts,StatsEvent,StatsAchievement,StatsLookup,StatsConfig,StatsVmSetup,StatsComponentSetup,StatsInit,StatsInvoke,StatsUninit,StatsDestroy,StatsScript,StatsBytecodeLookup,StatsCompile,StatsBytecodeLoad,VmCreate,VmState,VmRegistry,VmBase,VmOs,VmLibrary,VmFinalize,VmRootConfig,VmRootBookkeeping,GatherLights,GatherLighting,GatherHighlights,GatherEntities,GatherTiles,GatherEnvironment,GatherNpcRender,GatherObjectRender,GatherOtherRender,GatherNpcLights,GatherObjectLights,GatherOtherLights,GatherNpcDirectives,GatherObjectDirectives,GatherOtherDirectives,UiInput,UiUpdate,UiRender,UiPaneUpdate,UiPaneRender,UiInventoryUpdate,UiInteract,UiPaneBuild,UiWidgetTree,UiWidgetBuild,UiTooltip,ScriptComponentInit,ScriptContext,ScriptInitFunction,BootstrapCompile,BootstrapDump,BootstrapLoad,BootstrapExecute,ClientNpcUpdate,ClientObjectUpdate,ClientOtherUpdate,ObjectRender,ObjectEffects,ObjectTags,ObjectAnimator,ObjectOrientation,ObjectScripted,Count };
enum class Counter { EntityCreates,TilePackets,TileCells,TerrainHit,TerrainMiss,LiquidHit,
  LiquidMiss,TileTextureMiss,AssetMisses,ArchiveHit,ArchiveMiss,ArchiveEvict,TextureBytes,TileTextureReuse,StatsEvents,StatsAchievementChecks,StatsScripts,StatsScriptFiles,StatsFailures,StatsAlreadyUnlocked,StatsBytecodeHit,StatsBytecodeMiss,StatsBytecodeEvict,MetadataWritten,MetadataSkipped,ChatWritten,ChatSkipped,GatherNpcs,GatherObjects,GatherOther,GatherLightCalls,GatherLightSources,GatherDrawables,UiWidgets,BootstrapHit,BootstrapMiss,BootstrapPublished,BootstrapBypass,BootstrapBytes,BootstrapError,Count };
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_EXPLORATION_PROFILE)
void start();
void beginFrame(uint64_t number,unsigned scene,bool detailed);
void endFrame(unsigned scene,int64_t elapsedUs);
void count(Counter counter,uint64_t amount=1);
bool dump(char const* path);
class Scope {
public:
  Scope(Kind kind,char const* label="",uint64_t detail=0,bool sampledOnly=false) noexcept;
  ~Scope(){stop();}
  void stop() noexcept;
  Scope(Scope const&)=delete;
private:
  Kind m_kind;
  char const* m_label;
  uint64_t m_detail,m_frame=0;
  int64_t m_start=0;
};
#else
inline void start(){} inline void beginFrame(uint64_t,unsigned,bool){}
inline void endFrame(unsigned,int64_t){} inline void count(Counter,uint64_t=1){}
inline bool dump(char const*){return false;}
class Scope {public:Scope(Kind,char const* = "",uint64_t=0,bool=false) noexcept{} void stop() noexcept{}};
#endif
}

// Drop label/work argument evaluation as well as timing when compiled out.
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_EXPLORATION_PROFILE)
#define STAR_EXPLORATION_SCOPE(variable, ...) ::Star::Switch2ExplorationProfile::Scope variable(__VA_ARGS__)
#define STAR_EXPLORATION_COUNT(...) ::Star::Switch2ExplorationProfile::count(__VA_ARGS__)
#else
#define STAR_EXPLORATION_SCOPE(variable, ...) ::Star::Switch2ExplorationProfile::Scope variable(::Star::Switch2ExplorationProfile::Kind::Packet)
#define STAR_EXPLORATION_COUNT(...) ((void)0)
#endif
