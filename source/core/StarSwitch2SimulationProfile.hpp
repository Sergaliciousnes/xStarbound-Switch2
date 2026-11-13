#pragma once
#include <cstdint>
namespace Star::Switch2SimulationProfile {
enum class Kind { Work,Lock,WorldUpdate,Inbound,Messages,Outbound,Action,Sync,Sleep,
  EntityNpc,EntityObject,EntityOther,NpcUpdate,NpcScript,NpcMovement,NpcStatus,NpcShared,PathFind,PathExplore,Count };
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_EXPLORATION_PROFILE)
void start();
bool dump(char const* path);
void advance(double dt);
void entity(unsigned type);
void fidelity(unsigned level);
class Tick {
public:
  explicit Tick(char const* world,double step=1.0/60.0) noexcept;
  ~Tick();
  Tick(Tick const&)=delete;
private: bool m_active=false;
};
class Scope {
public:
  Scope(Kind kind,char const* label="",bool sampled=true) noexcept;
  ~Scope(){stop();}
  void stop() noexcept;
  Scope(Scope const&)=delete;
private:
  Kind m_kind;char const* m_label;char const* m_previous=nullptr;
  int64_t m_start=0;bool m_actor=false;
};
#else
inline void start(){} inline bool dump(char const*){return false;}
inline void advance(double){} inline void entity(unsigned){} inline void fidelity(unsigned){}
class Tick {public: explicit Tick(char const*,double=1.0/60.0) noexcept{}};
class Scope {public:Scope(Kind,char const* = "",bool=true) noexcept{} void stop() noexcept{}};
#endif
}
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_EXPLORATION_PROFILE)
#define STAR_SIM_SCOPE(variable,...) ::Star::Switch2SimulationProfile::Scope variable(__VA_ARGS__)
#define STAR_SIM_TICK(variable,...) ::Star::Switch2SimulationProfile::Tick variable(__VA_ARGS__)
#define STAR_SIM_ADVANCE(...) ::Star::Switch2SimulationProfile::advance(__VA_ARGS__)
#define STAR_SIM_ENTITY(...) ::Star::Switch2SimulationProfile::entity(__VA_ARGS__)
#define STAR_SIM_FIDELITY(...) ::Star::Switch2SimulationProfile::fidelity(__VA_ARGS__)
#else
#define STAR_SIM_SCOPE(variable,...) ::Star::Switch2SimulationProfile::Scope variable(::Star::Switch2SimulationProfile::Kind::Work)
#define STAR_SIM_TICK(variable,...) ::Star::Switch2SimulationProfile::Tick variable("")
#define STAR_SIM_ADVANCE(...) ((void)0)
#define STAR_SIM_ENTITY(...) ((void)0)
#define STAR_SIM_FIDELITY(...) ((void)0)
#endif
