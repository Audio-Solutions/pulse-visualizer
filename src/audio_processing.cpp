#include "audio_processing.hpp"

#include "audio_engine.hpp"
#include "config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fftw3.h>
#include <iostream>
#include <memory>
#include <thread>

#if defined(HAVE_AVX2)
#include <cmath>
#include <immintrin.h>
static inline __m256 _mm256_log10_ps(__m256 x) {
  float vals[8];
  _mm256_storeu_ps(vals, x);
  for (int i = 0; i < 8; ++i)
    vals[i] = log10f(vals[i]);
  return _mm256_loadu_ps(vals);
}
static inline __m256 _mm256_pow_ps(__m256 a, __m256 b) {
  float va[8], vb[8], vr[8];
  _mm256_storeu_ps(va, a);
  _mm256_storeu_ps(vb, b);
  for (int i = 0; i < 8; ++i)
    vr[i] = powf(va[i], vb[i]);
  return _mm256_loadu_ps(vr);
}
#endif

namespace AudioProcessing {

namespace Butterworth {

struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1 = 0.0f, x2 = 0.0f;
  float y1 = 0.0f, y2 = 0.0f;

  // Direct Form I
  float process(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }

  // Reset filter state
  void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};

// Designs a Butterworth bandpass filter and returns the biquad sections
std::vector<Biquad> designButterworthBandpass(int order, float centerFreq, float sampleRate, float bandwidth) {
  std::vector<Biquad> biquads;
  int nSections = order / 2;
  float w0 = 2.0f * M_PI * centerFreq / sampleRate;
  float Q = centerFreq / bandwidth;

  // Butterworth pole placement
  for (int k = 0; k < nSections; ++k) {
    float alpha = sinf(w0) / (2.0f * Q);

    float b0 = alpha;
    float b1 = 0.0f;
    float b2 = -alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosf(w0);
    float a2 = 1.0f - alpha;

    // Normalize coefficients
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    biquads.push_back({b0, b1, b2, a1, a2});
  }
  return biquads;
}

// Incremental bandpass filter that maintains state and only processes new samples
struct IncrementalBandpass {
  std::vector<Biquad> biquads;
  std::vector<float> outputBuffer;
  size_t lastProcessedPos = 0;
  size_t bufferSize = 0;
  float lastCenterFreq = 0.0f;
  float lastBandwidth = 0.0f;
  int lastOrder = 0;
  float lastSampleRate = 0.0f;

  // Initialize or update filter parameters
  void updateParameters(float centerFreq, float sampleRate, float bandwidth = 10.0f, int order = 8) {
    // Only redesign if parameters have changed
    if (centerFreq != lastCenterFreq || bandwidth != lastBandwidth || order != lastOrder ||
        sampleRate != lastSampleRate) {

      biquads = designButterworthBandpass(order, centerFreq, sampleRate, bandwidth);

      // Reset all biquad states when parameters change
      for (auto& bq : biquads) {
        bq.reset();
      }

      lastCenterFreq = centerFreq;
      lastBandwidth = bandwidth;
      lastOrder = order;
      lastSampleRate = sampleRate;
    }
  }

  template <typename Alloc>
  void processEntireBuffer(const std::vector<float, Alloc>& input, std::vector<float, Alloc>& output, size_t writePos) {
    size_t bufferSize = input.size();
    if (output.size() != bufferSize) {
      output.resize(bufferSize);
    }
    for (auto& bq : biquads) {
      bq.reset();
    }
    for (size_t i = 0; i < bufferSize; ++i) {
      size_t readPos = (writePos + i) % bufferSize;
      float inputSample = input[readPos];
      float filteredSample = inputSample;
      for (auto& bq : biquads) {
        filteredSample = bq.process(filteredSample);
      }
      output[readPos] = filteredSample;
    }
  }

  template <typename Alloc>
  void processNewSamples(const std::vector<float, Alloc>& input, std::vector<float, Alloc>& output, size_t writePos,
                         size_t newSamples) {
    if (input.empty() || newSamples == 0) {
      return;
    }
    size_t bufferSize = input.size();
    if (output.size() != bufferSize) {
      output.resize(bufferSize);
    }
    if (this->bufferSize != bufferSize) {
      this->bufferSize = bufferSize;
      lastProcessedPos = 0;
      processEntireBuffer(input, output, writePos);
      return;
    }
    for (size_t sample = 0; sample < newSamples; ++sample) {
      size_t currentPos = (writePos - newSamples + sample + bufferSize) % bufferSize;
      float inputSample = input[currentPos];
      float filteredSample = inputSample;
      for (auto& bq : biquads) {
        filteredSample = bq.process(filteredSample);
      }
      output[currentPos] = filteredSample;
    }
  }
};

// Global filter instance for the bandpass filter
static IncrementalBandpass bandpassFilter;

// Apply bandpass filter to circular buffer efficiently (only processes new data)
template <typename Alloc>
void applyBandpassCircular(const std::vector<float, Alloc>& input, std::vector<float, Alloc>& output, float centerFreq,
                           float sampleRate, size_t writePos, float bandwidth, int order) {
  if (input.empty()) {
    output.clear();
    return;
  }
  bandpassFilter.updateParameters(centerFreq, sampleRate, bandwidth, order);
  size_t bufferSize = input.size();
  size_t newSamples = 0;
  if (bandpassFilter.bufferSize != bufferSize) {
    bandpassFilter.processEntireBuffer(input, output, writePos);
  } else {
    size_t lastPos = bandpassFilter.lastProcessedPos;
    if (writePos >= lastPos) {
      newSamples = writePos - lastPos;
    } else {
      newSamples = (bufferSize - lastPos) + writePos;
    }
    bandpassFilter.processNewSamples(input, output, writePos, newSamples);
  }
  bandpassFilter.lastProcessedPos = writePos;
}

} // namespace Butterworth

namespace ConstantQ {

struct CQTParameters {
  float f0;
  int binsPerOctave;
  int numBins;
  float sampleRate;
  std::vector<float> frequencies;
  std::vector<float> qFactors;
  std::vector<int> kernelLengths;
  std::vector<std::vector<float>> kernelReals;
  std::vector<std::vector<float>> kernelImags;
  float sampleRateInv;
  float twoPi;
  float maxFreq;
};

CQTParameters initializeCQT(float f0, int binsPerOctave, float sampleRate, float maxFreq = 20000.0f,
                            int maxKernelLength = 8192) {
  CQTParameters params;
  params.f0 = f0;
  params.binsPerOctave = binsPerOctave;
  params.sampleRate = sampleRate;
  params.maxFreq = maxFreq;
  params.sampleRateInv = 1.0f / sampleRate;
  params.twoPi = 2.0f * M_PI;
  float octaves = log2f(maxFreq / f0);
  params.numBins = static_cast<int>(ceil(octaves * binsPerOctave));
  params.numBins = std::max(1, std::min(params.numBins, 1000));
  params.frequencies.resize(params.numBins);
  params.qFactors.resize(params.numBins);
  params.kernelLengths.resize(params.numBins);
  params.kernelReals.resize(params.numBins);
  params.kernelImags.resize(params.numBins);
  float binRatio = 1.0f / binsPerOctave;
  float qBase = 1.0f / (powf(2.0f, binRatio) - 1.0f);
  for (int k = 0; k < params.numBins; k++) {
    params.frequencies[k] = f0 * powf(2.0f, static_cast<float>(k) * binRatio);
    params.qFactors[k] = qBase;
  }
  return params;
}

void generateMorletKernel(CQTParameters& params, int binIndex, int maxKernelLength) {
  float fc = params.frequencies[binIndex];
  float Q = params.qFactors[binIndex];
  float sigma = Q / (params.twoPi * fc);
  float twoSigmaSquared = 2.0f * sigma * sigma;
  float omega = params.twoPi * fc;
  float dt = params.sampleRateInv;
  int kernelLength = static_cast<int>(ceilf(6.0f * sigma / dt));
  if (kernelLength > maxKernelLength) {
    kernelLength = maxKernelLength;
    sigma = static_cast<float>(kernelLength) * dt / 6.0f;
    twoSigmaSquared = 2.0f * sigma * sigma;
  }
  if (kernelLength % 2 == 0)
    kernelLength++;
  params.kernelLengths[binIndex] = kernelLength;
  params.kernelReals[binIndex].resize(kernelLength);
  params.kernelImags[binIndex].resize(kernelLength);
  int center = kernelLength / 2;
  float normalization = 0.0f;
  float phase0 = -omega * center * dt;
  float cosPhase = cosf(phase0);
  float sinPhase = sinf(phase0);
  float cosDelta = cosf(omega * dt);
  float sinDelta = sinf(omega * dt);
  for (int n = 0; n < kernelLength; n++) {
    float t = static_cast<float>(n - center) * dt;
    float envelope = expf(-(t * t) / twoSigmaSquared);
    float real = envelope * cosPhase;
    float imag = envelope * sinPhase;
    params.kernelReals[binIndex][n] = real;
    params.kernelImags[binIndex][n] = imag;
    normalization += envelope;
    float newCos = cosPhase * cosDelta - sinPhase * sinDelta;
    float newSin = sinPhase * cosDelta + cosPhase * sinDelta;
    cosPhase = newCos;
    sinPhase = newSin;
  }
  if (normalization > 0.0f) {
    float amplitudeNorm = 1.0f / normalization;
    for (int n = 0; n < kernelLength; n++) {
      params.kernelReals[binIndex][n] *= amplitudeNorm;
      params.kernelImags[binIndex][n] *= amplitudeNorm;
    }
  }
}

void generateCQTKernels(CQTParameters& params, int maxKernelLength) {
  for (int k = 0; k < params.numBins; k++) {
    generateMorletKernel(params, k, maxKernelLength);
  }
}

#if defined(HAVE_AVX2)
static inline float avx2_reduce_add_ps(__m256 v) {
  __m128 vlow = _mm256_castps256_ps128(v);
  __m128 vhigh = _mm256_extractf128_ps(v, 1);
  vlow = _mm_add_ps(vlow, vhigh);
  __m128 shuf = _mm_movehdup_ps(vlow);
  __m128 sums = _mm_add_ps(vlow, shuf);
  shuf = _mm_movehl_ps(shuf, sums);
  sums = _mm_add_ss(sums, shuf);
  return _mm_cvtss_f32(sums);
}
#endif

template <typename Alloc>
void computeCQT(const CQTParameters& params, const std::vector<float, Alloc>& circularBuffer, size_t writePos,
                std::vector<float>& cqtMagnitudes) {
  size_t bufferSize = circularBuffer.size();
  cqtMagnitudes.resize(params.numBins);

  for (int k = 0; k < params.numBins; k++) {
    const auto& kernelReal = params.kernelReals[k];
    const auto& kernelImag = params.kernelImags[k];
    int kernelLength = params.kernelLengths[k];
    if (kernelLength > static_cast<int>(bufferSize)) {
      cqtMagnitudes[k] = bufferSize - 1;
      continue;
    }
    size_t startPos = (writePos + bufferSize - kernelLength) % bufferSize;
    float realSum = 0.0f;
    float imagSum = 0.0f;
#if defined(HAVE_AVX2)
    const float* bufPtr = &circularBuffer[startPos];
    const float* realPtr = kernelReal.data();
    const float* imagPtr = kernelImag.data();
    bool aligned = ((reinterpret_cast<uintptr_t>(bufPtr) % 32) == 0) &&
                   ((reinterpret_cast<uintptr_t>(realPtr) % 32) == 0) &&
                   ((reinterpret_cast<uintptr_t>(imagPtr) % 32) == 0);
    if (startPos + kernelLength <= bufferSize) {
      int n = 0;
      __m256 realSumVec = _mm256_setzero_ps();
      __m256 imagSumVec = _mm256_setzero_ps();
      if (aligned) {
        for (; n + 7 < kernelLength; n += 8) {
          __m256 sampleVec = _mm256_load_ps(bufPtr + n);
          __m256 realVec = _mm256_load_ps(realPtr + n);
          __m256 imagVec = _mm256_load_ps(imagPtr + n);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      } else {
        for (; n + 7 < kernelLength; n += 8) {
          __m256 sampleVec = _mm256_loadu_ps(bufPtr + n);
          __m256 realVec = _mm256_loadu_ps(realPtr + n);
          __m256 imagVec = _mm256_loadu_ps(imagPtr + n);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      }
      float realSumArr[8], imagSumArr[8];
      _mm256_storeu_ps(realSumArr, realSumVec);
      _mm256_storeu_ps(imagSumArr, imagSumVec);
      for (int i = 0; i < 8; ++i) {
        realSum += realSumArr[i];
        imagSum += imagSumArr[i];
      }
      for (; n < kernelLength; ++n) {
        float sample = bufPtr[n];
        realSum += sample * realPtr[n];
        imagSum -= sample * imagPtr[n];
      }
    } else {
      size_t firstLen = bufferSize - startPos;
      size_t secondLen = kernelLength - firstLen;
      int n = 0;
      __m256 realSumVec = _mm256_setzero_ps();
      __m256 imagSumVec = _mm256_setzero_ps();
      const float* bufPtr1 = &circularBuffer[startPos];
      const float* realPtr1 = realPtr;
      const float* imagPtr1 = imagPtr;
      bool aligned1 = ((reinterpret_cast<uintptr_t>(bufPtr1) % 32) == 0) &&
                      ((reinterpret_cast<uintptr_t>(realPtr1) % 32) == 0) &&
                      ((reinterpret_cast<uintptr_t>(imagPtr1) % 32) == 0);
      if (aligned1) {
        for (; n + 7 < firstLen; n += 8) {
          __m256 sampleVec = _mm256_load_ps(bufPtr1 + n);
          __m256 realVec = _mm256_load_ps(realPtr1 + n);
          __m256 imagVec = _mm256_load_ps(imagPtr1 + n);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      } else {
        for (; n + 7 < firstLen; n += 8) {
          __m256 sampleVec = _mm256_loadu_ps(bufPtr1 + n);
          __m256 realVec = _mm256_loadu_ps(realPtr1 + n);
          __m256 imagVec = _mm256_loadu_ps(imagPtr1 + n);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      }
      float realSumArr[8], imagSumArr[8];
      _mm256_storeu_ps(realSumArr, realSumVec);
      _mm256_storeu_ps(imagSumArr, imagSumVec);
      for (int i = 0; i < 8; ++i) {
        realSum += realSumArr[i];
        imagSum += imagSumArr[i];
      }
      for (; n < firstLen; ++n) {
        float sample = bufPtr1[n];
        realSum += sample * realPtr1[n];
        imagSum -= sample * imagPtr1[n];
      }
      int m = 0;
      realSumVec = _mm256_setzero_ps();
      imagSumVec = _mm256_setzero_ps();
      const float* bufPtr2 = &circularBuffer[0];
      const float* realPtr2 = realPtr + firstLen;
      const float* imagPtr2 = imagPtr + firstLen;
      bool aligned2 = ((reinterpret_cast<uintptr_t>(bufPtr2) % 32) == 0) &&
                      ((reinterpret_cast<uintptr_t>(realPtr2) % 32) == 0) &&
                      ((reinterpret_cast<uintptr_t>(imagPtr2) % 32) == 0);
      if (aligned2) {
        for (; m + 7 < secondLen; m += 8) {
          __m256 sampleVec = _mm256_load_ps(bufPtr2 + m);
          __m256 realVec = _mm256_load_ps(realPtr2 + m);
          __m256 imagVec = _mm256_load_ps(imagPtr2 + m);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      } else {
        for (; m + 7 < secondLen; m += 8) {
          __m256 sampleVec = _mm256_loadu_ps(bufPtr2 + m);
          __m256 realVec = _mm256_loadu_ps(realPtr2 + m);
          __m256 imagVec = _mm256_loadu_ps(imagPtr2 + m);
          realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
          imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
        }
      }
      realSum += avx2_reduce_add_ps(realSumVec);
      imagSum += avx2_reduce_add_ps(imagSumVec);
      for (; m < secondLen; ++m) {
        float sample = bufPtr2[m];
        realSum += sample * realPtr2[m];
        imagSum -= sample * imagPtr2[m];
      }
    }
#else
    for (int n = 0; n < kernelLength; n++) {
      size_t bufferIdx = (startPos + n) % bufferSize;
      float sample = circularBuffer[bufferIdx];
      realSum += sample * kernelReal[n];
      imagSum -= sample * kernelImag[n];
    }
#endif
    float magnitude = sqrtf(realSum * realSum + imagSum * imagSum);
    cqtMagnitudes[k] = magnitude * 2.0f;
  }
}

} // namespace ConstantQ

// Utility function to (re)allocate FFTW buffers and plans in the main audio thread
void recreateFFTWPlans(AudioData* audioData, int fftSize) {
  std::lock_guard<std::mutex> lock(audioData->fftPlanMutexMid);
  std::lock_guard<std::mutex> lockSide(audioData->fftPlanMutexSide);
  if (audioData->fftPlanMid) {
    fftwf_destroy_plan(audioData->fftPlanMid);
    audioData->fftPlanMid = nullptr;
  }
  if (audioData->fftPlanSide) {
    fftwf_destroy_plan(audioData->fftPlanSide);
    audioData->fftPlanSide = nullptr;
  }
  if (audioData->fftInMid) {
    fftwf_free(audioData->fftInMid);
    audioData->fftInMid = nullptr;
  }
  if (audioData->fftInSide) {
    fftwf_free(audioData->fftInSide);
    audioData->fftInSide = nullptr;
  }
  if (audioData->fftOutMid) {
    fftwf_free(audioData->fftOutMid);
    audioData->fftOutMid = nullptr;
  }
  if (audioData->fftOutSide) {
    fftwf_free(audioData->fftOutSide);
    audioData->fftOutSide = nullptr;
  }
  audioData->fftInMid = fftwf_alloc_real(fftSize);
  audioData->fftInSide = fftwf_alloc_real(fftSize);
  audioData->fftOutMid = fftwf_alloc_complex(fftSize / 2 + 1);
  audioData->fftOutSide = fftwf_alloc_complex(fftSize / 2 + 1);
  audioData->fftPlanMid = fftwf_plan_dft_r2c_1d(fftSize, audioData->fftInMid, audioData->fftOutMid, FFTW_ESTIMATE);
  audioData->fftPlanSide = fftwf_plan_dft_r2c_1d(fftSize, audioData->fftInSide, audioData->fftOutSide, FFTW_ESTIMATE);
}

void audioThread(AudioData* audioData) {
  AudioThreadState state;

  // Initialize audio engine
  auto preferredEngineStr = Config::values().audio.engine;
  auto preferredEngine = AudioEngine::stringToType(preferredEngineStr);

  auto audioEngine = AudioEngine::createBestAvailableEngine(preferredEngine);
  if (!audioEngine) {
    std::cerr << "Failed to create audio engine!" << std::endl;
    return;
  }

  // Get engine-specific device name
  std::string deviceName = Config::values().audio.device;

  if (!audioEngine->initialize(deviceName, audioData)) {
    std::cerr << "Failed to initialize audio engine: " << audioEngine->getLastError() << std::endl;
    return;
  }

  std::cout << "Using audio engine: " << AudioEngine::typeToString(audioEngine->getType()) << std::endl;

  // Track config version for live updates
  size_t lastConfigVersion = Config::getVersion();
  std::string lastEngineTypeStr = preferredEngineStr;
  std::string lastDeviceName = deviceName;
  uint32_t lastSampleRate = audioEngine->getSampleRate();

  // Determine read size based on fps limit
  int bufferFrames = audioEngine->getSampleRate() / Config::values().window.fps_limit;
  uint32_t lastBufferSize = bufferFrames;
  int lastFftSize = Config::values().fft.size;
  bool lastEnableCqt = Config::values().fft.enable_cqt;

  const int READ_SAMPLES = bufferFrames * 2;
  std::vector<float> readBuffer(READ_SAMPLES);

  // Initialize FFTW plans
  recreateFFTWPlans(audioData, lastFftSize);

  // initialize fftw in outs
  if (!audioData->fftInMid || !audioData->fftInSide || !audioData->fftOutMid || !audioData->fftOutSide ||
      !audioData->fftPlanMid || !audioData->fftPlanSide) {
    std::cerr << "Failed to allocate FFTW buffers!" << std::endl;
  }

  state.lastFrameTime = std::chrono::steady_clock::now();

  while (audioData->running && audioEngine->isRunning()) {
    const auto& audio_cfg = Config::values().audio;
    const auto& fft_cfg = Config::values().fft;
    const auto& osc_cfg = Config::values().oscilloscope;
    const auto& bandpass_cfg = Config::values().bandpass_filter;
    audioData->sampleRate = audioEngine->getSampleRate();

    // Check for config changes that require audio engine reconfiguration
    size_t currentConfigVersion = Config::getVersion();
    if (currentConfigVersion != lastConfigVersion) {
      lastConfigVersion = currentConfigVersion;

      // Check if audio engine type has changed
      std::string currentEngineStr = Config::values().audio.engine;
      auto currentEngine = AudioEngine::stringToType(currentEngineStr);

      // Get current device name
      std::string currentDeviceName = Config::values().audio.device;

      // Get current audio settings
      uint32_t currentSampleRate = audioEngine->getSampleRate();

      // Get current buffer size from fps limit
      uint32_t currentBufferSize = audioEngine->getSampleRate() / Config::values().window.fps_limit;
      if (currentBufferSize <= 0) {
        currentBufferSize = 512;
      }

      bool engineTypeChanged = (currentEngine != audioEngine->getType());
      bool needsReconfiguration =
          audioEngine->needsReconfiguration(currentDeviceName, currentSampleRate, currentBufferSize);

      if (engineTypeChanged || needsReconfiguration) {
        // Clean up current engine
        audioEngine->cleanup(audioData);

        if (engineTypeChanged) {
          // Create new engine if type changed
          audioEngine = AudioEngine::createBestAvailableEngine(currentEngine);
          if (!audioEngine) {
            std::cerr << "Failed to create new audio engine!" << std::endl;
            return;
          }

          // Update device name for new engine type
          currentDeviceName = Config::values().audio.device;
        }

        // Initialize with new configuration
        if (!audioEngine->initialize(currentDeviceName, audioData)) {
          std::cerr << "Failed to reinitialize audio engine: " << audioEngine->getLastError() << std::endl;
          return;
        }

        // Update buffer sizes if they changed
        if (currentBufferSize != lastBufferSize) {
          bufferFrames = currentBufferSize;
          const int newReadSamples = bufferFrames * audioEngine->getChannels();
          readBuffer.resize(newReadSamples);
        }

        // Update tracking variables
        lastEngineTypeStr = currentEngineStr;
        lastDeviceName = currentDeviceName;
        lastSampleRate = audioEngine->getSampleRate();
        lastBufferSize = currentBufferSize;
      }

      int currentFftSize = Config::values().fft.size;
      bool currentEnableCqt = Config::values().fft.enable_cqt;
      // Only recreate FFTW plans if switching from CQT to FFT, or if FFT size changed while CQT is disabled
      if (!currentEnableCqt && (lastEnableCqt || currentFftSize != lastFftSize)) {
        recreateFFTWPlans(audioData, currentFftSize);
        lastFftSize = currentFftSize;
      }
      lastEnableCqt = currentEnableCqt;
    }

    // Update READ_SAMPLES based on current engine
    const int READ_SAMPLES = bufferFrames * 2;

    if (!audioEngine->readAudio(readBuffer.data(), bufferFrames)) {
      std::cerr << "Failed to read from audio engine: " << audioEngine->getLastError() << std::endl;
      break;
    }

    // Linearize gain
    float gain = powf(10.0f, audio_cfg.gain_db / 20.0f);

    // Write to circular buffer if pulseaudio
    if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
      for (int i = 0; i < READ_SAMPLES; i += 2) {
        float sampleLeft = readBuffer[i] * gain;
        float sampleRight = readBuffer[i + 1] * gain;

        // Write to circular buffer
        audioData->bufferMid[audioData->writePos] = (sampleLeft + sampleRight) / 2.0f;
        audioData->bufferSide[audioData->writePos] = (sampleLeft - sampleRight) / 2.0f;
        audioData->writePos = (audioData->writePos + 1) % audioData->bufferSize;
      }
    }

    // Signal FFT/CQT threads to start processing
    {
      std::lock_guard<std::mutex> lock(audioData->fftMutex);
      audioData->fftReadyMid = true;
      audioData->fftCvMid.notify_one();
      audioData->fftReadySide = true;
      audioData->fftCvSide.notify_one();
    }

    // Apply bandpass filter using the most recent pitch (may be 1 frame out of date)
    if (audioData->pitchConfidence > 0.0f) {
      float pitch = audioData->currentPitch;
      float bandwidth = bandpass_cfg.bandwidth;
      if (bandpass_cfg.bandwidth_type == "percent") {
        bandwidth = pitch * bandwidth / 100.0f;
      }
      Butterworth::applyBandpassCircular(audioData->bufferMid, audioData->bandpassedMid, pitch, audioData->sampleRate,
                                         audioData->writePos, bandwidth, bandpass_cfg.order);
    }

    // New data is available, signal main thread to redraw
    {
      std::lock_guard<std::mutex> lock(audioData->audioMutex);
      audioData->dataReady = true;
    }
    audioData->audioCv.notify_one();
  }

  audioEngine->cleanup(audioData);
}

// --- Mid channel FFT/CQT thread ---
void FFT_CQTThreadMid(AudioData* audioData) {
  AudioThreadState state;
  int FFT_SIZE = Config::values().fft.size;
  // Use main-thread-allocated buffers and plans
  int lastFftSize = FFT_SIZE;

  ConstantQ::CQTParameters cqtParams =
      ConstantQ::initializeCQT(Config::values().fft.min_freq, Config::values().fft.cqt_bins_per_octave,
                               audioData->sampleRate, Config::values().fft.max_freq, audioData->bufferSize);
  ConstantQ::generateCQTKernels(cqtParams, Config::values().fft.size);

  // Initialize CQT data vectors in AudioData (if needed)
  {
    audioData->cqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->prevCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->smoothedCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->interpolatedCqtValuesMid.resize(cqtParams.numBins, 0.0f);
    audioData->cqtFrequencies = cqtParams.frequencies;
  }

  while (audioData->running) {
    std::unique_lock<std::mutex> lock(audioData->fftMutex);
    audioData->fftCvMid.wait(lock, [&] { return audioData->fftReadyMid.load() || !audioData->running; });
    if (!audioData->running)
      break;
    audioData->fftReadyMid = false;
    lock.unlock();

    const auto& fft_cfg = Config::values().fft;
    int sampleRate = audioData->sampleRate;
    float minFreq = fft_cfg.min_freq;
    float maxFreq = fft_cfg.max_freq;

    int currentFftSize = Config::values().fft.size;
    if (currentFftSize != lastFftSize) {
      // Only update local FFT_SIZE and skip plan/buffer management
      FFT_SIZE = currentFftSize;
      lastFftSize = FFT_SIZE;
      // Resize display vectors as before
      const size_t fftBins = FFT_SIZE / 2 + 1;
      audioData->fftMagnitudesMid.resize(fftBins, 0.0f);
      audioData->prevFftMagnitudesMid.resize(fftBins, 0.0f);
      audioData->smoothedMagnitudesMid.resize(fftBins, 0.0f);
      audioData->interpolatedValuesMid.resize(fftBins, 0.0f);
    }

    float currentMinFreq = fft_cfg.min_freq;
    float currentMaxFreq = fft_cfg.max_freq;
    int currentCqtBinsPerOctave = fft_cfg.cqt_bins_per_octave;
    if (sampleRate != cqtParams.sampleRate || currentMinFreq != cqtParams.f0 || currentMaxFreq != cqtParams.maxFreq ||
        currentCqtBinsPerOctave != cqtParams.binsPerOctave) {
      cqtParams = ConstantQ::initializeCQT(currentMinFreq, currentCqtBinsPerOctave, sampleRate, currentMaxFreq,
                                           audioData->bufferSize);
      ConstantQ::generateCQTKernels(cqtParams, Config::values().fft.size);
      audioData->cqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
      audioData->prevCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
      audioData->smoothedCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
      audioData->interpolatedCqtValuesMid.resize(cqtParams.numBins, 0.0f);
      audioData->cqtFrequencies = cqtParams.frequencies;
    }

    // FFT and CQT calculations for mid channel only
    if (!fft_cfg.enable_cqt) {
      size_t startPos = (audioData->writePos + audioData->bufferSize - FFT_SIZE) % audioData->bufferSize;
      for (int i = 0; i < FFT_SIZE; i++) {
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->bufferSize;
        if (fft_cfg.stereo_mode == "leftright") {
          audioData->fftInMid[i] = (audioData->bufferSide[pos] - audioData->bufferMid[pos]) * 0.5f * window;
        } else {
          audioData->fftInMid[i] = audioData->bufferMid[pos] * window;
        }
      }
      std::lock_guard<std::mutex> lock(audioData->fftPlanMutexMid);
      if (audioData->fftPlanMid) {
        fftwf_execute(audioData->fftPlanMid);
      }
      audioData->prevFftMagnitudesMid = audioData->fftMagnitudesMid;
      const float fftScale = 2.0f / FFT_SIZE;
      for (int i = 0; i < FFT_SIZE / 2 + 1; i++) {
        float mag = sqrt(audioData->fftOutMid[i][0] * audioData->fftOutMid[i][0] +
                         audioData->fftOutMid[i][1] * audioData->fftOutMid[i][1]) *
                    fftScale;
        if (i != 0 && i != FFT_SIZE / 2)
          mag *= 2.0f;
        audioData->fftMagnitudesMid[i] = mag;
      }
    }
    if (fft_cfg.enable_cqt) {
      audioData->prevCqtMagnitudesMid = audioData->cqtMagnitudesMid;
      auto cqt_start = std::chrono::high_resolution_clock::now();
      ConstantQ::computeCQT(cqtParams, audioData->bufferMid, audioData->writePos, audioData->cqtMagnitudesMid);
      auto cqt_end = std::chrono::high_resolution_clock::now();
    }

    // Pitch detection for mid channel only
    float maxMagnitude = 0.0f;
    float peakDb = -100.0f;
    float peakFreq = 0.0f;
    int peakBin = 0;
    float avgMagnitude = 0.0f;

    if (fft_cfg.enable_cqt) {
      const auto& magnitudes = audioData->cqtMagnitudesMid;
      const auto& frequencies = cqtParams.frequencies;
      int minBin = 0, maxBin = magnitudes.size() - 1;
      for (int i = 0; i < magnitudes.size(); i++) {
        if (frequencies[i] >= minFreq) {
          minBin = i;
          break;
        }
      }
      for (int i = minBin; i < magnitudes.size(); i++) {
        if (frequencies[i] > maxFreq) {
          maxBin = i - 1;
          break;
        }
      }
      for (int i = minBin; i <= maxBin; i++) {
        float magnitude = magnitudes[i];
        float dB = 20.0f * log10f(magnitude + 1e-9f);
        if (magnitude > maxMagnitude) {
          maxMagnitude = magnitude;
          peakBin = i;
        }
        if (dB > peakDb) {
          peakDb = dB;
        }
        avgMagnitude += magnitude;
      }
      avgMagnitude /= (maxBin - minBin + 1);
      if (peakBin > 0 && peakBin < frequencies.size() - 1) {
        float y1 = magnitudes[peakBin - 1];
        float y2 = magnitudes[peakBin];
        float y3 = magnitudes[peakBin + 1];
        float denominator = y1 - 2.0f * y2 + y3;
        if (std::abs(denominator) > 1e-9f) {
          float offset = 0.5f * (y1 - y3) / denominator;
          offset = std::max(-0.5f, std::min(0.5f, offset));
          float logFreq1 = log2f(frequencies[peakBin - 1]);
          float logFreq2 = log2f(frequencies[peakBin]);
          float logFreq3 = log2f(frequencies[peakBin + 1]);
          float interpolatedLogFreq = logFreq2 + offset * (logFreq3 - logFreq1) * 0.5f;
          peakFreq = powf(2.0f, interpolatedLogFreq);
        } else {
          peakFreq = frequencies[peakBin];
        }
      } else if (peakBin >= 0 && peakBin < frequencies.size()) {
        peakFreq = frequencies[peakBin];
      }
    } else {
      const int fftSize = (audioData->fftMagnitudesMid.size() - 1) * 2;
      int minBin = static_cast<int>(minFreq * fftSize / sampleRate);
      int maxBin = static_cast<int>(maxFreq * fftSize / sampleRate);
      minBin = std::max(1, minBin);
      maxBin = std::min((int)audioData->fftMagnitudesMid.size() - 1, maxBin);
      for (int i = minBin; i <= maxBin; i++) {
        float magnitude = audioData->fftMagnitudesMid[i];
        float dB = 20.0f * log10f(magnitude + 1e-9f);
        if (magnitude > maxMagnitude) {
          maxMagnitude = magnitude;
          peakBin = i;
        }
        if (dB > peakDb) {
          peakDb = dB;
        }
        avgMagnitude += magnitude;
      }
      avgMagnitude /= (maxBin - minBin + 1);
      if (peakBin > 0 && peakBin < audioData->fftMagnitudesMid.size() - 1) {
        float y1 = audioData->fftMagnitudesMid[peakBin - 1];
        float y2 = audioData->fftMagnitudesMid[peakBin];
        float y3 = audioData->fftMagnitudesMid[peakBin + 1];
        float denominator = y1 - 2.0f * y2 + y3;
        if (std::abs(denominator) > 1e-9f) {
          float offset = 0.5f * (y1 - y3) / denominator;
          offset = std::max(-0.5f, std::min(0.5f, offset));
          float interpolatedBin = peakBin + offset;
          peakFreq = interpolatedBin * sampleRate / fftSize;
        } else {
          peakFreq = static_cast<float>(peakBin * sampleRate) / fftSize;
        }
      }
    }
    audioData->peakFreq = peakFreq;
    audioData->peakDb = peakDb;
    audioData->hasValidPeak = (peakDb > Config::values().audio.silence_threshold);
    static const char* noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    const char* const* noteNames = (fft_cfg.note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;
    freqToNote(peakFreq, audioData->peakNote, audioData->peakOctave, audioData->peakCents, noteNames);
    if (audioData->hasValidPeak) {
      audioData->currentPitch = peakFreq;
      float confidence = maxMagnitude / (avgMagnitude + 1e-6f);
      audioData->pitchConfidence = std::min(1.0f, confidence / 3.0f);
      audioData->samplesPerCycle = static_cast<float>(sampleRate) / peakFreq;
    } else {
      audioData->pitchConfidence *= 0.9f;
    }

    // Smoothing/interpolation for mid channel only
    auto currentFrameTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(currentFrameTime - state.lastFrameTime).count();
    state.lastFrameTime = currentFrameTime;
    if (audioData->fftUpdateInterval > 0.0f) {
      float timeSinceUpdate = std::chrono::duration<float>(currentFrameTime - audioData->lastFftUpdate).count();
      float targetInterpolation = timeSinceUpdate / audioData->fftUpdateInterval;
      state.fftInterpolation =
          state.fftInterpolation + (targetInterpolation - state.fftInterpolation) * fft_cfg.smoothing_factor;
      state.fftInterpolation = std::min(1.0f, state.fftInterpolation);
    }
    if (!fft_cfg.enable_cqt) {
#if defined(HAVE_AVX2)
      size_t n = 0;
      size_t N = audioData->fftMagnitudesMid.size();
      float interp = state.fftInterpolation;
      float one_minus_interp = 1.0f - interp;
      __m256 interp_vec = _mm256_set1_ps(interp);
      __m256 one_minus_interp_vec = _mm256_set1_ps(one_minus_interp);
      for (; n + 7 < N; n += 8) {
        __m256 prev = _mm256_loadu_ps(&audioData->prevFftMagnitudesMid[n]);
        __m256 curr = _mm256_loadu_ps(&audioData->fftMagnitudesMid[n]);
        __m256 res = _mm256_add_ps(_mm256_mul_ps(one_minus_interp_vec, prev), _mm256_mul_ps(interp_vec, curr));
        _mm256_storeu_ps(&audioData->interpolatedValuesMid[n], res);
      }
      for (; n < N; ++n) {
        audioData->interpolatedValuesMid[n] =
            one_minus_interp * audioData->prevFftMagnitudesMid[n] + interp * audioData->fftMagnitudesMid[n];
      }
#else
      for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
        audioData->interpolatedValuesMid[i] = (1.0f - state.fftInterpolation) * audioData->prevFftMagnitudesMid[i] +
                                              state.fftInterpolation * audioData->fftMagnitudesMid[i];
      }
#endif
    }
    if (fft_cfg.enable_cqt) {
#if defined(HAVE_AVX2)
      size_t n = 0;
      size_t N = audioData->cqtMagnitudesMid.size();
      float interp = state.fftInterpolation;
      float one_minus_interp = 1.0f - interp;
      __m256 interp_vec = _mm256_set1_ps(interp);
      __m256 one_minus_interp_vec = _mm256_set1_ps(one_minus_interp);
      for (; n + 7 < N; n += 8) {
        __m256 prev = _mm256_loadu_ps(&audioData->prevCqtMagnitudesMid[n]);
        __m256 curr = _mm256_loadu_ps(&audioData->cqtMagnitudesMid[n]);
        __m256 res = _mm256_add_ps(_mm256_mul_ps(one_minus_interp_vec, prev), _mm256_mul_ps(interp_vec, curr));
        _mm256_storeu_ps(&audioData->interpolatedCqtValuesMid[n], res);
      }
      for (; n < N; ++n) {
        audioData->interpolatedCqtValuesMid[n] =
            one_minus_interp * audioData->prevCqtMagnitudesMid[n] + interp * audioData->cqtMagnitudesMid[n];
      }
#else
      for (size_t i = 0; i < audioData->cqtMagnitudesMid.size(); ++i) {
        audioData->interpolatedCqtValuesMid[i] = (1.0f - state.fftInterpolation) * audioData->prevCqtMagnitudesMid[i] +
                                                 state.fftInterpolation * audioData->cqtMagnitudesMid[i];
      }
#endif
    }
    if (!fft_cfg.enable_cqt) {
      for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
        float current = audioData->interpolatedValuesMid[i];
        float previous = audioData->smoothedMagnitudesMid[i];
        float freq = (float)i * sampleRate / (audioData->fftMagnitudesMid.size() - 1) / 2;
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedMagnitudesMid[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
    }
    if (fft_cfg.enable_cqt) {
      for (size_t i = 0; i < audioData->cqtMagnitudesMid.size(); ++i) {
        float current = audioData->interpolatedCqtValuesMid[i];
        float previous = audioData->smoothedCqtMagnitudesMid[i];
        float freq = audioData->cqtFrequencies[i];
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedCqtMagnitudesMid[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
    }
  }
}

void FFT_CQTThreadSide(AudioData* audioData) {
  AudioThreadState state;
  int FFT_SIZE = Config::values().fft.size;
  // Use main-thread-allocated buffers and plans
  int lastFftSize = FFT_SIZE;

  ConstantQ::CQTParameters cqtParams =
      ConstantQ::initializeCQT(Config::values().fft.min_freq, Config::values().fft.cqt_bins_per_octave,
                               audioData->sampleRate, Config::values().fft.max_freq, audioData->bufferSize);
  ConstantQ::generateCQTKernels(cqtParams, Config::values().fft.size);

  // Initialize CQT data vectors in AudioData (if needed)
  {
    audioData->cqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->prevCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->smoothedCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->interpolatedCqtValuesSide.resize(cqtParams.numBins, 0.0f);
    audioData->cqtFrequencies = cqtParams.frequencies;
  }

  while (audioData->running) {
    std::unique_lock<std::mutex> lock(audioData->fftMutex);
    audioData->fftCvSide.wait(lock, [&] { return audioData->fftReadySide.load() || !audioData->running; });
    if (!audioData->running)
      break;
    audioData->fftReadySide = false;
    lock.unlock();

    // skip if phosphor is enabled
    if (Config::values().fft.enable_phosphor) {
      continue;
    }

    const auto& fft_cfg = Config::values().fft;
    int sampleRate = audioData->sampleRate;
    float minFreq = fft_cfg.min_freq;
    float maxFreq = fft_cfg.max_freq;

    int currentFftSize = Config::values().fft.size;
    if (currentFftSize != lastFftSize) {
      // Only update local FFT_SIZE and skip plan/buffer management
      FFT_SIZE = currentFftSize;
      lastFftSize = FFT_SIZE;
      // Resize display vectors as before
      const size_t fftBins = FFT_SIZE / 2 + 1;
      audioData->fftMagnitudesSide.resize(fftBins, 0.0f);
      audioData->prevFftMagnitudesSide.resize(fftBins, 0.0f);
      audioData->smoothedMagnitudesSide.resize(fftBins, 0.0f);
      audioData->interpolatedValuesSide.resize(fftBins, 0.0f);
    }

    float currentMinFreq = fft_cfg.min_freq;
    float currentMaxFreq = fft_cfg.max_freq;
    int currentCqtBinsPerOctave = fft_cfg.cqt_bins_per_octave;
    if (sampleRate != cqtParams.sampleRate || currentMinFreq != cqtParams.f0 || currentMaxFreq != cqtParams.maxFreq ||
        currentCqtBinsPerOctave != cqtParams.binsPerOctave) {
      cqtParams = ConstantQ::initializeCQT(currentMinFreq, currentCqtBinsPerOctave, sampleRate, currentMaxFreq,
                                           audioData->bufferSize);
      ConstantQ::generateCQTKernels(cqtParams, Config::values().fft.size);
      audioData->cqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
      audioData->prevCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
      audioData->smoothedCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
      audioData->interpolatedCqtValuesSide.resize(cqtParams.numBins, 0.0f);
      audioData->cqtFrequencies = cqtParams.frequencies;
    }

    // FFT and CQT calculations for side channel only
    if (!fft_cfg.enable_cqt) {
      size_t startPos = (audioData->writePos + audioData->bufferSize - FFT_SIZE) % audioData->bufferSize;
      for (int i = 0; i < FFT_SIZE; i++) {
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->bufferSize;
        if (fft_cfg.stereo_mode == "leftright") {
          audioData->fftInSide[i] = (audioData->bufferMid[pos] - audioData->bufferSide[pos]) * 0.5f * window;
        } else {
          audioData->fftInSide[i] = audioData->bufferSide[pos] * window;
        }
      }
      std::lock_guard<std::mutex> lock(audioData->fftPlanMutexSide);
      if (audioData->fftPlanSide) {
        fftwf_execute(audioData->fftPlanSide);
      }
      audioData->prevFftMagnitudesSide = audioData->fftMagnitudesSide;
      const float fftScale = 2.0f / FFT_SIZE;
      for (int i = 0; i < FFT_SIZE / 2 + 1; i++) {
        float mag = sqrt(audioData->fftOutSide[i][0] * audioData->fftOutSide[i][0] +
                         audioData->fftOutSide[i][1] * audioData->fftOutSide[i][1]) *
                    fftScale;
        if (i != 0 && i != FFT_SIZE / 2)
          mag *= 2.0f;
        audioData->fftMagnitudesSide[i] = mag;
      }
    }
    if (fft_cfg.enable_cqt) {
      audioData->prevCqtMagnitudesSide = audioData->cqtMagnitudesSide;
      ConstantQ::computeCQT(cqtParams, audioData->bufferSide, audioData->writePos, audioData->cqtMagnitudesSide);
    }

    // Smoothing/interpolation for side channel only
    auto currentFrameTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(currentFrameTime - state.lastFrameTime).count();
    state.lastFrameTime = currentFrameTime;
    if (audioData->fftUpdateInterval > 0.0f) {
      float timeSinceUpdate = std::chrono::duration<float>(currentFrameTime - audioData->lastFftUpdate).count();
      float targetInterpolation = timeSinceUpdate / audioData->fftUpdateInterval;
      state.fftInterpolation =
          state.fftInterpolation + (targetInterpolation - state.fftInterpolation) * fft_cfg.smoothing_factor;
      state.fftInterpolation = std::min(1.0f, state.fftInterpolation);
    }
    if (!fft_cfg.enable_cqt) {
#if defined(HAVE_AVX2)
      size_t N = audioData->fftMagnitudesSide.size();
      size_t n = 0;
      for (; n + 7 < N; n += 8) {
        __m256 current = _mm256_loadu_ps(&audioData->interpolatedValuesSide[n]);
        __m256 previous = _mm256_loadu_ps(&audioData->smoothedMagnitudesSide[n]);
        __m256 currentDb =
            _mm256_mul_ps(_mm256_set1_ps(20.0f), _mm256_log10_ps(_mm256_add_ps(current, _mm256_set1_ps(1e-9f))));
        __m256 previousDb =
            _mm256_mul_ps(_mm256_set1_ps(20.0f), _mm256_log10_ps(_mm256_add_ps(previous, _mm256_set1_ps(1e-9f))));
        __m256 diff = _mm256_sub_ps(currentDb, previousDb);
        __m256 speed = _mm256_set1_ps(fft_cfg.fall_speed);
        __m256 mask = _mm256_cmp_ps(diff, _mm256_setzero_ps(), _CMP_GT_OS);
        speed = _mm256_blendv_ps(speed, _mm256_set1_ps(fft_cfg.rise_speed), mask);
        __m256 maxChange = _mm256_mul_ps(speed, _mm256_set1_ps(dt));
        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), diff);
        __m256 change = _mm256_min_ps(absDiff, maxChange);
        change = _mm256_blendv_ps(_mm256_sub_ps(_mm256_setzero_ps(), change), change, mask);
        __m256 newDb = _mm256_add_ps(previousDb, change);
        __m256 newMag = _mm256_sub_ps(_mm256_pow_ps(_mm256_set1_ps(10.0f), _mm256_div_ps(newDb, _mm256_set1_ps(20.0f))),
                                      _mm256_set1_ps(1e-9f));
        _mm256_storeu_ps(&audioData->smoothedMagnitudesSide[n], newMag);
      }
      for (; n < N; ++n) {
        float current = audioData->interpolatedValuesSide[n];
        float previous = audioData->smoothedMagnitudesSide[n];
        float fftSize = (audioData->fftMagnitudesSide.size() - 1) * 2;
        float freq = (float)n * sampleRate / fftSize;
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedMagnitudesSide[n] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
#else
      for (size_t i = 0; i < audioData->fftMagnitudesSide.size(); ++i) {
        float current = audioData->interpolatedValuesSide[i];
        float previous = audioData->smoothedMagnitudesSide[i];
        float fftSize = (audioData->fftMagnitudesSide.size() - 1) * 2;
        float freq = (float)i * sampleRate / fftSize;
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
#endif
    }
    if (fft_cfg.enable_cqt) {
#if defined(HAVE_AVX2)
      size_t N = audioData->cqtMagnitudesSide.size();
      size_t n = 0;
      for (; n + 7 < N; n += 8) {
        __m256 current = _mm256_loadu_ps(&audioData->interpolatedCqtValuesSide[n]);
        __m256 previous = _mm256_loadu_ps(&audioData->smoothedCqtMagnitudesSide[n]);
        __m256 currentDb =
            _mm256_mul_ps(_mm256_set1_ps(20.0f), _mm256_log10_ps(_mm256_add_ps(current, _mm256_set1_ps(1e-9f))));
        __m256 previousDb =
            _mm256_mul_ps(_mm256_set1_ps(20.0f), _mm256_log10_ps(_mm256_add_ps(previous, _mm256_set1_ps(1e-9f))));
        __m256 diff = _mm256_sub_ps(currentDb, previousDb);
        __m256 speed = _mm256_set1_ps(fft_cfg.fall_speed);
        __m256 mask = _mm256_cmp_ps(diff, _mm256_setzero_ps(), _CMP_GT_OS);
        speed = _mm256_blendv_ps(speed, _mm256_set1_ps(fft_cfg.rise_speed), mask);
        __m256 maxChange = _mm256_mul_ps(speed, _mm256_set1_ps(dt));
        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), diff);
        __m256 change = _mm256_min_ps(absDiff, maxChange);
        change = _mm256_blendv_ps(_mm256_sub_ps(_mm256_setzero_ps(), change), change, mask);
        __m256 newDb = _mm256_add_ps(previousDb, change);
        __m256 newMag = _mm256_sub_ps(_mm256_pow_ps(_mm256_set1_ps(10.0f), _mm256_div_ps(newDb, _mm256_set1_ps(20.0f))),
                                      _mm256_set1_ps(1e-9f));
        _mm256_storeu_ps(&audioData->smoothedCqtMagnitudesSide[n], newMag);
      }
      for (; n < N; ++n) {
        float current = audioData->interpolatedCqtValuesSide[n];
        float previous = audioData->smoothedCqtMagnitudesSide[n];
        float freq = audioData->cqtFrequencies[n];
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedCqtMagnitudesSide[n] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
#else
      for (size_t i = 0; i < audioData->cqtMagnitudesSide.size(); ++i) {
        float current = audioData->interpolatedCqtValuesSide[i];
        float previous = audioData->smoothedCqtMagnitudesSide[i];
        float freq = audioData->cqtFrequencies[i];
        if (freq < minFreq || freq > maxFreq)
          continue;
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? fft_cfg.hover_fall_speed : fft_cfg.fall_speed;
        float speed = (diff > 0) ? fft_cfg.rise_speed : fallSpeed;
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;
        audioData->smoothedCqtMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
#endif
    }
  }
}

void freqToNote(float freq, std::string& noteName, int& octave, int& cents, const char* const* noteNames) {
  if (freq <= 0.0f) {
    noteName = "-";
    octave = 0;
    cents = 0;
    return;
  }

  // Calculate MIDI note number (A4 = 440Hz = MIDI 69)
  float midi = 69.0f + 12.0f * log2f(freq / 440.0f);

  // Handle out of range values
  if (midi < 0.0f || midi > 127.0f) {
    noteName = "-";
    octave = 0;
    cents = 0;
    return;
  }

  int midiInt = (int)roundf(midi);
  int noteIdx = (midiInt + 1200) % 12; // +1200 for negative safety
  octave = midiInt / 12 - 1;
  noteName = noteNames[noteIdx];
  cents = (int)roundf((midi - midiInt) * 100.0f);
}

} // namespace AudioProcessing
