#pragma once

#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

// Central cache of parsed configuration values
struct ConfigValues {
  struct Oscilloscope {
    std::string gradient_mode = "horizontal";
    bool enable_phosphor = false;
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
    bool enable_phosphor = true;
    float beam_multiplier = 1.0f;
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
    bool enable_phosphor = false;
    bool enable_cqt = false;
    int cqt_bins_per_octave = 24;
    float smoothing_factor = 0.2f;
    float rise_speed = 500.0f;
    float fall_speed = 50.0f;
    float hover_fall_speed = 10.0f;
    int size = 4096;
    float beam_multiplier = 1.0f;
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
    std::string default_theme = "mocha.txt";
    int fps_limit = 240;
  } window;

  struct Debug {
    bool log_fps = false;
    bool show_bandpassed = false;
  } debug;

  struct Phosphor {
    float near_blur_intensity = 0.2f;
    float far_blur_intensity = 0.6f;
    float beam_energy = 100.0f;
    float decay_slow = 10.0f;
    float decay_fast = 100.0f;
    float beam_size = 2.0f;
    float line_blur_spread = 16.0f;
    float line_width = 2.0f;
    uint age_threshold = 256;
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
};

class Config {
public:
  static void load(const std::string& filename = "~/.config/pulse-visualizer/config.yml");
  static void reload();
  static int getInt(const std::string& key);
  static float getFloat(const std::string& key);
  static bool getBool(const std::string& key);
  static std::string getString(const std::string& key);
  static YAML::Node get(const std::string& key);
  static bool reloadIfChanged();
  static size_t getVersion();
  // Access central cached values
  static const ConfigValues& values();

private:
  static YAML::Node configData;
  static std::string configPath;
  static std::unordered_map<std::string, YAML::Node> configCache;
  static void clearCache();
  static YAML::Node getJsonRef(const std::string& key);
  static time_t lastConfigMTime;
  static size_t configVersion;

#ifdef __linux__
  // inotify file descriptor & watch descriptor; -1 when not in use
  static int inotifyFd;
  static int inotifyWatch;
#endif
};