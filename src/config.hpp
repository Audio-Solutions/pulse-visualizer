#pragma once

#include <ctime>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Central cache of parsed configuration values
struct ConfigValues {
  struct Oscilloscope {
    std::string gradient_mode = "horizontal";
    bool enable_phosphor = false;
    bool follow_pitch = true;
  } oscilloscope;

  struct Lissajous {
    int max_points = 1000;
    bool enable_splines = true;
    int spline_segments = 8;
    bool enable_phosphor = true;
  } lissajous;

  struct FFT {
    std::string font;
    float min_freq = 10.0f;
    float max_freq = 20000.0f;
    float sample_rate = 44100.0f;
    float slope_correction_db = 4.5f;
    float min_db = -60.0f;
    float max_db = 10.0f;
    std::string stereo_mode = "midside";
    std::string note_key_mode = "sharp";
    bool enable_phosphor = false;
    bool enable_temporal_interpolation = true;
    bool enable_cqt = false;
    int cqt_bins_per_octave = 24;
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
    float smoothing_factor = 0.2f;
    float rise_speed = 500.0f;
    float fall_speed = 50.0f;
    float hover_fall_speed = 10.0f;
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    int fft_size = 4096;
    int fft_skip_frames = 2;
    int buffer_size = 32768;
    int display_samples = 2000;
    int channels = 2;
    float gain_db = 0.0f;
    std::string engine = "auto";
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
  } window;

  struct PulseAudio {
    std::string default_source = "your-audio-device.monitor";
    int buffer_size = 512;
  } pulseaudio;

  struct PipeWire {
    std::string default_source = "your-audio-device";
    int buffer_size = 512;
    int ring_multiplier = 4;
  } pipewire;

  struct Debug {
    bool log_fps = false;
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
  } phosphor;
};

class Config {
public:
  static void load(const std::string& filename = "~/.config/pulse-visualizer/config.json");
  static void reload();
  static int getInt(const std::string& key);
  static float getFloat(const std::string& key);
  static bool getBool(const std::string& key);
  static std::string getString(const std::string& key);
  static nlohmann::json get(const std::string& key);
  static bool reloadIfChanged();
  static size_t getVersion();
  // Access central cached values
  static const ConfigValues& values();

private:
  static nlohmann::json configData;
  static std::string configPath;
  static std::unordered_map<std::string, nlohmann::json> configCache;
  static void clearCache();
  static nlohmann::json& getJsonRef(const std::string& key);
  static time_t lastConfigMTime;
  static size_t configVersion;

#ifdef __linux__
  // inotify file descriptor & watch descriptor; -1 when not in use
  static int inotifyFd;
  static int inotifyWatch;
#endif
};