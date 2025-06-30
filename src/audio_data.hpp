#pragma once

#include "config.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct AudioData {
  std::vector<float> bufferMid;     // Mid channel
  std::vector<float> bufferSide;    // Side channel
  std::vector<float> bandpassedMid; // Mid channel (filtered)
  size_t writePos = 0;
  std::mutex mutex;
  std::atomic<bool> running {true};
  std::atomic<bool> fftHovering {false};
  size_t bufferSize;
  size_t displaySamples;
  int windowWidth;
  int windowHeight;

  // Frame timing for delta time tracking
  float dt = 0.0f; // Delta time in seconds
  std::chrono::steady_clock::time_point lastFrameTime;
  bool frameTimingInitialized = false;

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
      : bufferSize(Config::values().audio.buffer_size), displaySamples(Config::values().audio.display_samples),
        windowWidth(Config::values().window.default_width), windowHeight(Config::values().window.default_height) {}

  // Method to update delta time
  void updateDeltaTime() {
    auto currentTime = std::chrono::steady_clock::now();

    if (!frameTimingInitialized) {
      lastFrameTime = currentTime;
      frameTimingInitialized = true;
      dt = 0.0f; // First frame has no delta
      return;
    }

    dt = std::chrono::duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Clamp delta time to prevent large jumps (e.g., when debugging or window is inactive)
    // Maximum of 100ms (10 FPS) to prevent visual artifacts
    if (dt > 0.1f) {
      dt = 0.1f;
    }
  }
};