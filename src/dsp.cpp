/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include/dsp.hpp"

#include "include/audio_engine.hpp"
#include "include/config.hpp"
#include "include/sdl_window.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace DSP {

// Aligned memory allocator implementation
template <typename T, std::size_t Alignment> T* AlignedAllocator<T, Alignment>::allocate(std::size_t n) {
  void* ptr = nullptr;
#ifdef __linux
  if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0)
    throw std::bad_alloc();
#elif defined(_MSC_VER)
  ptr = _aligned_malloc(n * sizeof(T), Alignment);
  if (!ptr)
    throw std::bad_alloc();
#else
  ptr = std::aligned_alloc(Alignment, n * sizeof(T));
  if (!ptr)
    throw std::bad_alloc();
#endif
  return static_cast<T*>(ptr);
}

template <typename T, std::size_t Alignment>
void AlignedAllocator<T, Alignment>::deallocate(T* p, std::size_t) noexcept {
#ifdef _MSC_VER
  _aligned_free(p);
#else
  free(p);
#endif
}

// Audio buffer data with SIMD alignment
std::vector<float, AlignedAllocator<float, 32>> bufferMid;
std::vector<float, AlignedAllocator<float, 32>> bufferSide;
std::vector<float, AlignedAllocator<float, 32>> bandpassed;
std::vector<float, AlignedAllocator<float, 32>> lowpassed;

// FFT data buffers
std::vector<float> fftMidRaw;
std::vector<float> fftMid;
std::vector<float> fftSideRaw;
std::vector<float> fftSide;

const size_t bufferSize = 32768;
size_t writePos = 0;

// Pitch detection variables
float pitch;
float pitchDB;

std::tuple<std::string, int, int> toNote(float freq, std::string* noteNames) {
  if (freq < Config::options.fft.min_freq || freq > Config::options.fft.max_freq)
    return std::make_tuple("-", 0, 0);

  // Convert frequency to MIDI note number
  float midi = 69.f + 12.f * log2f(freq / 440.f);

  if (midi < 0.f)
    return std::make_tuple("-", 0, 0);

  int roundedMidi = static_cast<int>(roundf(midi));
  int noteIdx = (roundedMidi + 1200) % 12;
  return std::make_tuple(noteNames[noteIdx], roundedMidi / 12 - 1,
                         static_cast<int>(std::round((midi - static_cast<float>(roundedMidi)) * 100.f)));
}

namespace FIR {

Filter bandpass_filter;

float Filter::process(float x) {
  const size_t nTaps = static_cast<size_t>(order) + 1;
  if (nTaps == 0)
    return x;
  idx %= nTaps;
  delay[idx] = x;

  float out = 0.0f;

#ifdef HAVE_AVX2
  // Compute dot product without modulo by splitting into two contiguous segments:
  // [idx .. end] then [0 .. idx-1]
  const size_t firstLen = nTaps - idx;
  const float* coeffPtr = coeffs.data();
  const float* delayPtr1 = delay.data() + idx;

  size_t n = 0;
  __m256 sumVec = _mm256_setzero_ps();
  for (; n + 7 < firstLen; n += 8) {
    __m256 c = _mm256_loadu_ps(coeffPtr + n);
    __m256 d = _mm256_loadu_ps(delayPtr1 + n);
    sumVec = _mm256_fmadd_ps(c, d, sumVec);
  }
  out += avx2_reduce_add_ps(sumVec);
  for (; n < firstLen; ++n)
    out += coeffPtr[n] * delayPtr1[n];

  // Second contiguous segment
  const size_t secondLen = idx;
  const float* delayPtr2 = delay.data();
  sumVec = _mm256_setzero_ps();
  size_t k = 0;
  for (; k + 7 < secondLen; k += 8) {
    __m256 c = _mm256_loadu_ps(coeffPtr + firstLen + k);
    __m256 d = _mm256_loadu_ps(delayPtr2 + k);
    sumVec = _mm256_fmadd_ps(c, d, sumVec);
  }
  out += avx2_reduce_add_ps(sumVec);
  for (; k < secondLen; ++k)
    out += coeffPtr[firstLen + k] * delayPtr2[k];
#else
  // Fallback scalar path
  const size_t firstLen = nTaps - idx;
  for (size_t i = 0; i < firstLen; ++i)
    out += coeffs[i] * delay[idx + i];
  for (size_t i = 0; i < idx; ++i)
    out += coeffs[firstLen + i] * delay[i];
#endif

  idx = (idx + 1) % nTaps;
  return out;
}

void Filter::set_coefficients(const std::vector<float>& coeffs) {
  if (coeffs.size() == this->coeffs.size()) {
    this->coeffs = coeffs;
  } else {
    this->coeffs.resize(coeffs.size());
    this->coeffs = coeffs;
    order = coeffs.size() - 1;
    delay.resize(coeffs.size(), 0.0f);
  }
}

// Modified Bessel function of the first kind, order 0 (I0)
// Approximation from Abramowitz and Stegun
static inline double besselI0(double x) {
  double ax = std::abs(x);
  if (ax <= 3.75) {
    double y = (x / 3.75);
    y *= y;
    return 1.0 +
           y * (3.5156229 + y * (3.0899424 + y * (1.2067492 + y * (0.2659732 + y * (0.0360768 + y * 0.0045813)))));
  } else {
    double y = 3.75 / ax;
    return (std::exp(ax) / std::sqrt(ax)) *
           (0.39894228 +
            y * (0.01328592 +
                 y * (0.00225319 +
                      y * (-0.00157565 +
                           y * (0.00916281 +
                                y * (-0.02057706 + y * (0.02635537 + y * (-0.01647633 + y * 0.00392377))))))));
  }
}

std::vector<float> kaiser_window(size_t length, float beta) {
  std::vector<float> window(length);
  if (length == 0)
    return window;
  const double denom = besselI0(static_cast<double>(beta));
  const double M = static_cast<double>(length - 1);
  for (size_t n = 0; n < length; ++n) {
    double ratio = (M == 0.0) ? 0.0 : (2.0 * static_cast<double>(n) / M - 1.0);
    double val = besselI0(static_cast<double>(beta) * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / denom;
    window[n] = static_cast<float>(val);
  }
  return window;
}

void design(float center) {
  float bw = Config::options.bandpass_filter.bandwidth;

  // Kaiser window with beta chosen for ~60 dB sidelobe attenuation
  float sidelobe = Config::options.bandpass_filter.sidelobe;
  float beta = sidelobe < 21.0f   ? 0.0f
               : sidelobe < 50.0f ? 0.5842f * powf(sidelobe - 21.0f, 0.4f) + 0.07886f * (sidelobe - 21.0f)
                                  : 0.1102f * (sidelobe - 8.7f);

  float fs = Config::options.audio.sample_rate;
  float wc1 = 2.0f * M_PI * (center - bw / 2.0f) / fs;
  float wc2 = 2.0f * M_PI * (center + bw / 2.0f) / fs;
  wc1 = std::max(wc1, 0.001f);
  wc2 = std::min(wc2, static_cast<float>(M_PI) - 0.001f);

  float fP = wc1 / M_PI;
  float fS = wc2 / M_PI;
  float deltaF = fS - fP;
  int order = (sidelobe - 8) / (2.285 * deltaF * M_PI);
  order = std::clamp(order, 1, 512); // clamp to avoid excessive delay

  size_t len = order + 1;
  size_t center_tap = len / 2;
  std::vector<float> ideal(len);
  for (size_t i = 0; i < len; ++i) {
    if (i == center_tap) {
      ideal[i] = (wc2 - wc1) / M_PI;
    } else {
      float n = static_cast<float>(static_cast<int>(i) - static_cast<int>(center_tap));
      ideal[i] = (sinf(wc2 * n) - sinf(wc1 * n)) / (M_PI * n);
    }
  }
  std::vector<float> window = kaiser_window(len, beta);
  std::vector<float> windowed(len);
  for (size_t i = 0; i < len; ++i) {
    windowed[i] = ideal[i] * window[i];
  }

  float center_freq = 2.0f * M_PI * center / Config::options.audio.sample_rate;
  float response_at_center = 0.0f;
  for (size_t i = 0; i < len; ++i) {
    response_at_center += windowed[i] * cosf(center_freq * (static_cast<float>(i) - static_cast<float>(center_tap)));
  }

  if (std::abs(response_at_center) > FLT_EPSILON) {
    float scale_factor = 1.0f / response_at_center;
    for (float& coeff : windowed) {
      coeff *= scale_factor;
    }
  }
  bandpass_filter.set_coefficients(windowed);
}

void process(float center) {
  design(center);
  static size_t lastWritePos = 0;
  if (lastWritePos == writePos)
    return;
  size_t count = (writePos - lastWritePos) % bufferSize;
  for (size_t i = 0; i < count; i++) {
    size_t readIdx = (lastWritePos + i + bufferSize) % bufferSize;
    float input = bufferMid[readIdx];
    float filtered = bandpass_filter.process(input);
    bandpassed[(readIdx - bandpass_filter.order / 2 + bufferSize) % bufferSize] = filtered;
  }
  lastWritePos = writePos;
}

} // namespace FIR

namespace Lowpass {
struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1, x2, y1, y2;
  float process(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
  void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};
std::vector<Biquad> biquads;

void init() {
  biquads.clear();
  int sections = Config::options.lowpass.order / 2;
  float w0 = 2.f * M_PI * Config::options.lowpass.cutoff / Config::options.audio.sample_rate;
  for (int k = 0; k < sections; ++k) {
    float theta = M_PI * (2.f * k + 1.f) / (2.f * Config::options.lowpass.order);
    float sin_theta = sinf(theta);
    float cos_theta = cosf(theta);
    float alpha = sinf(w0) / (2.f * sin_theta);
    float b0 = (1.f - cosf(w0)) / 2.f;
    float b1 = 1.f - cosf(w0);
    float b2 = (1.f - cosf(w0)) / 2.f;
    float a0 = 1.f + alpha;
    float a1 = -2.f * cosf(w0);
    float a2 = 1.f - alpha;
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;
    biquads.push_back({b0, b1, b2, a1, a2});
  }
  for (auto& bq : biquads)
    bq.reset();
}

void reconfigure() {
  static float lastCutoff = Config::options.lowpass.cutoff;
  static float lastSampleRate = Config::options.audio.sample_rate;
  static int lastOrder = Config::options.lowpass.order;
  if (lastCutoff == Config::options.lowpass.cutoff && lastSampleRate == Config::options.audio.sample_rate &&
      lastOrder == Config::options.lowpass.order)
    return;
  lastCutoff = Config::options.lowpass.cutoff;
  lastSampleRate = Config::options.audio.sample_rate;
  lastOrder = Config::options.lowpass.order;
  init();
}

void process() {
  for (size_t i = 0; i < bufferSize; i++) {
    size_t readIdx = (writePos + i) % bufferSize;
    float filtered = bufferMid[readIdx];
    for (auto& bq : biquads)
      filtered = bq.process(filtered);
    lowpassed[readIdx] = filtered;
  }
}
}; // namespace Lowpass

namespace ConstantQ {

// Constant Q Transform parameters
float Q;
size_t bins;
std::vector<float> frequencies;
std::vector<size_t> lengths;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> reals;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> imags;

std::pair<size_t, size_t> find(float f) {
  size_t n = frequencies.size();
  if (n == 0)
    return {0, 0};
  if (f <= frequencies[0])
    return {0, 0};
  if (f >= frequencies[n - 1])
    return {n - 1, n - 1};

  size_t left = 0, right = n - 1;
  while (left < right) {
    size_t mid = left + ((right - left) >> 1);
    if (frequencies[mid] < f)
      left = mid + 1;
    else
      right = mid;
  }
  if (left == 0)
    return {0, 0};
  return {left - 1, left};
}

void init() {
  // Calculate number of frequency bins
  float octaves = log2f(Config::options.fft.max_freq / Config::options.fft.min_freq);
  bins = std::clamp(static_cast<size_t>(ceil(octaves * Config::options.fft.cqt_bins_per_octave)),
                    static_cast<size_t>(1), static_cast<size_t>(1000));
  frequencies.resize(bins);
  lengths.resize(bins);
  reals.resize(bins);
  imags.resize(bins);

  // Calculate Q factor and frequency bins
  float binRatio = 1.f / Config::options.fft.cqt_bins_per_octave;
  Q = 1.f / (powf(2.f, binRatio) - 1.f);
  for (int k = 0; k < bins; k++) {
    frequencies[k] = Config::options.fft.min_freq * powf(2.f, static_cast<float>(k) * binRatio);
  }
}

void genMortletKernel(int bin) {
  const float& fc = frequencies[bin];
  float omega = 2 * M_PI * fc;
  float sigma = Q / omega;
  float dt = 1.f / Config::options.audio.sample_rate;
  size_t length = static_cast<size_t>(ceilf(6.0f * sigma / dt));
  if (length > Config::options.fft.size) {
    length = Config::options.fft.size;
    sigma = static_cast<float>(length) * dt / 6.0f;
  }

  if (length % 2 == 0)
    length++;

  lengths[bin] = length;
  reals[bin].resize(length);
  imags[bin].resize(length);

  // Generate Mortlet wavelet kernel
  ssize_t center = length / 2;
  float norm = 0.f;
  float cosPhase = cosf(-omega * center * dt);
  float sinPhase = sinf(-omega * center * dt);
  float cosDelta = cosf(omega * dt);
  float sinDelta = sinf(omega * dt);

  for (ssize_t n = 0; n < length; n++) {
    float t = static_cast<float>(n - center) * dt;
    float envelope = expf(-(t * t) / (2 * sigma * sigma));
    reals[bin][n] = envelope * cosPhase;
    imags[bin][n] = envelope * sinPhase;
    norm += envelope;
    float newCos = cosPhase * cosDelta - sinPhase * sinDelta;
    float newSin = sinPhase * cosDelta + cosPhase * sinDelta;
    cosPhase = newCos;
    sinPhase = newSin;
  }

  // Normalize kernel
  if (norm > 0.f) {
    float ampNorm = 1.f / norm;
    for (size_t n = 0; n < length; n++) {
      reals[bin][n] *= ampNorm;
      imags[bin][n] *= ampNorm;
    }
  }
}

void generate() {
  // Generate kernels for all frequency bins
  for (int k = 0; k < bins; k++) {
    genMortletKernel(k);
  }
}

bool regenerate() {
  static int lastCQTBins = Config::options.fft.cqt_bins_per_octave;
  static float lastMinFreq = Config::options.fft.min_freq;
  static float lastMaxFreq = Config::options.fft.max_freq;

  // Check if regeneration is needed
  if (lastCQTBins == Config::options.fft.cqt_bins_per_octave && lastMinFreq == Config::options.fft.min_freq &&
      lastMaxFreq == Config::options.fft.max_freq) [[likely]]
    return false;

  lastCQTBins = Config::options.fft.cqt_bins_per_octave;
  lastMinFreq = Config::options.fft.min_freq;
  lastMaxFreq = Config::options.fft.max_freq;

  init();
  generate();

  return true;
}

template <typename Alloc> void compute(const std::vector<float, Alloc>& in, std::vector<float>& out) {
  out.resize(bins);

#ifdef HAVE_AVX2
  // SIMD-optimized processing function
  auto process_segment = [](const float* buf, const float* real, const float* imag,
                            size_t length) -> std::pair<float, float> {
    size_t n = 0;
    __m256 realSumVec = _mm256_setzero_ps();
    __m256 imagSumVec = _mm256_setzero_ps();

    bool aligned = (((uintptr_t)buf % 32) == 0) && (((uintptr_t)real % 32) == 0) && (((uintptr_t)imag % 32) == 0);

    if (aligned) {
      for (; n + 7 < length; n += 8) {
        __m256 sampleVec = _mm256_load_ps(buf + n);
        __m256 realVec = _mm256_load_ps(real + n);
        __m256 imagVec = _mm256_load_ps(imag + n);
        realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
        imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
      }
    } else {
      for (; n + 7 < length; n += 8) {
        __m256 sampleVec = _mm256_loadu_ps(buf + n);
        __m256 realVec = _mm256_loadu_ps(real + n);
        __m256 imagVec = _mm256_loadu_ps(imag + n);
        realSumVec = _mm256_fmadd_ps(sampleVec, realVec, realSumVec);
        imagSumVec = _mm256_fnmadd_ps(sampleVec, imagVec, imagSumVec);
      }
    }

    float realSum = avx2_reduce_add_ps(realSumVec);
    float imagSum = avx2_reduce_add_ps(imagSumVec);

    for (; n < length; ++n) {
      float sample = buf[n];
      realSum += sample * real[n];
      imagSum -= sample * imag[n];
    }

    return {realSum, imagSum};
  };
#endif

  // Process each frequency bin
  for (int k = 0; k < bins; k++) {
    const auto& kReals = reals[k];
    const auto& kImags = imags[k];
    size_t length = lengths[k];
    size_t start = (writePos + bufferSize - length) % bufferSize;

    float realSum = 0.f;
    float imagSum = 0.f;

#ifdef HAVE_AVX2
    // Handle buffer wrapping with SIMD optimization
    if (start + length <= bufferSize) {
      auto result = process_segment(&in[start], kReals.data(), kImags.data(), length);
      realSum = result.first;
      imagSum = result.second;
    } else {
      size_t firstLen = bufferSize - start;
      size_t secondLen = length - firstLen;

      auto result1 = process_segment(&in[start], kReals.data(), kImags.data(), firstLen);
      realSum += result1.first;
      imagSum += result1.second;

      auto result2 = process_segment(&in[0], kReals.data() + firstLen, kImags.data() + firstLen, secondLen);
      realSum += result2.first;
      imagSum += result2.second;
    }
#else
    // Standard processing without SIMD
    for (size_t n = 0; n < length; n++) {
      size_t bufferIdx = (start + n) % bufferSize;
      float sample = in[bufferIdx];
      realSum += sample * kReals[n];
      imagSum -= sample * kImags[n];
    }
#endif
    out[k] = std::sqrt(realSum * realSum + imagSum * imagSum) * 2.f;
  }
}
} // namespace ConstantQ

namespace FFT {

// FFTW plans and buffers
std::mutex mutexMid;
std::mutex mutexSide;
fftwf_plan mid;
fftwf_plan side;
float* inMid;
float* inSide;
fftwf_complex* outMid;
fftwf_complex* outSide;

void init() {
  int& size = Config::options.fft.size;
  inMid = fftwf_alloc_real(size);
  inSide = fftwf_alloc_real(size);
  outMid = fftwf_alloc_complex(size / 2 + 1);
  outSide = fftwf_alloc_complex(size / 2 + 1);
  mid = fftwf_plan_dft_r2c_1d(size, inMid, outMid, FFTW_ESTIMATE);
  side = fftwf_plan_dft_r2c_1d(size, inSide, outSide, FFTW_ESTIMATE);

  if (!inMid || !inSide || !outMid || !outSide || !mid || !side)
    LOG_ERROR("Failed to allocate FFTW buffers");
}

void cleanup() {
  if (mid) {
    fftwf_destroy_plan(mid);
    mid = nullptr;
  }
  if (side) {
    fftwf_destroy_plan(side);
    side = nullptr;
  }

  if (inMid) {
    fftwf_free(inMid);
    inMid = nullptr;
  }
  if (inSide) {
    fftwf_free(inSide);
    inSide = nullptr;
  }

  if (outMid) {
    fftwf_free(outMid);
    outMid = nullptr;
  }
  if (outSide) {
    fftwf_free(outSide);
    outSide = nullptr;
  }
}

bool recreatePlans() {
  static size_t lastFFTSize = Config::options.fft.size;
  static bool lastCQTState = Config::options.fft.enable_cqt;
  static float lastSampleRate = Config::options.audio.sample_rate;

  // Check if FFT plans need to be recreated
  if (lastFFTSize == Config::options.fft.size && lastCQTState == Config::options.fft.enable_cqt &&
      lastSampleRate == Config::options.audio.sample_rate) [[likely]]
    return false;

  lastFFTSize = Config::options.fft.size;
  lastCQTState = Config::options.fft.enable_cqt;
  lastSampleRate = Config::options.audio.sample_rate;

  std::lock_guard<std::mutex> lockMid(mutexMid);
  std::lock_guard<std::mutex> lockSide(mutexSide);

  cleanup();
  init();

  if (!inMid || !inSide || !outMid || !outSide || !mid || !side)
    LOG_ERROR("Failed to allocate FFTW buffers");

  return true;
}
} // namespace FFT

namespace Threads {

// Thread synchronization
std::mutex mutex;
std::atomic<bool> dataReadyFFTMain;
std::atomic<bool> dataReadyFFTAlt;
std::condition_variable fft;

int FFTMain() {
  while (SDLWindow::running) {
    std::unique_lock<std::mutex> lock(mutex);
    fft.wait(lock, [] { return dataReadyFFTMain.load() || !SDLWindow::running; });
    if (!SDLWindow::running)
      break;
    dataReadyFFTMain = false;

    // Process main channel FFT
    if (Config::options.fft.enable_cqt) {
      ConstantQ::compute(bufferMid, fftMidRaw);
    } else {
      fftMidRaw.resize(Config::options.fft.size);
      size_t start = (writePos + bufferSize - Config::options.fft.size) % bufferSize;

      // Apply window function and prepare FFT input
      for (int i = 0; i < Config::options.fft.size; i++) {
        float win = 0.5f * (1.f - cos(2.f * M_PI * i / (Config::options.fft.size)));
        size_t pos = (start + i) % bufferSize;
        if (Config::options.fft.stereo_mode == "leftright")
          FFT::inMid[i] = (bufferSide[pos] - bufferMid[pos]) * 0.5f * win;
        else
          FFT::inMid[i] = bufferMid[pos] * win;
      }

      // Execute FFT
      {
        std::lock_guard<std::mutex> lockFft(FFT::mutexMid);
        fftwf_execute(FFT::mid);
      }

      // Convert to magnitude spectrum
      const float scale = 2.f / Config::options.fft.size;
      for (int i = 0; i < Config::options.fft.size / 2 + 1; i++) {
        float mag = sqrt(FFT::outMid[i][0] * FFT::outMid[i][0] + FFT::outMid[i][1] * FFT::outMid[i][1]) * scale;
        if (i != 0 && i != Config::options.fft.size / 2)
          mag *= 2.f;
        fftMidRaw[i] = mag;
      }
    }

    // Find peak frequency for pitch detection
    float peakDb = -INFINITY;
    float peakFreq = 0.f;
    uint32_t peakBin = 0;

    for (int i = 0; i < fftMidRaw.size(); i++) {
      float mag = fftMidRaw[i];
      float dB = 20.f * log10f(mag + FLT_EPSILON);
      if (dB > peakDb) {
        peakBin = i;
        peakDb = dB;
      }
    };

    // Interpolate peak frequency
    float y1 = fftMidRaw[std::max(0, static_cast<int>(peakBin) - 1)];
    float y2 = fftMidRaw[peakBin];
    float y3 = fftMidRaw[std::min(static_cast<uint32_t>(fftMidRaw.size() - 1), peakBin + 1)];
    float denom = y1 - 2.f * y2 + y3;
    if (Config::options.fft.enable_cqt) {
      if (std::abs(denom) > FLT_EPSILON) {
        float offset = std::clamp(0.5f * (y1 - y3) / denom, -0.5f, 0.5f);
        float binInterp = static_cast<float>(peakBin) + offset;
        float logMinFreq = log2f(Config::options.fft.min_freq);
        float binRatio = 1.f / Config::options.fft.cqt_bins_per_octave;
        float intpLog = logMinFreq + binInterp * binRatio;
        peakFreq = powf(2.f, intpLog);
      } else
        peakFreq = ConstantQ::frequencies[peakBin];
    } else {
      if (std::abs(denom) > FLT_EPSILON) {
        float offset = std::clamp(0.5f * (y1 + y3) / denom, -0.5f, 0.5f);
        peakFreq = (peakBin + offset) * Config::options.audio.sample_rate / Config::options.fft.size;
      } else
        peakFreq = static_cast<float>(peakBin * Config::options.audio.sample_rate) / Config::options.fft.size;
    }

    pitch = peakFreq;
    pitchDB = peakDb;

    // Apply smoothing if enabled
    if (Config::options.fft.enable_smoothing) {
      fftMid.resize(fftMidRaw.size());
      size_t bins = fftMid.size();
      size_t i = 0;
      const float riseSpeed = Config::options.fft.rise_speed * WindowManager::dt;
      const float fallSpeed =
          (SpectrumAnalyzer::window && SpectrumAnalyzer::window->hovering ? Config::options.fft.hover_fall_speed
                                                                          : Config::options.fft.fall_speed) *
          WindowManager::dt;
      // Calculate minimum value for clamping (1 dB below screen minimum)
      const float minValue = powf(10.f, (Config::options.fft.min_db - 1.0f) / 20.f);
      const float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);

      // Pre-calculate constants for AVX2
#ifdef HAVE_AVX2
      const __m256 riseSpeedVec = _mm256_set1_ps(riseSpeed);
      const __m256 fallSpeedVec = _mm256_set1_ps(fallSpeed);
      const __m256 minvVec = _mm256_set1_ps(FLT_EPSILON);
      const __m256 dbScaleVec = _mm256_set1_ps(20.0f);
      const __m256 oneVec = _mm256_set1_ps(1.0f);
      const __m256 negOneVec = _mm256_set1_ps(-1.0f);
      const __m256 refFreqVec = _mm256_set1_ps(440.0f * 2.0f);
      const __m256 kVec = _mm256_set1_ps(k);
      const __m256 minValVec = _mm256_set1_ps(minValue);
      const __m256 sampleRateVec = _mm256_set1_ps(Config::options.audio.sample_rate);
      const __m256 fftSizeVec = _mm256_set1_ps(static_cast<float>(bins));
      const __m256 freqScaleVec = _mm256_div_ps(sampleRateVec, fftSizeVec);
      const __m256 incrementVec = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
#endif

      // Pre-calculate constants for scalar loop
      const float refFreq = 440.0f * 2.0f;

      // SIMD-optimized smoothing
#ifdef HAVE_AVX2
      for (; i + 7 < bins; i += 8) {
        __m256 cur = _mm256_loadu_ps(&fftMidRaw[i]);
        __m256 prev = _mm256_loadu_ps(&fftMid[i]);

        __m256 curDB = _mm256_mul_ps(_mm256_log10_ps(_mm256_add_ps(cur, minvVec)), dbScaleVec);
        __m256 prevDB = _mm256_mul_ps(_mm256_log10_ps(_mm256_add_ps(prev, minvVec)), dbScaleVec);

        __m256 diff = _mm256_sub_ps(curDB, prevDB);
        __m256 isRise = _mm256_cmp_ps(diff, _mm256_setzero_ps(), _CMP_GT_OS);

        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.f), diff);
        __m256 change = _mm256_mul_ps(_mm256_min_ps(absDiff, _mm256_blendv_ps(fallSpeedVec, riseSpeedVec, isRise)),
                                      _mm256_blendv_ps(negOneVec, oneVec, isRise));

        __m256 newDB =
            _mm256_blendv_ps(_mm256_add_ps(prevDB, change), curDB,
                             _mm256_cmp_ps(absDiff, _mm256_blendv_ps(fallSpeedVec, riseSpeedVec, isRise), _CMP_LE_OS));

        __m256 newVal = _mm256_sub_ps(_mm256_pow_ps(_mm256_set1_ps(10.f), _mm256_div_ps(newDB, dbScaleVec)), minvVec);

        __m256 frequencies;
        if (Config::options.fft.enable_cqt)
          frequencies = _mm256_loadu_ps(DSP::ConstantQ::frequencies.data() + i);
        else
          frequencies = _mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(static_cast<float>(i)), incrementVec), freqScaleVec);

        // Apply inverse slope correction: 1.0f / powf(f * 1.0f / (440.0f * 2.0f), k)
        newVal = _mm256_max_ps(
            newVal, _mm256_mul_ps(minValVec,
                                  _mm256_div_ps(oneVec, _mm256_pow_ps(_mm256_div_ps(frequencies, refFreqVec), kVec))));

        _mm256_storeu_ps(&fftMid[i], newVal);
      }
#endif
      // Standard smoothing
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftMidRaw[i] + FLT_EPSILON);
        float prevDB = 20.f * log10f(fftMid[i] + FLT_EPSILON);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        fftMid[i] = powf(10.f, newDB / 20.f) - FLT_EPSILON;

        float f;
        if (Config::options.fft.enable_cqt)
          f = DSP::ConstantQ::frequencies[i];
        else
          f = static_cast<float>(i) * (Config::options.audio.sample_rate / bins);
        float adjustedMinValue = minValue * 1.0f / powf(f / refFreq, k);

        if (fftMid[i] < adjustedMinValue) {
          fftMid[i] = adjustedMinValue;
        }
      }
    }
  }

  return 0;
}

int FFTAlt() {
  while (SDLWindow::running) {
    std::unique_lock<std::mutex> lock(mutex);
    fft.wait(lock, [] { return dataReadyFFTAlt.load() || !SDLWindow::running; });
    if (!SDLWindow::running)
      break;
    dataReadyFFTAlt = false;

    if (Config::options.phosphor.enabled)
      continue;

    // Process alternative channel FFT (for stereo visualization)
    if (Config::options.fft.enable_cqt) {
      ConstantQ::compute(bufferSide, fftSideRaw);
    } else {
      fftSideRaw.resize(Config::options.fft.size);
      size_t start = (writePos + bufferSize - Config::options.fft.size) % bufferSize;

      // Apply window function and prepare FFT input
      for (int i = 0; i < Config::options.fft.size; i++) {
        float win = 0.5f * (1.f - cos(2.f * M_PI * i / (Config::options.fft.size)));
        size_t pos = (start + i) % bufferSize;
        if (Config::options.fft.stereo_mode == "leftright")
          FFT::inSide[i] = (bufferSide[pos] + bufferMid[pos]) * 0.5f * win;
        else
          FFT::inSide[i] = bufferSide[pos] * win;
      }

      // Execute FFT
      {
        std::lock_guard<std::mutex> lockFft(FFT::mutexSide);
        fftwf_execute(FFT::side);
      }

      // Convert to magnitude spectrum
      const float scale = 2.f / Config::options.fft.size;
      for (int i = 0; i < Config::options.fft.size / 2 + 1; i++) {
        float mag = sqrt(FFT::outSide[i][0] * FFT::outSide[i][0] + FFT::outSide[i][1] * FFT::outSide[i][1]) * scale;
        if (i != 0 && i != Config::options.fft.size / 2)
          mag *= 2.f;
        fftSideRaw[i] = mag;
      }
    }

    // Apply smoothing to alternative channel
    if (Config::options.fft.enable_smoothing) {
      fftSide.resize(fftSideRaw.size());
      size_t bins = fftSide.size();
      size_t i = 0;
      const float riseSpeed = Config::options.fft.rise_speed * WindowManager::dt;
      const float fallSpeed =
          (SpectrumAnalyzer::window->hovering ? Config::options.fft.hover_fall_speed : Config::options.fft.fall_speed) *
          WindowManager::dt;
      // Calculate minimum value for clamping (1 dB below screen minimum)
      const float minValue = powf(10.f, (Config::options.fft.min_db - 1.0f) / 20.f);
      const float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);

      // Pre-calculate constants for AVX2
#ifdef HAVE_AVX2
      const __m256 riseSpeedVec = _mm256_set1_ps(riseSpeed);
      const __m256 fallSpeedVec = _mm256_set1_ps(fallSpeed);
      const __m256 minvVec = _mm256_set1_ps(FLT_EPSILON);
      const __m256 dbScaleVec = _mm256_set1_ps(20.0f);
      const __m256 oneVec = _mm256_set1_ps(1.0f);
      const __m256 negOneVec = _mm256_set1_ps(-1.0f);
      const __m256 refFreqVec = _mm256_set1_ps(440.0f * 2.0f);
      const __m256 kVec = _mm256_set1_ps(k);
      const __m256 minValVec = _mm256_set1_ps(minValue);
      const __m256 sampleRateVec = _mm256_set1_ps(Config::options.audio.sample_rate);
      const __m256 fftSizeVec = _mm256_set1_ps(static_cast<float>(bins));
      const __m256 freqScaleVec = _mm256_div_ps(sampleRateVec, fftSizeVec);
      const __m256 incrementVec = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
#endif

      // Pre-calculate constants for scalar loop
      const float refFreq = 440.0f * 2.0f;

      // SIMD-optimized smoothing
#ifdef HAVE_AVX2
      for (; i + 7 < bins; i += 8) {
        __m256 cur = _mm256_loadu_ps(&fftSideRaw[i]);
        __m256 prev = _mm256_loadu_ps(&fftSide[i]);

        __m256 curDB = _mm256_mul_ps(_mm256_log10_ps(_mm256_add_ps(cur, minvVec)), dbScaleVec);
        __m256 prevDB = _mm256_mul_ps(_mm256_log10_ps(_mm256_add_ps(prev, minvVec)), dbScaleVec);

        __m256 diff = _mm256_sub_ps(curDB, prevDB);
        __m256 isRise = _mm256_cmp_ps(diff, _mm256_setzero_ps(), _CMP_GT_OS);

        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.f), diff);
        __m256 change = _mm256_mul_ps(_mm256_min_ps(absDiff, _mm256_blendv_ps(fallSpeedVec, riseSpeedVec, isRise)),
                                      _mm256_blendv_ps(negOneVec, oneVec, isRise));

        __m256 newDB =
            _mm256_blendv_ps(_mm256_add_ps(prevDB, change), curDB,
                             _mm256_cmp_ps(absDiff, _mm256_blendv_ps(fallSpeedVec, riseSpeedVec, isRise), _CMP_LE_OS));

        __m256 newVal = _mm256_sub_ps(_mm256_pow_ps(_mm256_set1_ps(10.f), _mm256_div_ps(newDB, dbScaleVec)), minvVec);

        __m256 frequencies;
        if (Config::options.fft.enable_cqt)
          frequencies = _mm256_loadu_ps(DSP::ConstantQ::frequencies.data() + i);
        else
          frequencies = _mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(static_cast<float>(i)), incrementVec), freqScaleVec);

        // Apply inverse slope correction: 1.0f / powf(f * 1.0f / (440.0f * 2.0f), k)
        newVal = _mm256_max_ps(
            newVal, _mm256_mul_ps(minValVec,
                                  _mm256_div_ps(oneVec, _mm256_pow_ps(_mm256_div_ps(frequencies, refFreqVec), kVec))));

        _mm256_storeu_ps(&fftSide[i], newVal);
      }
#endif
      // Standard smoothing
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftSideRaw[i] + FLT_EPSILON);
        float prevDB = 20.f * log10f(fftSide[i] + FLT_EPSILON);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        float newVal = powf(10.f, newDB / 20.f) - FLT_EPSILON;

        // Apply slope correction and minimum value clamping (same as main FFT)
        float frequency;
        if (Config::options.fft.enable_cqt)
          frequency = DSP::ConstantQ::frequencies[i];
        else
          frequency = static_cast<float>(i) * Config::options.audio.sample_rate / static_cast<float>(bins);

        // Apply inverse slope correction: 1.0f / powf(f * 1.0f / (440.0f * 2.0f), k)
        float minVal = minValue / powf(frequency / refFreq, k);
        newVal = std::max(newVal, minVal);

        fftSide[i] = newVal;
      }
    }
  }

  return 0;
}

int mainThread() {
  LUFS::init();
  std::vector<float> readBuf;

  // Start FFT processing threads
  std::thread FFTMainThread(Threads::FFTMain);
  std::thread FFTAltThread(Threads::FFTAlt);

  while (SDLWindow::running) {
    // Calculate samples to read based on frame time
    size_t sampleCount = Config::options.audio.sample_rate / Config::options.window.fps_limit;
    readBuf.resize(sampleCount * 2);

    // Read audio from engine
    if (!AudioEngine::read(readBuf.data(), sampleCount)) {
      LOG_ERROR("Failed to read from audio engine");
    }

#if HAVE_PULSEAUDIO
    float gain = powf(10.0f, Config::options.audio.gain_db / 20.0f);
    if (AudioEngine::Pulseaudio::running) {
      size_t i = 0;
#ifdef HAVE_AVX2
      // SIMD-optimized audio processing for PulseAudio
      size_t simd_samples = (sampleCount * 2) & ~7;

      __m256i left_idx = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
      __m256i right_idx = _mm256_setr_epi32(1, 3, 5, 7, 0, 0, 0, 0);

      __m256 vgain = _mm256_set1_ps(0.5f * gain);

      for (; i < simd_samples; i += 8) {
        __m256 samples = _mm256_loadu_ps(&readBuf[i]);

        __m256 left = _mm256_permutevar8x32_ps(samples, left_idx);
        __m256 right = _mm256_permutevar8x32_ps(samples, right_idx);

        __m256 mid = _mm256_mul_ps(_mm256_add_ps(left, right), vgain);
        __m256 side = _mm256_mul_ps(_mm256_sub_ps(left, right), vgain);

        _mm_storeu_ps(&bufferMid[writePos], _mm256_castps256_ps128(mid));
        _mm_storeu_ps(&bufferSide[writePos], _mm256_castps256_ps128(side));
        writePos = (writePos + 4) % bufferSize;
      }
#endif
      for (; i < sampleCount * 2; i += 2) {
        float left = readBuf[i] * gain;
        float right = readBuf[i + 1] * gain;
        bufferMid[writePos] = (left + right) / 2.f;
        bufferSide[writePos] = (left - right) / 2.f;
        writePos = (writePos + 1) % bufferSize;
      }
    }
#endif

    // Signal FFT threads that new data is available
    {
      std::lock_guard<std::mutex> lock(mutex);
      dataReadyFFTMain = true;
      dataReadyFFTAlt = true;
      fft.notify_all();
    }

    // Process bandpass filter if pitch is detected
    if (pitch > Config::options.fft.min_freq && pitch < Config::options.fft.max_freq)
      FIR::process(pitch);

    // Process lowpass filter if enabled
    if (Config::options.oscilloscope.enable_lowpass)
      Lowpass::process();

    // Add samples to LUFS calculation from processed buffers and process LUFS
    {
      std::lock_guard<std::mutex> lock(LUFS::mutex);
      LUFS::addSamples(sampleCount);
      LUFS::process();
    }

    // Process peak detection
    Peak::process();

    // Process RMS calculation
    RMS::process();

    // Signal main thread that DSP processing is complete
    {
      std::lock_guard<std::mutex> lock(::mainThread);
      dataReady = true;
      mainCv.notify_one();
    }
  }

  // Ensure FFT threads are notified to exit
  {
    std::lock_guard<std::mutex> lock(mutex);
    dataReadyFFTMain = true;
    dataReadyFFTAlt = true;
    fft.notify_all();
  }

  FFTMainThread.join();
  FFTAltThread.join();
  LUFS::reset();
  return 0;
}
} // namespace Threads

namespace LUFS {

std::mutex mutex;

// LUFS value
float lufs = -70.0f;

// libebur128 state
ebur128_state* state = nullptr;

void init() {
  std::lock_guard<std::mutex> lock(mutex);
  if (state) {
    ebur128_destroy(&state);
  }

  state = ebur128_init(2, Config::options.audio.sample_rate, EBUR128_MODE_M | EBUR128_MODE_S | EBUR128_MODE_I);
  if (!state) {
    LOG_ERROR("Failed to initialize libebur128");
  }
}

void addSamples(size_t count) {
  if (!state)
    return;

  // Convert float samples to double for libebur128
  std::vector<double> doubleSamples(count * 2);

  // Get samples from the most recent buffer positions
  size_t startPos = (writePos - count + bufferSize) % bufferSize;

  for (size_t i = 0; i < count; i++) {
    size_t bufferIdx = (startPos + i) % bufferSize;

    // Convert mid/side back to left/right channels
    float mid = bufferMid[bufferIdx];
    float side = bufferSide[bufferIdx];
    float left = mid + side;
    float right = mid - side;

    doubleSamples[i * 2] = static_cast<double>(left);      // Left channel
    doubleSamples[i * 2 + 1] = static_cast<double>(right); // Right channel
  }

  ebur128_add_frames_double(state, doubleSamples.data(), count);
}

void process() {
  if (!state)
    return;

  double lufsd;
  if (Config::options.lufs.mode == "shortterm")
    ebur128_loudness_shortterm(state, &lufsd);
  else if (Config::options.lufs.mode == "momentary")
    ebur128_loudness_momentary(state, &lufsd);
  else if (Config::options.lufs.mode == "integrated")
    ebur128_loudness_global(state, &lufsd);
  else
    ebur128_loudness_momentary(state, &lufsd);
  lufs = static_cast<float>(lufsd);
}

void reset() {
  if (state) {
    ebur128_destroy(&state);
    state = nullptr;
  }
}

} // namespace LUFS

namespace Peak {
float left;
float right;

void process() {
  static size_t lastWritePos = 0;
  size_t numSamples = (writePos >= lastWritePos) ? (writePos - lastWritePos) : (bufferSize - lastWritePos + writePos);
  left = 0.0f;
  right = 0.0f;
  for (size_t i = 0; i < numSamples; ++i) {
    size_t bufferIdx = (lastWritePos + i) % bufferSize;
    float sampleMid = bufferMid[bufferIdx];
    float sampleSide = bufferSide[bufferIdx];
    float sampleLeft = sampleMid + sampleSide;
    float sampleRight = sampleMid - sampleSide;
    float absLeft = std::abs(sampleLeft);
    float absRight = std::abs(sampleRight);
    if (absLeft > left)
      left = absLeft;
    if (absRight > right)
      right = absRight;
  }
  lastWritePos = writePos;
}

} // namespace Peak

namespace RMS {
float rms;

void process() {
  size_t samples = Config::options.audio.sample_rate * Config::options.vu.time_window / 1000.0f;
  size_t start = (writePos - samples + bufferSize) % bufferSize;
  rms = 0.0f;
  for (size_t i = 0; i < samples; ++i) {
    size_t bufferIdx = (start + i) % bufferSize;
    float& sample = bufferMid[bufferIdx];
    rms += sample * sample;
  }
  rms /= samples;
  rms = sqrt(rms);
}

} // namespace RMS

// Template instantiations
template class AlignedAllocator<float, 32>;
template void ConstantQ::compute<AlignedAllocator<float, 32>>(const std::vector<float, AlignedAllocator<float, 32>>& in,
                                                              std::vector<float>& out);

} // namespace DSP