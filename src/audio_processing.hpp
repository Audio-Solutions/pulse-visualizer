#pragma once

#include "audio_data.hpp"

#include <chrono>
#include <vector>

namespace AudioProcessing {
// Signal processing functions

namespace Butterworth {
// Apply bandpass filter to circular buffer in temporal order
void applyBandpassCircular(const std::vector<float>& input, std::vector<float>& output, float centerFreq,
                           float sampleRate, size_t writePos, float bandwidth = 10.0f, int order = 8);
} // namespace Butterworth

// Audio processing state
struct AudioThreadState {
  // Previous values for smoothing
  float prevPeakFreq = 0.0f;
  float prevPeakDb = -100.0f;

  // Timing state
  std::chrono::steady_clock::time_point lastFrameTime;
  float fftInterpolation = 0.0f;

  // Config cache
  int lissajous_points = 500;
  float fft_min_freq = 10.0f;
  float fft_max_freq = 20000.0f;
  float fft_smoothing_factor = 0.2f;
  float fft_rise_speed = 500.0f;
  float fft_fall_speed = 50.0f;
  float fft_hover_fall_speed = 10.0f;
  float silence_threshold = -100.0f;
  float sampleRate = 44100.0f;
  int fft_size = -1;
  size_t lastConfigVersion = 0;

  // Note conversion cache
  const char* const* noteNames = nullptr;
  std::string note_key_mode = "sharp";

  void updateConfigCache();
};

// Audio thread function
void audioThread(AudioData* audioData);

// Helper function to convert frequency to note
void freqToNote(float freq, std::string& noteName, int& octave, int& cents, AudioThreadState& state);
} // namespace AudioProcessing