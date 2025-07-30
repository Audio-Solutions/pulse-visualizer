#pragma once
#include "common.hpp"

namespace Spline {

template <int Density> struct SplineConfig {
  static constexpr int density = Density;
  static constexpr float scale = 1.0f / Density;

  // Pre-computed basis function coefficients for each step
  static constexpr std::array<std::array<float, 4>, Density> basis_functions = []() {
    std::array<std::array<float, 4>, Density> result {};
    for (int j = 0; j < Density; ++j) {
      float u = static_cast<float>(j) * scale;
      float u2 = u * u;
      float u3 = u2 * u;

      result[j][0] = -0.5f * u3 + u2 - 0.5f * u;        // b0
      result[j][1] = 1.5f * u3 - 2.5f * u2 + 1.0f;      // b1
      result[j][2] = -1.5f * u3 + 2.0f * u2 + 0.5f * u; // b2
      result[j][3] = 0.5f * u3 - 0.5f * u2;             // b3
    }
    return result;
  }();
};

/**
 * @brief Generates a smooth spline curve from control points using cubic B-spline interpolation
 * @param control Vector of control points as (x, y) pairs
 * @param min Minimum bounds for the output points (x_min, y_min)
 * @param max Maximum bounds for the output points (x_max, y_max)
 * @return Vector of interpolated points forming the smooth spline curve
 */
template <int Density = 10>
std::vector<std::pair<float, float>> generate(const std::vector<std::pair<float, float>>& control,
                                              std::pair<float, float> min, std::pair<float, float> max) {

  static_assert(Density > 0, "Density must be positive");
  if (control.size() < 4)
    return {};

  std::vector<std::pair<float, float>> spline;
  spline.reserve((control.size() - 3) * Density);

  for (size_t i = 0; i < control.size() - 3; i++) {
    const auto& p0 = control[i];
    const auto& p1 = control[i + 1];
    const auto& p2 = control[i + 2];
    const auto& p3 = control[i + 3];

    // Unrolled loop with pre-computed basis functions
    for (int j = 0; j < Density; j++) {
      const auto& basis = SplineConfig<Density>::basis_functions[j];

      float x = basis[0] * p0.first + basis[1] * p1.first + basis[2] * p2.first + basis[3] * p3.first;
      float y = basis[0] * p0.second + basis[1] * p1.second + basis[2] * p2.second + basis[3] * p3.second;

      x = std::clamp(x, min.first, max.first);
      y = std::clamp(y, min.second, max.second);

      spline.emplace_back(x, y);
    }
  }

  return spline;
}

} // namespace Spline