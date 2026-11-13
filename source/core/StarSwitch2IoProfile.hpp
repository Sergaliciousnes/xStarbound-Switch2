#pragma once
#include <cstddef>
#include <cstdint>
namespace Star::Switch2IoProfile {
enum class Op { Open, Close, Read, ReadAbsolute, Write, WriteAbsolute, Sync, Seek, Tell, Size, Eof, Resize, Count };
enum class Cache { BlockHit, BlockMiss, BlockPending, BlockDisabled, ScriptHit, ScriptMiss, Count };
#ifdef STAR_SWITCH2_IO_PROFILE
uint16_t file(char const* path);
void start();
void threadName(char const* name);
void beginFrame(uint64_t number);
void endFrame(unsigned scene,int64_t elapsedUs);
void cache(Cache event);
bool dump(char const* path);
class Operation {
public:
  Operation(uint16_t file,Op operation,uint64_t requested=0,int64_t offset=-1);
  ~Operation();
  void acquired();
  void complete(uint64_t bytes=0){m_bytes=bytes;m_complete=true;}
  Operation(Operation const&)=delete;
private:
  void* m_slot=nullptr;
  uint16_t m_file=0; Op m_op;
  uint64_t m_requested=0,m_bytes=0;
  int64_t m_start=0,m_acquired=0,m_offset=-1;
  bool m_complete=false;
};
class Asset {
public: explicit Asset(char const* path);~Asset();
private: char const* m_previous;
};
#else
inline uint16_t file(char const*){return 0;}
inline void start(){} inline void threadName(char const*){} inline void beginFrame(uint64_t){}
inline void endFrame(unsigned,int64_t){} inline void cache(Cache){} inline bool dump(char const*){return true;}
class Operation {public: Operation(uint16_t,Op,uint64_t=0,int64_t=-1){} void acquired(){} void complete(uint64_t=0){} };
class Asset {public: explicit Asset(char const*){} };
#endif
}
