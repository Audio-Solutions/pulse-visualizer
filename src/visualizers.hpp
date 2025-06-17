#pragma once

#include "audio_data.hpp"

namespace Visualizers {
  // Individual visualizer functions
  void drawLissajous(const AudioData& audioData, int lissajousSize);
  void drawOscilloscope(const AudioData& audioData, int scopeWidth);
  void drawFFT(const AudioData& audioData, int fftWidth);
  void drawSplitter(const AudioData& audioData, int splitterX);
} 