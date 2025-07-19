#include "audio_processing.hpp"

#include "audio_engine.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fftw3.h>
#include <iostream>
#include <memory>

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
};

// Helper for pre-warping frequencies for bilinear transform
inline float prewarp(float f, float fs) { return tanf(M_PI * f / fs) / M_PI; }

// Calculate group delay for a Butterworth bandpass filter
float calculateGroupDelay(float centerFreq, float sampleRate, float bandwidth, int order) {
  float w0 = 2.0f * M_PI * centerFreq / sampleRate;
  float BW = 2.0f * M_PI * bandwidth / sampleRate;
  float Q = centerFreq / bandwidth;

  // For the specific Butterworth bandpass filter implementation used here,
  // the group delay is approximately proportional to the filter order
  // and inversely proportional to the bandwidth
  // An accurate approximation for this filter design:
  float groupDelay = static_cast<float>(order) / (4.0f * M_PI * bandwidth) * 0.15f;

  // Convert to samples
  return groupDelay * sampleRate;
}

// Designs a Butterworth bandpass filter and returns the biquad sections
std::vector<Biquad> designButterworthBandpass(int order, float centerFreq, float sampleRate, float bandwidth) {
  std::vector<Biquad> biquads;
  int nSections = order / 2;
  float w0 = 2.0f * M_PI * centerFreq / sampleRate;
  float BW = 2.0f * M_PI * bandwidth / sampleRate;
  float Q = centerFreq / bandwidth;

  // Butterworth pole placement
  for (int k = 0; k < nSections; ++k) {
    float theta = M_PI * (2.0f * k + 1.0f) / (2.0f * order);
    float poleReal = -sinf(theta);
    float poleImag = cosf(theta);

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

// Process a buffer through a single biquad section
void processBiquad(const std::vector<float>& in, std::vector<float>& out, Biquad& bq) {
  for (size_t i = 0; i < in.size(); ++i) {
    out[i] = bq.process(in[i]);
  }
}

// Process a circular buffer through a single biquad section in temporal order
void processBiquadCircular(const std::vector<float>& in, std::vector<float>& out, Biquad& bq, size_t writePos) {
  size_t bufferSize = in.size();

  // Process samples in temporal order starting from the oldest sample
  // The oldest sample is at writePos, newest is at (writePos - 1 + bufferSize) % bufferSize
  for (size_t i = 0; i < bufferSize; ++i) {
    size_t readPos = (writePos + i) % bufferSize;
    size_t outPos = (writePos + i) % bufferSize;
    out[outPos] = bq.process(in[readPos]);
  }
}

// Apply cascaded biquads to a circular buffer in temporal order
void applyBandpassCircular(const std::vector<float>& input, std::vector<float>& output, float centerFreq,
                           float sampleRate, size_t writePos, float bandwidth, int order) {
  if (input.empty()) {
    output.clear();
    return;
  }

  // Ensure output buffer is the same size as input
  if (output.size() != input.size()) {
    output.resize(input.size());
  }

  std::vector<Biquad> biquads = designButterworthBandpass(order, centerFreq, sampleRate, bandwidth);
  std::vector<float> temp1 = input;
  std::vector<float> temp2(input.size());

  for (auto& bq : biquads) {
    processBiquadCircular(temp1, temp2, bq, writePos);
    std::swap(temp1, temp2);
  }

  // Calculate group delay compensation
  float groupDelaySamples = calculateGroupDelay(centerFreq, sampleRate, bandwidth, order);
  int delayCompensation = static_cast<int>(roundf(groupDelaySamples));

  // Apply group delay compensation by shifting the output forward
  // This aligns the filtered signal with the original signal
  std::vector<float> compensatedOutput(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    size_t compensatedPos = (writePos + i + delayCompensation) % input.size();
    compensatedOutput[compensatedPos] = temp1[(writePos + i) % input.size()];
  }

  output = compensatedOutput;
}

} // namespace Butterworth

namespace ConstantQ {

struct CQTParameters {
  float f0;                                              // Lowest frequency
  int binsPerOctave;                                     // Number of bins per octave
  int numBins;                                           // Total number of CQT bins
  float sampleRate;                                      // Sample rate
  std::vector<float> frequencies;                        // Center frequencies for each bin
  std::vector<float> qFactors;                           // Q factor for each bin
  std::vector<std::vector<std::complex<float>>> kernels; // CQT filter kernels
  std::vector<int> kernelLengths;                        // Length of each kernel

  float sampleRateInv; // 1.0 / sampleRate
  float twoPi;         // 2 * PI
};

// Initialize CQT parameters and generate center frequencies
CQTParameters initializeCQT(float f0, int binsPerOctave, float sampleRate, float maxFreq = 20000.0f,
                            int maxKernelLength = 8192) {
  CQTParameters params;
  params.f0 = f0;
  params.binsPerOctave = binsPerOctave;
  params.sampleRate = sampleRate;

  params.sampleRateInv = 1.0f / sampleRate;
  params.twoPi = 2.0f * M_PI;

  // Calculate number of bins needed to reach maxFreq
  float octaves = log2f(maxFreq / f0);
  params.numBins = static_cast<int>(ceil(octaves * binsPerOctave));

  // Ensure we have at least one bin and don't exceed reasonable limits
  params.numBins = std::max(1, std::min(params.numBins, 1000));

  // Generate center frequencies logarithmically spaced
  params.frequencies.resize(params.numBins);
  params.qFactors.resize(params.numBins);
  params.kernels.resize(params.numBins);
  params.kernelLengths.resize(params.numBins);

  float binRatio = 1.0f / binsPerOctave;
  float qBase = 1.0f / (powf(2.0f, binRatio) - 1.0f);

  for (int k = 0; k < params.numBins; k++) {
    // Logarithmic frequency spacing
    params.frequencies[k] = f0 * powf(2.0f, static_cast<float>(k) * binRatio);

    params.qFactors[k] = qBase;
  }

  return params;
}

// Generate Complex Morlet wavelet kernel for a specific frequency bin
void generateMorletKernel(CQTParameters& params, int binIndex, int maxKernelLength) {
  float fc = params.frequencies[binIndex];
  float Q = params.qFactors[binIndex];

  float sigma = Q / (params.twoPi * fc);
  float twoSigmaSquared = 2.0f * sigma * sigma;
  float omega = params.twoPi * fc;

  // Kernel length based on 6 standard deviations (99.7% of energy)
  int kernelLength = static_cast<int>(ceilf(6.0f * sigma / params.sampleRateInv));

  if (kernelLength > maxKernelLength) {
    kernelLength = maxKernelLength;
    // Recalculate for capped length
    sigma = static_cast<float>(kernelLength) * params.sampleRateInv / 6.0f;
    twoSigmaSquared = 2.0f * sigma * sigma;
  }

  if (kernelLength % 2 == 0)
    kernelLength++; // Make odd for symmetry

  params.kernelLengths[binIndex] = kernelLength;
  params.kernels[binIndex].resize(kernelLength);

  int center = kernelLength / 2;
  float normalization = 0.0f;

  for (int n = 0; n < kernelLength; n++) {
    float t = static_cast<float>(n - center) * params.sampleRateInv;

    float tSquared = t * t;
    float envelope = expf(-tSquared / twoSigmaSquared);

    float phase = omega * t;
    float cosPhase = cosf(phase);
    float sinPhase = sinf(phase);

    // Store kernel value
    params.kernels[binIndex][n] = std::complex<float>(envelope * cosPhase, envelope * sinPhase);

    // Accumulate normalization factor (sum of magnitudes for amplitude normalization)
    normalization += envelope;
  }

  // Apply amplitude normalization for flat frequency response
  // This ensures equal amplitude sine waves produce equal CQT magnitudes regardless of frequency
  if (normalization > 0.0f) {
    float amplitudeNorm = 1.0f / normalization;
    for (auto& sample : params.kernels[binIndex]) {
      sample *= amplitudeNorm;
    }
  }
}

// Generate all CQT kernels
void generateCQTKernels(CQTParameters& params, int maxKernelLength) {
  for (int k = 0; k < params.numBins; k++) {
    generateMorletKernel(params, k, maxKernelLength);
  }
}

// Apply CQT to a circular buffer and compute magnitudes
void computeCQT(const CQTParameters& params, const std::vector<float>& circularBuffer, size_t writePos,
                std::vector<float>& cqtMagnitudes) {
  size_t bufferSize = circularBuffer.size();
  cqtMagnitudes.resize(params.numBins);

  for (int k = 0; k < params.numBins; k++) {
    const auto& kernel = params.kernels[k];
    int kernelLength = params.kernelLengths[k];

    // Ensure we have enough samples in the buffer
    if (kernelLength > static_cast<int>(bufferSize)) {
      cqtMagnitudes[k] = bufferSize - 1;
      continue;
    }

    size_t startPos = (writePos + bufferSize - kernelLength) % bufferSize;

    float realSum = 0.0f;
    float imagSum = 0.0f;

    for (int n = 0; n < kernelLength; n++) {
      size_t bufferIdx = (startPos + n) % bufferSize;
      float sample = circularBuffer[bufferIdx];
      // Use conjugate: conj(a + bi) = a - bi
      realSum += sample * kernel[n].real();
      imagSum -= sample * kernel[n].imag(); // Note the minus for conjugate
    }

    // Compute magnitude: |a + bi| = sqrt(a² + b²)
    // Apply scaling to match FFT amplitude response
    float magnitude = sqrtf(realSum * realSum + imagSum * imagSum);

    // Scale to match FFT levels - the factor accounts for the difference in how
    // CQT kernels vs FFT bins respond to sine waves of equal amplitude
    cqtMagnitudes[k] = magnitude * 2.0f;
  }
}

} // namespace ConstantQ

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
  std::string deviceName;
  if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
    deviceName = Config::values().pulseaudio.default_source;
  } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
    deviceName = Config::values().pipewire.default_source;
  }

  if (!audioEngine->initialize(deviceName)) {
    std::cerr << "Failed to initialize audio engine: " << audioEngine->getLastError() << std::endl;
    return;
  }

  std::cout << "Using audio engine: " << AudioEngine::typeToString(audioEngine->getType()) << std::endl;

  // Track config version for live updates
  size_t lastConfigVersion = Config::getVersion();
  std::string lastEngineTypeStr = preferredEngineStr;
  std::string lastDeviceName = deviceName;
  uint32_t lastSampleRate = audioEngine->getSampleRate();
  uint32_t lastChannels = Config::values().audio.channels;
  float lastMinFreq = Config::values().fft.min_freq;
  float lastMaxFreq = Config::values().fft.max_freq;
  int lastCqtBinsPerOctave = Config::values().fft.cqt_bins_per_octave;

  // Determine read size based on engine-specific buffer_size (frames per channel)
  int bufferFrames = 0;
  if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
    bufferFrames = Config::values().pulseaudio.buffer_size;
  } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
    bufferFrames = Config::values().pipewire.buffer_size;
  }
  if (bufferFrames <= 0) {
    bufferFrames = 512; // fallback
  }
  uint32_t lastBufferSize = bufferFrames;

  const int READ_SAMPLES = bufferFrames * audioEngine->getChannels();
  std::vector<float> readBuffer(READ_SAMPLES);

  // Initialize state first to get FFT size
  state.lastFrameTime = std::chrono::steady_clock::now();
  int FFT_SIZE = Config::values().audio.fft_size;
  fftwf_complex* out = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
  float* in = fftwf_alloc_real(FFT_SIZE);
  fftwf_plan plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, in, out, FFTW_ESTIMATE);
  int lastFftSize = FFT_SIZE;

  // Initialize CQT parameters
  ConstantQ::CQTParameters cqtParams =
      ConstantQ::initializeCQT(Config::values().fft.min_freq, Config::values().fft.cqt_bins_per_octave,
                               audioEngine->getSampleRate(), Config::values().fft.max_freq, audioData->bufferSize);
  ConstantQ::generateCQTKernels(cqtParams, Config::values().audio.fft_size);

  // Initialize CQT data vectors in AudioData
  {
    std::unique_lock<std::mutex> lock(audioData->mutex);
    audioData->cqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->prevCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->smoothedCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
    audioData->interpolatedCqtValuesMid.resize(cqtParams.numBins, 0.0f);
    audioData->cqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->prevCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->smoothedCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
    audioData->interpolatedCqtValuesSide.resize(cqtParams.numBins, 0.0f);
    audioData->cqtFrequencies = cqtParams.frequencies;
    lock.unlock();
  }

  // FFT processing control
  int fftSkipCount = 0;
  int FFT_SKIP_FRAMES = Config::values().audio.fft_skip_frames;
  int lastFftSkipFrames = FFT_SKIP_FRAMES;

  while (audioData->running && audioEngine->isRunning()) {
    const auto& audio_cfg = Config::values().audio;
    const auto& fft_cfg = Config::values().fft;
    audioData->sampleRate = audioEngine->getSampleRate();

    // Check for config changes that require audio engine reconfiguration
    size_t currentConfigVersion = Config::getVersion();
    if (currentConfigVersion != lastConfigVersion) {
      lastConfigVersion = currentConfigVersion;

      // Check if audio engine type has changed
      std::string currentEngineStr = Config::values().audio.engine;
      auto currentEngine = AudioEngine::stringToType(currentEngineStr);

      // Get current device name
      std::string currentDeviceName;
      if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
        currentDeviceName = Config::values().pulseaudio.default_source;
      } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
        currentDeviceName = Config::values().pipewire.default_source;
      }

      // Get current audio settings
      uint32_t currentSampleRate = audioEngine->getSampleRate();
      uint32_t currentChannels = Config::values().audio.channels;

      // Get current buffer size
      uint32_t currentBufferSize = 0;
      if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
        currentBufferSize = Config::values().pulseaudio.buffer_size;
      } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
        currentBufferSize = Config::values().pipewire.buffer_size;
      }
      if (currentBufferSize <= 0) {
        currentBufferSize = 512;
      }

      bool engineTypeChanged = (currentEngine != audioEngine->getType());
      bool needsReconfiguration =
          audioEngine->needsReconfiguration(currentDeviceName, currentSampleRate, currentChannels, currentBufferSize);

      if (engineTypeChanged || needsReconfiguration) {
        // Clean up current engine
        audioEngine->cleanup();

        if (engineTypeChanged) {
          // Create new engine if type changed
          audioEngine = AudioEngine::createBestAvailableEngine(currentEngine);
          if (!audioEngine) {
            std::cerr << "Failed to create new audio engine!" << std::endl;
            return;
          }

          // Update device name for new engine type
          if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
            currentDeviceName = Config::values().pulseaudio.default_source;
          } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
            currentDeviceName = Config::values().pipewire.default_source;
          }
        }

        // Initialize with new configuration
        if (!audioEngine->initialize(currentDeviceName)) {
          std::cerr << "Failed to reinitialize audio engine: " << audioEngine->getLastError() << std::endl;
          return;
        }

        // Update buffer sizes if they changed
        if (currentBufferSize != lastBufferSize || currentChannels != lastChannels) {
          bufferFrames = currentBufferSize;
          const int newReadSamples = bufferFrames * audioEngine->getChannels();
          readBuffer.resize(newReadSamples);
        }

        // Update tracking variables
        lastEngineTypeStr = currentEngineStr;
        lastDeviceName = currentDeviceName;
        lastSampleRate = audioEngine->getSampleRate();
        lastChannels = currentChannels;
        lastBufferSize = currentBufferSize;
      }

      // Reinitialize CQT if any CQT-related parameters changed (independent of engine changes)
      float currentMinFreq = fft_cfg.min_freq;
      float currentMaxFreq = fft_cfg.max_freq;
      int currentCqtBinsPerOctave = fft_cfg.cqt_bins_per_octave;
      if (currentSampleRate != lastSampleRate || currentMinFreq != lastMinFreq || currentMaxFreq != lastMaxFreq ||
          currentCqtBinsPerOctave != lastCqtBinsPerOctave) {
        cqtParams = ConstantQ::initializeCQT(currentMinFreq, currentCqtBinsPerOctave, audioEngine->getSampleRate(),
                                             currentMaxFreq, audioData->bufferSize);
        ConstantQ::generateCQTKernels(cqtParams, Config::values().audio.fft_size);

        // Resize CQT data vectors
        std::unique_lock<std::mutex> cqtLock(audioData->mutex);
        audioData->cqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
        audioData->prevCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
        audioData->smoothedCqtMagnitudesMid.resize(cqtParams.numBins, 0.0f);
        audioData->interpolatedCqtValuesMid.resize(cqtParams.numBins, 0.0f);
        audioData->cqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
        audioData->prevCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
        audioData->smoothedCqtMagnitudesSide.resize(cqtParams.numBins, 0.0f);
        audioData->interpolatedCqtValuesSide.resize(cqtParams.numBins, 0.0f);
        audioData->cqtFrequencies = cqtParams.frequencies;
        cqtLock.unlock();

        lastMinFreq = currentMinFreq;
        lastMaxFreq = currentMaxFreq;
        lastCqtBinsPerOctave = currentCqtBinsPerOctave;
      }

      // Check if FFT size has changed
      int currentFftSize = Config::values().audio.fft_size;
      if (currentFftSize != lastFftSize) {

        // Clean up old FFTW resources
        fftwf_destroy_plan(plan);
        fftwf_free(in);
        fftwf_free(out);

        // Create new FFTW resources
        FFT_SIZE = currentFftSize;
        out = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
        in = fftwf_alloc_real(FFT_SIZE);
        plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, in, out, FFTW_ESTIMATE);
        lastFftSize = FFT_SIZE;

        // Resize AudioData FFT vectors to match new FFT size
        std::unique_lock<std::mutex> lock(audioData->mutex);
        const size_t fftBins = FFT_SIZE / 2 + 1;
        audioData->fftMagnitudesMid.resize(fftBins, 0.0f);
        audioData->prevFftMagnitudesMid.resize(fftBins, 0.0f);
        audioData->smoothedMagnitudesMid.resize(fftBins, 0.0f);
        audioData->interpolatedValuesMid.resize(fftBins, 0.0f);
        audioData->fftMagnitudesSide.resize(fftBins, 0.0f);
        audioData->prevFftMagnitudesSide.resize(fftBins, 0.0f);
        audioData->smoothedMagnitudesSide.resize(fftBins, 0.0f);
        audioData->interpolatedValuesSide.resize(fftBins, 0.0f);
        lock.unlock();
      }

      // Check if FFT skip frames has changed
      int currentFftSkipFrames = Config::values().audio.fft_skip_frames;
      if (currentFftSkipFrames != lastFftSkipFrames) {
        FFT_SKIP_FRAMES = currentFftSkipFrames;
        lastFftSkipFrames = currentFftSkipFrames;
      }

      // Check if audio buffer size has changed (this affects the circular buffer in AudioData)
      size_t currentAudioBufferSize = Config::values().audio.buffer_size;
      size_t currentDisplaySamples = Config::values().audio.display_samples;
      if (currentAudioBufferSize != audioData->bufferSize || currentDisplaySamples != audioData->displaySamples) {

        // Lock mutex to safely resize buffers
        std::unique_lock<std::mutex> lock(audioData->mutex);

        // Update AudioData buffer sizes
        audioData->bufferSize = currentAudioBufferSize;
        audioData->displaySamples = currentDisplaySamples;

        // Resize circular buffers
        audioData->bufferMid.resize(audioData->bufferSize, 0.0f);
        audioData->bufferSide.resize(audioData->bufferSize, 0.0f);
        audioData->bandpassedMid.resize(audioData->bufferSize, 0.0f);

        // Reset write position to avoid out-of-bounds access
        audioData->writePos = 0;

        lock.unlock();
      }
    }

    // Update READ_SAMPLES based on current engine
    const int READ_SAMPLES = bufferFrames * audioEngine->getChannels();

    if (!audioEngine->readAudio(readBuffer.data(), bufferFrames)) {
      std::cerr << "Failed to read from audio engine: " << audioEngine->getLastError() << std::endl;
      break;
    }

    // Linearize gain
    float gain = powf(10.0f, audio_cfg.gain_db / 20.0f);

    // Write to circular buffer
    std::unique_lock<std::mutex> lock(audioData->mutex);
    for (int i = 0; i < READ_SAMPLES; i += 2) {
      float sampleLeft = readBuffer[i] * gain;
      float sampleRight = readBuffer[i + 1] * gain;

      // Write to circular buffer
      audioData->bufferMid[audioData->writePos] = (sampleLeft + sampleRight) / 2.0f;
      audioData->bufferSide[audioData->writePos] = (sampleLeft - sampleRight) / 2.0f;
      audioData->writePos = (audioData->writePos + 1) % audioData->bufferSize;
    }

    // Control FFT processing frequency
    if (++fftSkipCount <= FFT_SKIP_FRAMES) {
      lock.unlock();
      continue;
    }
    fftSkipCount = 0;

    // Compute FFT only if CQT is not enabled
    if (!fft_cfg.enable_cqt) {
      // Copy and window the input data from circular buffer (mid channel)
      size_t startPos = (audioData->writePos + audioData->bufferSize - FFT_SIZE) % audioData->bufferSize;

      for (int i = 0; i < FFT_SIZE; i++) {
        // Apply Hanning window
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->bufferSize;
        in[i] = audioData->bufferMid[pos] * window;
      }

      // Execute FFT (mid channel)
      fftwf_execute(plan);

      // Store previous frame for interpolation
      audioData->prevFftMagnitudesMid = audioData->fftMagnitudesMid;

      // Calculate new magnitudes with proper FFTW3 scaling
      const float fftScale = 2.0f / FFT_SIZE; // 2.0f for Hann window sum

      for (int i = 0; i < FFT_SIZE / 2 + 1; i++) {
        float mag = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]) * fftScale;
        // Double all bins except DC and Nyquist
        if (i != 0 && i != FFT_SIZE / 2)
          mag *= 2.0f;
        audioData->fftMagnitudesMid[i] = mag;
      }

      // Now process side channel FFT
      audioData->prevFftMagnitudesSide = audioData->fftMagnitudesSide;

      // Copy and window the input data from circular buffer (side channel)
      for (int i = 0; i < FFT_SIZE; i++) {
        // Apply Hanning window
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->bufferSize;
        in[i] = audioData->bufferSide[pos] * window;
      }

      // Execute FFT (side channel)
      fftwf_execute(plan);

      // Calculate side channel magnitudes with proper FFTW3 scaling
      for (int i = 0; i < FFT_SIZE / 2 + 1; i++) {
        float mag = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]) * fftScale;
        // Double all bins except DC and Nyquist
        if (i != 0 && i != FFT_SIZE / 2)
          mag *= 2.0f;
        audioData->fftMagnitudesSide[i] = mag;
      }
    }

    // Compute CQT only if enabled
    if (fft_cfg.enable_cqt) {
      // Store previous CQT frames for interpolation
      audioData->prevCqtMagnitudesMid = audioData->cqtMagnitudesMid;
      audioData->prevCqtMagnitudesSide = audioData->cqtMagnitudesSide;

      // Compute CQT for both mid and side channels
      ConstantQ::computeCQT(cqtParams, audioData->bufferMid, audioData->writePos, audioData->cqtMagnitudesMid);
      ConstantQ::computeCQT(cqtParams, audioData->bufferSide, audioData->writePos, audioData->cqtMagnitudesSide);
    }

    // Update FFT timing
    auto now = std::chrono::steady_clock::now();
    if (audioData->lastFftUpdate != std::chrono::steady_clock::time_point()) {
      audioData->fftUpdateInterval = std::chrono::duration<float>(now - audioData->lastFftUpdate).count();
    }
    audioData->lastFftUpdate = now;

    // Unified pitch/peak detection using configurable frequency range
    int sampleRate = audioEngine->getSampleRate();
    const float minFreq = fft_cfg.min_freq;
    const float maxFreq = fft_cfg.max_freq;

    float maxMagnitude = 0.0f;
    float peakDb = -100.0f;
    float peakFreq = 0.0f;
    int peakBin = 0;
    float avgMagnitude = 0.0f;

    if (fft_cfg.enable_cqt) {
      // Use CQT bins for pitch detection
      const auto& magnitudes = audioData->cqtMagnitudesMid;
      const auto& frequencies = cqtParams.frequencies;

      // Find bins within frequency range
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

      // find peak, calculate dB, and accumulate average
      for (int i = minBin; i <= maxBin; i++) {
        float magnitude = magnitudes[i];

        // Convert to dB using proper scaling
        float dB = 20.0f * log10f(magnitude + 1e-9f);

        // Track peak magnitude and bin
        if (magnitude > maxMagnitude) {
          maxMagnitude = magnitude;
          peakBin = i;
        }

        // Track peak dB
        if (dB > peakDb) {
          peakDb = dB;
        }

        // Accumulate for average
        avgMagnitude += magnitude;
      }
      avgMagnitude /= (maxBin - minBin + 1);

      // Apply parabolic interpolation for precise frequency estimation in CQT
      if (peakBin > 0 && peakBin < frequencies.size() - 1) {
        float y1 = magnitudes[peakBin - 1];
        float y2 = magnitudes[peakBin];
        float y3 = magnitudes[peakBin + 1];

        // Parabolic interpolation to find precise peak location
        float denominator = y1 - 2.0f * y2 + y3;
        if (std::abs(denominator) > 1e-9f) { // Avoid division by zero
          float offset = 0.5f * (y1 - y3) / denominator;
          // Clamp offset to reasonable range
          offset = std::max(-0.5f, std::min(0.5f, offset));

          // For CQT, interpolate in log frequency space since bins are logarithmically spaced
          float logFreq1 = log2f(frequencies[peakBin - 1]);
          float logFreq2 = log2f(frequencies[peakBin]);
          float logFreq3 = log2f(frequencies[peakBin + 1]);

          // Interpolate in log space
          float interpolatedLogFreq = logFreq2 + offset * (logFreq3 - logFreq1) * 0.5f;
          peakFreq = powf(2.0f, interpolatedLogFreq);
        } else {
          // Fallback to bin center if parabolic interpolation fails
          peakFreq = frequencies[peakBin];
        }
      } else if (peakBin >= 0 && peakBin < frequencies.size()) {
        // Edge case: use bin center frequency
        peakFreq = frequencies[peakBin];
      }
    } else {
      // Use FFT bins for pitch detection
      const int fftSize = (audioData->fftMagnitudesMid.size() - 1) * 2;

      int minBin = static_cast<int>(minFreq * fftSize / sampleRate);
      int maxBin = static_cast<int>(maxFreq * fftSize / sampleRate);

      // Clamp to valid bin range
      minBin = std::max(1, minBin);
      maxBin = std::min((int)audioData->fftMagnitudesMid.size() - 1, maxBin);

      // find peak, calculate dB, and accumulate average
      for (int i = minBin; i <= maxBin; i++) {
        float magnitude = audioData->fftMagnitudesMid[i];
        float dB = 20.0f * log10f(magnitude + 1e-9f);

        // Track peak magnitude and bin
        if (magnitude > maxMagnitude) {
          maxMagnitude = magnitude;
          peakBin = i;
        }

        // Track peak dB
        if (dB > peakDb) {
          peakDb = dB;
        }

        // Accumulate for average
        avgMagnitude += magnitude;
      }
      avgMagnitude /= (maxBin - minBin + 1);

      // Apply parabolic interpolation for precise frequency estimation
      if (peakBin > 0 && peakBin < audioData->fftMagnitudesMid.size() - 1) {
        float y1 = audioData->fftMagnitudesMid[peakBin - 1];
        float y2 = audioData->fftMagnitudesMid[peakBin];
        float y3 = audioData->fftMagnitudesMid[peakBin + 1];

        // Parabolic interpolation to find precise peak location
        float denominator = y1 - 2.0f * y2 + y3;
        if (std::abs(denominator) > 1e-9f) { // Avoid division by zero
          float offset = 0.5f * (y1 - y3) / denominator;
          // Clamp offset to reasonable range
          offset = std::max(-0.5f, std::min(0.5f, offset));

          float interpolatedBin = peakBin + offset;
          peakFreq = interpolatedBin * sampleRate / fftSize;
        } else {
          // Fallback to bin center if parabolic interpolation fails
          peakFreq = static_cast<float>(peakBin * sampleRate) / fftSize;
        }
      }
    }

    // Store computed values
    audioData->peakFreq = peakFreq;
    audioData->peakDb = peakDb;

    // Check if we have a valid peak (above silence threshold)
    audioData->hasValidPeak = (peakDb > audio_cfg.silence_threshold);

    // Convert to note
    static const char* noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    const char* const* noteNames = (fft_cfg.note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;
    freqToNote(peakFreq, audioData->peakNote, audioData->peakOctave, audioData->peakCents, noteNames);

    // Update pitch-related data based on validity
    if (audioData->hasValidPeak) {
      audioData->currentPitch = peakFreq;
      // Use confidence ratio (peak vs average) scaled to 0-1 range
      float confidence = maxMagnitude / (avgMagnitude + 1e-6f);
      audioData->pitchConfidence = std::min(1.0f, confidence / 3.0f);
      audioData->samplesPerCycle = static_cast<float>(sampleRate) / peakFreq;

      // Apply bandpass filter to the buffer using circular-buffer-aware processing
      Butterworth::applyBandpassCircular(audioData->bufferMid, audioData->bandpassedMid, peakFreq, 44100.0f,
                                         audioData->writePos,
                                         100.0f); // 100Hz bandwidth
    } else {
      audioData->pitchConfidence *= 0.9f; // Decay confidence when no valid peak
    }

    // Pre-compute smoothed FFT values for display
    auto currentFrameTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(currentFrameTime - state.lastFrameTime).count();
    state.lastFrameTime = currentFrameTime;

    // Update interpolation factor
    if (audioData->fftUpdateInterval > 0.0f) {
      float timeSinceUpdate = std::chrono::duration<float>(currentFrameTime - audioData->lastFftUpdate).count();
      float targetInterpolation = timeSinceUpdate / audioData->fftUpdateInterval;
      state.fftInterpolation =
          state.fftInterpolation + (targetInterpolation - state.fftInterpolation) * audio_cfg.smoothing_factor;
      state.fftInterpolation = std::min(1.0f, state.fftInterpolation);
    }

    // Apply linear interpolation for both channels (FFT)
    if (!fft_cfg.enable_cqt) {
      for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
        audioData->interpolatedValuesMid[i] = (1.0f - state.fftInterpolation) * audioData->prevFftMagnitudesMid[i] +
                                              state.fftInterpolation * audioData->fftMagnitudesMid[i];
        audioData->interpolatedValuesSide[i] = (1.0f - state.fftInterpolation) * audioData->prevFftMagnitudesSide[i] +
                                               state.fftInterpolation * audioData->fftMagnitudesSide[i];
      }
    }

    // Apply linear interpolation for both channels (CQT)
    if (fft_cfg.enable_cqt) {
      for (size_t i = 0; i < audioData->cqtMagnitudesMid.size(); ++i) {
        audioData->interpolatedCqtValuesMid[i] = (1.0f - state.fftInterpolation) * audioData->prevCqtMagnitudesMid[i] +
                                                 state.fftInterpolation * audioData->cqtMagnitudesMid[i];
        audioData->interpolatedCqtValuesSide[i] =
            (1.0f - state.fftInterpolation) * audioData->prevCqtMagnitudesSide[i] +
            state.fftInterpolation * audioData->cqtMagnitudesSide[i];
      }
    }

    // Apply temporal smoothing with asymmetric speeds (FFT channels)
    if (!fft_cfg.enable_cqt) {
      // Apply temporal smoothing with asymmetric speeds (mid channel)
      for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
        float current = audioData->interpolatedValuesMid[i];
        float previous = audioData->smoothedMagnitudesMid[i];

        // Calculate frequency for this bin
        float freq = (float)i * sampleRate / (audioData->fftMagnitudesMid.size() - 1) / 2;
        if (freq < minFreq || freq > maxFreq)
          continue;

        // Convert to dB for consistent speed calculation
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);

        // Calculate the difference and determine if we're rising or falling
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? audio_cfg.hover_fall_speed : audio_cfg.fall_speed;
        float speed = (diff > 0) ? audio_cfg.rise_speed : fallSpeed;

        // Apply speed-based smoothing in dB space
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;

        // Convert back to linear space
        audioData->smoothedMagnitudesMid[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }

      // Apply temporal smoothing with asymmetric speeds (side channel)
      for (size_t i = 0; i < audioData->fftMagnitudesSide.size(); ++i) {
        float current = audioData->interpolatedValuesSide[i];
        float previous = audioData->smoothedMagnitudesSide[i];

        // Calculate frequency for this bin
        float fftSize = (audioData->fftMagnitudesSide.size() - 1) * 2;
        float freq = (float)i * sampleRate / fftSize;
        if (freq < minFreq || freq > maxFreq)
          continue;

        // Convert to dB for consistent speed calculation
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);

        // Calculate the difference and determine if we're rising or falling
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? audio_cfg.hover_fall_speed : audio_cfg.fall_speed;
        float speed = (diff > 0) ? audio_cfg.rise_speed : fallSpeed;

        // Apply speed-based smoothing in dB space
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;

        // Convert back to linear space
        audioData->smoothedMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
    }

    // Apply temporal smoothing with asymmetric speeds (CQT channels)
    if (fft_cfg.enable_cqt) {
      // Apply temporal smoothing with asymmetric speeds (CQT mid channel)
      for (size_t i = 0; i < audioData->cqtMagnitudesMid.size(); ++i) {
        float current = audioData->interpolatedCqtValuesMid[i];
        float previous = audioData->smoothedCqtMagnitudesMid[i];

        // Calculate frequency for this CQT bin
        float freq = audioData->cqtFrequencies[i];
        if (freq < minFreq || freq > maxFreq)
          continue;

        // Convert to dB for consistent speed calculation
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);

        // Calculate the difference and determine if we're rising or falling
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? audio_cfg.hover_fall_speed : audio_cfg.fall_speed;
        float speed = (diff > 0) ? audio_cfg.rise_speed : fallSpeed;

        // Apply speed-based smoothing in dB space
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;

        // Convert back to linear space
        audioData->smoothedCqtMagnitudesMid[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }

      // Apply temporal smoothing with asymmetric speeds (CQT side channel)
      for (size_t i = 0; i < audioData->cqtMagnitudesSide.size(); ++i) {
        float current = audioData->interpolatedCqtValuesSide[i];
        float previous = audioData->smoothedCqtMagnitudesSide[i];

        // Calculate frequency for this CQT bin
        float freq = audioData->cqtFrequencies[i];
        if (freq < minFreq || freq > maxFreq)
          continue;

        // Convert to dB for consistent speed calculation
        float currentDb = 20.0f * log10f(current + 1e-9f);
        float previousDb = 20.0f * log10f(previous + 1e-9f);

        // Calculate the difference and determine if we're rising or falling
        float diff = currentDb - previousDb;
        float fallSpeed = audioData->fftHovering ? audio_cfg.hover_fall_speed : audio_cfg.fall_speed;
        float speed = (diff > 0) ? audio_cfg.rise_speed : fallSpeed;

        // Apply speed-based smoothing in dB space
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;

        // Convert back to linear space
        audioData->smoothedCqtMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
    }

    // Signal that new audio data is available for rendering
    audioData->signalNewData();

    lock.unlock();
  }

  fftwf_destroy_plan(plan);
  fftwf_free(in);
  fftwf_free(out);

  audioEngine->cleanup();
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