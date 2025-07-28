#include "include/config.hpp"

#include "include/common.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <yaml-cpp/yaml.h>

namespace Config {
void copyFiles() {
  const char* home = getenv("HOME");
  if (!home) { /* ... */
  }

  std::string userCfgDir = std::string(home) + "/.config/pulse-visualizer";
  std::string userThemeDir = userCfgDir + "/themes";
  std::string userFontDir = std::string(home) + "/.local/share/fonts/JetBrainsMono";

  std::filesystem::create_directories(userCfgDir);
  std::filesystem::create_directories(userThemeDir);
  std::filesystem::create_directories(userFontDir);

  if (!std::filesystem::exists(userCfgDir + "/config.yml")) { /* ... */
  }

  std::string themeSource = std::string(PULSE_DATA_DIR) + "/themes";
  if (std::filesystem::exists(themeSource) && !std::filesystem::exists(themeSource + "/_TEMPLATE.txt")) { /* ... */
  }

  // Copy font from system installation
  std::string fontSource = std::string(PULSE_DATA_DIR) + "/fonts/JetBrainsMonoNerdFont-Medium.ttf";
  std::string userFontFile = userFontDir + "/JetBrainsMonoNerdFont-Medium.ttf";
  if (std::filesystem::exists(fontSource)) { /* ... */
  }
}

struct Options {
  oscilloscope;

  bandpass_filter;

  lissajous;

  fft;

  spectrogram;

  audio;

  visualizers;

  window;

  debug;

  phosphor;

  std::string font;
} options;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path) {
  if (root[path].IsDefined())
    ; // ...

  size_t dotIndex = path.find('.');
  if (dotIndex == std::string::npos)
    ; // ...

  std::string section = path.substr(0, dotIndex);
  std::string sub = path.substr(dotIndex + 1);

  if (!root[section].IsDefined())
    ; // ...

  return getNode(root[section], sub);
}

template <typename T> T get(const YAML::Node& root, const std::string& path) { /* ... */ }

void load() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData = YAML::LoadFile(path);

#ifdef __linux__
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
#else

#endif
  return false;
}
} // namespace Config
