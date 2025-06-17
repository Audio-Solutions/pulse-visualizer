#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <chrono>
#include <string>
#include "config.hpp"

struct AudioData {
  static const size_t BUFFER_SIZE = Config::Audio::BUFFER_SIZE;
  std::vector<float> buffer;
  std::vector<float> bandpassed;
  size_t writePos = 0;
  size_t readPos = 0;
  std::mutex mutex;
  std::atomic<bool> running{true};
  static const size_t DISPLAY_SAMPLES = Config::Audio::DISPLAY_SAMPLES;
  float currentPitch = 0.0f;
  float pitchConfidence = 0.0f;
  float samplesPerCycle = 0.0f;
  size_t cycleCount = 2;
  std::atomic<size_t> availableSamples{0};  // Number of samples available for reading
  float lastCycleOffset = 0.0f;  // Track partial cycle offset for smooth transitions
  float lastPhaseOffset = 0.0f;  // Track phase offset for synchronization
  int windowWidth = Config::DEFAULT_WINDOW_WIDTH;   // Current window width
  int windowHeight = Config::DEFAULT_WINDOW_HEIGHT;  // Current window height
  
  // Pre-allocated FFT data to avoid reallocations
  std::vector<float> fftMagnitudes;
  std::vector<float> prevFftMagnitudes;
  std::vector<float> smoothedMagnitudes;
  std::vector<float> interpolatedValues;
  
  float splitterPos = Config::DEFAULT_SPLITTER_POS;
  bool draggingSplitter = false;
  std::chrono::steady_clock::time_point lastFftUpdate;
  float fftUpdateInterval = 0.0f;  // Time between FFT updates in seconds
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