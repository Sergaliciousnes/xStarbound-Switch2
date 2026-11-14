#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "StarSwitch2MouseMode.hpp"

namespace Star {

// Platform-independent state mapping; HID sampling and engine events live in
// the SDL integration. Stick Y is positive upwards, as in the native HID API.
class Switch2Controls {
public:
  struct Sample {
    bool focused = true;
    bool connected = false;
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    // Native Joy-Con motion in 1280x720 reference pixels, Y positive upwards.
    bool pointerActive = false;
    float pointerX = 0, pointerY = 0;
    bool jump = false, pause = false, primary = false, secondary = false, interact = false;
    bool previous = false, next = false;
    bool ability = false, chat = false;
    bool dpadLeft = false, dpadRight = false, dpadUp = false, dpadDown = false;
    bool touching = false;
    bool multipleTouches = false;
    uint32_t touchId = 0;
    // Touch coordinates normalized to the bottom-left engine origin.
    float touchX = 0, touchY = 0;
  };

  struct State {
    bool left = false, right = false, up = false, down = false;
    bool jump = false, pause = false, primary = false, secondary = false, interact = false;
    bool previousPressed = false, nextPressed = false;
    bool ability = false, alt = false, shift = false, chatPressed = false;
    bool mouseMode = false;
    float cursorX = 640, cursorY = 360;
  };

  State update(Sample const& sample, float seconds, int width, int height) {
    bool pad = sample.focused && sample.connected;
    State next;
    next.cursorX = m_state.cursorX;
    next.cursorY = m_state.cursorY;
    m_axisLeft = axisHeld(pad ? -sample.leftX : 0, m_axisLeft);
    m_axisRight = axisHeld(pad ? sample.leftX : 0, m_axisRight);
    m_axisUp = axisHeld(pad ? sample.leftY : 0, m_axisUp);
    m_axisDown = axisHeld(pad ? -sample.leftY : 0, m_axisDown);
    next.left = m_axisLeft || (pad && sample.dpadLeft);
    next.right = m_axisRight || (pad && sample.dpadRight);
    next.up = m_axisUp || (pad && sample.dpadUp);
    next.down = m_axisDown || (pad && sample.dpadDown);
    next.shift = pad && sample.dpadDown;
    next.jump = pad && sample.jump;
    next.pause = pad && sample.pause;
    next.primary = pad && sample.primary;
    next.secondary = pad && sample.secondary;
    bool y = pad && sample.interact;
    if (!y || !m_yHeld) m_ySeconds = 0;
    else if (std::isfinite(seconds) && seconds > 0) m_ySeconds += seconds;
    m_yHeld = y;
    next.alt = y && m_ySeconds > 2.0f;
    next.interact = y && !next.alt;
    next.ability = pad && sample.ability;
    bool chat = pad && sample.chat;
    next.chatPressed = chat && !m_chat;
    m_chat = chat;
    bool previous = pad && sample.previous, following = pad && sample.next;
    next.previousPressed = previous && !m_previous;
    next.nextPressed = following && !m_next;
    m_previous = previous;
    m_next = following;

    // A tap clicks only on release. Keep the full elapsed interval here;
    // cursor movement's capped delta must not turn a long stall into a tap.
    if (sample.touching && !m_touching) {
      m_tapEligible = sample.focused && !sample.multipleTouches && !sample.primary && !sample.secondary;
      m_touchId = sample.touchId;
      m_touchStartX = sample.touchX;
      m_touchStartY = sample.touchY;
      m_touchSeconds = 0;
    } else if (m_touching && m_tapEligible) {
      if (!std::isfinite(seconds) || seconds < 0) m_tapEligible = false;
      else m_touchSeconds += seconds;
    }
    if (!sample.focused || sample.multipleTouches || sample.primary || sample.secondary || m_touchSeconds > 0.5f)
      m_tapEligible = false;
    if (sample.touching) {
      float dx = (sample.touchX - m_touchStartX) * 1279.0f;
      float dy = (sample.touchY - m_touchStartY) * 719.0f;
      // Allow small contact jitter; once a swipe exceeds 12 screen pixels,
      // returning to its starting point must not restore tap eligibility.
      // Measure in HID's reference touch space so 1080p has the same tolerance.
      if (sample.touchId != m_touchId || dx * dx + dy * dy > 12.0f * 12.0f)
        m_tapEligible = false;
    }
    bool tapped = m_touching && !sample.touching && m_tapEligible;
    m_touching = sample.touching;
    if (!sample.touching) m_tapEligible = false;
    // One frame of primary produces a normal down/up pair through the existing
    // event mapping. Never release or duplicate a held physical trigger.
    next.primary = next.primary || tapped;

    bool mouseMoving = sample.pointerActive && std::isfinite(sample.pointerX)
        && std::isfinite(sample.pointerY) && (sample.pointerX != 0 || sample.pointerY != 0);
    float stickLength = std::hypot(sample.rightX, sample.rightY);
    next.mouseMode = m_mouseMode.update(sample.connected, sample.focused,
        mouseMoving && !sample.touching && !tapped, stickLength, seconds);
    if (sample.focused && sample.touching) {
      next.cursorX = std::clamp(sample.touchX, 0.0f, 1.0f) * std::max(width - 1, 0);
      next.cursorY = std::clamp(sample.touchY, 0.0f, 1.0f) * std::max(height - 1, 0);
    } else if (pad && !tapped && next.mouseMode) {
      // Keep the stick suppressed during brief sensor gaps. The mode latch
      // handles deliberate joystick handoff and extended inactivity.
      if (mouseMoving) {
        float scale = 0.9f * height / 720.0f;
        next.cursorX += sample.pointerX * scale;
        next.cursorY += sample.pointerY * scale;
      }
    } else if (pad && !tapped) {
      float length = std::hypot(sample.rightX, sample.rightY);
      constexpr float DeadZone = 0.12f;
      if (length > DeadZone) {
        float amount = (std::min(length, 1.0f) - DeadZone) / (1.0f - DeadZone);
        // Smaller radial deadzone responds to gentle movement. Blend quadratic
        // and cubic response for finer aim while retaining full-stick speed.
        float response = amount * amount * (0.5f + 0.5f * amount);
        float distance = 900.0f * (height / 720.0f) * response * std::clamp(seconds, 0.0f, 0.05f);
        next.cursorX += distance * sample.rightX / length;
        next.cursorY += distance * sample.rightY / length;
      }
    }
    next.cursorX = std::clamp(next.cursorX, 0.0f, float(std::max(width - 1, 0)));
    next.cursorY = std::clamp(next.cursorY, 0.0f, float(std::max(height - 1, 0)));
    m_state = next;
    return next;
  }

  // Release transient state around native applets, but retain the chat edge
  // latch so holding Minus across an applet cannot reopen the keyboard.
  void suspend() {
    float x = m_state.cursorX, y = m_state.cursorY;
    m_state = {}; m_state.cursorX = x; m_state.cursorY = y;
    m_axisLeft = m_axisRight = m_axisUp = m_axisDown = false;
    m_yHeld = false; m_ySeconds = 0;
    m_mouseMode.pause();
    m_previous = m_next = m_touching = m_tapEligible = false;
  }

  void setCursor(float x, float y) { m_state.cursorX = x; m_state.cursorY = y; }

private:
  static bool axisHeld(float value, bool held) {
    return value >= (held ? 0.25f : 0.35f);
  }
  State m_state;
  Switch2MouseMode m_mouseMode;
  bool m_previous = false, m_next = false, m_chat = false;
  bool m_axisLeft = false, m_axisRight = false, m_axisUp = false, m_axisDown = false;
  bool m_yHeld = false;
  float m_ySeconds = 0;
  bool m_touching = false, m_tapEligible = false;
  uint32_t m_touchId = 0;
  float m_touchStartX = 0, m_touchStartY = 0, m_touchSeconds = 0;
};

}
