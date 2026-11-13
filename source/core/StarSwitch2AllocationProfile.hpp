#pragma once
#include <cstddef>
#include <cstdint>

namespace Star::Switch2AllocationProfile {
enum class Kind { New,NewArray,Delete,DeleteArray,DeleteSized,DeleteArraySized,Malloc,Calloc,Realloc,Free,Count };
constexpr unsigned OutsideStage=64;
struct Context {uint64_t frame=0;unsigned scene=0,stage=OutsideStage;char const* label="outside-scope";unsigned window=0;};
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_ALLOCATION_PROFILE)
void start();
void frame(bool detailed,unsigned window=0,unsigned scene=0);
void thread(char const* name);
bool dump(char const* path);
bool captureRequested() noexcept;
class Call {
public:
  Call(Kind kind,size_t bytes,void const* caller,bool meaningful=true) noexcept;
  ~Call() noexcept;
  void result(void const* value){m_result=value!=nullptr;}
  void success(){m_result=true;}
  Call(Call const&)=delete;
private:
  Context m_context;
  Kind m_kind;
  uint64_t m_bytes=0;
  int64_t m_offset=0,m_start=0;
  bool m_entered=false,m_sample=false,m_result=false;
};
#else
inline void start(){} inline void frame(bool,unsigned=0,unsigned=0){} inline void thread(char const*){} inline bool dump(char const*){return false;}
inline bool captureRequested() noexcept{return false;}
class Call {
public:
  Call(Kind,size_t,void const*,bool=true) noexcept{}
  void result(void const*){} void success(){}
};
#endif
}
namespace Star::Switch2WaitProfile {
bool allocationContext(Switch2AllocationProfile::Context& context);
}
