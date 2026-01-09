/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "common.hpp"

namespace Spline {

struct SplineConfig {
  // Compute cubic Hermite basis functions for parameter t in [0, 1].
  // h00 =  2t^3 - 3t^2 + 1
  // h10 =  t^3 - 2t^2 + t
  // h01 = -2t^3 + 3t^2
  // h11 = t^3 - t^2
  static void hermite_basis(float t, float (&hb)[4]) {
    float t2 = t * t;
    float t3 = t2 * t;

    hb[0] = 2.0f * t3 - 3.0f * t2 + 1.0f; // h00
    hb[1] = t3 - 2.0f * t2 + t;           // h10
    hb[2] = -2.0f * t3 + 3.0f * t2;       // h01
    hb[3] = t3 - t2;                      // h11
  }
};

/**
 * @brief Generates a Catmull-Rom style spline with controllable tension and runtime density.
 * @param control Vector of control points as (x, y) pairs.
 * @param min Minimum bounds for the output points (x_min, y_min).
 * @param max Maximum bounds for the output points (x_max, y_max).
 * @param density Number of samples per segment; controls point density along the curve.
 * @param tension 0 = straight (close to polyline), 1 = standard Catmull-Rom.
 *                Values > 1 tighten the curve further by scaling the tangents.
 * @return Vector of interpolated points forming the smooth spline curve.
 */
inline std::vector<std::pair<float, float>> generate(const std::vector<std::pair<float, float>>& control,
                                                     std::pair<float, float> min, std::pair<float, float> max,
                                                     int density, float tension = 1.0f) {
  if (density <= 0)
    return {};
  if (control.size() < 4)
    return {};

  std::vector<std::pair<float, float>> spline;
  spline.reserve((control.size() - 3) * density);

  // τ controls Catmull-Rom tangents: m_k = τ (p_{k+1} - p_{k-1}).
  // τ = 0.5 is the classic Catmull-Rom; scaling by tension lets the caller adjust smoothness.
  float tau = 0.5f * tension;

  for (size_t i = 0; i + 3 < control.size(); ++i) {
    const auto& p0 = control[i];
    const auto& p1 = control[i + 1];
    const auto& p2 = control[i + 2];
    const auto& p3 = control[i + 3];

    // Tangents at p1 and p2 derived from neighboring control points.
    float m1x = tau * (p2.first - p0.first);
    float m1y = tau * (p2.second - p0.second);
    float m2x = tau * (p3.first - p1.first);
    float m2y = tau * (p3.second - p1.second);

    // Sample 'density' points along the segment from p1 to p2.
    for (int j = 0; j < density; ++j) {
      float t = static_cast<float>(j) / static_cast<float>(density); // local parameter in [0, 1)

      // Evaluate Hermite basis at t.
      float hb[4];
      SplineConfig::hermite_basis(t, hb);

      float h00 = hb[0];
      float h10 = hb[1];
      float h01 = hb[2];
      float h11 = hb[3];

      // Cubic Hermite interpolation using positions p1, p2 and tangents m1, m2.
      float x = h00 * p1.first + h10 * m1x + h01 * p2.first + h11 * m2x;
      float y = h00 * p1.second + h10 * m1y + h01 * p2.second + h11 * m2y;

      // blend toward the straight line between p1 and p2 when tension < 1.
      // This gives a continuous control from polyline (tension = 0) to full spline (tension = 1).
      if (tension < 1.0f) {
        float lx = (1.0f - t) * p1.first + t * p2.first;
        float ly = (1.0f - t) * p1.second + t * p2.second;
        float w = tension;
        x = lx * (1.0f - w) + x * w;
        y = ly * (1.0f - w) + y * w;
      }

      // Clamp to provided bounds.
      x = std::clamp(x, min.first, max.first);
      y = std::clamp(y, min.second, max.second);

      spline.emplace_back(x, y);
    }
  }

  return spline;
}

} // namespace Spline
