#pragma once
#include "common.hpp"

namespace DSP {

/**
 * @brief Aligned memory allocator for SIMD operations
 * @tparam T Element type
 * @tparam Alignment Memory alignment in bytes
 */
template <typename T, std::size_t Alignment> struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <class U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  template <class U> struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  T* allocate(std::size_t n);
  void deallocate(T* p, std::size_t) noexcept;
};

// Audio buffer data
extern std::vector<float, AlignedAllocator<float, 32>> bufferMid;
extern std::vector<float, AlignedAllocator<float, 32>> bufferSide;
extern std::vector<float, AlignedAllocator<float, 32>> bandpassed;
extern std::vector<float, AlignedAllocator<float, 32>> lowpassed;

// FFT data
extern std::vector<float> fftMidRaw;
extern std::vector<float> fftMid;
extern std::vector<float> fftSideRaw;
extern std::vector<float> fftSide;

extern const size_t bufferSize;
extern size_t writePos;

// Pitch detection
extern float pitch;
extern float pitchDB;

/**
 * @brief Convert frequency to musical note
 * @param freq Frequency in Hz
 * @param noteNames Array of note names
 * @return Tuple of (note name, octave, cents)
 */
std::tuple<std::string, int, int> toNote(float freq, std::string* noteNames);

/**
 * @brief Butterworth filter implementation
 */
namespace Butterworth {

/**
 * @brief Biquad filter structure
 */
struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1 = 0.f, x2 = 0.f;
  float y1 = 0.f, y2 = 0.f;

  /**
   * @brief Process input sample through filter
   * @param x Input sample
   * @return Filtered output sample
   */
  float process(float x);

  /**
   * @brief Reset filter state
   */
  void reset();
};

extern std::vector<Biquad> biquads;

/**
 * @brief Design bandpass filter
 * @param center Center frequency in Hz
 */
void design(float center = 10.0f);

/**
 * @brief Process audio through bandpass filter
 * @param center Center frequency in Hz
 */
void process(float center);

} // namespace Butterworth

/**
 * @brief Lowpass filter implementation using Butterworth filter
 */
namespace Lowpass {

/**
 * @brief Initialize lowpass filter
 */
void init();

/**
 * @brief Reconfigure lowpass filter if parameters have changed
 */
void reconfigure();

/**
 * @brief Process audio through lowpass filter
 */
void process();

} // namespace Lowpass

/**
 * @brief Constant Q Transform implementation
 */
namespace ConstantQ {
extern float Q;
extern size_t bins;
extern std::vector<float> frequencies;
extern std::vector<size_t> lengths;
extern std::vector<std::vector<float, AlignedAllocator<float, 32>>> reals;
extern std::vector<std::vector<float, AlignedAllocator<float, 32>>> imags;

/**
 * @brief Find frequency range indices in Constant Q Transform
 * @param f Target frequency
 * @return Pair of start and end indices
 */
std::pair<size_t, size_t> find(float f);

/**
 * @brief Initialize Constant Q Transform
 */
void init();

/**
 * @brief Generate Mortlet wavelet kernel for bin
 * @param bin Frequency bin index
 */
void genMortletKernel(int bin);

/**
 * @brief Generate all Constant Q kernels
 */
void generate();

/**
 * @brief Regenerate Constant Q kernels
 * @return true if successful
 */
bool regenerate();

/**
 * @brief Compute Constant Q Transform
 * @param in Input signal
 * @param out Output spectrum
 */
template <typename Alloc> void compute(const std::vector<float, Alloc>& in, std::vector<float>& out);

} // namespace ConstantQ

/**
 * @brief FFT processing namespace
 */
namespace FFT {
extern std::mutex mutexMid;
extern std::mutex mutexSide;
extern fftwf_plan mid;
extern fftwf_plan side;
extern float* inMid;
extern float* inSide;
extern fftwf_complex* outMid;
extern fftwf_complex* outSide;

/**
 * @brief Initialize FFT plans
 */
void init();

/**
 * @brief Cleanup FFT resources
 */
void cleanup();

/**
 * @brief Recreate FFT plans
 * @return true if successful
 */
bool recreatePlans();

} // namespace FFT

/**
 * @brief DSP processing threads namespace
 */
namespace Threads {
extern std::mutex mutex;
extern std::atomic<bool> dataReadyFFTMain;
extern std::atomic<bool> dataReadyFFTAlt;
extern std::condition_variable fft;

/**
 * @brief Main FFT processing thread
 * @return Thread exit code
 */
int FFTMain();

/**
 * @brief Alternative FFT processing thread
 * @return Thread exit code
 */
int FFTAlt();

/**
 * @brief Main DSP processing thread
 * @return Thread exit code
 */
int main();

} // namespace Threads

/**
 * @brief LUFS measurement using libebur128
 */
namespace LUFS {
extern float lufs;

/**
 * @brief Initialize LUFS measurement
 */
void init();

/**
 * @brief Add audio samples for LUFS calculation from processed buffers
 * @param count Number of samples to process
 */
void addSamples(size_t count);

/**
 * @brief Process LUFS calculation
 */
void process();

/**
 * @brief Reset LUFS measurement
 */
void reset();

} // namespace LUFS

namespace Peak {
extern float left;
extern float right;

/**
 * @brief Process peak detection
 */
void process();

} // namespace Peak

namespace RMS {
extern float rms;

/**
 * @brief process RMS calculation
 */
void process();
} // namespace RMS

} // namespace DSP