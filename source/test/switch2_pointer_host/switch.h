#pragma once
#include <cstdint>
using Result = std::uint32_t;
#define R_SUCCEEDED(result) ((result) == 0)
struct PadState {};
struct PadPointer { bool active=false; int delta_x=0, delta_y=0; };
namespace PointerTest { extern int starts, stops, updates, width, height; extern Result nextResult; extern PadPointer next; }
inline Result padPointerInitialize(PadPointer*, int width, int height) { ++PointerTest::starts; PointerTest::width=width; PointerTest::height=height; return PointerTest::nextResult; }
inline void padPointerExit(PadPointer*) { ++PointerTest::stops; }
inline void padPointerUpdate(PadPointer* pointer, PadState const*) { ++PointerTest::updates; *pointer=PointerTest::next; }
