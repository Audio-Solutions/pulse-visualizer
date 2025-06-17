#pragma once

#include <vector>
#include "audio_data.hpp"

namespace AudioProcessing {
  // Signal processing functions
  void applyBandpass(const std::vector<float>& input, std::vector<float>& output, 
                     float centerFreq, float sampleRate, float bandwidth = 10.0f);
  
  // Audio thread function
  void audioThread(AudioData* audioData);
} 