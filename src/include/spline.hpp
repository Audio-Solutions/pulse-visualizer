#pragma once
#include "common.hpp"

namespace Spline {

/**
 * @brief Generates a smooth spline curve from control points using cubic B-spline interpolation
 * @param control Vector of control points as (x, y) pairs
 * @param density Number of points to generate between each control point segment
 * @param min Minimum bounds for the output points (x_min, y_min)
 * @param max Maximum bounds for the output points (x_max, y_max)
 * @return Vector of interpolated points forming the smooth spline curve
 */
std::vector<std::pair<float, float>> generate(std::vector<std::pair<float, float>> control, int density,
                                              std::pair<float, float> min, std::pair<float, float> max);

} // namespace Spline