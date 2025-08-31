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

#define _USE_MATH_DEFINES

#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <fftw3.h>
#include <filesystem>
#include <fstream>
#include <ft2build.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include FT_FREETYPE_H
#include <atomic>
#include <cmath>
#include <complex>
#include <deque>
#include <ebur128.h>
#include <functional>
#include <numeric>
#include <signal.h>
#include <yaml-cpp/yaml.h>

// Define to shut up intellisense
#ifndef PULSE_DATA_DIR
#define PULSE_DATA_DIR ""
#endif
#ifndef VERSION_STRING
#define VERSION_STRING "0.0.0"
#endif
#ifndef VERSION_COMMIT
#define VERSION_COMMIT "0000000"
#endif
#ifndef VERSION_FULL
#define VERSION_FULL "0.0.0-0000000"
#endif

namespace CmdlineArgs {
extern bool debug;
extern bool help;
extern bool console;
} // namespace CmdlineArgs

// constexpr recursive function template to isolate filename
template <std::size_t N> constexpr const char* constexpr_strrchr(const char (&str)[N], std::size_t pos = N - 2) {
  return pos == static_cast<std::size_t>(-1)
             ? str
             : (str[pos] == '/' || str[pos] == '\\' ? str + pos + 1 : constexpr_strrchr(str, pos - 1));
}

// Logging macros
#define LOG_ERROR(x)                                                                                                   \
  std::cout << "ERROR in " << constexpr_strrchr(__FILE__) << "#" << __LINE__ << ": " << x << std::endl;
#define LOG_DEBUG(x)                                                                                                   \
  if (CmdlineArgs::debug)                                                                                              \
    std::cout << "DEBUG in " << constexpr_strrchr(__FILE__) << "#" << __LINE__ << ": " << x << std::endl;

#ifdef HAVE_AVX2
#include <immintrin.h>

#ifndef HAVE_MM256_LOG10_PS

/**
 * @brief Computes log10 for 8 float values using AVX2
 * @param x Vector of 8 float values
 * @return Vector with log10 applied to each element
 */
static inline __m256 _mm256_log10_ps(__m256 x) {
  float vals[8];
  _mm256_storeu_ps(vals, x);
  for (int i = 0; i < 8; ++i)
    vals[i] = log10f(vals[i]);
  return _mm256_loadu_ps(vals);
}

#endif

#ifndef HAVE_MM256_POW_PS

/**
 * @brief Computes power function for 8 float values using AVX2
 * @param a Base values vector
 * @param b Exponent values vector
 * @return Vector with pow(a, b) applied to each element
 */
static inline __m256 _mm256_pow_ps(__m256 a, __m256 b) {
  float va[8], vb[8], vr[8];
  _mm256_storeu_ps(va, a);
  _mm256_storeu_ps(vb, b);
  for (int i = 0; i < 8; ++i)
    vr[i] = powf(va[i], vb[i]);
  return _mm256_loadu_ps(vr);
}

#endif

/**
 * @brief Reduces 8 float values to a single sum using AVX2
 * @param v Vector of 8 float values
 * @return Sum of all elements in the vector
 */
inline float avx2_reduce_add_ps(__m256 v) {
  __m128 vlow = _mm256_castps256_ps128(v);
  __m128 vhigh = _mm256_extractf128_ps(v, 1);
  vlow = _mm_add_ps(vlow, vhigh);
  vlow = _mm_hadd_ps(vlow, vlow);
  vlow = _mm_hadd_ps(vlow, vlow);
  return _mm_cvtss_f32(vlow);
}
#endif

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define NOMINMAX
#include <comdef.h>
#include <cstddef>
// clang-format off
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on

typedef ptrdiff_t ssize_t;
#endif

#if HAVE_PULSEAUDIO
#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/simple.h>
#endif

#if HAVE_PIPEWIRE
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#endif

#if HAVE_WASAPI

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>
#include <audioclient.h>
#include <initguid.h>
#include <mmdeviceapi.h>

#endif

#ifndef FLT_EPSILON
#define FLT_EPSILON 1e-6f
#endif

// Thread synchronization variables for DSP data processing
extern std::mutex mainThread;
extern std::condition_variable mainCv;
extern std::atomic<bool> dataReady;

/**
 * @brief Expands a path starting with '~' to the user's home directory
 * @param path The path to expand
 * @return The expanded path with home directory substituted
 */
std::string expandUserPath(const std::string& path);