#pragma once
#include <cstdint>
using Result = std::uint32_t;
#define R_SUCCEEDED(result) ((result) == 0)
namespace SocketTest { extern int starts, stops; extern Result nextResult; }
inline Result socketInitializeDefault() { ++SocketTest::starts; return SocketTest::nextResult; }
inline void socketExit() { ++SocketTest::stops; }
