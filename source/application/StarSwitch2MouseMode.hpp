#pragma once
#include <cmath>

namespace Star {
// Require deliberate motion to enter, then latch the mode through brief pauses.
// Timers use observed frame intervals; a stalled frame cannot arm either input.
class Switch2MouseMode {
public:
  bool update(bool connected, bool focused, bool moving, float stickLength, float seconds) {
    if (!connected) { reset(); return false; }
    if (!focused) { pause(); return m_active; }
    float dt = std::isfinite(seconds) && seconds > 0 ? seconds : 0;
    bool continuousFrame = dt > 0 && dt <= 0.15f;
    bool stick = std::isfinite(stickLength) && stickLength >= 0.25f;
    if (m_active) {
      m_idle = moving ? 0 : m_idle + dt;
      m_stick = stick && continuousFrame ? m_stick + dt : 0;
      if (m_stick + 0.00001f >= 0.4f || m_idle >= 30.0f) reset();
      return m_active;
    }
    if (!continuousFrame || stick) { pause(); return false; }
    if (!m_started) {
      if (moving) { m_started = true; m_samples = 1; }
      return false;
    }
    m_elapsed += dt;
    if (moving) { m_motion += dt; m_gap = 0; ++m_samples; }
    else m_gap += dt;
    // Tolerate short gaps while requiring movement in most of the window.
    if (m_gap > 0.12f || m_elapsed > 2.0f) { pause(); return false; }
    if (moving && m_elapsed + 0.00001f >= 1.0f
        && m_motion >= m_elapsed * 0.65f && m_samples >= 8) {
      m_active = true; m_idle = 0; pause();
    }
    return m_active;
  }
  void pause() {
    m_started = false; m_samples = 0;
    m_elapsed = m_motion = m_gap = m_stick = 0;
  }
private:
  void reset() { m_active = false; m_idle = 0; pause(); }
  bool m_active = false, m_started = false;
  unsigned m_samples = 0;
  float m_elapsed = 0, m_motion = 0, m_gap = 0, m_idle = 0, m_stick = 0;
};
}
