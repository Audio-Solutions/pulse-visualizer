#pragma once

#include "config.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct AudioData {
  std::vector<float> bufferMid;     // Mid channel
  std::vector<float> bufferSide;    // Side channel
  std::vector<float> bandpassedMid; // Mid channel (filtered)
  size_t writePos = 0;
  std::atomic<bool> running {true};
  std::atomic<bool> fftHovering {false};
  size_t bufferSize;
  int windowWidth;
  int windowHeight;

  float sampleRate = 44100.0f;
  float currentPitch = 0.0f;
  float pitchConfidence = 0.0f;
  float samplesPerCycle = 0.0f;
  size_t cycleCount = 2;
  float lastCycleOffset = 0.0f; // Track partial cycle offset for smooth transitions
  float lastPhaseOffset = 0.0f; // Track phase offset for synchronization

  // Pre-allocated FFT data to avoid reallocations
  std::vector<float> fftMagnitudesMid;
  std::vector<float> prevFftMagnitudesMid;
  std::vector<float> smoothedMagnitudesMid;
  std::vector<float> interpolatedValuesMid;
  std::vector<float> fftMagnitudesSide;
  std::vector<float> prevFftMagnitudesSide;
  std::vector<float> smoothedMagnitudesSide;
  std::vector<float> interpolatedValuesSide;

  // Pre-allocated CQT data to avoid reallocations
  std::vector<float> cqtMagnitudesMid;
  std::vector<float> prevCqtMagnitudesMid;
  std::vector<float> smoothedCqtMagnitudesMid;
  std::vector<float> interpolatedCqtValuesMid;
  std::vector<float> cqtMagnitudesSide;
  std::vector<float> prevCqtMagnitudesSide;
  std::vector<float> smoothedCqtMagnitudesSide;
  std::vector<float> interpolatedCqtValuesSide;
  std::vector<float> cqtFrequencies; // Center frequencies for each CQT bin

  std::chrono::steady_clock::time_point lastFftUpdate;
  float fftUpdateInterval = 0.0f; // Time between FFT updates in seconds

  // Pre-computed FFT display data
  std::vector<std::pair<float, float>> fftDisplayPoints;
  float peakFreq = 0.0f;
  float peakDb = -100.0f;
  std::string peakNote = "-";
  int peakOctave = 0;
  int peakCents = 0;
  bool hasValidPeak = false;

  AudioData()
      : windowWidth(Config::values().window.default_width), windowHeight(Config::values().window.default_height) {}

  float getAudioDeltaTime() const { return 1.0f / Config::values().window.fps_limit; }
};