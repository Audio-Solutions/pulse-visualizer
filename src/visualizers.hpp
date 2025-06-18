#pragma once

#include "audio_data.hpp"

#include <utility>
#include <vector>

namespace Visualizers {
// Helper functions for spline generation
std::vector<std::pair<float, float>> generateCatmullRomSpline(const std::vector<std::pair<float, float>>& controlPoints,
                                                              int segmentsPerSegment = 10);
std::vector<float> calculateCumulativeDistances(const std::vector<std::pair<float, float>>& points);

// Individual visualizer functions
void drawLissajous(const AudioData& audioData, int lissajousSize);
void drawOscilloscope(const AudioData& audioData, int scopeWidth);
void drawFFT(const AudioData& audioData, int fftWidth);
void drawSplitter(const AudioData& audioData, int splitterX);
} // namespace Visualizers