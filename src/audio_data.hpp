#pragma once

#include "config.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <fftw3.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T, std::size_t Alignment> struct AlignedAllocator {
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using void_pointer = void*;
  using const_void_pointer = const void*;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  template <class U> struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  AlignedAllocator() noexcept {}
  template <class U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  pointer allocate(size_type n) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0)
      throw std::bad_alloc();
    return reinterpret_cast<pointer>(ptr);
  }
  void deallocate(pointer p, size_type) noexcept { free(p); }
};

struct AudioData {
  std::vector<float, AlignedAllocator<float, 32>> bufferMid;     // Mid channel
  std::vector<float, AlignedAllocator<float, 32>> bufferSide;    // Side channel
  std::vector<float, AlignedAllocator<float, 32>> bandpassedMid; // Mid channel (filtered)
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

  // Main thread synchronization
  std::atomic<bool> dataReady;
  std::mutex audioMutex;
  std::condition_variable audioCv;

  // FFT/CQT thread synchronization
  std::mutex fftMutex;
  std::atomic<bool> fftReadyMid {false};
  std::condition_variable fftCvMid;
  std::atomic<bool> fftReadySide {false};
  std::condition_variable fftCvSide;

  // FFTW plans
  std::mutex fftPlanMutexMid;
  fftwf_plan fftPlanMid;
  std::mutex fftPlanMutexSide;
  fftwf_plan fftPlanSide;
  float* fftInMid;
  float* fftInSide;
  fftwf_complex* fftOutMid;
  fftwf_complex* fftOutSide;

  AudioData()
      : windowWidth(Config::values().window.default_width), windowHeight(Config::values().window.default_height),
        fftInMid(nullptr), fftInSide(nullptr), fftOutMid(nullptr), fftOutSide(nullptr), fftPlanMid(nullptr),
        fftPlanSide(nullptr) {}

  float getAudioDeltaTime() const { return 1.0f / Config::values().window.fps_limit; }
};
