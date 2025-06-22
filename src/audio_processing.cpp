#include "audio_processing.hpp"

#include "audio_engine.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fftw3.h>
#include <iostream>

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
  output = temp1;
}

} // namespace Butterworth

void AudioThreadState::updateConfigCache() {
  if (Config::getVersion() != lastConfigVersion) {
    lissajous_points = Config::getInt("lissajous.points");
    fft_min_freq = Config::getFloat("fft.fft_min_freq");
    fft_max_freq = Config::getFloat("fft.fft_max_freq");
    fft_smoothing_factor = Config::getFloat("fft.fft_smoothing_factor");
    fft_rise_speed = Config::getFloat("fft.fft_rise_speed");
    fft_fall_speed = Config::getFloat("fft.fft_fall_speed");
    fft_hover_fall_speed = Config::getFloat("fft.fft_hover_fall_speed");
    silence_threshold = Config::getFloat("audio.silence_threshold");
    note_key_mode = Config::getString("fft.note_key_mode");
    sampleRate = Config::getFloat("audio.sample_rate");
    fft_size = Config::getInt("fft.fft_size");

    static const char* noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    noteNames = (note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;

    lastConfigVersion = Config::getVersion();
  }
}

// Helper function to convert frequency to note
void freqToNote(float freq, std::string& noteName, int& octave, int& cents, AudioThreadState& state) {
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
  noteName = state.noteNames[noteIdx];
  cents = (int)roundf((midi - midiInt) * 100.0f);
}

void audioThread(AudioData* audioData) {
  AudioThreadState state;

  // Initialize audio engine
  auto preferredEngineStr = Config::getString("audio.engine");
  auto preferredEngine = AudioEngine::stringToType(preferredEngineStr);

  auto audioEngine = AudioEngine::createBestAvailableEngine(preferredEngine);
  if (!audioEngine) {
    std::cerr << "Failed to create audio engine!" << std::endl;
    return;
  }

  // Get engine-specific device name
  std::string deviceName;
  if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
    deviceName = Config::getString("pulseaudio.default_source");
  } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
    deviceName = Config::getString("pipewire.default_source");
  }

  if (!audioEngine->initialize(deviceName)) {
    std::cerr << "Failed to initialize audio engine: " << audioEngine->getLastError() << std::endl;
    return;
  }

  std::cout << "Using audio engine: " << AudioEngine::typeToString(audioEngine->getType()) << std::endl;

  // Determine read size based on engine-specific buffer_size (frames per channel)
  int bufferFrames = 0;
  if (audioEngine->getType() == AudioEngine::Type::PULSEAUDIO) {
    bufferFrames = Config::getInt("pulseaudio.buffer_size");
  } else if (audioEngine->getType() == AudioEngine::Type::PIPEWIRE) {
    bufferFrames = Config::getInt("pipewire.buffer_size");
  }
  if (bufferFrames <= 0) {
    bufferFrames = 512; // fallback
  }

  const int READ_SAMPLES = bufferFrames * audioEngine->getChannels();
  std::vector<float> readBuffer(READ_SAMPLES);

  // Initialize state first to get FFT size
  state.lastFrameTime = std::chrono::steady_clock::now();
  state.updateConfigCache();

  const int FFT_SIZE = state.fft_size;
  fftwf_complex* out = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
  float* in = fftwf_alloc_real(FFT_SIZE);
  fftwf_plan plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, in, out, FFTW_ESTIMATE);

  // FFT processing control
  int fftSkipCount = 0;
  const int FFT_SKIP_FRAMES = 2;

  while (audioData->running && audioEngine->isRunning()) {
    state.updateConfigCache();
    audioData->sampleRate = state.sampleRate;

    if (!audioEngine->readAudio(readBuffer.data(), bufferFrames)) {
      std::cerr << "Failed to read from audio engine: " << audioEngine->getLastError() << std::endl;
      break;
    }

    // Write to circular buffer
    std::unique_lock<std::mutex> lock(audioData->mutex);
    for (int i = 0; i < READ_SAMPLES; i += 2) {
      float sampleLeft = readBuffer[i];
      float sampleRight = readBuffer[i + 1];

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

    // Update FFT timing
    auto now = std::chrono::steady_clock::now();
    if (audioData->lastFftUpdate != std::chrono::steady_clock::time_point()) {
      audioData->fftUpdateInterval = std::chrono::duration<float>(now - audioData->lastFftUpdate).count();
    }
    audioData->lastFftUpdate = now;

    // Unified pitch/peak detection using configurable frequency range
    int sampleRate = audioEngine->getSampleRate();
    const float minFreq = state.fft_min_freq;
    const float maxFreq = state.fft_max_freq;
    const int fftSize = (audioData->fftMagnitudesMid.size() - 1) * 2;

    int minBin = static_cast<int>(minFreq * fftSize / sampleRate);
    int maxBin = static_cast<int>(maxFreq * fftSize / sampleRate);

    // Clamp to valid bin range
    minBin = std::max(1, minBin);
    maxBin = std::min((int)audioData->fftMagnitudesMid.size() - 1, maxBin);

    float maxMagnitude = 0.0f;
    float peakDb = -100.0f;
    float peakFreq = 0.0f;
    int peakBin = 0;
    float avgMagnitude = 0.0f;

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

    // Store computed values
    audioData->peakFreq = peakFreq;
    audioData->peakDb = peakDb;

    // Check if we have a valid peak (above silence threshold)
    audioData->hasValidPeak = (peakDb > state.silence_threshold);

    // Convert to note
    freqToNote(peakFreq, audioData->peakNote, audioData->peakOctave, audioData->peakCents, state);

    // Update pitch-related data based on validity
    if (audioData->hasValidPeak) {
      audioData->currentPitch = peakFreq;
      // Use confidence ratio (peak vs average) scaled to 0-1 range
      float confidence = maxMagnitude / (avgMagnitude + 1e-6f);
      audioData->pitchConfidence = std::min(1.0f, confidence / 3.0f);
      audioData->samplesPerCycle = static_cast<float>(sampleRate) / peakFreq;

      // Apply bandpass filter to the buffer using circular-buffer-aware processing
      Butterworth::applyBandpassCircular(audioData->bufferMid, audioData->bandpassedMid, peakFreq, 44100.0f,
                                         (audioData->writePos + audioData->displaySamples) % audioData->bufferSize,
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
          state.fftInterpolation + (targetInterpolation - state.fftInterpolation) * state.fft_smoothing_factor;
      state.fftInterpolation = std::min(1.0f, state.fftInterpolation);
    }

    // Apply linear interpolation for both channels
    for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
      audioData->interpolatedValuesMid[i] = (1.0f - state.fftInterpolation) * audioData->prevFftMagnitudesMid[i] +
                                            state.fftInterpolation * audioData->fftMagnitudesMid[i];
      audioData->interpolatedValuesSide[i] = (1.0f - state.fftInterpolation) * audioData->prevFftMagnitudesSide[i] +
                                             state.fftInterpolation * audioData->fftMagnitudesSide[i];
    }

    // Apply temporal smoothing with asymmetric speeds (mid channel)
    for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
      float current = audioData->interpolatedValuesMid[i];
      float previous = audioData->smoothedMagnitudesMid[i];

      // Calculate frequency for this bin
      float freq = (float)i * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;

      // Convert to dB for consistent speed calculation
      float currentDb = 20.0f * log10f(current + 1e-9f);
      float previousDb = 20.0f * log10f(previous + 1e-9f);

      // Calculate the difference and determine if we're rising or falling
      float diff = currentDb - previousDb;
      float fallSpeed = audioData->fftHovering ? state.fft_hover_fall_speed : state.fft_fall_speed;
      float speed = (diff > 0) ? state.fft_rise_speed : fallSpeed;

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
      float freq = (float)i * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;

      // Convert to dB for consistent speed calculation
      float currentDb = 20.0f * log10f(current + 1e-9f);
      float previousDb = 20.0f * log10f(previous + 1e-9f);

      // Calculate the difference and determine if we're rising or falling
      float diff = currentDb - previousDb;
      float fallSpeed = audioData->fftHovering ? state.fft_hover_fall_speed : state.fft_fall_speed;
      float speed = (diff > 0) ? state.fft_rise_speed : fallSpeed;

      // Apply speed-based smoothing in dB space
      float maxChange = speed * dt;
      float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
      float newDb = previousDb + change;

      // Convert back to linear space
      audioData->smoothedMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
    }
    lock.unlock();
  }

  fftwf_destroy_plan(plan);
  fftwf_free(in);
  fftwf_free(out);

  audioEngine->cleanup();
}

} // namespace AudioProcessing