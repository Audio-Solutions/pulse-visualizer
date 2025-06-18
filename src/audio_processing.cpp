#include "audio_processing.hpp"

#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fftw3.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>
#include <pulse/simple.h>

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

// Apply cascaded biquads to the signal
void applyBandpass(const std::vector<float>& input, std::vector<float>& output, float centerFreq, float sampleRate,
                   float bandwidth, int order = 8) {
  if (input.empty()) {
    output.clear();
    return;
  }
  std::vector<Biquad> biquads = designButterworthBandpass(order, centerFreq, sampleRate, bandwidth);
  std::vector<float> temp1 = input;
  std::vector<float> temp2(input.size());

  for (auto& bq : biquads) {
    processBiquad(temp1, temp2, bq);
    std::swap(temp1, temp2);
  }
  output = temp1;
}

} // namespace Butterworth

// Helper function to convert frequency to note
void freqToNote(float freq, std::string& noteName, int& octave, int& cents) {
  static const char* noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static const char* noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

  const char* const* noteNames = Config::FFT::NOTE_KEY_MODE == Config::FFT::SHARP ? noteNamesSharp : noteNamesFlat;

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

void audioThread(AudioData* audioData) {
  pa_simple* pa = nullptr;
  int error;

  // Create a new playback stream
  pa_sample_spec ss = {.format = PA_SAMPLE_FLOAT32, .rate = 44100, .channels = 2};

  pa_buffer_attr attr = {.maxlength = 4096, .tlength = 1024, .prebuf = 512, .minreq = 512, .fragsize = 1024};

  pa = pa_simple_new(nullptr, "Audio Visualizer", PA_STREAM_RECORD, Config::PulseAudio::DEFAULT_SOURCE,
                     "Audio Visualization", &ss, nullptr, &attr, &error);

  if (!pa) {
    fprintf(stderr, "Failed to create PulseAudio stream: %s\n", pa_strerror(error));
    return;
  }

  // Pre-allocate all buffers to avoid reallocations
  audioData->bufferMid.resize(audioData->BUFFER_SIZE, 0.0f);
  audioData->bufferSide.resize(audioData->BUFFER_SIZE, 0.0f);
  audioData->bandpassedMid.resize(audioData->BUFFER_SIZE, 0.0f);

  const int READ_SIZE = 512;
  float readBuffer[READ_SIZE];

  const int FFT_SIZE = 4096;
  fftwf_complex* out = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
  float* in = fftwf_alloc_real(FFT_SIZE);
  fftwf_plan plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, in, out, FFTW_ESTIMATE);

  // Pre-allocate all FFT-related vectors
  audioData->fftMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->prevFftMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->smoothedMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->interpolatedValuesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->fftMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->prevFftMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->smoothedMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData->interpolatedValuesSide.resize(FFT_SIZE / 2 + 1, 0.0f);

  // FFT processing control - reduce frequency to save CPU
  int fftSkipCount = 0;
  const int FFT_SKIP_FRAMES = 2; // Process FFT every 3rd frame to reduce CPU load

  // Previous values for smoothing
  static float prevPeakFreq = 0.0f;
  static float prevPeakDb = -100.0f;

  while (audioData->running) {
    if (pa_simple_read(pa, readBuffer, sizeof(readBuffer), &error) < 0) {
      fprintf(stderr, "Failed to read from PulseAudio stream: %s\n", pa_strerror(error));
      break;
    }

    // Write to circular buffer
    std::unique_lock<std::mutex> lock(audioData->mutex);
    for (int i = 0; i < READ_SIZE; i += 2) {
      float sampleLeft = readBuffer[i];
      float sampleRight = readBuffer[i + 1];

      // Store Lissajous points (using left and right channels)
      if (i + 1 < READ_SIZE) {
        audioData->lissajousPoints.push_back({sampleLeft, sampleRight});
        if (audioData->lissajousPoints.size() > ::LISSAJOUS_POINTS) {
          audioData->lissajousPoints.pop_front();
        }
      }

      // Write to circular buffer
      audioData->bufferMid[audioData->writePos] = (sampleLeft + sampleRight) / 2.0f;
      audioData->bufferSide[audioData->writePos] = (sampleLeft - sampleRight) / 2.0f;
      audioData->writePos = (audioData->writePos + 1) % audioData->BUFFER_SIZE;
      audioData->availableSamples++;
    }

    // Reduce FFT processing frequency to save CPU
    if (++fftSkipCount <= FFT_SKIP_FRAMES) {
      lock.unlock();
      continue;
    }
    fftSkipCount = 0;

    if (audioData->availableSamples > 0) {
      // Copy and window the input data from circular buffer (mid channel)
      size_t startPos = (audioData->writePos + audioData->BUFFER_SIZE - FFT_SIZE) % audioData->BUFFER_SIZE;
      for (int i = 0; i < FFT_SIZE; i++) {
        // Apply Hanning window
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->BUFFER_SIZE;
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

      // Copy and window the input data from circular buffer (side channel)
      for (int i = 0; i < FFT_SIZE; i++) {
        // Apply Hanning window
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        size_t pos = (startPos + i) % audioData->BUFFER_SIZE;
        in[i] = audioData->bufferSide[pos] * window;
      }

      // Execute FFT (side channel)
      fftwf_execute(plan);

      // Store previous frame for interpolation
      audioData->prevFftMagnitudesSide = audioData->fftMagnitudesSide;

      // Calculate new magnitudes with proper FFTW3 scaling
      for (int i = 0; i < FFT_SIZE / 2 + 1; i++) {
        float mag = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]) * fftScale;
        // Double all bins except DC and Nyquist
        if (i != 0 && i != FFT_SIZE / 2)
          mag *= 2.0f;
        audioData->fftMagnitudesSide[i] = mag;
      }

      // Update FFT timing
      auto now = std::chrono::steady_clock::now();
      if (audioData->lastFftUpdate != std::chrono::steady_clock::time_point()) {
        audioData->fftUpdateInterval = std::chrono::duration<float>(now - audioData->lastFftUpdate).count();
      }
      audioData->lastFftUpdate = now;

      // Pitch detection logic
      const int MIN_FREQ = 10;
      const int MAX_FREQ = 5000;
      int sampleRate = ss.rate;

      float maxMagnitude = 0.0f;
      int peakBin = 0;

      int minBin = static_cast<int>(MIN_FREQ * FFT_SIZE / sampleRate);
      int maxBin = static_cast<int>(MAX_FREQ * FFT_SIZE / sampleRate);

      // Find the bin with maximum magnitude
      for (int i = minBin; i <= maxBin; i++) {
        float magnitude = audioData->fftMagnitudesMid[i];
        if (magnitude > maxMagnitude) {
          maxMagnitude = magnitude;
          peakBin = i;
        }
      }

      float avgMagnitude = 0.0f;
      for (int i = minBin; i <= maxBin; i++) {
        avgMagnitude += audioData->fftMagnitudesMid[i];
      }
      avgMagnitude /= (maxBin - minBin + 1);

      float confidence = maxMagnitude / (avgMagnitude + 1e-6f);
      float pitch = 0.0f;

      if (peakBin > 0 && peakBin < FFT_SIZE / 2 - 1) {
        // Get magnitudes of surrounding bins
        float m0 = audioData->fftMagnitudesMid[peakBin - 1];
        float m1 = audioData->fftMagnitudesMid[peakBin];
        float m2 = audioData->fftMagnitudesMid[peakBin + 1];

        // Calculate weighted average of bin positions
        float totalWeight = m0 + m1 + m2;
        if (totalWeight > 0) {
          float weightedPos = (m0 * (peakBin - 1) + m1 * peakBin + m2 * (peakBin + 1)) / totalWeight;
          float frequency = static_cast<float>(weightedPos * sampleRate) / FFT_SIZE;

          if (confidence > 1.5f) {
            pitch = frequency;
          }
        }
      }

      if (pitch > 0) {
        audioData->currentPitch = pitch;
        audioData->pitchConfidence = 1.0f;
        audioData->samplesPerCycle = static_cast<float>(ss.rate) / pitch;

        // Apply bandpass filter to the buffer
        Butterworth::applyBandpass(audioData->bufferMid, audioData->bandpassedMid, pitch, 44100.0f,
                                   100.0f); // 100Hz bandwidth
      } else {
        audioData->pitchConfidence *= 0.95f;
      }

      // PRE-COMPUTE FFT DISPLAY DATA (moved from render thread)
      // This eliminates heavy computation in the render thread

      // Find peak frequency for display
      float peakDb = -100.0f;
      float peakFreq = 0.0f;
      peakBin = 0;
      const float minFreq = Config::FFT::FFT_MIN_FREQ;
      const float maxFreq = Config::FFT::FFT_MAX_FREQ;
      const int fftSize = (audioData->fftMagnitudesMid.size() - 1) * 2;

      for (size_t i = 1; i < audioData->fftMagnitudesMid.size(); ++i) {
        float freq = (float)i * sampleRate / fftSize;
        if (freq < minFreq || freq > maxFreq)
          continue;
        float mag = audioData->fftMagnitudesMid[i];
        float dB = 20.0f * log10f(mag + 1e-9f);
        if (dB > peakDb) {
          peakDb = dB;
          peakFreq = freq;
          peakBin = i;
        }
      }

      // Refine peak frequency using weighted average
      if (peakBin > 0 && peakBin < audioData->fftMagnitudesMid.size() - 1) {
        float m0 = audioData->fftMagnitudesMid[peakBin - 1];
        float m1 = audioData->fftMagnitudesMid[peakBin];
        float m2 = audioData->fftMagnitudesMid[peakBin + 1];

        float totalWeight = m0 + m1 + m2;
        if (totalWeight > 0) {
          float weightedPos = (m0 * (peakBin - 1) + m1 * peakBin + m2 * (peakBin + 1)) / totalWeight;
          peakFreq = static_cast<float>(weightedPos * sampleRate) / fftSize;
        }
      }

      // Store computed values
      audioData->peakFreq = peakFreq;
      audioData->peakDb = peakDb;

      // Convert to note
      freqToNote(peakFreq, audioData->peakNote, audioData->peakOctave, audioData->peakCents);

      // Check if we have a valid peak (above silence threshold)
      const float SILENCE_THRESHOLD_DB = -60.0f;
      audioData->hasValidPeak = (peakDb > SILENCE_THRESHOLD_DB);

      // Pre-compute smoothed FFT values for display
      static auto lastFrameTime = std::chrono::steady_clock::now();
      auto currentFrameTime = std::chrono::steady_clock::now();
      float dt = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
      lastFrameTime = currentFrameTime;

      // Update interpolation factor
      static float fftInterpolation = 0.0f;
      if (audioData->fftUpdateInterval > 0.0f) {
        float timeSinceUpdate = std::chrono::duration<float>(currentFrameTime - audioData->lastFftUpdate).count();
        float targetInterpolation = timeSinceUpdate / audioData->fftUpdateInterval;
        fftInterpolation =
            fftInterpolation + (targetInterpolation - fftInterpolation) * Config::FFT::FFT_SMOOTHING_FACTOR;
        fftInterpolation = std::min(1.0f, fftInterpolation);
      }

      // Apply linear interpolation
      for (size_t i = 0; i < audioData->fftMagnitudesMid.size(); ++i) {
        audioData->interpolatedValuesMid[i] = (1.0f - fftInterpolation) * audioData->prevFftMagnitudesMid[i] +
                                              fftInterpolation * audioData->fftMagnitudesMid[i];
      }

      for (size_t i = 0; i < audioData->fftMagnitudesSide.size(); ++i) {
        audioData->interpolatedValuesSide[i] = (1.0f - fftInterpolation) * audioData->prevFftMagnitudesSide[i] +
                                               fftInterpolation * audioData->fftMagnitudesSide[i];
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
        float speed = (diff > 0) ? Config::FFT::FFT_RISE_SPEED : Config::FFT::FFT_FALL_SPEED;

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
        float speed = (diff > 0) ? Config::FFT::FFT_RISE_SPEED : Config::FFT::FFT_FALL_SPEED;

        // Apply speed-based smoothing in dB space
        float maxChange = speed * dt;
        float change = std::min(std::abs(diff), maxChange) * (diff > 0 ? 1.0f : -1.0f);
        float newDb = previousDb + change;

        // Convert back to linear space
        audioData->smoothedMagnitudesSide[i] = powf(10.0f, newDb / 20.0f) - 1e-9f;
      }
    }
    lock.unlock();
  }

  fftwf_destroy_plan(plan);
  fftwf_free(in);
  fftwf_free(out);

  pa_simple_free(pa);
}

} // namespace AudioProcessing