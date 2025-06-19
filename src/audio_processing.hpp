#pragma once

#include "audio_data.hpp"

#include <vector>

namespace AudioProcessing {
// Signal processing functions

namespace Butterworth {
// Apply bandpass filter to circular buffer in temporal order
void applyBandpassCircular(const std::vector<float>& input, std::vector<float>& output, float centerFreq,
                           float sampleRate, size_t writePos, float bandwidth = 10.0f, int order = 8);
} // namespace Butterworth

// Audio thread function
void audioThread(AudioData* audioData);
} // namespace AudioProcessing