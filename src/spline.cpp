#include "include/spline.hpp"

namespace Spline {
std::vector<std::pair<float, float>> generate(std::vector<std::pair<float, float>> control, int density,
                                              std::pair<float, float> min, std::pair<float, float> max) {
  std::vector<std::pair<float, float>> spline;
  spline.reserve((control.size() - 3) * (density + 1));

  float scale = 1.f / density;

  for (size_t i = 0; i < control.size() - 3; i++) {
    const auto& p0 = control[i];
    const auto& p1 = control[i + 1];
    const auto& p2 = control[i + 2];
    const auto& p3 = control[i + 3];

    for (int j = 0; j < density; j++) {
      float u = static_cast<float>(j) * scale;
      float u2 = u * u;
      float u3 = u2 * u;

      float b0 = -0.5f * u3 + u2 - 0.5f * u;
      float b1 = 1.5f * u3 - 2.5f * u2 + 1.0f;
      float b2 = -1.5f * u3 + 2.0f * u2 + 0.5f * u;
      float b3 = 0.5f * u3 - 0.5f * u2;

      float x = b0 * p0.first + b1 * p1.first + b2 * p2.first + b3 * p3.first;
      float y = b0 * p0.second + b1 * p1.second + b2 * p2.second + b3 * p3.second;

      x = std::clamp(x, min.first, max.first);
      y = std::clamp(y, min.second, max.second);

      spline.push_back({x, y});
    }
  }

  return spline;
}

} // namespace Spline