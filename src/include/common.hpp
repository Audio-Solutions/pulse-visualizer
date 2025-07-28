#pragma once

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
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
#include <vector>
#include FT_FREETYPE_H
#include <yaml-cpp/yaml.h>

// Define to shut up intellisense
#ifndef PULSE_DATA_DIR
#define PULSE_DATA_DIR ""
#endif

#ifdef HAVE_AVX2
#include <mmintrin.h>
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

inline float avx2_reduce_add_ps(__m256 v) {
  __m128 vlow = _mm256_castps256_ps128(v);
  __m128 vhigh = _mm256_extractf128_ps(v, 1);
  vlow = _mm_add_ps(vlow, vhigh);
  vlow = _mm_hadd_ps(vlow, vlow);
  vlow = _mm_hadd_ps(vlow, vlow);
  return _mm_cvtss_f32(vlow);
}
#endif

template <typename T> inline T lerp(T a, T b, T t) { return a + (b - a) * t; }

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
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

extern std::mutex mainThread;
extern std::condition_variable mainCv;
extern std::atomic<bool> dataReady;

std::string expandUserPath(const std::string& path);