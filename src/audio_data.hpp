#pragma once

#include "config.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct AudioData {
  static const size_t BUFFER_SIZE = Config::Audio::BUFFER_SIZE;
  std::vector<float> bufferMid;     // Mid channel
  std::vector<float> bufferSide;    // Side channel
  std::vector<float> bandpassedMid; // Mid channel (filtered)
  size_t writePos = 0;
  size_t readPos = 0;
  std::mutex mutex;
  std::atomic<bool> running {true};
  static const size_t DISPLAY_SAMPLES = Config::Audio::DISPLAY_SAMPLES;
  float currentPitch = 0.0f;
  float pitchConfidence = 0.0f;
  float samplesPerCycle = 0.0f;
  size_t cycleCount = 2;
  std::atomic<size_t> availableSamples {0};         // Number of samples available for reading
  float lastCycleOffset = 0.0f;                     // Track partial cycle offset for smooth transitions
  float lastPhaseOffset = 0.0f;                     // Track phase offset for synchronization
  int windowWidth = Config::DEFAULT_WINDOW_WIDTH;   // Current window width
  int windowHeight = Config::DEFAULT_WINDOW_HEIGHT; // Current window height

  // Pre-allocated FFT data to avoid reallocations
  std::vector<float> fftMagnitudesMid;
  std::vector<float> prevFftMagnitudesMid;
  std::vector<float> smoothedMagnitudesMid;
  std::vector<float> interpolatedValuesMid;
  std::vector<float> fftMagnitudesSide;
  std::vector<float> prevFftMagnitudesSide;
  std::vector<float> smoothedMagnitudesSide;
  std::vector<float> interpolatedValuesSide;

  float splitterPos = Config::DEFAULT_SPLITTER_POS;
  bool draggingSplitter = false;
  std::chrono::steady_clock::time_point lastFftUpdate;
  float fftUpdateInterval = 0.0f; // Time between FFT updates in seconds
  std::deque<std::pair<float, float>> lissajousPoints;

  // Pre-computed FFT display data

  std::vector<std::pair<float, float>> fftDisplayPoints;
  float peakFreq = 0.0f;
  float peakDb = -100.0f;
  std::string peakNote = "-";
  int peakOctave = 0;
  int peakCents = 0;
  bool hasValidPeak = false;
};