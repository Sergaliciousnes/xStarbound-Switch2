#pragma once
#include <switch.h>

namespace Star {
// PadPointer owns the native Nmouse reference and consumes each HID sample once.
// Drain it while unfocused, and discard queued motion after an applet or HOME.
class Switch2Pointer {
public:
  ~Switch2Pointer() { close(); }
  Switch2Pointer() = default;
  Switch2Pointer(Switch2Pointer const&) = delete;
  Switch2Pointer& operator=(Switch2Pointer const&) = delete;
  Result initialize() {
    if (m_ready) return 0;
    auto result = padPointerInitialize(&m_pointer, 1280, 720);
    m_ready = R_SUCCEEDED(result);
    return result;
  }
  void close() {
    if (m_ready) padPointerExit(&m_pointer);
    m_ready = m_active = false;
    m_dx = m_dy = 0;
    m_focused = false;
    m_discard = true;
  }
  void suspend() { m_discard = true; }
  void update(PadState const& pad, bool focused) {
    m_active = false; m_dx = m_dy = 0;
    if (!m_ready) return;
    padPointerUpdate(&m_pointer, &pad);
    m_active = focused && m_focused && !m_discard && m_pointer.active;
    if (m_active) { m_dx = m_pointer.delta_x; m_dy = m_pointer.delta_y; }
    m_focused = focused;
    m_discard = false;
  }
  bool active() const { return m_active; }
  int deltaX() const { return m_dx; }
  int deltaY() const { return m_dy; }
private:
  PadPointer m_pointer{};
  bool m_ready = false, m_active = false, m_focused = false, m_discard = true;
  int m_dx = 0, m_dy = 0;
};
}
