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

// ------------------------------
// |          TEMPORARY         |
// ------------------------------

#ifndef HAVE_AVX2
#define HAVE_AVX2
#endif

// #ifdef HAVE_AVX2
// #undef HAVE_AVX2
// #endif

#ifndef HAVE_PULSEAUDIO
#define HAVE_PULSEAUDIO 1
#endif

#ifndef HAVE_PIPEWIRE
#define HAVE_PIPEWIRE 1
#endif

// ------------------------------
// |          TEMPORARY         |
// ------------------------------

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

void checkGLError(const char* location) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    std::cerr << "OpenGL error 0x" << std::hex << err << " at " << location << std::dec << std::endl;
  }
}

std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

std::mutex mainThread;
std::condition_variable mainCv;
std::atomic<bool> dataReady;

namespace WindowManager {
float dt;
}

namespace Config {
void copyFiles() {
  const char* home = getenv("HOME");
  if (!home) {
    std::cerr << "Warning: HOME environment variable not set, cannot setup user config" << std::endl;
    return;
  }

  std::string userCfgDir = std::string(home) + "/.config/pulse-visualizer";
  std::string userThemeDir = userCfgDir + "/themes";
  std::string userFontDir = std::string(home) + "/.local/share/fonts/JetBrainsMono";

  std::filesystem::create_directories(userCfgDir);
  std::filesystem::create_directories(userThemeDir);
  std::filesystem::create_directories(userFontDir);

  if (!std::filesystem::exists(userCfgDir + "/config.yml")) {
    std::string cfgSource = std::string(PULSE_DATA_DIR) + "/config.yml.template";
    if (std::filesystem::exists(cfgSource)) {
      try {
        std::filesystem::copy_file(cfgSource, userCfgDir + "/config.yml");
        std::cout << "Created user config file: " << userCfgDir << "/config.yml" << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to copy config template: " << e.what() << std::endl;
      }
    }
  }

  std::string themeSource = std::string(PULSE_DATA_DIR) + "/themes";
  if (std::filesystem::exists(themeSource) && !std::filesystem::exists(themeSource + "/_TEMPLATE.txt")) {
    try {
      for (const auto& entry : std::filesystem::directory_iterator(themeSource)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
          std::string destFile = userThemeDir + "/" + entry.path().filename().string();
          if (!std::filesystem::exists(destFile)) {
            std::filesystem::copy_file(entry.path(), destFile);
          }
        }
      }
      std::cout << "Copied themes to: " << userThemeDir << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "Warning: Failed to copy themes: " << e.what() << std::endl;
    }
  }

  // Copy font from system installation
  std::string fontSource = std::string(PULSE_DATA_DIR) + "/fonts/JetBrainsMonoNerdFont-Medium.ttf";
  std::string userFontFile = userFontDir + "/JetBrainsMonoNerdFont-Medium.ttf";
  if (std::filesystem::exists(fontSource)) {
    try {
      if (!std::filesystem::exists(userFontFile)) {
        std::filesystem::copy_file(fontSource, userFontFile);
        std::cout << "Copied font to: " << userFontFile << std::endl;
      }
    } catch (const std::exception& e) {
      std::cerr << "Warning: Failed to copy font: " << e.what() << std::endl;
    }
  }
}

struct Options {
  struct Oscilloscope {
    bool follow_pitch = true;
    std::string alignment = "left";
    std::string alignment_type = "peak";
    bool limit_cycles = false;
    int cycles = 1;
    float min_cycle_time = 16.0f;
    float time_window = 64.0f;
    float beam_multiplier = 1.0f;
  } oscilloscope;

  struct BandpassFilter {
    float bandwidth = 50.0f;
    std::string bandwidth_type = "hz";
    int order = 8;
  } bandpass_filter;

  struct Lissajous {
    bool enable_splines = true;
    int spline_segments = 8;
    float beam_multiplier = 1.0f;
    float readback_multiplier = 1.0f;
  } lissajous;

  struct FFT {
    float min_freq = 10.0f;
    float max_freq = 20000.0f;
    float sample_rate = 44100.0f;
    float slope_correction_db = 4.5f;
    float min_db = -60.0f;
    float max_db = 10.0f;
    std::string stereo_mode = "midside";
    std::string note_key_mode = "sharp";
    bool enable_cqt = false;
    int cqt_bins_per_octave = 24;
    bool enable_smoothing = false;
    float rise_speed = 500.0f;
    float fall_speed = 50.0f;
    float hover_fall_speed = 10.0f;
    int size = 4096;
    float beam_multiplier = 1.0f;
    bool frequency_markers = false;
  } fft;

  struct Spectrogram {
    float time_window = 2.0f;
    float min_db = -80.0f;
    float max_db = -10.0f;
    bool interpolation = true;
    std::string frequency_scale = "log";
    float min_freq = 20.0f;
    float max_freq = 20000.0f;
  } spectrogram;

  struct Audio {
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    float gain_db = 0.0f;
    std::string engine = "auto";
    std::string device = "auto";
  } audio;

  struct Visualizers {
    int spectrogram_order = 0;
    int lissajous_order = 1;
    int oscilloscope_order = 2;
    int fft_order = 3;
  } visualizers;

  struct Window {
    int default_width = 1080;
    int default_height = 200;
    std::string theme = "mocha.txt";
    int fps_limit = 240;
  } window;

  struct Debug {
    bool log_fps = false;
    bool show_bandpassed = false;
  } debug;

  struct Phosphor {
    bool enabled = false;
    float near_blur_intensity = 0.2f;
    float far_blur_intensity = 0.6f;
    float beam_energy = 100.0f;
    float decay_slow = 10.0f;
    float decay_fast = 100.0f;
    float beam_size = 2.0f;
    float line_blur_spread = 16.0f;
    float line_width = 2.0f;
    int age_threshold = 256;
    float range_factor = 5.0f;
    bool enable_grain = true;
    int spline_density = 20;
    float tension = 0.5f;
    bool enable_curved_screen = false;
    float screen_curvature = 0.3f;
    float screen_gap = 0.05f;
    float grain_strength = 0.5f;
    float vignette_strength = 0.3f;
    float chromatic_aberration_strength = 0.003f;
  } phosphor;

  std::string font;
} options;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path) {
  if (root[path].IsDefined())
    return root[path];

  size_t dotIndex = path.find('.');
  if (dotIndex == std::string::npos)
    return std::nullopt;

  std::string section = path.substr(0, dotIndex);
  std::string sub = path.substr(dotIndex + 1);

  if (!root[section].IsDefined())
    return std::nullopt;

  return getNode(root[section], sub);
}

template <typename T> T get(const YAML::Node& root, const std::string& path) {
  auto node = getNode(root, path);
  if (node.has_value())
    return node.value().as<T>();

  std::cerr << path << " is missing." << std::endl;

  return T {};
}

void load() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData = YAML::LoadFile(path);

#ifdef __linux__
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyWatch != -1)
      inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = inotify_add_watch(inotifyFd, path.c_str(), IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
  }
#endif

  options.oscilloscope.follow_pitch = get<bool>(configData, "oscilloscope.follow_pitch");
  options.oscilloscope.alignment = get<std::string>(configData, "oscilloscope.alignment");
  options.oscilloscope.alignment_type = get<std::string>(configData, "oscilloscope.alignment_type");
  options.oscilloscope.limit_cycles = get<bool>(configData, "oscilloscope.limit_cycles");
  options.oscilloscope.cycles = get<int>(configData, "oscilloscope.cycles");
  options.oscilloscope.min_cycle_time = get<float>(configData, "oscilloscope.min_cycle_time");
  options.oscilloscope.time_window = get<float>(configData, "oscilloscope.time_window");
  options.oscilloscope.beam_multiplier = get<float>(configData, "oscilloscope.beam_multiplier");

  options.bandpass_filter.bandwidth = get<float>(configData, "bandpass_filter.bandwidth");
  options.bandpass_filter.bandwidth_type = get<std::string>(configData, "bandpass_filter.bandwidth_type");
  options.bandpass_filter.order = get<int>(configData, "bandpass_filter.order");

  options.lissajous.enable_splines = get<bool>(configData, "lissajous.enable_splines");
  options.lissajous.spline_segments = get<int>(configData, "lissajous.spline_segments");
  options.lissajous.beam_multiplier = get<float>(configData, "lissajous.beam_multiplier");
  options.lissajous.readback_multiplier = get<float>(configData, "lissajous.readback_multiplier");

  options.fft.min_freq = get<float>(configData, "fft.min_freq");
  options.fft.max_freq = get<float>(configData, "fft.max_freq");
  options.fft.sample_rate = get<float>(configData, "audio.sample_rate");
  options.fft.slope_correction_db = get<float>(configData, "fft.slope_correction_db");
  options.fft.min_db = get<float>(configData, "fft.min_db");
  options.fft.max_db = get<float>(configData, "fft.max_db");
  options.fft.stereo_mode = get<std::string>(configData, "fft.stereo_mode");
  options.fft.note_key_mode = get<std::string>(configData, "fft.note_key_mode");
  options.fft.enable_cqt = get<bool>(configData, "fft.enable_cqt");
  options.fft.cqt_bins_per_octave = get<int>(configData, "fft.cqt_bins_per_octave");
  options.fft.enable_smoothing = get<bool>(configData, "fft.enable_smoothing");
  options.fft.rise_speed = get<float>(configData, "fft.rise_speed");
  options.fft.fall_speed = get<float>(configData, "fft.fall_speed");
  options.fft.hover_fall_speed = get<float>(configData, "fft.hover_fall_speed");
  options.fft.size = get<int>(configData, "fft.size");
  options.fft.beam_multiplier = get<float>(configData, "fft.beam_multiplier");
  options.fft.frequency_markers = get<bool>(configData, "fft.frequency_markers");

  options.spectrogram.time_window = get<float>(configData, "spectrogram.time_window");
  options.spectrogram.min_db = get<float>(configData, "spectrogram.min_db");
  options.spectrogram.max_db = get<float>(configData, "spectrogram.max_db");
  options.spectrogram.interpolation = get<bool>(configData, "spectrogram.interpolation");
  options.spectrogram.frequency_scale = get<std::string>(configData, "spectrogram.frequency_scale");
  options.spectrogram.min_freq = get<float>(configData, "spectrogram.min_freq");
  options.spectrogram.max_freq = get<float>(configData, "spectrogram.max_freq");

  options.audio.silence_threshold = get<float>(configData, "audio.silence_threshold");
  options.audio.sample_rate = get<float>(configData, "audio.sample_rate");
  options.audio.gain_db = get<float>(configData, "audio.gain_db");
  options.audio.engine = get<std::string>(configData, "audio.engine");
  options.audio.device = get<std::string>(configData, "audio.device");

  options.visualizers.spectrogram_order = get<int>(configData, "visualizers.spectrogram_order");
  options.visualizers.lissajous_order = get<int>(configData, "visualizers.lissajous_order");
  options.visualizers.oscilloscope_order = get<int>(configData, "visualizers.oscilloscope_order");
  options.visualizers.fft_order = get<int>(configData, "visualizers.fft_order");

  options.window.default_width = get<int>(configData, "window.default_width");
  options.window.default_height = get<int>(configData, "window.default_height");
  options.window.theme = get<std::string>(configData, "window.theme");
  options.window.fps_limit = get<int>(configData, "window.fps_limit");

  options.debug.log_fps = get<bool>(configData, "debug.log_fps");
  options.debug.show_bandpassed = get<bool>(configData, "debug.show_bandpassed");

  options.phosphor.enabled = get<bool>(configData, "phosphor.enabled");
  options.phosphor.near_blur_intensity = get<float>(configData, "phosphor.near_blur_intensity");
  options.phosphor.far_blur_intensity = get<float>(configData, "phosphor.far_blur_intensity");
  options.phosphor.beam_energy = get<float>(configData, "phosphor.beam_energy");
  options.phosphor.decay_slow = get<float>(configData, "phosphor.decay_slow");
  options.phosphor.decay_fast = get<float>(configData, "phosphor.decay_fast");
  options.phosphor.beam_size = get<float>(configData, "phosphor.beam_size");
  options.phosphor.line_blur_spread = get<float>(configData, "phosphor.line_blur_spread");
  options.phosphor.line_width = get<float>(configData, "phosphor.line_width");
  options.phosphor.age_threshold = get<int>(configData, "phosphor.age_threshold");
  options.phosphor.range_factor = get<float>(configData, "phosphor.range_factor");
  options.phosphor.enable_grain = get<bool>(configData, "phosphor.enable_grain");
  options.phosphor.spline_density = get<int>(configData, "phosphor.spline_density");
  options.phosphor.tension = get<float>(configData, "phosphor.tension");
  options.phosphor.enable_curved_screen = get<bool>(configData, "phosphor.enable_curved_screen");
  options.phosphor.screen_curvature = get<float>(configData, "phosphor.screen_curvature");
  options.phosphor.screen_gap = get<float>(configData, "phosphor.screen_gap");
  options.phosphor.grain_strength = get<float>(configData, "phosphor.grain_strength");
  options.phosphor.vignette_strength = get<float>(configData, "phosphor.vignette_strength");
  options.phosphor.chromatic_aberration_strength = get<float>(configData, "phosphor.chromatic_aberration_strength");

  options.font = get<std::string>(configData, "font");
}

bool reload() {
#ifdef __linux__
  if (inotifyFd != -1 && inotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(inotifyFd, buf, sizeof(buf));
    if (len > 0) {
      load();
      return true;
    }
  }
#else
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    std::cerr << "Warning: could not stat config file." << std::endl;
    return false;
  }
  static time_t lastConfigMTime = 0;
  if (st.st_mtime != lastConfigMTime) {
    load();
    return true;
  }
#endif
  return false;
}
} // namespace Config

namespace DSP {
template <typename T, std::size_t Alignment> struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <class U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  template <class U> struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  T* allocate(std::size_t n) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0)
      throw std::bad_alloc();
    return static_cast<T*>(ptr);
  }

  void deallocate(T* p, std::size_t) noexcept { free(p); }
};

std::vector<float, AlignedAllocator<float, 32>> bufferMid;
std::vector<float, AlignedAllocator<float, 32>> bufferSide;
std::vector<float, AlignedAllocator<float, 32>> bandpassed;

std::vector<float> fftMidRaw;
std::vector<float> fftMid;
std::vector<float> fftSideRaw;
std::vector<float> fftSide;

const size_t bufferSize = 32768;
size_t writePos = 0;

float pitch;
float pitchDB;

std::tuple<std::string, int, int> toNote(float freq, std::string* noteNames) {
  if (freq < Config::options.fft.min_freq || freq > Config::options.fft.max_freq)
    return std::make_tuple("-", 0, 0);

  float midi = 69.f + 12.f * log2f(freq / 440.f);

  if (midi < 0.f || midi > 127.f)
    return std::make_tuple("-", 0, 0);

  int roundedMidi = static_cast<int>(roundf(midi));
  int noteIdx = (roundedMidi + 1200) % 12;
  return std::make_tuple(noteNames[noteIdx], roundedMidi / 12 - 1,
                         static_cast<int>(std::round((midi - static_cast<float>(roundedMidi)) * 100.f)));
}

namespace Butterworth {
struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1 = 0.f, x2 = 0.f;
  float y1 = 0.f, y2 = 0.f;

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

void design(float center = 10.0f) {
  biquads.clear();
  int sections = Config::options.bandpass_filter.order / 2;
  float w0 = 2.f * M_PI * center / Config::options.audio.sample_rate;
  float bw = Config::options.bandpass_filter.bandwidth;
  if (Config::options.bandpass_filter.bandwidth_type == "percent")
    bw = center * bw / 100.0f;
  float Q = center / bw;

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
float Q;
size_t bins;
std::vector<float> frequencies;
std::vector<size_t> lengths;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> reals;
std::vector<std::vector<float, AlignedAllocator<float, 32>>> imags;

void init() {
  float octaves = log2f(Config::options.fft.max_freq / Config::options.fft.min_freq);
  bins = std::clamp(static_cast<size_t>(ceil(octaves * Config::options.fft.cqt_bins_per_octave)),
                    static_cast<size_t>(1), static_cast<size_t>(1000));
  frequencies.resize(bins);
  lengths.resize(bins);
  reals.resize(bins);
  imags.resize(bins);

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

  if (norm > 0.f) {
    float ampNorm = 1.f / norm;
    for (size_t n = 0; n < length; n++) {
      reals[bin][n] *= ampNorm;
      imags[bin][n] *= ampNorm;
    }
  }
}

void generate() {
  for (int k = 0; k < bins; k++) {
    genMortletKernel(k);
  }
}

bool regenerate() {
  static int lastCQTBins = Config::options.fft.cqt_bins_per_octave;
  static float lastMinFreq = Config::options.fft.min_freq;
  static float lastMaxFreq = Config::options.fft.max_freq;

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
  auto process_segment = [](const float* buf, const float* real, const float* imag,
                            size_t length) -> std::pair<float, float> {
    size_t n = 0;
    __m256 realSumVec = _mm256_setzero_ps();
    __m256 imagSumVec = _mm256_setzero_ps();

    bool aligned = (((uintptr_t)buf % 32) == 0) && (((uintptr_t)real % 32) == 0) && (((uintptr_t)imag % 32) == 0);

    // Vectorized accumulate
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

    // Horizontal add vector sums to scalars
    float realSum = avx2_reduce_add_ps(realSumVec);
    float imagSum = avx2_reduce_add_ps(imagSumVec);

    // Process leftover scalar elements
    for (; n < length; ++n) {
      float sample = buf[n];
      realSum += sample * real[n];
      imagSum -= sample * imag[n];
    }

    return {realSum, imagSum};
  };
#endif // HAVE_AVX2

  for (int k = 0; k < bins; k++) {
    const auto& kReals = reals[k];
    const auto& kImags = imags[k];
    size_t length = lengths[k];
    size_t start = (writePos + bufferSize - length) % bufferSize;

    float realSum = 0.f;
    float imagSum = 0.f;

#ifdef HAVE_AVX2
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

  if (lastFFTSize == Config::options.fft.size && lastCQTState == Config::options.fft.enable_cqt) [[likely]]
    return false;

  lastFFTSize = Config::options.fft.size;
  lastCQTState = Config::options.fft.enable_cqt;

  std::lock_guard<std::mutex> lockMid(mutexSide);
  std::lock_guard<std::mutex> lockSide(mutexMid);

  cleanup();
  init();

  if (!inMid || !inSide || !outMid || !outSide || !mid || !side)
    std::cerr << "Failed to allocate FFTW buffers" << std::endl;

  return true;
}
} // namespace FFT
} // namespace DSP

namespace AudioEngine {
enum Type { PULSEAUDIO, PIPEWIRE, AUTO };

Type toType(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);

  if (str == "pulseaudio" || str == "pulse" || str == "pa")
    return Type::PULSEAUDIO;
  else if (str == "pipewire" || str == "pw")
    return Type::PIPEWIRE;
  return Type::AUTO;
}

namespace Pulseaudio {
#if HAVE_PULSEAUDIO
pa_simple* paStream;
bool running = false;
bool initialized = false;

struct DeviceInfo {
  uint32_t index;
  std::string name;
  std::string desc;
  bool isMonitor;
};

std::vector<DeviceInfo> availableSources;
std::string defaultSink;

void sourceInfoCallback(pa_context* ctx, const pa_source_info* info, int eol, void*) {
  if (eol || !info)
    return;

  DeviceInfo dev;
  dev.index = info->index;
  dev.name = info->name;
  dev.desc = info->description;
  dev.isMonitor = info->monitor_of_sink != PA_INVALID_INDEX;
  availableSources.push_back(dev);
}

void enumerate() {
  availableSources.clear();
  defaultSink.clear();

  pa_mainloop* ml = pa_mainloop_new();
  if (!ml)
    return;

  pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "pulse-device-enum");
  if (!ctx) {
    pa_mainloop_free(ml);
    return;
  }

  pa_context_set_state_callback(ctx, [](pa_context*, void*) {}, nullptr);

  if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return;
  }

  bool done = false;
  do {
    if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
      ;
    break;

    pa_context_state_t state = pa_context_get_state(ctx);
    if (state == PA_CONTEXT_READY) {
      pa_operation* opServer = pa_context_get_server_info(
          ctx,
          [](pa_context* ctx, const pa_server_info* info, void*) {
            if (!info)
              return;

            if (info->default_sink_name)
              defaultSink = info->default_sink_name;
          },
          nullptr);

      if (opServer) {
        while (pa_operation_get_state(opServer) == PA_OPERATION_RUNNING)
          pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(opServer);
      }

      pa_operation* opSources = pa_context_get_source_info_list(ctx, sourceInfoCallback, nullptr);
      if (opSources) {
        while (pa_operation_get_state(opSources) == PA_OPERATION_RUNNING)
          pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(opSources);
      }
      done = true;
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      done = true;
    }
  } while (!done);

  pa_context_disconnect(ctx);
  pa_context_unref(ctx);
  pa_mainloop_free(ml);
}

std::string find(std::string dev) {
  if (dev.empty()) {
    if (defaultSink.empty())
      return "default";

    dev = defaultSink + ".monitor";
  }

  for (const auto& source : availableSources) {
    if (source.name.find(dev) != std::string::npos ||
        source.name.find(dev.substr(0, dev.find(".monitor"))) != std::string::npos) {
      return source.name;
    }
  }

  std::cout << "Warning: PulseAudio device '" << dev << "' not found. using system default" << std::endl;
  return "default";
}

void cleanup() {
  if (paStream) {
    pa_simple_free(paStream);
    paStream = nullptr;
  }
  initialized = false;
  running = false;
}

bool init() {
  if (initialized)
    cleanup();

  enumerate();

  pa_sample_spec sampleSpec;
  sampleSpec.rate = Config::options.audio.sample_rate;
  sampleSpec.channels = 2;

  uint32_t samples = 0;
  try {
    samples = static_cast<uint32_t>(sampleSpec.rate / Config::options.window.fps_limit);
  } catch (...) {
    samples = 512;
  }

  const uint32_t bytesPerSample = sampleSpec.channels * sizeof(float);
  const uint32_t bufferSize = samples * bytesPerSample;

  pa_buffer_attr attr = {.maxlength = bufferSize * 4,
                         .tlength = bufferSize,
                         .prebuf = static_cast<uint32_t>(-1),
                         .minreq = bufferSize / 2,
                         .fragsize = bufferSize};

  std::string dev = find(Config::options.audio.device);

  int err = 0;
  paStream = pa_simple_new(nullptr, "Pulse Visualizer", PA_STREAM_RECORD, dev.c_str(), "Pulse Audio Visualizer",
                           &sampleSpec, nullptr, &attr, &err);

  if (!paStream) {
    std::cerr << "Failed to create PulseAudio stream: " << pa_strerror(err) << std::endl;
    return false;
  }

  initialized = true;
  running = true;

  if (dev.empty())
    return true;

  std::cout << "Connected to PulseAudio device '" << dev << "'" << std::endl;
  return true;
}

bool read(float* buffer, const size_t& samples) {
  if (!initialized || !paStream)
    return false;

  int err;
  if (pa_simple_read(paStream, buffer, samples * 2 * sizeof(float), &err) < 0) {
    std::cout << "Failed to read from pulseAudio stream: " << pa_strerror(err) << std::endl;
    running = false;
    return false;
  }

  return true;
}

bool reconfigure() {
  static std::string lastDevice = Config::options.audio.device;
  static uint32_t lastSampleRate = Config::options.audio.sample_rate;
  static size_t lastBuffer = DSP::bufferSize;

  if (lastDevice == Config::options.audio.device && lastBuffer == DSP::bufferSize &&
      lastSampleRate == Config::options.audio.sample_rate) [[likely]]
    return false;

  lastSampleRate = Config::options.audio.sample_rate;
  lastBuffer = DSP::bufferSize;
  lastDevice = Config::options.audio.device;

  init();

  return true;
}
#else
bool init() { return false; }
bool read(float*, size_t) { return false; }
bool reconfigure(const std::string&, uint32_t, size_t) { return false; }
#endif
} // namespace Pulseaudio

namespace PipeWire {
#if HAVE_PIPEWIRE
struct pw_thread_loop* loop;
struct pw_context* ctx;
struct pw_core* core;
struct pw_stream* stream;
struct spa_hook streamListener;
struct pw_registry* registry;
struct spa_hook registryListener;

bool initialized = false;
bool running = false;

std::atomic<uint32_t> writtenSamples;
std::mutex mutex;
std::condition_variable cv;

struct DeviceInfo {
  uint32_t id;
  std::string name;
  std::string desc;
  std::string mediaClass;
};

std::vector<DeviceInfo> availableDevices;

void registryEventGlobal(void*, uint32_t id, uint32_t perm, const char* type, uint32_t vers,
                         const struct spa_dict* props) {
  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);

    if (mediaClass && (strcmp(mediaClass, "Audio/Source") == 0 || strcmp(mediaClass, "Audio/Sink") == 0)) {
      DeviceInfo dev;
      dev.id = id;
      dev.name = name ? name : "";
      dev.desc = desc ? desc : "";
      dev.mediaClass = mediaClass;
      availableDevices.push_back(dev);
    }
  }
}

void registryEventGlobalRemove(void*, uint32_t id) {
  // TODO: implement removal handling
}

void onStreamStateChanged(void*, enum pw_stream_state old, enum pw_stream_state state, const char* err) {
  switch (state) {
  case PW_STREAM_STATE_ERROR:
    std::cerr << "PipeWire stream error: " << (err ? err : "unknown") << std::endl;
  case PW_STREAM_STATE_UNCONNECTED:
    running = false;
    break;
  case PW_STREAM_STATE_STREAMING:
    running = true;
    break;
  case PW_STREAM_STATE_PAUSED:
    pw_stream_set_active(stream, true);
  default:
    break;
  }
}

#include <immintrin.h>

void onProcess(void*) {
  struct pw_buffer* b;
  struct spa_buffer* buf;

  if ((b = pw_stream_dequeue_buffer(stream)) == nullptr)
    return;

  buf = b->buffer;

  if (buf->datas[0].data == nullptr || buf->datas[0].chunk->size == 0) {
    pw_stream_queue_buffer(stream, b);
    return;
  }

  float* samples = static_cast<float*>(buf->datas[0].data);
  size_t n_samples = buf->datas[0].chunk->size / sizeof(float);

  if (n_samples > buf->datas[0].maxsize / sizeof(float))
    n_samples = buf->datas[0].maxsize / sizeof(float);

  float gain = powf(10.0f, Config::options.audio.gain_db / 20.0f);

#ifdef HAVE_AVX2
  size_t simd_samples = n_samples & ~7;
  __m256i left_idx = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
  __m256i right_idx = _mm256_setr_epi32(1, 3, 5, 7, 0, 0, 0, 0);

  for (size_t i = 0; i < simd_samples; i += 8) {
    __m256 data = _mm256_loadu_ps(&samples[i]);

    __m256 left = _mm256_permutevar8x32_ps(data, left_idx);
    __m256 right = _mm256_permutevar8x32_ps(data, right_idx);

    __m256 vgain = _mm256_set1_ps(gain);
    left = _mm256_mul_ps(left, vgain);
    right = _mm256_mul_ps(right, vgain);

    __m256 mid = _mm256_mul_ps(_mm256_add_ps(left, right), _mm256_set1_ps(0.5f));
    __m256 side = _mm256_mul_ps(_mm256_sub_ps(left, right), _mm256_set1_ps(0.5f));

    _mm_storeu_ps(&DSP::bufferMid[DSP::writePos], _mm256_castps256_ps128(mid));
    _mm_storeu_ps(&DSP::bufferSide[DSP::writePos], _mm256_castps256_ps128(side));
    DSP::writePos = (DSP::writePos + 4) % DSP::bufferSize;
  }

  for (size_t i = simd_samples; i + 1 < n_samples; i += 2) {
#else
  for (size_t i = 0; i + 1 < n_samples; i += 2) {
#endif
    float left = samples[i] * gain;
    float right = samples[i + 1] * gain;
    DSP::bufferMid[DSP::writePos] = (left + right) / 2.0f;
    DSP::bufferSide[DSP::writePos] = (left - right) / 2.0f;
    DSP::writePos = (DSP::writePos + 1) % DSP::bufferSize;
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    writtenSamples += n_samples;
  }
  cv.notify_one();

  pw_stream_queue_buffer(stream, b);
}

std::pair<std::string, uint32_t> find(std::string dev) {
  if (dev.empty()) {
    return std::make_pair("default", PW_ID_ANY);
  }

  for (const auto& device : availableDevices) {
    if (device.name.find(dev) != std::string::npos ||
        device.name.find(dev.substr(0, dev.find(".monitor"))) != std::string::npos) {
      return std::make_pair(device.name, device.id);
    }
  }

  std::cout << "Warning: PipeWire device '" << dev << "' not found. using system default" << std::endl;
  return std::make_pair("default", PW_ID_ANY);
}

void cleanup() {
  if (loop) {
    pw_thread_loop_lock(loop);

    if (registry) {
      spa_hook_remove(&registryListener);
      pw_proxy_destroy((struct pw_proxy*)registry);
      registry = nullptr;
    }

    if (stream) {
      pw_stream_destroy(stream);
      stream = nullptr;
    }

    pw_thread_loop_unlock(loop);
    pw_thread_loop_stop(loop);
  }

  if (core) {
    pw_core_disconnect(core);
    core = nullptr;
  }

  if (ctx) {
    pw_context_destroy(ctx);
    ctx = nullptr;
  }

  if (loop) {
    pw_thread_loop_destroy(loop);
    loop = nullptr;
  }

  pw_deinit();

  initialized = false;
  running = false;
}

bool init() {
  if (initialized)
    cleanup();

  pw_init(nullptr, nullptr);

  loop = pw_thread_loop_new("pulse-visualizer", nullptr);
  if (!loop) {
    std::cerr << "Failed to create PipeWire thread loop" << std::endl;
    return false;
  }

  ctx = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
  if (!ctx) {
    std::cerr << "Failed to create PipeWire context" << std::endl;
    return false;
  }

  core = pw_context_connect(ctx, nullptr, 0);
  if (!core) {
    std::cerr << "Failed to connect to PipeWire" << std::endl;
    return false;
  }

  if (pw_thread_loop_start(loop) < 0) {
    std::cerr << "Failed to start PipeWire thread loop" << std::endl;
    return false;
  }

  availableDevices.clear();
  static const struct pw_registry_events registryEvents = {
      .version = PW_VERSION_REGISTRY_EVENTS, .global = registryEventGlobal, .global_remove = registryEventGlobalRemove};

  pw_thread_loop_lock(loop);
  registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(registry, &registryListener, &registryEvents, nullptr);
  pw_thread_loop_unlock(loop);

  constexpr auto TIMEOUT = std::chrono::milliseconds(300);
  auto start = std::chrono::steady_clock::now();
  while (availableDevices.empty() && std::chrono::steady_clock::now() - start < TIMEOUT)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  const struct spa_pod* params[1];
  struct spa_audio_info_raw info = {};
  uint8_t buffer[1024];
  struct spa_pod_builder b;

  info.format = SPA_AUDIO_FORMAT_F32;
  info.channels = 2;
  info.rate = Config::options.audio.sample_rate;

  spa_pod_builder_init(&b, buffer, sizeof(buffer));
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  static const struct pw_stream_events streamEvents = {
      .version = PW_VERSION_STREAM_EVENTS, .state_changed = onStreamStateChanged, .process = onProcess};

  pw_thread_loop_lock(loop);

  size_t targetSamples = info.rate / Config::options.window.fps_limit;
  size_t samples = 1;
  while (samples < targetSamples)
    samples *= 2;

  samples = std::max(samples, static_cast<size_t>(128));

  std::string nodeLatency = std::to_string(samples) + "/" + std::to_string(info.rate);

  stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "Pulse Visualizer",
                                pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true",
                                                  PW_KEY_NODE_LATENCY, nodeLatency.c_str(), nullptr),
                                &streamEvents, nullptr);

  if (!stream) {
    std::cerr << "Failed to create PipeWire stream" << std::endl;
    return false;
  }

  auto [dev, devId] = find(Config::options.audio.device);
  if (pw_stream_connect(stream, PW_DIRECTION_INPUT, devId,
                        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                     PW_STREAM_FLAG_DONT_RECONNECT),
                        params, 1) < 0) {
    pw_thread_loop_unlock(loop);
    std::cerr << "Failed to connect PipeWire stream" << std::endl;
    return false;
  }

  pw_thread_loop_unlock(loop);

  start = std::chrono::steady_clock::now();
  while (!running && std::chrono::steady_clock::now() - start < TIMEOUT)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::cout << "Connected to PipeWire device: '" << dev << "'" << std::endl;

  initialized = true;
  return true;
}

bool read(float*, const size_t& frames) {
  if (!initialized || !stream)
    return false;

  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return writtenSamples > frames; });
  writtenSamples = 0;

  return true;
}

bool reconfigure() {
  static std::string lastDevice = Config::options.audio.device;
  static uint32_t lastSampleRate = Config::options.audio.sample_rate;
  static size_t lastBuffer = DSP::bufferSize;

  if (lastDevice == Config::options.audio.device && lastBuffer == DSP::bufferSize &&
      lastSampleRate == Config::options.audio.sample_rate) [[likely]]
    return false;

  lastSampleRate = Config::options.audio.sample_rate;
  lastBuffer = DSP::bufferSize;
  lastDevice = Config::options.audio.device;

  init();

  return true;
}
#else
bool init() { return false; }
bool read(float*, size_t) { return false; }
bool reconfigure(const std::string&, uint32_t, size_t) { return false; }
#endif
} // namespace PipeWire

std::optional<Type> init() {
  Type engine = toType(Config::options.audio.engine);

#if !HAVE_PULSEAUDIO
  if (engine == Type::PULSEAUDIO) {
    std::cerr << "Pulse not compiled with Pulseaudio support. Using auto" << std::endl;
    engine = Type::AUTO;
  }
#endif

#if !HAVE_PIPEWIRE
  if (engine == Type::PIPEWIRE) {
    std::cerr << "Pulse not compiled with PipeWire support. Using auto" << std::endl;
    engine = Type::AUTO;
  }
#endif

  switch (engine) {
  case Type::AUTO:
#if HAVE_PIPEWIRE
    if (PipeWire::init()) {
#if HAVE_PULSEAUDIO
      Pulseaudio::cleanup();
#endif
      return Type::PIPEWIRE;
    }
#endif
#if HAVE_PULSEAUDIO
    if (Pulseaudio::init()) {
#if HAVE_PIPEWIRE
      PipeWire::cleanup();
#endif
      return Type::PULSEAUDIO;
    }
#endif
    break;
  case Type::PULSEAUDIO:
    if (Pulseaudio::init()) {
#if HAVE_PIPEWIRE
      PipeWire::cleanup();
#endif
      return Type::PULSEAUDIO;
    } else
      return std::nullopt;
    break;
  case Type::PIPEWIRE:
    if (PipeWire::init()) {
#if HAVE_PULSEAUDIO
      Pulseaudio::cleanup();
#endif
      return Type::PIPEWIRE;
    } else
      return std::nullopt;
    break;
  }

  return std::nullopt;
}

void cleanup() {
#if HAVE_PULSEAUDIO
  AudioEngine::Pulseaudio::cleanup();
#endif
#if HAVE_PIPEWIRE
  AudioEngine::PipeWire::cleanup();
#endif
}

bool reconfigure() {
#if HAVE_PULSEAUDIO
  if (Pulseaudio::running) {
    return AudioEngine::Pulseaudio::reconfigure();
  }
#endif
#if HAVE_PIPEWIRE
  if (PipeWire::running) {
    return AudioEngine::PipeWire::reconfigure();
  }
#endif
  return false;
}

bool read(float* buffer, const size_t& samples) {
#if HAVE_PULSEAUDIO
  if (Pulseaudio::running) {
    return AudioEngine::Pulseaudio::read(buffer, samples);
  }
#endif
#if HAVE_PIPEWIRE
  if (PipeWire::running) {
    return AudioEngine::PipeWire::read(buffer, samples);
  }
#endif
  return false;
}

} // namespace AudioEngine

namespace Theme {
struct Colors {
  float color[4] = {0};
  float selection[4] = {0};
  float text[4] = {0};
  float accent[4] = {0};
  float background[4] = {0};
  float bgaccent[4] = {0};

  float waveform[4] = {0}; // Unused, no waveform visualizer yet

  float rgb_waveform_opacity_with_history = 0; // What?
  float history_low[4] = {0};
  float history_mid[4] = {0};
  float history_high[4] = {0};

  float waveform_low[4] = {0};
  float waveform_mid[4] = {0};
  float waveform_high[4] = {0};

  float oscilloscope_main[4] = {0};
  float oscilloscope_bg[4] = {0};

  float stereometer[4] = {0}; // Lissajous

  float stereometer_low[4] = {0};  // Unused
  float stereometer_mid[4] = {0};  // Unused
  float stereometer_high[4] = {0}; // Unused

  float spectrum_analyzer_main[4] = {0};
  float spectrum_analyzer_secondary[4] = {0};
  float spectrum_analyzer_frequency_lines[4] = {0};
  float spectrum_analyzer_reference_line[4] = {0}; // Unused
  float spectrum_analyzer_threshold_line[4] = {0}; // Unused

  float spectrogram_low = 0;
  float spectrogram_high = 0;
  float color_bars_low = 0;     // ?
  float color_bars_high = 0;    // ?
  float color_bars_opacity = 0; // ?

  float spectrogram_main[4] = {0};
  float color_bars_main[4] = {0};

  float loudness_main[4] = {0}; // Unused
  float loudness_text[4] = {0}; // Unused
} colors;

#ifdef __linux__
int themeInotifyFd = -1;
int themeInotifyWatch = -1;
std::string currentThemePath;
#endif

float* alpha(float* color, float alpha) {
  static float tmp[4];
  memcpy(tmp, color, 3 * sizeof(float));
  tmp[3] = alpha;
  return tmp;
}

void mix(float* a, float* b, float* out, float t) {
  for (int i = 0; i < 4; ++i) {
    out[i] = a[i] * (1.0f - t) + b[i] * t;
  }
}

void load(const std::string& name) {
  std::string path = "~/.config/pulse-visualizer/themes/" + name;
  if (name.find(".txt") == std::string::npos)
    path += ".txt";

  path = expandUserPath(path);
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open theme file: " << path << std::endl;
    return;
  }

#ifdef __linux__
  currentThemePath = path;
  if (themeInotifyFd == -1) {
    themeInotifyFd = inotify_init1(IN_NONBLOCK);
  }
  if (themeInotifyWatch != -1) {
    inotify_rm_watch(themeInotifyFd, themeInotifyWatch);
  }
  themeInotifyWatch = inotify_add_watch(themeInotifyFd, path.c_str(), IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
#endif

  static std::unordered_map<std::string, float*> colorMapArrays = {
      {"color",                             colors.color                            },
      {"selection",                         colors.selection                        },
      {"text",                              colors.text                             },
      {"accent",                            colors.accent                           },
      {"bg",                                colors.background                       },
      {"bgaccent",                          colors.bgaccent                         },
      {"waveform",                          colors.waveform                         },
      {"history_low",                       colors.history_low                      },
      {"history_mid",                       colors.history_mid                      },
      {"history_high",                      colors.history_high                     },
      {"waveform_low",                      colors.waveform_low                     },
      {"waveform_mid",                      colors.waveform_mid                     },
      {"waveform_high",                     colors.waveform_high                    },
      {"oscilloscope_main",                 colors.oscilloscope_main                },
      {"oscilloscope_bg",                   colors.oscilloscope_bg                  },
      {"stereometer",                       colors.stereometer                      },
      {"stereometer_low",                   colors.stereometer_low                  },
      {"stereometer_mid",                   colors.stereometer_mid                  },
      {"stereometer_high",                  colors.stereometer_high                 },
      {"spectrum_analyzer_main",            colors.spectrum_analyzer_main           },
      {"spectrum_analyzer_secondary",       colors.spectrum_analyzer_secondary      },
      {"spectrum_analyzer_frequency_lines", colors.spectrum_analyzer_frequency_lines},
      {"spectrum_analyzer_reference_line",  colors.spectrum_analyzer_reference_line },
      {"spectrum_analyzer_threshold_line",  colors.spectrum_analyzer_threshold_line },
      {"spectrogram_main",                  colors.spectrogram_main                 },
      {"color_bars_main",                   colors.color_bars_main                  },
      {"loudness_main",                     colors.loudness_main                    },
      {"loudness_text",                     colors.loudness_text                    },
  };

  static std::unordered_map<std::string, float*> colorMapFloats = {
      {"rgb_waveform_opacity_with_history", &colors.rgb_waveform_opacity_with_history},
      {"spectrogram_low",                   &colors.spectrogram_low                  },
      {"spectrogram_high",                  &colors.spectrogram_high                 },
      {"color_bars_low",                    &colors.color_bars_low                   },
      {"color_bars_high",                   &colors.color_bars_high                  },
      {"color_bars_opacity",                &colors.color_bars_opacity               }
  };

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t colonIndex = line.find(':');
    if (colonIndex == std::string::npos)
      continue;

    std::string key = line.substr(0, colonIndex);
    std::string value = line.substr(colonIndex + 1);

    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    auto parseAndStoreColor = [&]() -> bool {
      auto itA = colorMapArrays.find(key);
      auto itB = colorMapFloats.find(key);
      if (itA == colorMapArrays.end() && itB == colorMapFloats.end())
        return true; // Doesnt exist, just ignore

      std::istringstream iss(value);
      std::string token;
      std::vector<int> components;

      while (std::getline(iss, token, ','))
        try {
          components.push_back(std::stoi(token));
        } catch (...) {
          return false;
        }

      if (components.size() == 1) {
        *itB->second = components.at(0) / 255.f;
        return true;
      }

      if (components.size() < 3)
        return false;

      int alpha = components.size() >= 4 ? components.at(3) : 255;
      static float color[4];
      std::transform(components.begin(), components.begin() + 3, color, [](int v) { return v / 255.f; });
      color[3] = alpha / 255.f;

      memcpy(itA->second, color, 4 * sizeof(float));
      return true;
    };

    if (!parseAndStoreColor()) {
      std::cerr << key << " is invalid or has invalid data: " << value << std::endl;
    }
  }

  static std::vector<float*> mainColors = {colors.color,  colors.selection,  colors.text,
                                           colors.accent, colors.background, colors.bgaccent};

  static std::vector<std::string> colorNames = {"color", "selection", "text", "accent", "bg", "bgaccent"};

  for (size_t i = 0; i < mainColors.size(); ++i) {
    if (std::abs(mainColors[i][3]) < 1e-6f) {
      std::cout << "Color " << colorNames[i] << " is missing!" << std::endl;
    }
  }
}

bool reload() {
#ifdef __linux__
  if (themeInotifyFd != -1 && themeInotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(themeInotifyFd, buf, sizeof(buf));
    if (len > 0) {
      load(Config::options.window.theme);
      return true;
    }
  }
#else
  struct stat st;
  if (stat(currentThemePath.c_str(), &st) != 0) {
    std::cerr << "Warning: could not stat theme file." << std::endl;
    return false;
  }
  static time_t lastThemeMTime = 0;
  if (st.st_mtime != lastThemeMTime) {
    lastThemeMTime = st.st_mtime;
    load(Config::options.window.theme);
    return true;
  }
#endif
  return false;
}
} // namespace Theme

namespace SDLWindow {
SDL_Window* win = nullptr;
SDL_GLContext glContext = 0;
bool focused = false;
bool running = false;
int width, height;

void deinit() {
  if (glContext)
    SDL_GL_DeleteContext(glContext);
  if (win)
    SDL_DestroyWindow(win);
  SDL_Quit();
}

void init() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
    exit(1);
  }

  // OpenGL 2.1, Double buffering enabled
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  win = SDL_CreateWindow("Pulse", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1080, 200,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    deinit();
    exit(1);
  }

  glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
    deinit();
    exit(1);
  }

  GLenum err = glewInit();
  if (err != GLEW_OK) {
    throw std::runtime_error(std::string("WindowManager::init(): GLEW initialization failed") +
                             reinterpret_cast<const char*>(glewGetErrorString(err)));
  }

  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0f);

  running = true;
}

void handleEvent(SDL_Event& event) {

  switch (event.type) {
  case SDL_QUIT:
    deinit();
    running = false;
    return;

  case SDL_KEYDOWN:
    switch (event.key.keysym.sym) {
    case SDLK_q:
    case SDLK_ESCAPE:
      deinit();
      running = false;
      return;

    default:
      break;
    }
    break;

  case SDL_WINDOWEVENT:
    switch (event.window.event) {
    case SDL_WINDOWEVENT_ENTER:
      focused = true;
      break;

    case SDL_WINDOWEVENT_LEAVE:
      focused = false;
      break;

    case SDL_WINDOWEVENT_RESIZED:
      width = event.window.data1;
      height = event.window.data2;

    default:
      break;
    }
    break;

  default:
    break;
  }
}

__always_inline void display() { SDL_GL_SwapWindow(win); }

void clear() {
  float* c = Theme::colors.background;
  glClearColor(c[0], c[1], c[2], c[3]);
  glClear(GL_COLOR_BUFFER_BIT);
}
} // namespace SDLWindow

namespace Graphics {
void drawLine(const float& x1, const float& y1, const float& x2, const float& y2, const float* color,
              const float& thickness) {
  glColor4fv(color);
  glLineWidth(thickness);
  glBegin(GL_LINES);
  glVertex2f(x1, y1);
  glVertex2f(x2, y2);
  glEnd();
}

void drawFilledRect(const float& x, const float& y, const float& width, const float& height, const float* color) {
  glColor4fv(color);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);
  glEnd();
}
} // namespace Graphics

namespace WindowManager {
constexpr int MIN_WIDTH = 80;

void setViewport(int x, int width, int height) {
  glViewport(x, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, width, 0, height, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

struct Splitter {
  SDL_Window* win = nullptr;
  int x, dx;
  bool draggable = true;
  bool dragging = false;
  bool hovering = false;

  void handleEvent(const SDL_Event& event) {
    if (!draggable)
      return;
    int mouseX;
    switch (event.type) {
    case SDL_MOUSEMOTION:
      mouseX = event.motion.x;
      if (!dragging)
        hovering = abs(mouseX - x) < 5;
      if (dragging) {
        dx = mouseX - x;
        x = mouseX;
      }
      break;

    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT) {
        mouseX = event.button.x;
        if (abs(mouseX - x) < 5) {
          dragging = true;
        }
      }
      break;

    case SDL_MOUSEBUTTONUP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        mouseX = event.button.x;
        dragging = false;
        hovering = abs(mouseX - x) < 5;
      }
      break;

    default:
      break;
    }
  }

  void draw() {
    if (SDLWindow::height == 0) [[unlikely]]
      return;

    setViewport(x - 5, 10, SDLWindow::height);

    float color[4] = {0.5, 0.5, 0.5, 1.0};
    Graphics::drawLine(5, 0, 5, SDLWindow::height, Theme::colors.accent, 2.0f);

    if (hovering) {
      glColor4fv(Theme::alpha(Theme::colors.accent, 0.3f));
      glBegin(GL_QUADS);
      glVertex2f(0, 0);
      glVertex2f(10, 0);
      glVertex2f(10, SDLWindow::height);
      glVertex2f(0, SDLWindow::height);
      glEnd();
    }
  }
};

struct VisualizerWindow {
  SDL_Window* win = nullptr;
  int x, width;
  float aspectRatio = 0; // If nonzero, will enforce ratio.
  size_t pointCount;
  struct Phosphor {
    GLuint energyTexture = 0;
    GLuint ageTexture = 0;
    GLuint tempTexture = 0;
    GLuint tempTexture2 = 0;
    GLuint outputTexture = 0;
    GLuint frameBuffer = 0;
    GLuint vertexBuffer = 0;
    int textureWidth = 0;
    int textureHeight = 0;
  } phosphor;

  void (*render)() = nullptr;

  void transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type) {
    int minWidth = std::min(width, phosphor.textureWidth);
    int minHeight = std::min(SDLWindow::height, phosphor.textureHeight);
    std::vector<uint8_t> newData(width * SDLWindow::height * 4, 0);

    if (glIsTexture(oldTex)) [[likely]] {
      std::vector<uint8_t> oldData(phosphor.textureWidth * phosphor.textureHeight * 4);
      glBindTexture(GL_TEXTURE_2D, oldTex);
      glGetTexImage(GL_TEXTURE_2D, 0, format, type, oldData.data());

      int offsetX = std::max(0, (width - phosphor.textureWidth) / 2);
      int offsetY = std::max(0, (SDLWindow::height - phosphor.textureHeight) / 2);

#ifdef HAVE_AVX2
      int rowBytes = minWidth * 4;
      constexpr int SIMD_WIDTH = 32;

      for (int y = 0; y < minHeight; ++y) {
        int oldRow = y * phosphor.textureWidth * 4;
        int newRow = (y + offsetY) * width * 4 + offsetX * 4;

        int x = 0;
        for (; x + SIMD_WIDTH <= rowBytes; x += SIMD_WIDTH) {
          __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldData[oldRow + x]));
          _mm256_storeu_si256(reinterpret_cast<__m256i*>(&newData[newRow + x]), v);
        }
        for (; x < rowBytes; x += 4) {
          newData[newRow + x + 0] = oldData[oldRow + x + 0];
          newData[newRow + x + 1] = oldData[oldRow + x + 1];
          newData[newRow + x + 2] = oldData[oldRow + x + 2];
          newData[newRow + x + 3] = oldData[oldRow + x + 3];
        }
      }
#else
      for (int y = 0; y < minHeight; ++y) {
        for (int x = 0; x < minWidth; ++x) {
          int oldIdx = (y * phosphor.textureWidth + x) * 4;
          int newIdx = ((y + offsetY) * width + (x + offsetX)) * 4;
          if (newIdx + 3 < newData.size() && oldIdx + 3 < oldData.size()) {
            newData[newIdx] = oldData[oldIdx];
            newData[newIdx + 1] = oldData[oldIdx + 1];
            newData[newIdx + 2] = oldData[oldIdx + 2];
            newData[newIdx + 3] = oldData[oldIdx + 3];
          }
        }
      }
#endif
    }
    glBindTexture(GL_TEXTURE_2D, newTex);
    glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED_INTEGER ? GL_R32UI : GL_RGBA8, width, SDLWindow::height, 0, format,
                 type, newData.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };

  void resizeTextures() {
    bool sizeChanged = (phosphor.textureWidth != width || phosphor.textureHeight != SDLWindow::height);

    GLuint* textures[] = {&phosphor.energyTexture, &phosphor.ageTexture,    &phosphor.tempTexture,
                          &phosphor.tempTexture2,  &phosphor.outputTexture, &phosphor.frameBuffer};

    bool textureUninitialized = [textures]() -> bool {
      for (auto texture : textures) {
        if (*texture == 0)
          return true;
      }
      return false;
    }();

    if (!sizeChanged && !textureUninitialized) [[likely]] {
      return;
    }

    glFinish();
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

    std::vector<GLuint> oldTextures;
    oldTextures.reserve(5);

    for (int i = 0; i < 5; ++i) {
      oldTextures.push_back(*textures[i]);
      GLuint newTexture = 0;
      glGenTextures(1, &newTexture);
      GLenum err = glGetError();
      if (err != GL_NO_ERROR) [[unlikely]]
        throw std::runtime_error(
            "WindowManager::VisualizerWindow::resizeTextures(): OpenGL error during texture generation: " +
            std::to_string(err));
      if (i == 4)
        transferTexture(oldTextures[i], newTexture, GL_RGBA, GL_UNSIGNED_BYTE);
      else
        transferTexture(oldTextures[i], newTexture, GL_RED_INTEGER, GL_UNSIGNED_INT);
      if (oldTextures[i]) {
        glDeleteTextures(1, &oldTextures[i]);
      }
      *textures[i] = newTexture;
    }

    phosphor.textureHeight = SDLWindow::height;
    phosphor.textureWidth = width;

    glBindTexture(GL_TEXTURE_2D, 0);
    if (phosphor.frameBuffer)
      glDeleteFramebuffers(1, &phosphor.frameBuffer);
    glGenFramebuffers(1, &phosphor.frameBuffer);

    if (phosphor.vertexBuffer)
      glDeleteBuffers(1, &phosphor.vertexBuffer);
    glGenBuffers(1, &phosphor.vertexBuffer);
  }

  void draw() {
    if (!glIsTexture(phosphor.outputTexture)) [[unlikely]] {
      throw std::runtime_error("WindowManager::VisualizerWindow::draw(): outputTexture is not a Texture");
    }

    setViewport(x, width, SDLWindow::height);

    if (Config::options.phosphor.enabled) {

      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, phosphor.outputTexture);
      glColor4f(1.f, 1.f, 1.f, 1.f);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

      glBegin(GL_QUADS);
      glTexCoord2f(0.f, 0.f);
      glVertex2f(0.f, 0.f);
      glTexCoord2f(1.f, 0.f);
      glVertex2f(static_cast<float>(width), 0.f);
      glTexCoord2f(1.f, 1.f);
      glVertex2f(static_cast<float>(width), static_cast<float>(SDLWindow::height));
      glTexCoord2f(0.f, 1.f);
      glVertex2f(0.f, static_cast<float>(SDLWindow::height));
      glEnd();

      glDisable(GL_TEXTURE_2D);
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) [[unlikely]]
      std::cout << "WindowManager::VisualizerWindow::draw(): OpenGL error during draw: " + std::to_string(err)
                << std::endl;
  }
};

std::vector<VisualizerWindow> windows;
std::vector<Splitter> splitters;

void drawSplitters() {
  for (Splitter& splitter : splitters)
    splitter.draw();
}

void renderAll() {
  for (VisualizerWindow& window : windows)
    if (window.render)
      window.render();
}

void resizeTextures() {
  for (VisualizerWindow& window : windows) {
    window.resizeTextures();
  }
}

void resizeWindows() {
  if (splitters.size() == 0) {
    windows[0].x = 0;
    windows[0].width = SDLWindow::width;
  } else {
    windows[0].x = 0;
    windows[0].width = splitters[0].x - 1;
    for (int i = 1; i < windows.size() - 1; i++) {
      windows[i].x = splitters[i - 1].x + 1;
      windows[i].width = splitters[i].x - splitters[i - 1].x - 2;
    }

    windows.back().x = splitters.back().x + 1;
    windows.back().width = SDLWindow::width - windows.back().x - 1;
  }
}

int moveSplitter(int index, int targetX = -1) {
  VisualizerWindow* left = &windows[index];
  VisualizerWindow* right = &windows[index + 1];
  Splitter* splitter = &splitters[index];
  Splitter* splitterLeft = (index != 0 ? &splitters.at(index - 1) : nullptr);
  Splitter* splitterRight = (index != splitters.size() - 1 ? &splitters.at(index + 1) : nullptr);

  static int iter = 0;

  if (targetX != -1)
    splitter->x = targetX;
  else
    iter = 0;

  if (++iter > 20) {
    std::cerr << "Unable to solve splitters, max iterations reached." << std::endl;
    return -1;
  }

  auto move = [&](Splitter* neighborSplitter, VisualizerWindow* neighborWindow, int direction, int boundary) -> bool {
    bool solved = true;
    if (neighborWindow && neighborWindow->aspectRatio != 0.0f) {
      int forceWidth = std::min(static_cast<int>(neighborWindow->aspectRatio * SDLWindow::height),
                                static_cast<int>(SDLWindow::width - (windows.size() - 1) * MIN_WIDTH));
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) != forceWidth) {
          moveSplitter(index + direction, splitter->x + direction * forceWidth);
          solved = false;
        }
      } else {
        if (direction * (boundary - splitter->x) != forceWidth) {
          splitter->x = boundary - direction * forceWidth;
          solved = false;
        }
      }
    } else {
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) < MIN_WIDTH) {
          moveSplitter(index + direction, splitter->x + direction * MIN_WIDTH);
          solved = false;
        }
      } else {
        if (direction * (boundary - splitter->x) < MIN_WIDTH) {
          splitter->x = boundary - direction * MIN_WIDTH;
          solved = false;
        }
      }
    }
    return solved;
  };

  int i = 0;
  while (!(move(splitterRight, right, 1, SDLWindow::width) && move(splitterLeft, left, -1, 0)))
    i++;
  return i;
}

void updateSplitters() {

  auto it =
      std::find_if(splitters.begin(), splitters.end(), [](const auto& splitter) { return splitter.dragging == true; });

  if (it == splitters.end()) [[likely]] {
    for (int i = 0; i < splitters.size(); i++) {
      moveSplitter(i);
    }
    return;
  }

  int movingSplitterIndex = (it != splitters.end()) ? std::distance(splitters.begin(), it) : -1;

  moveSplitter(movingSplitterIndex);
}
} // namespace WindowManager

namespace Graphics {

namespace Font {
FT_Face face = nullptr;
FT_Library ftLib = nullptr;

// Add texture cache
struct GlyphTexture {
  GLuint textureId;
  int width, height;
  int bearingX, bearingY;
  int advance;
};

std::unordered_map<char, GlyphTexture> glyphCache;

void load() {
  std::string path = expandUserPath(Config::options.font);
  struct stat buf;
  if (stat(path.c_str(), &buf) != 0)
    return;

  if (!ftLib)
    if (FT_Init_FreeType(&ftLib) != 0)
      return;

  if (FT_New_Face(ftLib, path.c_str(), 0, &face))
    return;
}

// Add cleanup function
void cleanup() {
  for (auto& [ch, glyph] : glyphCache) {
    if (glIsTexture(glyph.textureId)) {
      glDeleteTextures(1, &glyph.textureId);
    }
  }
  glyphCache.clear();
}

GlyphTexture& getGlyphTexture(char c, float size) {
  auto it = glyphCache.find(c);
  if (it != glyphCache.end()) {
    return it->second;
  }

  // Generate new glyph texture
  FT_Set_Pixel_Sizes(face, 0, size);

  if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
    // Return a default/empty glyph on error
    static GlyphTexture empty = {0, 0, 0, 0, 0, 0};
    return empty;
  }

  FT_GlyphSlot g = face->glyph;

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, g->bitmap.width, g->bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               g->bitmap.buffer);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  GlyphTexture glyph = {
      tex,           static_cast<int>(g->bitmap.width),  static_cast<int>(g->bitmap.rows), g->bitmap_left,
      g->bitmap_top, static_cast<int>(g->advance.x >> 6)};

  glyphCache[c] = glyph;
  return glyphCache[c];
}

void drawText(const char* text, const float& x, const float& y, const float& size, const float* color) {
  if (!text || !*text || !face)
    return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glColor4fv(color);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  float _x = x, _y = y;
  for (const char* p = text; *p; p++) {
    const char& c = *p;
    if (c == '\n') {
      _x = x;
      _y += size;
      continue;
    }

    GlyphTexture& glyph = getGlyphTexture(c, size);
    if (glyph.textureId == 0)
      continue;

    glBindTexture(GL_TEXTURE_2D, glyph.textureId);

    float x0 = _x + static_cast<float>(glyph.bearingX);
    float y0 = _y - static_cast<float>(glyph.height - glyph.bearingY);
    float w = static_cast<float>(glyph.width);
    float h = static_cast<float>(glyph.height);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(x0, y0);
    glTexCoord2f(1, 1);
    glVertex2f(x0 + w, y0);
    glTexCoord2f(1, 0);
    glVertex2f(x0 + w, y0 + h);
    glTexCoord2f(0, 0);
    glVertex2f(x0, y0 + h);
    glEnd();

    _x += static_cast<float>(glyph.advance);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}
} // namespace Font

void drawLines(const WindowManager::VisualizerWindow* window, const std::vector<std::pair<float, float>> points,
               float* color) {
  std::vector<float> vertexData;
  vertexData.reserve(points.size() * 2);

  float lastX = -10000.f, lastY = -10000.f;
  for (size_t i = 0; i < points.size(); i++) {
    float x = points[i].first;
    float y = points[i].second;
    if (i == 0 || std::abs(x - lastX) >= 1.0f || std::abs(y - lastY) >= 1.0f) {
      vertexData.push_back(x);
      vertexData.push_back(y);
      lastX = x;
      lastY = y;
    }
  }

  if (vertexData.size() < 4)
    return;

  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  glBindBuffer(GL_ARRAY_BUFFER, window->phosphor.vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

  glColor4fv(color);
  glLineWidth(2.f);

  glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertexData.size() / 2));

  glDisable(GL_LINE_SMOOTH);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

namespace Shader {
// 0: compute, 1: decay, 2: blur, 3: colormap
std::vector<GLuint> shaders(4, 0);

std::string loadFile(const char* path) {
  std::string local = std::string("../") + path;
  std::ifstream lFile(local);
  if (lFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(lFile, line))
      content += line + "\n";
    return content;
  }

  std::string inst = std::string(PULSE_DATA_DIR) + "/" + path;
  std::ifstream sFile(inst);
  if (sFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(sFile, line))
      content += line + "\n";
    return content;
  }

  std::cerr << "Failed to open shader file '" << path << "'" << std::endl;
  return "";
}

GLuint load(const char* path, GLenum type) {
  std::string src = loadFile(path);
  if (src.empty())
    return 0;

  GLuint shader = glCreateShader(type);
  auto p = src.c_str();
  glShaderSource(shader, 1, &p, nullptr);
  glCompileShader(shader);

  GLint tmp = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &tmp);
  if (tmp != GL_FALSE)
    return shader;

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &tmp);
  std::vector<char> log(tmp);
  glGetShaderInfoLog(shader, tmp, &tmp, log.data());

  std::cerr << "Shader compilation failed for '" << path << "': " << log.data() << std::endl;
  glDeleteShader(shader);
  return 0;
}

void ensureShaders() {
  if (std::all_of(shaders.begin(), shaders.end(), [](int x) { return x != 0; }))
    return;

  static std::vector<std::string> shaderPaths = {"shaders/phosphor_compute.comp", "shaders/phosphor_decay.comp",
                                                 "shaders/phosphor_blur.comp", "shaders/phosphor_colormap.comp"};

  for (int i = 0; i < shaders.size(); i++) {
    if (shaders[i])
      continue;

    GLuint shader = load(shaderPaths[i].c_str(), GL_COMPUTE_SHADER);
    if (!shader)
      continue;

    shaders[i] = glCreateProgram();
    glAttachShader(shaders[i], shader);
    glLinkProgram(shaders[i]);

    GLint tmp;
    glGetProgramiv(shaders[i], GL_LINK_STATUS, &tmp);
    if (!tmp) {
      char log[512];
      glGetProgramInfoLog(shaders[i], 512, NULL, log);
      std::cerr << "Shader linking failed:" << log << std::endl;
      glDeleteProgram(shaders[i]);
      shaders[i] = 0;
    }

    glDeleteShader(shader);
  }
}

void dispatchCompute(const WindowManager::VisualizerWindow* win, const int& vertexCount, const GLuint& ageTex,
                     const GLuint& vertexBuffer, const GLuint& out) {
  if (!shaders[0])
    return;

  glUseProgram(shaders[0]);

  glUniform2i(glGetUniformLocation(shaders[0], "texSize"), win->width, SDLWindow::height);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vertexBuffer);
  glBindImageTexture(0, out, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint g = (vertexCount + 63) / 64;
  glDispatchCompute(g, 1, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchDecay(const WindowManager::VisualizerWindow* win, const GLuint& ageTex, const GLuint& in,
                   const GLuint& out) {
  if (!shaders[1])
    return;

  glUseProgram(shaders[1]);

  float decaySlow = exp(-WindowManager::dt * Config::options.phosphor.decay_slow);
  float decayFast = exp(-WindowManager::dt * Config::options.phosphor.decay_fast);

  glUniform1f(glGetUniformLocation(shaders[1], "decaySlow"), decaySlow);
  glUniform1f(glGetUniformLocation(shaders[1], "decayFast"), decayFast);
  glUniform1ui(glGetUniformLocation(shaders[1], "ageThreshold"), Config::options.phosphor.age_threshold);

  glBindImageTexture(0, in, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
  glBindImageTexture(2, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& in,
                  const GLuint& out) {
  if (!shaders[2])
    return;

  glUseProgram(shaders[2]);

  glUniform1f(glGetUniformLocation(shaders[2], "line_blur_spread"), Config::options.phosphor.line_blur_spread);
  glUniform1f(glGetUniformLocation(shaders[2], "line_width"), Config::options.phosphor.line_width);
  glUniform1f(glGetUniformLocation(shaders[2], "range_factor"), Config::options.phosphor.range_factor);
  glUniform1i(glGetUniformLocation(shaders[2], "blur_direction"), dir);
  glUniform1i(glGetUniformLocation(shaders[2], "kernel_type"), kernel);
  glUniform1f(glGetUniformLocation(shaders[2], "f_intensity"), Config::options.phosphor.near_blur_intensity);
  glUniform1f(glGetUniformLocation(shaders[2], "g_intensity"), Config::options.phosphor.far_blur_intensity);
  glUniform2f(glGetUniformLocation(shaders[2], "texSize"), static_cast<float>(win->width),
              static_cast<float>(SDLWindow::height));

  glBindImageTexture(0, in, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, out, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* lineColor, const GLuint& in,
                      const GLuint& out) {
  if (!shaders[3])
    return;

  glUseProgram(shaders[3]);

  float* bg = Theme::colors.background;

  glUniform3f(glGetUniformLocation(shaders[3], "blackColor"), bg[0], bg[1], bg[2]);
  glUniform3f(glGetUniformLocation(shaders[3], "beamColor"), lineColor[0], lineColor[1], lineColor[2]);
  glUniform1i(glGetUniformLocation(shaders[3], "enablePhosphorGrain"), Config::options.phosphor.enable_grain);
  glUniform1i(glGetUniformLocation(shaders[3], "enableCurvedScreen"), Config::options.phosphor.enable_curved_screen);
  glUniform1f(glGetUniformLocation(shaders[3], "screenCurvature"), Config::options.phosphor.screen_curvature);
  glUniform1f(glGetUniformLocation(shaders[3], "screenGapFactor"), Config::options.phosphor.screen_gap);
  glUniform1f(glGetUniformLocation(shaders[3], "grainStrength"), Config::options.phosphor.grain_strength);
  glUniform2i(glGetUniformLocation(shaders[3], "texSize"), win->width, SDLWindow::height);
  glUniform1f(glGetUniformLocation(shaders[3], "vignetteStrength"), Config::options.phosphor.vignette_strength);
  glUniform1f(glGetUniformLocation(shaders[3], "chromaticAberrationStrength"),
              Config::options.phosphor.chromatic_aberration_strength);

  glBindImageTexture(0, in, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}
} // namespace Shader

namespace Phosphor {
void render(const WindowManager::VisualizerWindow* win, const std::vector<std::pair<float, float>> points,
            const std::vector<float>& energies, const float* lineColor, bool renderPoints = true) {

  Graphics::Shader::ensureShaders();

  // Copy to temp
  glCopyImageSubData(win->phosphor.energyTexture, GL_TEXTURE_2D, 0, 0, 0, 0, win->phosphor.tempTexture, GL_TEXTURE_2D,
                     0, 0, 0, 0, win->width, SDLWindow::height, 1);

  // Decay from temp to energyTex
  Shader::dispatchDecay(win, win->phosphor.ageTexture, win->phosphor.tempTexture, win->phosphor.energyTexture);

  if (renderPoints)
    // Draw lines on energyTex
    Shader::dispatchCompute(win, static_cast<GLuint>(points.size()), win->phosphor.ageTexture,
                            win->phosphor.vertexBuffer, win->phosphor.energyTexture);

  // Clear temp2
  glBindFramebuffer(GL_FRAMEBUFFER, win->phosphor.frameBuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTexture2, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Blur go brrr
  for (int k = 0; k < 3; k++) {
    Shader::dispatchBlur(win, 0, k, win->phosphor.energyTexture, win->phosphor.tempTexture);
    Shader::dispatchBlur(win, 1, k, win->phosphor.tempTexture, win->phosphor.tempTexture2);
  }

  Shader::dispatchColormap(win, lineColor, win->phosphor.tempTexture2, win->phosphor.outputTexture);
}
} // namespace Phosphor

void rgbaToHsva(float* rgba, float* hsva) {
  float r = rgba[0], g = rgba[1], b = rgba[2], a = rgba[3];
  float maxc = std::max({r, g, b}), minc = std::min({r, g, b});
  float d = maxc - minc;
  float h = 0, s = (maxc == 0 ? 0 : d / maxc), v = maxc;
  if (d > 1e-6f) {
    if (maxc == r)
      h = (g - b) / d + (g < b ? 6 : 0);
    else if (maxc == g)
      h = (b - r) / d + 2;
    else
      h = (r - g) / d + 4;
    h /= 6;
  }
  hsva[0] = h;
  hsva[1] = s;
  hsva[2] = v;
  hsva[3] = a;
}

void hsvaToRgba(float* hsva, float* rgba) {
  float h = hsva[0], s = hsva[1], v = hsva[2], a = hsva[3];
  float r, g, b;

  if (s <= 1e-6f) {
    r = g = b = v;
  } else {
    h = std::fmod(h, 1.0f) * 6.0f;
    int i = static_cast<int>(std::floor(h));
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0:
      r = v;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = v;
      b = p;
      break;
    case 2:
      r = p;
      g = v;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = v;
      break;
    case 4:
      r = t;
      g = p;
      b = v;
      break;
    case 5:
    default:
      r = v;
      g = p;
      b = q;
      break;
    }
  }
  rgba[0] = r;
  rgba[1] = g;
  rgba[2] = b;
  rgba[3] = a;
}

} // namespace Graphics
namespace Spline {
std::vector<std::pair<float, float>> generate(std::vector<std::pair<float, float>> control, int density,
                                              std::pair<float, float> min, std::pair<float, float> max) {
  std::vector<std::pair<float, float>> spline;
  spline.reserve((control.size() - 3) * (density + 1));

  float scale = 1.f / density;

  for (size_t i = 0; i < control.size() - 3; i++) {
    const auto& p0 = control[i];
    const auto& p1 = control[i + 1];
    const auto& p2 = control[i + 2];
    const auto& p3 = control[i + 3];

    for (int j = 0; j < density; j++) {
      float u = static_cast<float>(j) * scale;
      float u2 = u * u;
      float u3 = u2 * u;

      // Standard Catmull-Rom spline (tension = 0.5)
      float b0 = -0.5f * u3 + u2 - 0.5f * u;
      float b1 = 1.5f * u3 - 2.5f * u2 + 1.0f;
      float b2 = -1.5f * u3 + 2.0f * u2 + 0.5f * u;
      float b3 = 0.5f * u3 - 0.5f * u2;

      float x = b0 * p0.first + b1 * p1.first + b2 * p2.first + b3 * p3.first;
      float y = b0 * p0.second + b1 * p1.second + b2 * p2.second + b3 * p3.second;

      x = std::clamp(x, min.first, max.first);
      y = std::clamp(y, min.second, max.second);

      spline.push_back({x, y});
    }
  }

  return spline;
}
} // namespace Spline

namespace Lissajous {
std::vector<std::pair<float, float>> points;
WindowManager::VisualizerWindow* window;

void render() {
  static size_t lastWritePos = DSP::writePos;
  size_t readCount = ((DSP::writePos - lastWritePos + DSP::bufferSize) % DSP::bufferSize) *
                     (1.f + Config::options.lissajous.readback_multiplier);

  if (readCount == 0)
    return;

  lastWritePos = DSP::writePos;

  points.resize(readCount);

  size_t start = (DSP::bufferSize + DSP::writePos - readCount) % DSP::bufferSize;
  for (size_t i = 0; i < readCount; i++) {
    size_t idx = (start + i) % DSP::bufferSize;

    float left = DSP::bufferMid[idx] + DSP::bufferSide[idx];
    float right = DSP::bufferMid[idx] - DSP::bufferSide[idx];

    float x = (1.f + left) * window->width / 2.f;
    float y = (1.f + right) * window->width / 2.f;

    points[i] = {x, y};
  }

  // Weird ass constraints because line is 2 wide
  if (Config::options.lissajous.enable_splines)
    points = Spline::generate(points, Config::options.lissajous.spline_segments, {1.f, 0.f},
                              {window->width - 1, window->width});

  std::vector<float> vertexData;

  float* color = Theme::colors.color;

  if (Config::options.phosphor.enabled) {
    vertexData.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    constexpr float REF_AREA = 200.f * 200.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA * (window->width * window->width);

    energy *= Config::options.lissajous.beam_multiplier / (1.f + Config::options.lissajous.readback_multiplier) /
              Config::options.phosphor.spline_density;

    float dt = 1.f / Config::options.audio.sample_rate;

    for (size_t i = 0; i < points.size() - 1; i++) {
      const auto& p1 = points[i];
      const auto& p2 = points[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float len = std::max(1e-12f, sqrtf(dx * dx + dy * dy));
      float totalE = energy * (dt / len);

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(i < energies.size() ? energies[i] : 0);
      vertexData.push_back(0); // Padding
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, points, energies, color, DSP::pitchDB > Config::options.audio.silence_threshold);
    window->draw();
  } else {
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, color);
  }
}
} // namespace Lissajous

namespace Oscilloscope {
std::vector<std::pair<float, float>> points;
WindowManager::VisualizerWindow* window;
void render() {
  size_t samples = Config::options.oscilloscope.time_window * Config::options.audio.sample_rate / 1000;
  if (Config::options.oscilloscope.limit_cycles) {
    samples = Config::options.audio.sample_rate / DSP::pitch * Config::options.oscilloscope.cycles;
    samples = std::max(samples, static_cast<size_t>(Config::options.oscilloscope.min_cycle_time *
                                                    Config::options.audio.sample_rate / 1000.0f));
  }

  size_t target = (DSP::writePos + DSP::bufferSize - samples);
  size_t range = Config::options.audio.sample_rate / DSP::pitch * 2.f;
  size_t zeroCross = target;
  if (Config::options.oscilloscope.follow_pitch) {
    if (Config::options.oscilloscope.alignment == "center")
      target = (target + samples / 2) % DSP::bufferSize;
    else if (Config::options.oscilloscope.alignment == "right")
      target = (target + samples) % DSP::bufferSize;

    for (uint32_t i = 0; i < range && i < DSP::bufferSize; i++) {
      size_t pos = (target + DSP::bufferSize - i) % DSP::bufferSize;
      size_t prev = (pos + DSP::bufferSize - 1) % DSP::bufferSize;
      if (DSP::bandpassed[prev] < 0.f && DSP::bandpassed[pos] >= 0.f) {
        zeroCross = pos;
        break;
      }
    }

    size_t phaseOffset = (target + DSP::bufferSize - zeroCross) % DSP::bufferSize;
    if (Config::options.oscilloscope.alignment_type == "peak")
      phaseOffset += Config::options.audio.sample_rate / DSP::pitch * 0.75f;
    target = (DSP::writePos + DSP::bufferSize - phaseOffset - samples) % DSP::bufferSize;
  }

  points.resize(samples);

  const float scale = static_cast<float>(window->width) / samples;
  for (size_t i = 0; i < samples; i++) {
    size_t pos = (target + i) % DSP::bufferSize;
    float x = static_cast<float>(i) * scale;
    float y;
    if (Config::options.debug.show_bandpassed) [[unlikely]]
      y = SDLWindow::height * 0.5f + DSP::bandpassed[pos] * 0.5f * SDLWindow::height;
    else
      y = SDLWindow::height * 0.5f + DSP::bufferMid[pos] * 0.5f * SDLWindow::height;

    points[i] = {x, y};
  }

  std::vector<float> vertexData;
  float* color = Theme::colors.color;
  if (Theme::colors.oscilloscope_main[3] > 1e-6f)
    color = Theme::colors.oscilloscope_main;

  if (Config::options.phosphor.enabled) {
    vertexData.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    constexpr float REF_AREA = 300.f * 300.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA * (window->width * SDLWindow::height);
    energy *= Config::options.oscilloscope.beam_multiplier / samples * 2048 * WindowManager::dt / 0.016f;

    for (size_t i = 0; i < points.size() - 1; i++) {
      const auto& p1 = points[i];
      const auto& p2 = points[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float len = sqrtf(dx * dx + dy * dy);
      float totalE = energy * ((1.f / Config::options.audio.sample_rate) / len);

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(i < energies.size() ? energies[i] : 0);
      vertexData.push_back(0); // Padding
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, points, energies, color, DSP::pitchDB > Config::options.audio.silence_threshold);
    window->draw();
  } else {
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, color);
  }
}
} // namespace Oscilloscope

namespace SpectrumAnalyzer {
std::vector<std::pair<float, float>> pointsMain;
std::vector<std::pair<float, float>> pointsAlt;
WindowManager::VisualizerWindow* window;
void render() {
  float logMin = log(Config::options.fft.min_freq);
  float logMax = log(Config::options.fft.max_freq);

  WindowManager::setViewport(window->x, window->width, SDLWindow::height);
  if (!Config::options.phosphor.enabled && Config::options.fft.frequency_markers) {
    auto drawLogLine = [&](float f) {
      if (f < Config::options.fft.min_freq || f > Config::options.fft.max_freq)
        return;
      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = logX * window->width;
      Graphics::drawLine(x, 0, x, SDLWindow::height, Theme::colors.accent, 1.f);
    };

    for (int decade = 1; decade <= 20000; decade *= 10) {
      for (int mult = 1; mult < 10; ++mult) {
        int freq = mult * decade;
        if (freq < Config::options.fft.min_freq)
          continue;
        if (freq > Config::options.fft.max_freq)
          break;
        drawLogLine(static_cast<float>(freq));
      }
    }
  }

  const std::vector<float>& inMain = Config::options.fft.enable_smoothing ? DSP::fftMid : DSP::fftMidRaw;
  const std::vector<float>& inAlt = Config::options.fft.enable_smoothing ? DSP::fftSide : DSP::fftSideRaw;

  pointsMain.clear();
  pointsAlt.clear();
  pointsMain.resize(inMain.size());
  pointsAlt.resize(inAlt.size());

  for (size_t bin = 0; bin < inMain.size(); bin++) {
    float f;
    if (Config::options.fft.enable_cqt)
      f = DSP::ConstantQ::frequencies[bin];
    else
      f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inMain.size());

    float logX = (log(f) - logMin) / (logMax - logMin);
    float x = logX * window->width;
    float mag = inMain[bin];

    float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);
    float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
    mag *= gain;

    float dB = 20.f * log10f(mag + 1e-12f);

    float y = (dB - Config::options.fft.min_db) / (Config::options.fft.max_db - Config::options.fft.min_db) *
              SDLWindow::height;
    pointsMain[bin] = {x, y};
  }

  for (size_t bin = 0; bin < inAlt.size() && !Config::options.phosphor.enabled; bin++) {
    float f;
    if (Config::options.fft.enable_cqt)
      f = DSP::ConstantQ::frequencies[bin];
    else
      f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inAlt.size());

    float logX = (log(f) - logMin) / (logMax - logMin);
    float x = logX * window->width;
    float mag = inAlt[bin];

    float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);
    float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
    mag *= gain;

    float dB = 20.f * log10f(mag + 1e-12f);

    float y = (dB - Config::options.fft.min_db) / (Config::options.fft.max_db - Config::options.fft.min_db) *
              SDLWindow::height;
    pointsAlt[bin] = {x, y};
  }

  float* color = Theme::colors.color;
  if (Theme::colors.spectrum_analyzer_main[3] > 1e-6f)
    color = Theme::colors.spectrum_analyzer_main;

  float* colorAlt = Theme::alpha(Theme::colors.color, 0.5f);
  if (Theme::colors.spectrum_analyzer_secondary[3] > 1e-6f)
    colorAlt = Theme::colors.spectrum_analyzer_secondary;

  if (Config::options.phosphor.enabled) {
    std::vector<float> vectorData;
    std::vector<float> energies;
    energies.reserve(pointsMain.size());
    vectorData.reserve(pointsMain.size() * 4);

    constexpr float REF_AREA = 400.f * 300.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA *
                   (Config::options.fft.enable_cqt ? window->width * SDLWindow::height : 400.f * 50.f);

    energy *= Config::options.fft.beam_multiplier * WindowManager::dt / 0.016f;

    float dt = 1.0f / Config::options.audio.sample_rate;

    for (size_t i = 0; i < pointsMain.size() - 1; i++) {
      const auto& p1 = pointsMain[i];
      const auto& p2 = pointsMain[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float segLen = std::max(sqrtf(dx * dx + dy * dy), 1e-12f);

      float totalE = energy * (dt / (Config::options.fft.enable_cqt ? segLen : 1.f / sqrtf(dx))) * 2.f;

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < pointsMain.size(); i++) {
      vectorData.push_back(pointsMain[i].first);
      vectorData.push_back(pointsMain[i].second);
      vectorData.push_back(i < energies.size() ? energies[i] : 0);
      vectorData.push_back(0); // Padding
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vectorData.size() * sizeof(float), vectorData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, pointsMain, energies, color);
    window->draw();
  } else {
    Graphics::drawLines(window, pointsAlt, colorAlt);
    Graphics::drawLines(window, pointsMain, color);
  }

  if (DSP::pitchDB > Config::options.audio.silence_threshold) {
    static std::string noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static std::string noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    std::string* noteNames = (Config::options.fft.note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;
    auto [note, octave, cents] = DSP::toNote(DSP::pitch, noteNames);
    char overlay[128];
    snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", DSP::pitchDB, DSP::pitch,
             note.c_str(), octave, cents);
    float x = 10.0f;
    float y = SDLWindow::height - 20.0f;

    Graphics::Font::drawText(overlay, x, y, 14.f, Theme::colors.text);
  }
}
} // namespace SpectrumAnalyzer

namespace Spectrogram {
WindowManager::VisualizerWindow* window;
float normalize(float db) {
  float norm =
      (db - Config::options.fft.min_db) / (Config::options.spectrogram.max_db - Config::options.spectrogram.min_db);
  return std::clamp(norm, 0.f, 1.f);
}

std::pair<size_t, size_t> find(const std::vector<float>& freqs, float f) {
  if (freqs.empty())
    return {0, 0};
  if (f <= freqs.front())
    return {0, 0};
  if (f >= freqs.back())
    return {freqs.size() - 1, freqs.size() - 1};

  auto it = std::lower_bound(freqs.begin(), freqs.end(), f);
  size_t idx = std::distance(freqs.begin(), it);
  if (it == freqs.begin())
    return {0, 0};
  return {idx - 1, idx};
}

std::vector<float>& mapSpectrum(const std::vector<float>& in) {
  static std::vector<float> spectrum;
  spectrum.resize(SDLWindow::height);

  float logMin = log10f(Config::options.fft.min_freq);
  float logMax = log10f(Config::options.fft.max_freq);
  float logRange = logMax - logMin;
  float freqRange = Config::options.fft.max_freq - Config::options.fft.min_freq;

  for (size_t i = 0; i < spectrum.size(); i++) {
    float normalized = static_cast<float>(i) / static_cast<float>(spectrum.size() - 1);
    float logFreq = logMin + normalized * logRange;
    float linFreq = Config::options.fft.min_freq + normalized * freqRange;
    float target = powf(10.f, Config::options.spectrogram.frequency_scale == "log" ? logFreq : linFreq);

    size_t bin1, bin2;
    if (Config::options.fft.enable_cqt) {
      std::tie(bin1, bin2) = find(DSP::ConstantQ::frequencies, target);
    } else {
      bin1 = target / (Config::options.audio.sample_rate * 0.5f / in.size());
      bin2 = bin1 + 1;
    }

    if (bin1 < in.size()) {
      if (bin2 < in.size() && bin1 != bin2) {
        if (Config::options.fft.enable_cqt) {
          float f1 = DSP::ConstantQ::frequencies[bin1];
          float f2 = DSP::ConstantQ::frequencies[bin2];
          float frac = (target - f1) / (f2 - f1);
          spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
        } else {
          float frac = static_cast<float>(target) / (Config::options.audio.sample_rate * 0.5f / in.size()) - bin1;
          spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
        }
      } else {
        spectrum[i] = in[bin1];
      }
    }
  }

  return spectrum;
}

void render() {
  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  static size_t current = 0;

  float interval = Config::options.spectrogram.time_window / static_cast<float>(window->width);
  static float accumulator = 0;
  accumulator += WindowManager::dt;
  if (accumulator > interval) {
    accumulator -= interval;
    std::vector<float>& spectrum = mapSpectrum(DSP::fftMidRaw);

    static std::vector<float> columnData;
    columnData.resize(SDLWindow::height * 3);

    for (size_t i = 0; i < SDLWindow::height; ++i) {
      columnData[i * 3 + 0] = Theme::colors.background[0];
      columnData[i * 3 + 1] = Theme::colors.background[1];
      columnData[i * 3 + 2] = Theme::colors.background[2];
    }

    bool monochrome = true;
    float* color = Theme::colors.color;
    if (Theme::colors.spectrogram_main[3] > 1e-6f) {
      color = Theme::colors.spectrogram_main;
    } else if (Theme::colors.spectrogram_low != 0 && Theme::colors.spectrogram_high != 0) {
      monochrome = false;
    }

    float lowRgba[4], highRgba[4];
    for (size_t i = 0; i < spectrum.size(); i++) {
      float mag = spectrum[i];

      if (mag > 1e-6f) {
        float dB = 20.f * log10f(mag);
        float intens = normalize(dB);

        if (intens > 1e-6f) {
          float intensColor[4];
          if (monochrome)
            Theme::mix(Theme::colors.background, color, intensColor, intens);
          else {
            float hsva[4] = {0.f, 1.f, 1.f, 1.f};
            if (intens < 0.5f) {
              hsva[0] = Theme::colors.spectrogram_low;
              float rgba[4];
              Graphics::hsvaToRgba(hsva, rgba);
              Theme::mix(Theme::colors.background, rgba, intensColor, intens * 2.f);
            } else {
              hsva[0] = lerp(Theme::colors.spectrogram_low, Theme::colors.spectrogram_high, (intens - 0.5f) * 2.f);
              Graphics::hsvaToRgba(hsva, intensColor);
            }
          }

          columnData[i * 3 + 0] = intensColor[0];
          columnData[i * 3 + 1] = intensColor[1];
          columnData[i * 3 + 2] = intensColor[2];
        }
      }
    }

    if (current > window->width)
      current = window->width;

    // reusing that because yes
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, current, 0, 1, SDLWindow::height, GL_RGB, GL_FLOAT, columnData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    current = (current + 1) % window->width;
  }

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  float colWidth = 1.f / window->width;
  float currentU = static_cast<float>(current) * colWidth;
  float part1 = (1.f - currentU) * window->width;
  float part2 = currentU * window->width;

  if (part1 > 0.f) {
    glBegin(GL_QUADS);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(part1, 0.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(part1, SDLWindow::height);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(0.0f, SDLWindow::height);
    glEnd();
  }

  if (part2 > 0.f) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(part1, 0.0f);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(window->width, 0.0f);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(window->width, SDLWindow::height);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(part1, SDLWindow::height);
    glEnd();
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}
} // namespace Spectrogram

namespace DSP {
namespace Threads {
std::mutex mutex;
std::atomic<bool> dataReadyFFTMain;
std::atomic<bool> dataReadyFFTAlt;
std::condition_variable fft;

int FFTMain() {
  // jaoeifjseaoijfeso

  while (SDLWindow::running) {
    std::unique_lock<std::mutex> lock(mutex);
    fft.wait(lock, [] { return dataReadyFFTMain.load() || !SDLWindow::running; });
    if (!SDLWindow::running)
      break;
    dataReadyFFTMain = false;

    if (Config::options.fft.enable_cqt) {
      ConstantQ::compute(bufferMid, fftMidRaw);
    } else {
      fftMidRaw.resize(Config::options.fft.size);
      size_t start = (writePos + bufferSize - Config::options.fft.size) % bufferSize;
      for (int i = 0; i < Config::options.fft.size; i++) {
        float win = 0.5f * (1.f - cos(2.f * M_PI * i / (Config::options.fft.size)));
        size_t pos = (start + i) % bufferSize;
        if (Config::options.fft.stereo_mode == "leftright")
          FFT::inMid[i] = (bufferSide[pos] - bufferMid[pos]) * 0.5f * win;
        else
          FFT::inMid[i] = bufferMid[pos] * win;
      }

      {
        std::lock_guard<std::mutex> lockFft(FFT::mutexMid);
        fftwf_execute(FFT::mid);
      }

      const float scale = 2.f / Config::options.fft.size;
      for (int i = 0; i < Config::options.fft.size / 2 + 1; i++) {
        float mag = sqrt(FFT::outMid[i][0] * FFT::outMid[i][0] + FFT::outMid[i][1] * FFT::outMid[i][1]) * scale;
        if (i != 0 && i != Config::options.fft.size / 2)
          mag *= 2.f;
        fftMidRaw[i] = mag;
      }
    }

    float peakDb = -100.f;
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

    if (Config::options.fft.enable_smoothing) {
      fftMid.resize(fftMidRaw.size());
      size_t bins = fftMid.size();
      size_t i = 0;
      const float riseSpeed = Config::options.fft.rise_speed * WindowManager::dt;
      const float fallSpeed = Config::options.fft.fall_speed * WindowManager::dt;
      // Placeholder for hover_fall_speed, currently unused, TODO: implement
#ifdef HAVE_AVX2
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
  // send god

  while (SDLWindow::running) {
    std::unique_lock<std::mutex> lock(mutex);
    fft.wait(lock, [] { return dataReadyFFTAlt.load() || !SDLWindow::running; });
    if (!SDLWindow::running)
      break;
    dataReadyFFTAlt = false;

    if (Config::options.phosphor.enabled)
      continue;

    if (Config::options.fft.enable_cqt) {
      ConstantQ::compute(bufferSide, fftSideRaw);
    } else {
      fftSideRaw.resize(Config::options.fft.size);
      size_t start = (writePos + bufferSize - Config::options.fft.size) % bufferSize;
      for (int i = 0; i < Config::options.fft.size; i++) {
        float win = 0.5f * (1.f - cos(2.f * M_PI * i / (Config::options.fft.size)));
        size_t pos = (start + i) % bufferSize;
        if (Config::options.fft.stereo_mode == "leftright")
          FFT::inSide[i] = (bufferSide[pos] + bufferMid[pos]) * 0.5f * win;
        else
          FFT::inSide[i] = bufferSide[pos] * win;
      }

      {
        std::lock_guard<std::mutex> lockFft(FFT::mutexSide);
        fftwf_execute(FFT::side);
      }

      const float scale = 2.f / Config::options.fft.size;
      for (int i = 0; i < Config::options.fft.size / 2 + 1; i++) {
        float mag = sqrt(FFT::outSide[i][0] * FFT::outSide[i][0] + FFT::outSide[i][1] * FFT::outSide[i][1]) * scale;
        if (i != 0 && i != Config::options.fft.size / 2)
          mag *= 2.f;
        fftSideRaw[i] = mag;
      }
    }

    if (Config::options.fft.enable_smoothing) {
      fftSide.resize(fftSideRaw.size());
      size_t bins = fftSide.size();
      size_t i = 0;
      const float riseSpeed = Config::options.fft.rise_speed * WindowManager::dt;
      const float fallSpeed = Config::options.fft.fall_speed * WindowManager::dt;

#ifdef HAVE_AVX2
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
  // Crimes
  Butterworth::design();
  std::vector<float> readBuf;

  std::thread FFTMainThread(Threads::FFTMain);
  std::thread FFTAltThread(Threads::FFTAlt);

  while (SDLWindow::running) {
    size_t sampleCount = Config::options.audio.sample_rate * WindowManager::dt;
    readBuf.resize(sampleCount * 2);

    if (!AudioEngine::read(readBuf.data(), sampleCount)) {
      std::cerr << "Failed to read from audio engine" << std::endl;
      SDLWindow::running = false;
      break;
    }

    float gain = powf(10.0f, Config::options.audio.gain_db / 20.0f);

#if HAVE_PULSEAUDIO
    if (AudioEngine::Pulseaudio::running) {
#ifdef HAVE_AVX2
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

    {
      std::lock_guard<std::mutex> lock(mutex);
      dataReadyFFTMain = true;
      dataReadyFFTAlt = true;
      fft.notify_all();
    }

    if (pitch > Config::options.fft.min_freq && pitch < Config::options.fft.max_freq)
      Butterworth::process(pitch);

    {
      std::lock_guard<std::mutex> lock(mainThread);
      dataReady = true;
      mainCv.notify_one();
    }
  }

  FFTMainThread.join();
  FFTAltThread.join();
  return 0;
}
} // namespace Threads
} // namespace DSP

namespace WindowManager {
void reorder() {
  std::vector<std::pair<void (*)(), WindowManager::VisualizerWindow*&>> visualizers = {
      {SpectrumAnalyzer::render, SpectrumAnalyzer::window},
      {Lissajous::render,        Lissajous::window       },
      {Oscilloscope::render,     Oscilloscope::window    },
      {Spectrogram::render,      Spectrogram::window     }
  };

  std::vector<int*> orders = {&Config::options.visualizers.fft_order, &Config::options.visualizers.lissajous_order,
                              &Config::options.visualizers.oscilloscope_order,
                              &Config::options.visualizers.spectrogram_order};

  std::vector<std::pair<int, size_t>> orderIndexPairs;
  for (size_t i = 0; i < orders.size(); ++i)
    if (*orders[i] != -1)
      orderIndexPairs.emplace_back(*orders[i], i);

  std::sort(orderIndexPairs.begin(), orderIndexPairs.end());

  windows.clear();
  splitters.clear();
  windows.reserve(orders.size());

  for (size_t i = 0; i < orderIndexPairs.size(); ++i) {
    size_t idx = orderIndexPairs[i].second;
    VisualizerWindow vw {};
    vw.aspectRatio = (visualizers[idx].first == Lissajous::render) ? 1.0f : 0.0f;
    vw.render = visualizers[idx].first;
    windows.push_back(vw);
    visualizers[idx].second = &windows.back();

    if (i + 1 < orderIndexPairs.size()) {
      Splitter s;
      if (i == 0 && vw.aspectRatio != 0.0f)
        s.draggable = false;
      splitters.push_back(s);
    }
  }

  if (!windows.empty() && !splitters.empty() && windows.back().aspectRatio != 0.0f)
    splitters.back().draggable = false;
}
} // namespace WindowManager

int main() {
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);

  SDLWindow::init();
  SDLWindow::clear();
  SDLWindow::display();

  Config::copyFiles();

  Config::load();
  Theme::load(Config::options.window.theme);
  Graphics::Font::load();
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();

  WindowManager::reorder();

  std::thread DSPThread(DSP::Threads::main);

  auto lastTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  while (1) {
    if (Config::reload()) {
      // Reload themes, etc
      Theme::load(Config::options.window.theme);
      Graphics::Font::cleanup();
      Graphics::Font::load();
      WindowManager::reorder();
      AudioEngine::reconfigure();
      DSP::FFT::recreatePlans();
      DSP::ConstantQ::regenerate();
    }

    if (Theme::reload()) {
      Graphics::Font::cleanup();
      Graphics::Font::load();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      SDLWindow::handleEvent(event);
      for (auto& splitter : WindowManager::splitters)
        splitter.handleEvent(event);
    }
    if (!SDLWindow::running)
      break;
    glUseProgram(0);
    // Wait for window
    if (SDLWindow::width == 0 || SDLWindow::height == 0)
      continue;

    WindowManager::updateSplitters();
    WindowManager::resizeWindows();
    WindowManager::resizeTextures();

    std::unique_lock<std::mutex> lock(mainThread);
    mainCv.wait(lock, [] { return dataReady.load(); });
    dataReady = false;

    SDLWindow::clear();
    WindowManager::renderAll();
    WindowManager::drawSplitters();
    SDLWindow::display();

    auto now = std::chrono::steady_clock::now();
    WindowManager::dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    if (Config::options.debug.log_fps) {
      frameCount++;
      static float fpsTimeAccum = 0.0f;
      fpsTimeAccum += WindowManager::dt;
      if (fpsTimeAccum >= 1.0f) {
        std::cout << "FPS: " << static_cast<int>(frameCount / fpsTimeAccum) << std::endl;
        frameCount = 0;
        fpsTimeAccum = 0.0f;
      }
    }
  }

  DSPThread.join();

  AudioEngine::cleanup();
  DSP::FFT::cleanup();
  Graphics::Font::cleanup();
  SDLWindow::deinit();
  return 0;
}