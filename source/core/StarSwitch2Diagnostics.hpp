#pragma once

namespace Star::Switch2Diagnostics {
enum class Channel { Main, Vulkan, Server, ClientWorld, Celestial, Lighting, Render, Texture, Count };
enum class Counter { Frames, ServerTicks, WorldStarts, SearchAttempts, Chunks, Reads, Writes, Count };
#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_DIAGNOSTICS)
void startWorldEntryTrace();
// Labels must be string literals (the watchdog reads them on another thread).
void stage(Channel channel, char const* label);
void count(Counter counter);
#else
inline void startWorldEntryTrace() {}
inline void stage(Channel, char const*) {}
inline void count(Counter) {}
#endif
}
