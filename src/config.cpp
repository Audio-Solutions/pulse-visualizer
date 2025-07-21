#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#endif

std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

YAML::Node Config::configData;
std::string Config::configPath = "~/.config/pulse-visualizer/config.yml";
std::unordered_map<std::string, YAML::Node> Config::configCache;
time_t Config::lastConfigMTime = 0;
size_t Config::configVersion = 0;
ConfigValues g_cachedValues;

#ifdef __linux__
int Config::inotifyFd = -1;
int Config::inotifyWatch = -1;
#endif

void Config::clearCache() { configCache.clear(); }

void Config::load(const std::string& filename) {
  configPath = filename;
  std::string expanded = expandUserPath(filename);

#ifdef __linux__
  // (Re)initialise inotify watcher (Linux only). Non-blocking so it doesn't
  // stall the main thread; we still fall back to stat-based polling when
  // inotify isn't available.
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
  }
  if (inotifyFd != -1) {
    if (inotifyWatch != -1) {
      inotify_rm_watch(inotifyFd, inotifyWatch);
    }
    inotifyWatch = inotify_add_watch(inotifyFd, expanded.c_str(), IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
  }
#endif

  std::ifstream in(expanded);
  if (!in) {
    // If the config file is missing we fall back to an empty YAML object so the
    // program can still launch with the built-in defaults.  We warn once so the
    // user knows what happened.
    std::cerr << "Warning: could not open config file: " << expanded << " (using defaults)" << std::endl;
    configData = YAML::Node();
    clearCache();
    return;
  }

  try {
    configData = YAML::Load(in);
  } catch (const YAML::Exception& e) {
    // If YAML parsing fails, warn and fall back to defaults
    std::cerr << "Warning: YAML parsing error in config file " << expanded << ": " << e.what() << " (using defaults)"
              << std::endl;
    configData = YAML::Node();
    clearCache();
    return;
  } catch (const std::exception& e) {
    // Catch any other parsing errors
    std::cerr << "Warning: Error parsing config file " << expanded << ": " << e.what() << " (using defaults)"
              << std::endl;
    configData = YAML::Node();
    clearCache();
    return;
  }
  clearCache();
  // Update last modification time
  struct stat st;
  if (stat(expanded.c_str(), &st) == 0) {
    lastConfigMTime = st.st_mtime;
  }
  configVersion++; // Increment version on every reload

  // Re-populate cached values struct for fast access across the codebase.
  try {
    auto& v = g_cachedValues;
    v.oscilloscope.gradient_mode = getString("oscilloscope.gradient_mode");
    v.oscilloscope.enable_phosphor = getBool("oscilloscope.enable_phosphor");
    v.oscilloscope.follow_pitch = getBool("oscilloscope.follow_pitch");
    v.oscilloscope.alignment = getString("oscilloscope.alignment");
    v.oscilloscope.alignment_type = getString("oscilloscope.alignment_type");
    v.oscilloscope.limit_cycles = getBool("oscilloscope.limit_cycles");
    v.oscilloscope.cycles = getInt("oscilloscope.cycles");
    v.oscilloscope.min_cycle_time = getFloat("oscilloscope.min_cycle_time");
    v.oscilloscope.time_window = getFloat("oscilloscope.time_window");
    v.oscilloscope.beam_multiplier = getFloat("oscilloscope.beam_multiplier");

    v.bandpass_filter.bandwidth = getFloat("bandpass_filter.bandwidth");
    v.bandpass_filter.bandwidth_type = getString("bandpass_filter.bandwidth_type");
    v.bandpass_filter.order = getInt("bandpass_filter.order");

    v.lissajous.enable_splines = getBool("lissajous.enable_splines");
    v.lissajous.spline_segments = getInt("lissajous.spline_segments");
    v.lissajous.enable_phosphor = getBool("lissajous.enable_phosphor");
    v.lissajous.beam_multiplier = getFloat("lissajous.beam_multiplier");

    v.fft.min_freq = getFloat("fft.min_freq");
    v.fft.max_freq = getFloat("fft.max_freq");
    v.fft.sample_rate = getFloat("audio.sample_rate");
    v.fft.slope_correction_db = getFloat("fft.slope_correction_db");
    v.fft.min_db = getFloat("fft.min_db");
    v.fft.max_db = getFloat("fft.max_db");
    v.fft.stereo_mode = getString("fft.stereo_mode");
    v.fft.note_key_mode = getString("fft.note_key_mode");
    v.fft.enable_phosphor = getBool("fft.enable_phosphor");
    v.fft.enable_cqt = getBool("fft.enable_cqt");
    v.fft.cqt_bins_per_octave = getInt("fft.cqt_bins_per_octave");
    v.fft.smoothing_factor = getFloat("fft.smoothing_factor");
    v.fft.rise_speed = getFloat("fft.rise_speed");
    v.fft.fall_speed = getFloat("fft.fall_speed");
    v.fft.hover_fall_speed = getFloat("fft.hover_fall_speed");
    v.fft.size = getInt("fft.size");
    v.fft.beam_multiplier = getFloat("fft.beam_multiplier");

    v.spectrogram.time_window = getFloat("spectrogram.time_window");
    v.spectrogram.min_db = getFloat("spectrogram.min_db");
    v.spectrogram.max_db = getFloat("spectrogram.max_db");
    v.spectrogram.interpolation = getBool("spectrogram.interpolation");
    v.spectrogram.frequency_scale = getString("spectrogram.frequency_scale");
    v.spectrogram.min_freq = getFloat("spectrogram.min_freq");
    v.spectrogram.max_freq = getFloat("spectrogram.max_freq");

    v.audio.silence_threshold = getFloat("audio.silence_threshold");
    v.audio.sample_rate = getFloat("audio.sample_rate");
    v.audio.gain_db = getFloat("audio.gain_db");
    v.audio.engine = getString("audio.engine");
    v.audio.device = getString("audio.device");

    v.visualizers.spectrogram_order = getInt("visualizers.spectrogram_order");
    v.visualizers.lissajous_order = getInt("visualizers.lissajous_order");
    v.visualizers.oscilloscope_order = getInt("visualizers.oscilloscope_order");
    v.visualizers.fft_order = getInt("visualizers.fft_order");

    v.window.default_width = getInt("window.default_width");
    v.window.default_height = getInt("window.default_height");
    v.window.default_theme = getString("window.default_theme");
    v.window.fps_limit = getInt("window.fps_limit");

    v.debug.log_fps = getBool("debug.log_fps");
    v.debug.show_bandpassed = getBool("debug.show_bandpassed");

    v.phosphor.near_blur_intensity = getFloat("phosphor.near_blur_intensity");
    v.phosphor.far_blur_intensity = getFloat("phosphor.far_blur_intensity");
    v.phosphor.beam_energy = getFloat("phosphor.beam_energy");
    v.phosphor.decay_slow = getFloat("phosphor.decay_slow");
    v.phosphor.decay_fast = getFloat("phosphor.decay_fast");
    v.phosphor.beam_size = getFloat("phosphor.beam_size");
    v.phosphor.line_blur_spread = getFloat("phosphor.line_blur_spread");
    v.phosphor.line_width = getFloat("phosphor.line_width");
    v.phosphor.age_threshold = getInt("phosphor.age_threshold");
    v.phosphor.range_factor = getFloat("phosphor.range_factor");
    v.phosphor.enable_grain = getBool("phosphor.enable_grain");
    v.phosphor.spline_density = getInt("phosphor.spline_density");
    v.phosphor.tension = getFloat("phosphor.tension");
    v.phosphor.enable_curved_screen = getBool("phosphor.enable_curved_screen");
    v.phosphor.screen_curvature = getFloat("phosphor.screen_curvature");
    v.phosphor.screen_gap = getFloat("phosphor.screen_gap");
    v.phosphor.grain_strength = getFloat("phosphor.grain_strength");
    v.phosphor.vignette_strength = getFloat("phosphor.vignette_strength");
    v.phosphor.chromatic_aberration_strength = getFloat("phosphor.chromatic_aberration_strength");

    v.font = getString("font");
  } catch (...) {
    // If any key is missing we already warned; values struct keeps defaults.
  }
}

void Config::reload() { load(configPath); }

#ifdef __linux__
bool Config::reloadIfChanged() {
  // Prefer inotify when available
  if (inotifyFd != -1 && inotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(inotifyFd, buf, sizeof(buf));
    if (len > 0) {
      // One or more events – reload the config
      load(configPath);
      return true;
    }
    // No events; fall through to stat fallback to catch first-run differences
  }
#else
bool Config::reloadIfChanged() {
#endif
  std::string expanded = expandUserPath(configPath);
  struct stat st;
  if (stat(expanded.c_str(), &st) != 0) {
    std::cerr << "Warning: could not stat config file: " << expanded << std::endl;
    return false;
  }
  if (st.st_mtime != lastConfigMTime) {
    load(configPath);
    return true;
  }
  return false;
}

// Helper to get YAML node by key path
YAML::Node Config::getJsonRef(const std::string& key) {
  static YAML::Node nullNode;

  // Handle direct access for single-level keys
  if (configData[key]) {
    return configData[key];
  }

  // For multi-level keys, use a simpler approach
  size_t dotPos = key.find('.');
  if (dotPos == std::string::npos) {
    return nullNode;
  }

  std::string section = key.substr(0, dotPos);
  std::string subKey = key.substr(dotPos + 1);

  if (!configData[section] || !configData[section][subKey]) {
    return nullNode;
  }

  return configData[section][subKey];
}

int Config::getInt(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.IsScalar()) {
      std::cerr << "Warning: config key '" << key << "' is not an integer (cached), using 0" << std::endl;
      configCache[key] = 0;
      return 0;
    }
    try {
      return it->second.as<int>();
    } catch (const YAML::Exception&) {
      std::cerr << "Warning: config key '" << key << "' is not an integer (cached), using 0" << std::endl;
      configCache[key] = 0;
      return 0;
    }
  }
  YAML::Node val = getJsonRef(key);
  if (!val.IsScalar()) {
    std::cerr << "Warning: config key '" << key << "' expected integer, using 0" << std::endl;
    configCache[key] = 0;
    return 0;
  }
  try {
    int result = val.as<int>();
    configCache[key] = val;
    return result;
  } catch (const YAML::Exception&) {
    std::cerr << "Warning: config key '" << key << "' expected integer, using 0" << std::endl;
    configCache[key] = 0;
    return 0;
  }
}

float Config::getFloat(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.IsScalar()) {
      std::cerr << "Warning: config key '" << key << "' is not a number (cached), using 0.0" << std::endl;
      configCache[key] = 0.0f;
      return 0.0f;
    }
    try {
      return it->second.as<float>();
    } catch (const YAML::Exception&) {
      std::cerr << "Warning: config key '" << key << "' is not a number (cached), using 0.0" << std::endl;
      configCache[key] = 0.0f;
      return 0.0f;
    }
  }
  YAML::Node val = getJsonRef(key);
  if (!val.IsScalar()) {
    std::cerr << "Warning: config key '" << key << "' expected number, using 0.0" << std::endl;
    configCache[key] = 0.0f;
    return 0.0f;
  }
  try {
    float result = val.as<float>();
    configCache[key] = val;
    return result;
  } catch (const YAML::Exception&) {
    std::cerr << "Warning: config key '" << key << "' expected number, using 0.0" << std::endl;
    configCache[key] = 0.0f;
    return 0.0f;
  }
}

bool Config::getBool(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.IsScalar()) {
      std::cerr << "Warning: config key '" << key << "' is not a boolean (cached), using false" << std::endl;
      configCache[key] = false;
      return false;
    }
    try {
      return it->second.as<bool>();
    } catch (const YAML::Exception&) {
      std::cerr << "Warning: config key '" << key << "' is not a boolean (cached), using false" << std::endl;
      configCache[key] = false;
      return false;
    }
  }
  YAML::Node val = getJsonRef(key);
  if (!val.IsScalar()) {
    std::cerr << "Warning: config key '" << key << "' expected boolean, using false" << std::endl;
    configCache[key] = false;
    return false;
  }
  try {
    bool result = val.as<bool>();
    configCache[key] = val;
    return result;
  } catch (const YAML::Exception&) {
    std::cerr << "Warning: config key '" << key << "' expected boolean, using false" << std::endl;
    configCache[key] = false;
    return false;
  }
}

std::string Config::getString(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.IsScalar()) {
      std::cerr << "Warning: config key '" << key << "' is not a string (cached), using empty string" << std::endl;
      configCache[key] = "";
      return "";
    }
    try {
      return it->second.as<std::string>();
    } catch (const YAML::Exception&) {
      std::cerr << "Warning: config key '" << key << "' is not a string (cached), using empty string" << std::endl;
      configCache[key] = "";
      return "";
    }
  }
  YAML::Node val = getJsonRef(key);
  if (!val.IsScalar()) {
    std::cerr << "Warning: config key '" << key << "' expected string, using empty string" << std::endl;
    configCache[key] = "";
    return "";
  }
  try {
    std::string result = val.as<std::string>();
    configCache[key] = val;
    return result;
  } catch (const YAML::Exception&) {
    std::cerr << "Warning: config key '" << key << "' expected string, using empty string" << std::endl;
    configCache[key] = "";
    return "";
  }
}

YAML::Node Config::get(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    return it->second;
  }
  YAML::Node val = getJsonRef(key);
  configCache[key] = val;
  return val;
}

size_t Config::getVersion() { return configVersion; }

const ConfigValues& Config::values() { return g_cachedValues; }