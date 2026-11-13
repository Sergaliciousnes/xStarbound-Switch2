#pragma once
#include <switch.h>

namespace Star {
// Own one SDK socket-service reference across the complete application run.
// Keep single-player available when initialization fails; callers log the Result.
class Switch2SocketSession {
public:
  Switch2SocketSession() : m_result(socketInitializeDefault()) {}
  ~Switch2SocketSession() { if (R_SUCCEEDED(m_result)) socketExit(); }
  Switch2SocketSession(Switch2SocketSession const&) = delete;
  Switch2SocketSession& operator=(Switch2SocketSession const&) = delete;
  Result result() const { return m_result; }
private:
  Result m_result;
};
}
