#include "StarPlantVisualBounds.hpp"
#include <cstdio>
#include <random>
#include <stdexcept>

void check(bool passed, char const* message) { if (!passed) throw std::runtime_error(message); }
int main() {
  try {
    auto flat = Star::plantVisualBounds(2, 3, 4, 5, false, false, false);
    check(flat == std::array<float, 4>{2, 3, 6, 8}, "Unrotated bounds");
    auto flipped = Star::plantVisualBounds(2, 3, 4, 5, true, false, false);
    check(flipped == std::array<float, 4>{-6, 3, -2, 8}, "Mirrored bounds include mirrored position");
    std::mt19937 rng(931);
    std::uniform_real_distribution<double> offset(-80, 80), extent(0.125, 80), jitter(-0.4, 0.4);
    unsigned samples = 0;
    for (unsigned plant = 0; plant < 300; ++plant) {
      double x = float(offset(rng)), y = float(offset(rng));
      double w = float(extent(rng)), h = float(extent(rng));
      for (bool flip : {false, true}) {
        for (unsigned type = 0; type < 3; ++type) {
          bool rotating = type != 0, crown = type == 2;
          auto bounds = Star::plantVisualBounds(float(x), float(y), float(w), float(h), flip, rotating, crown);
          for (int degree = -180; degree <= 180; degree += 3) {
            double angle = rotating ? degree * 3.141592653589793 / 180 : 0;
            double damage = jitter(rng);
            for (unsigned corner = 0; corner < 4; ++corner) {
              // Reproduce the original drawable scale/rotate/translate order.
              double px = x + (corner & 1 ? w : 0) + damage;
              double py = y + (corner & 2 ? h : 0);
              if (flip) px = -px;
              if (rotating) {
                double pivotX = x + w / 2, pivotY = crown ? y : y + h / 2;
                double dx = px - pivotX, dy = py - pivotY;
                px = pivotX + dx * std::cos(angle) - dy * std::sin(angle);
                py = pivotY + dx * std::sin(angle) + dy * std::cos(angle);
                if (crown) py -= 0.4;
              }
              double pad = std::abs(damage) + 0.002;
              check(px >= bounds[0] - pad && px <= bounds[2] + pad
                  && py >= bounds[1] - pad && py <= bounds[3] + pad,
                  "Envelope clipped a mirrored/rotating/damaged plant corner");
              ++samples;
            }
          }
        }
      }
    }
    std::printf("Plant bounds tests passed: %u transformed corners, including mirroring, full rotations, crown shifts and damage jitter.\n", samples);
    return 0;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
