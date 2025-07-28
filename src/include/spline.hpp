#pragma once
#include "common.hpp"

namespace Spline {
std::vector<std::pair<float, float>> generate(std::vector<std::pair<float, float>> control, int density,
                                              std::pair<float, float> min, std::pair<float, float> max);

} // namespace Spline