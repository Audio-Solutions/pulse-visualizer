#pragma once
#include "common.hpp"

namespace Config {

/**
 * @brief Application configuration options structure
 */
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
    bool enable_lowpass = false;
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
    std::string mode = "none";
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

  std::vector<std::string> visualizers;

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

  struct Lowpass {
    float cutoff = 20000.0f;
    int order = 1;
  } lowpass;

  std::string font;
};

extern Options options;

#ifdef __linux__
extern int inotifyFd;
extern int inotifyWatch;
#endif

/**
 * @brief Copy default configuration files to user directory if they don't exist
 */
void copyFiles();

/**
 * @brief Get a nested node from YAML configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired node
 * @return Optional YAML node if found
 */
std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path);

template <typename T> T get(const YAML::Node& root, const std::string& path);

/**
 * @brief Load configuration from file
 */
void load();

/**
 * @brief Check if configuration file has been modified and reload if necessary
 * @return true if configuration was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

} // namespace Config