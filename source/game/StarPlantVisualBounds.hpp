#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Star {
// A conservative local-space envelope for the existing plant drawable
// transforms. Rotating pieces use a full-circle bound, independent of wind.
inline std::array<float, 4> plantVisualBounds(float x, float y, float width, float height,
    bool flip, bool rotating, bool crown) {
  float left = flip ? -x - width : x;
  float right = flip ? -x : x + width;
  if (!rotating) return {left, y, right, y + height};
  float pivotX = x + width / 2;
  float pivotY = crown ? y : y + height / 2;
  float dx = std::max(std::abs(left - pivotX), std::abs(right - pivotX));
  float dy = std::max(std::abs(y - pivotY), std::abs(y + height - pivotY));
  float radius = std::hypot(dx, dy);
  if (crown) pivotY -= 0.40f;
  return {pivotX - radius, pivotY - radius, pivotX + radius, pivotY + radius};
}
}
