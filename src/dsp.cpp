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
  if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0)
    throw std::bad_alloc();
  return static_cast<T*>(ptr);
}

template <typename T, std::size_t Alignment>
void AlignedAllocator<T, Alignment>::deallocate(T* p, std::size_t) noexcept {
  free(p);
}

// Audio buffer data with SIMD alignment
std::vector<float, AlignedAllocator<float, 32>> bufferMid;
std::vector<float, AlignedAllocator<float, 32>> bufferSide;
std::vector<float, AlignedAllocator<float, 32>> bandpassed;

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

namespace Butterworth {

// Biquad filter coefficients
std::vector<Biquad> biquads;

float Biquad::process(float x) {
  // Direct Form II biquad filter implementation
  float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;
  return y;
}

void Biquad::reset() { x1 = x2 = y1 = y2 = 0.0f; }

void design(float center) {
  biquads.clear();
  int sections = Config::options.bandpass_filter.order / 2;
  float w0 = 2.f * M_PI * center / Config::options.audio.sample_rate;
  float bw = Config::options.bandpass_filter.bandwidth;
  if (Config::options.bandpass_filter.bandwidth_type == "percent")
    bw = center * bw / 100.0f;
  float Q = center / bw;

  // Design Butterworth bandpass filter
  for (int k = 0; k < sections; k++) {
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
}

void process(float center) {
  design(center);

  // Apply bandpass filter to audio buffer
  for (size_t i = 0; i < bufferSize; i++) {
    size_t readIdx = (writePos + i) % bufferSize;
    float filtered = bufferMid[readIdx];
    for (auto& bq : biquads)
      filtered = bq.process(filtered);
    bandpassed[readIdx] = filtered;
  }
}
} // namespace Butterworth

namespace ConstantQ {

// Constant Q Transform parameters
float Q;
size_t bins;
std::vector<float> frequencies;
std::vector<size_t> lengths;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> reals;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> imags;

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
    std::cerr << "Failed to allocate FFTW buffers" << std::endl;
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

  // Check if FFT plans need to be recreated
  if (lastFFTSize == Config::options.fft.size && lastCQTState == Config::options.fft.enable_cqt) [[likely]]
    return false;

  lastFFTSize = Config::options.fft.size;
  lastCQTState = Config::options.fft.enable_cqt;

  std::lock_guard<std::mutex> lockMid(mutexMid);
  std::lock_guard<std::mutex> lockSide(mutexSide);

  cleanup();
  init();

  if (!inMid || !inSide || !outMid || !outSide || !mid || !side)
    std::cerr << "Failed to allocate FFTW buffers" << std::endl;

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
      float dB = 20.f * log10f(mag + 1e-12f);
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
      if (std::abs(denom) > 1e-12f) {
        float offset = std::clamp(0.5f * (y1 - y3) / denom, -0.5f, 0.5f);
        float binInterp = static_cast<float>(peakBin) + offset;
        float logMinFreq = log2f(Config::options.fft.min_freq);
        float binRatio = 1.f / Config::options.fft.cqt_bins_per_octave;
        float intpLog = logMinFreq + binInterp * binRatio;
        peakFreq = powf(2.f, intpLog);
      } else
        peakFreq = ConstantQ::frequencies[peakBin];
    } else {
      if (std::abs(denom) > 1e-12f) {
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
          (SpectrumAnalyzer::window->hovering ? Config::options.fft.hover_fall_speed : Config::options.fft.fall_speed) *
          WindowManager::dt;
#ifdef HAVE_AVX2
      // SIMD-optimized smoothing
      for (; i + 7 < bins; i += 8) {
        __m256 cur = _mm256_loadu_ps(&fftMidRaw[i]);
        __m256 prev = _mm256_loadu_ps(&fftMid[i]);
        __m256 minv = _mm256_set1_ps(1e-12f);

        __m256 curDB = _mm256_log10_ps(_mm256_add_ps(cur, minv));
        curDB = _mm256_mul_ps(curDB, _mm256_set1_ps(20.0f));
        __m256 prevDB = _mm256_log10_ps(_mm256_add_ps(prev, minv));
        prevDB = _mm256_mul_ps(prevDB, _mm256_set1_ps(20.0f));

        __m256 diff = _mm256_sub_ps(curDB, prevDB);
        __m256 zero = _mm256_setzero_ps();
        __m256 isRise = _mm256_cmp_ps(diff, zero, _CMP_GT_OS);

        __m256 speed = _mm256_blendv_ps(_mm256_set1_ps(fallSpeed), _mm256_set1_ps(riseSpeed), isRise);
        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.f), diff);
        __m256 change = _mm256_min_ps(absDiff, speed);
        __m256 sign = _mm256_blendv_ps(_mm256_set1_ps(-1.f), _mm256_set1_ps(1.f), isRise);
        change = _mm256_mul_ps(change, sign);

        __m256 mask = _mm256_cmp_ps(absDiff, speed, _CMP_LE_OS);
        __m256 newDB = _mm256_blendv_ps(_mm256_add_ps(prevDB, change), curDB, mask);

        __m256 newVal = _mm256_pow_ps(_mm256_set1_ps(10.f), _mm256_div_ps(newDB, _mm256_set1_ps(20.f)));
        newVal = _mm256_sub_ps(newVal, minv);

        _mm256_storeu_ps(&fftMid[i], newVal);
      }
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftMidRaw[i] + 1e-12f);
        float prevDB = 20.f * log10f(fftMid[i] + 1e-12f);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        fftMid[i] = powf(10.f, newDB / 20.f) - 1e-12f;
      }
#else
      // Standard smoothing
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftMidRaw[i] + 1e-12f);
        float prevDB = 20.f * log10f(fftMid[i] + 1e-12f);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        fftMid[i] = powf(10.f, newDB / 20.f) - 1e-12f;
      }
#endif
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

#ifdef HAVE_AVX2
      // SIMD-optimized smoothing for alternative channel
      for (; i + 7 < bins; i += 8) {
        __m256 cur = _mm256_loadu_ps(&fftSideRaw[i]);
        __m256 prev = _mm256_loadu_ps(&fftSide[i]);
        __m256 minv = _mm256_set1_ps(1e-12f);

        __m256 curDB = _mm256_log10_ps(_mm256_add_ps(cur, minv));
        curDB = _mm256_mul_ps(curDB, _mm256_set1_ps(20.0f));
        __m256 prevDB = _mm256_log10_ps(_mm256_add_ps(prev, minv));
        prevDB = _mm256_mul_ps(prevDB, _mm256_set1_ps(20.0f));

        __m256 diff = _mm256_sub_ps(curDB, prevDB);
        __m256 zero = _mm256_setzero_ps();
        __m256 isRise = _mm256_cmp_ps(diff, zero, _CMP_GT_OS);

        __m256 speed = _mm256_blendv_ps(_mm256_set1_ps(fallSpeed), _mm256_set1_ps(riseSpeed), isRise);
        __m256 absDiff = _mm256_andnot_ps(_mm256_set1_ps(-0.f), diff);
        __m256 change = _mm256_min_ps(absDiff, speed);
        __m256 sign = _mm256_blendv_ps(_mm256_set1_ps(-1.f), _mm256_set1_ps(1.f), isRise);
        change = _mm256_mul_ps(change, sign);

        __m256 mask = _mm256_cmp_ps(absDiff, speed, _CMP_LE_OS);
        __m256 newDB = _mm256_blendv_ps(_mm256_add_ps(prevDB, change), curDB, mask);

        __m256 newVal = _mm256_pow_ps(_mm256_set1_ps(10.f), _mm256_div_ps(newDB, _mm256_set1_ps(20.f)));
        newVal = _mm256_sub_ps(newVal, minv);

        _mm256_storeu_ps(&fftSide[i], newVal);
      }
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftSideRaw[i] + 1e-12f);
        float prevDB = 20.f * log10f(fftSide[i] + 1e-12f);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        fftSide[i] = powf(10.f, newDB / 20.f) - 1e-12f;
      }
#else
      // Standard smoothing for alternative channel
      for (; i < bins; ++i) {
        float currentDB = 20.f * log10f(fftSideRaw[i] + 1e-12f);
        float prevDB = 20.f * log10f(fftSide[i] + 1e-12f);
        float diff = currentDB - prevDB;
        float speed = diff > 0 ? riseSpeed : fallSpeed;
        float absDiff = std::abs(diff);
        float change = std::min(absDiff, speed) * (diff > 0.f ? 1.f : -1.f);
        float newDB;
        if (absDiff <= speed)
          newDB = currentDB;
        else
          newDB = prevDB + change;
        fftSide[i] = powf(10.f, newDB / 20.f) - 1e-12f;
      }
#endif
    }
  }

  return 0;
}

int main() {
  Butterworth::design();
  std::vector<float> readBuf;

  // Start FFT processing threads
  std::thread FFTMainThread(Threads::FFTMain);
  std::thread FFTAltThread(Threads::FFTAlt);

  while (SDLWindow::running) {
    // Calculate samples to read based on frame time
    size_t sampleCount = Config::options.audio.sample_rate * WindowManager::dt;
    readBuf.resize(sampleCount * 2);

    // Read audio from engine
    if (!AudioEngine::read(readBuf.data(), sampleCount)) {
      std::cerr << "Failed to read from audio engine" << std::endl;
      SDLWindow::running = false;
      break;
    }

    float gain = powf(10.0f, Config::options.audio.gain_db / 20.0f);

#if HAVE_PULSEAUDIO
    if (AudioEngine::Pulseaudio::running) {
#ifdef HAVE_AVX2
      // SIMD-optimized audio processing for PulseAudio
      size_t simd_samples = (sampleCount * 2) & ~7;

      __m256i left_idx = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
      __m256i right_idx = _mm256_setr_epi32(1, 3, 5, 7, 0, 0, 0, 0);

      __m256 vgain = _mm256_set1_ps(0.5f * gain);

      for (size_t i = 0; i < simd_samples; i += 8) {
        __m256 samples = _mm256_loadu_ps(&readBuf[i]);

        __m256 left = _mm256_permutevar8x32_ps(samples, left_idx);
        __m256 right = _mm256_permutevar8x32_ps(samples, right_idx);

        __m256 mid = _mm256_mul_ps(_mm256_add_ps(left, right), vgain);
        __m256 side = _mm256_mul_ps(_mm256_sub_ps(left, right), vgain);

        _mm_storeu_ps(&bufferMid[writePos], _mm256_castps256_ps128(mid));
        _mm_storeu_ps(&bufferSide[writePos], _mm256_castps256_ps128(side));
        writePos = (writePos + 4) % bufferSize;
      }

      for (size_t i = simd_samples; i < sampleCount * 2; i += 2) {
#else
      for (int i = 0; i < sampleCount * 2; i += 2) {
#endif
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
      Butterworth::process(pitch);

    // Signal main thread that DSP processing is complete
    {
      std::lock_guard<std::mutex> lock(::mainThread);
      ::dataReady = true;
      ::mainCv.notify_one();
    }
  }

  FFTMainThread.join();
  FFTAltThread.join();
  return 0;
}
} // namespace Threads

// Template instantiations
template class AlignedAllocator<float, 32>;
template void ConstantQ::compute<AlignedAllocator<float, 32>>(const std::vector<float, AlignedAllocator<float, 32>>& in,
                                                              std::vector<float>& out);

} // namespace DSP