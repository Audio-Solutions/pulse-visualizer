/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
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

#include <glad/gl.h>
// ^ glad needs to be included first
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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

  T* allocate(std::size_t n) {
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

  void deallocate(T* p, std::size_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    free(p);
#endif
  }
};

namespace WindowManager {

struct Bounds {
  int x, y, w, h;
};

struct Size {
  int w, h;
};

class VisualizerWindow {
public:
  std::string id;
  std::string displayName;
  std::string group;
  Bounds bounds;
  float aspectRatio = 0;
  size_t forceWidth = 0;
  size_t pointCount = 0;
  Size minSize = Size(0, 0);
  bool hovering = false;
  bool dragging = false;
  constexpr static size_t buttonSize = 20;
  constexpr static size_t buttonPadding = 10;

  virtual ~VisualizerWindow() = default;

  /**
   * @brief Configure visualizer-specific layout/state on reload.
   */
  virtual void configure() {}

  /**
   * @brief Render this visualizer into the active SDL window context.
   */
  virtual void render() {}

  struct Phosphor {
    GLuint energyTextureR = 0;
    GLuint energyTextureG = 0;
    GLuint energyTextureB = 0;
    GLuint tempTextureR = 0;
    GLuint tempTextureG = 0;
    GLuint tempTextureB = 0;
    GLuint tempTexture2R = 0;
    GLuint tempTexture2G = 0;
    GLuint tempTexture2B = 0;
    GLuint tempTexture3R = 0;
    GLuint tempTexture3G = 0;
    GLuint tempTexture3B = 0;
    GLuint outputTexture = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    bool unused = false;
  } phosphor;

  virtual void handleEvent(const SDL_Event& event) final;
  virtual void transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type) final;
  virtual void resizeTextures() final;
  virtual void draw() final;
  virtual void cleanup() final;
};

enum class ConstraintType { Minimum = 0, Forced };
struct Constraint {
  ConstraintType w = ConstraintType::Minimum;
  ConstraintType h = ConstraintType::Minimum;
};

/**
 * @brief Splitter Orientation enum
 */
enum class Orientation { Horizontal, Vertical };

struct Splitter;
using Variant = std::variant<std::shared_ptr<VisualizerWindow>, Splitter>;
using Node = std::shared_ptr<Variant>;
struct Splitter {
  Orientation orientation;
  float ratio;
  Bounds bounds;
  Node primary;
  Node secondary;

  bool dragging = false;
  bool hovering = false;
  bool prevHovering = false;
  bool movable = true;
  std::string group;

  Constraint primaryConstraint {};
  Constraint secondaryConstraint {};
  Size primaryConstraintSize = Size(0, 0);
  Size secondaryConstraintSize = Size(0, 0);

  void render();
  void handleEvent(const SDL_Event& event);
};
} // namespace WindowManager

namespace Config {
/**
 * @brief Display rotation enum used by visualizer components.
 */
enum Rotation { ROTATION_0 = 0, ROTATION_90 = 1, ROTATION_180 = 2, ROTATION_270 = 3 };

/**
 * @brief Supported dynamic plugin config scalar types.
 */
enum class PluginConfigType { Bool, Int, Float, String };

/**
 * @brief UI and validation metadata for a plugin config option.
 */
struct PluginConfigSpec {
  PluginConfigType type = PluginConfigType::String;
  std::string groupKey = "misc";
  std::string groupLabel = "Misc";
  std::string label;
  std::string description;
  float min = 0.0f;
  float max = 0.0f;
  int precision = 1;
  bool zeroOff = false;
  bool useTick = false;
  std::vector<int> detentsInt;
  std::vector<float> detentsFloat;
  std::vector<std::string> choices;
};

/**
 * @brief Runtime value type for plugin config scalars.
 */
using PluginConfigValue = std::variant<bool, int, float, std::string>;

/**
 * @brief Application configuration options structure
 */
struct Options {
  struct Oscilloscope {
    float beam_multiplier = 1.0f;
    bool flip_x = false;
    Rotation rotation = ROTATION_0;
    float window = 50.0f;

    struct EdgeCompression {
      bool enabled = false;
      float range = 0.1;
    } edge_compression;

    struct Pitch {
      bool follow = true;
      std::string type = "zero_crossing";
      std::string alignment = "center";
      int cycles = 3;
      float min_cycle_time = 16.0f;
    } pitch;

    struct Lowpass {
      bool enabled = false;
      float cutoff = 200.0f;
      int order = 4;
    } lowpass;

    struct Bandpass {
      float bandwidth = 10.0f;
      float sidelobe = 60.0f;
    } bandpass;

    struct Spline {
      int segments = 4;
      float tension = 1.0f;
    } spline;
  } oscilloscope;

  struct Lissajous {
    float beam_multiplier = 1.0f;
    std::string mode = "none";
    Rotation rotation = ROTATION_0;

    struct Spline {
      int segments = 10;
      float tension = 1.0f;
    } spline;
  } lissajous;

  struct FFT {
    float beam_multiplier = 1.0f;
    Rotation rotation = ROTATION_0;
    bool flip_x = false;
    bool markers = true;
    int size = 4096;
    float slope = 3.0f;
    std::string key = "sharp";
    std::string mode = "midside";
    bool cursor = false;
    bool readout_header = true;

    struct Limits {
      float max_db = 0.0f;
      float max_freq = 22000.0f;
      float min_db = -60.0f;
      float min_freq = 10.0f;
    } limits;

    struct Smoothing {
      bool enabled = true;
      float fall_speed = 50.0f;
      float hover_fall_speed = 10.0f;
      float rise_speed = 500.0f;
    } smoothing;

    struct CQT {
      int bins_per_octave = 60;
      bool enabled = true;
    } cqt;

    struct Sphere {
      bool enabled = false;
      float max_freq = 5000.0f;
      float base_radius = 0.1f;
    } sphere;
  } fft;

  struct Spectrogram {
    float window = 2.0f;
    bool iterative_reassignment = true;
    std::string frequency_scale = "log";
    float slope = 0.0f;

    struct Limits {
      float max_db = -10.0f;
      float min_db = -60.0f;
      float min_freq = 10.0f;
      float max_freq = 22000.0f;
    } limits;
  } spectrogram;

  struct Waveform {
    float window = 2.0f;
    std::string mode = "stereo";
    float low_mid_split_hz = 250.0f;
    float mid_high_split_hz = 4000.0f;
    float slope = 3.0f;
    bool midline = true;
  } waveform;

  struct Audio {
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    float gain_db = 0.0f;
    std::string engine = "auto";
    std::string device = "default";
  } audio;

  std::unordered_map<std::string, WindowManager::Node> visualizers;

  struct Window {
    int default_width = 1080;
    int default_height = 200;
    std::string theme = "mocha.txt";
    int fps_limit = 240;
    bool decorations = true;
    bool always_on_top = false;
    bool wayland = false;
  } window;

  struct Debug {
    bool log_fps = false;
    bool show_bandpassed = false;
  } debug;

  struct Phosphor {
    bool enabled = false;

    struct Beam {
      float energy = 90.0f;
      bool rainbow = false;
    } beam;

    struct Blur {
      float spread = 128.0f;
      float near_intensity = 0.6f;
      float far_intensity = 0.8f;
    } blur;

    struct Screen {
      float decay = 30.0f;
      float curvature = 0.1f;
      float gap = 0.03f;
      float vignette = 0.3f;
      float chromatic_aberration = 0.008f;
      float grain = 0.1f;
      int aa_size = 4;
    } screen;

    struct Reflections {
      float strength = 0.7f;
    } reflections;
  } phosphor;

  struct LUFS {
    std::string mode = "momentary";
    std::string scale = "linear";
    std::string label = "off";
  } lufs;

  struct VU {
    float window = 100.0f;
    std::string style = "digital";
    float calibration_db = 0.0f;
    std::string scale = "linear";
    float needle_width = 2.0f;

    struct Momentum {
      bool enabled = true;
      float damping_ratio = 10.0f;
      float spring_constant = 500.0f;
    } momentum;
  } vu;

#ifdef _WIN32
  std::string font = "JetBrainsMonoNerdFont-Medium.ttf";
#else
  std::string font = "~/.local/share/fonts/JetBrainsMono/JetBrainsMonoNerdFont-Medium.ttf";
#endif
};
} // namespace Config

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
} // namespace DSP

namespace Theme {
/**
 * @brief Color scheme structure containing all theme colors
 */
struct Colors {
  float color[4] = {0};
  float selection[4] = {0};
  float text[4] = {0};
  float accent[4] = {0};
  float background[4] = {0};
  float bgAccent[4] = {0};

  float waveform[4] = {0};

  float rgb_waveform_opacity_with_history = 0;
  float history_low[4] = {0};
  float history_mid[4] = {0};
  float history_high[4] = {0};

  float waveform_low[4] = {0};
  float waveform_mid[4] = {0};
  float waveform_high[4] = {0};

  float oscilloscope_main[4] = {0};
  float oscilloscope_bg[4] = {0};

  float stereoMeter[4] = {0};

  float stereoMeter_low[4] = {0};
  float stereoMeter_mid[4] = {0};
  float stereoMeter_high[4] = {0};

  float spectrum_analyzer_main[4] = {0};
  float spectrum_analyzer_secondary[4] = {0};
  float spectrum_analyzer_frequency_lines[4] = {0};
  float spectrum_analyzer_reference_line[4] = {0};
  float spectrum_analyzer_threshold_line[4] = {0};

  float spectrogram_low = 0;
  float spectrogram_high = 0;
  float color_bars_low = 0;
  float color_bars_high = 0;
  float color_bars_opacity = 0;

  float spectrogram_main[4] = {0};
  float color_bars_main[4] = {0};

  float loudness_main[4] = {0};
  float loudness_text[4] = {0};

  float vu_main[4] = {0};
  float vu_caution[4] = {0};
  float vu_clip[4] = {0};

  float phosphor_border[4] = {0};
  // Custom spectrogram gradient mapping: pairs of dB value -> RGBA color (0..1)
  std::vector<std::pair<float, std::array<float, 4>>> spectrogram_gradient;
};
} // namespace Theme

namespace SDLWindow {
struct State {
  SDL_Window* win = nullptr;
  SDL_WindowID winID = 0;
  SDL_GLContext glContext = nullptr;
  std::pair<int, int> windowSizes;
  std::pair<int, int> mousePos;
  bool focused, removable = false;
  WindowManager::Node root;
};
} // namespace SDLWindow