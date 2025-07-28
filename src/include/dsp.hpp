#pragma once
#include "common.hpp"

namespace DSP {
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

extern std::vector<float, AlignedAllocator<float, 32>> bufferMid;
extern std::vector<float, AlignedAllocator<float, 32>> bufferSide;
extern std::vector<float, AlignedAllocator<float, 32>> bandpassed;

extern std::vector<float> fftMidRaw;
extern std::vector<float> fftMid;
extern std::vector<float> fftSideRaw;
extern std::vector<float> fftSide;

extern const size_t bufferSize;
extern size_t writePos;

extern float pitch;
extern float pitchDB;

std::tuple<std::string, int, int> toNote(float freq, std::string* noteNames);

namespace Butterworth {
struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1 = 0.f, x2 = 0.f;
  float y1 = 0.f, y2 = 0.f;

  float process(float x);
  void reset();
};

extern std::vector<Biquad> biquads;

void design(float center = 10.0f);
void process(float center);
} // namespace Butterworth

namespace ConstantQ {
extern float Q;
extern size_t bins;
extern std::vector<float> frequencies;
extern std::vector<size_t> lengths;
extern std::vector<std::vector<float, AlignedAllocator<float, 32>>> reals;
extern std::vector<std::vector<float, AlignedAllocator<float, 32>>> imags;

void init();
void genMortletKernel(int bin);
void generate();
bool regenerate();
template <typename Alloc> void compute(const std::vector<float, Alloc>& in, std::vector<float>& out);
} // namespace ConstantQ

namespace FFT {
extern std::mutex mutexMid;
extern std::mutex mutexSide;
extern fftwf_plan mid;
extern fftwf_plan side;
extern float* inMid;
extern float* inSide;
extern fftwf_complex* outMid;
extern fftwf_complex* outSide;

void init();
void cleanup();
bool recreatePlans();
} // namespace FFT

namespace Threads {
extern std::mutex mutex;
extern std::atomic<bool> dataReadyFFTMain;
extern std::atomic<bool> dataReadyFFTAlt;
extern std::condition_variable fft;

int FFTMain();
int FFTAlt();
int main();
} // namespace Threads

} // namespace DSP