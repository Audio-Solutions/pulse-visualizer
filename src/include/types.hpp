#pragma once
#include <map>
#include <string>
#include <vector>

namespace Config {
enum Rotation { ROTATION_0 = 0, ROTATION_90 = 1, ROTATION_180 = 2, ROTATION_270 = 3 };

/**
 * @brief Application configuration options structure
 */
struct Options {
  struct Oscilloscope {
    float beam_multiplier = 1.0f;
    bool flip_x = false;
    Rotation rotation = ROTATION_0;
    float window = 50.0f;
    bool centered = false;

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
  } oscilloscope;

  struct Lissajous {
    float beam_multiplier = 1.0f;
    int spline_segments = 10;
    float spline_tension = 0.5f;
    std::string mode = "none";
    Rotation rotation = ROTATION_0;
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
    bool interpolation = true;
    std::string frequency_scale = "log";

    struct Limits {
      float max_db = -10.0f;
      float max_freq = 22000.0f;
      float min_db = -60.0f;
      float min_freq = 20.0f;
    } limits;
  } spectrogram;

  struct Audio {
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    float gain_db = 0.0f;
    std::string engine = "auto";
    std::string device = "auto";
  } audio;

  std::map<std::string, std::vector<std::string>> visualizers;

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
      float width = 0.5f;
    } beam;

    struct Blur {
      float spread = 128.0f;
      float near_intensity = 0.6f;
      float far_intensity = 0.8f;
    } blur;

    float decay;

    struct Screen {
      float curvature = 0.1f;
      float gap = 0.03f;
      float vignette = 0.3f;
      float chromatic_aberration = 0.008f;
      float grain = 0.1f;
    } screen;

    struct Reflections {
      float strength = 0.7f;
      int box_blur_size = 7;
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

  std::string font;
};
} // namespace Config

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
};
} // namespace Theme