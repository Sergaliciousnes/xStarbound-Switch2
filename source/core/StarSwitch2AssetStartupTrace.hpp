#pragma once
#include <cstdint>
namespace Star::Switch2AssetStartupTrace {
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_ASSET_STARTUP_TRACE)
void checkpoint(char const* phase,char const* path="",uint64_t progress=0,uint64_t total=0) noexcept;
#else
inline void checkpoint(char const*,char const* ="",uint64_t=0,uint64_t=0) noexcept {}
#endif
}
