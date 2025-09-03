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

#pragma once
#include "common.hpp"

namespace DSP {

// Audio buffer data
extern std::vector<float, AlignedAllocator<float, 32>> bufferMid;
extern std::vector<float, AlignedAllocator<float, 32>> bufferSide;
extern std::vector<float, AlignedAllocator<float, 32>> bandpassed;
extern std::vector<float, AlignedAllocator<float, 32>> lowpassed;

// FFT data
extern std::vector<float> fftMidRaw;
extern std::vector<float> fftMid;
extern std::vector<float> fftMidPhase;
extern std::vector<float> fftSideRaw;
extern std::vector<float> fftSide;
extern std::vector<float> fftSidePhase;

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
 * @brief Linear phase FIR bandpass filter implementation
 */
namespace FIR {

struct Filter {
  std::vector<float> coeffs;
  std::vector<float> delay;
  size_t idx;
  size_t order;

  float process(float x);
  void set_coefficients(const std::vector<float>& coeffs);
};

extern Filter bandpass_filter;

void design(float center);
void process(float center);

} // namespace FIR

/**
 * @brief Lowpass filter implementation
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
template <typename Alloc>
void compute(const std::vector<float, Alloc>& in, std::vector<float>& out, std::vector<float>& phase);

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
int mainThread();

} // namespace Threads

/**
 * @brief LUFS measurement using libebur128
 */
namespace LUFS {
extern std::mutex mutex;
extern ebur128_state* state;
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