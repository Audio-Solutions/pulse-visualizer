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

nlohmann::json Config::configData;
std::string Config::configPath = "~/.config/pulse-visualizer/config.json";
std::unordered_map<std::string, nlohmann::json> Config::configCache;
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
    // If the config file is missing we fall back to an empty JSON object so the
    // program can still launch with the built-in defaults.  We warn once so the
    // user knows what happened.
    std::cerr << "Warning: could not open config file: " << expanded << " (using defaults)" << std::endl;
    configData = nlohmann::json::object();
    clearCache();
    return;
  }

  try {
    in >> configData;
  } catch (const nlohmann::json::exception& e) {
    // If JSON parsing fails, warn and fall back to defaults
    std::cerr << "Warning: JSON parsing error in config file " << expanded << ": " << e.what() << " (using defaults)"
              << std::endl;
    configData = nlohmann::json::object();
    clearCache();
    return;
  } catch (const std::exception& e) {
    // Catch any other parsing errors
    std::cerr << "Warning: Error parsing config file " << expanded << ": " << e.what() << " (using defaults)"
              << std::endl;
    configData = nlohmann::json::object();
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
    v.oscilloscope.amplitude_scale = getFloat("oscilloscope.amplitude_scale");
    v.oscilloscope.gradient_mode = getString("oscilloscope.gradient_mode");
    v.oscilloscope.enable_phosphor = getBool("oscilloscope.enable_phosphor");
    v.oscilloscope.follow_pitch = getBool("oscilloscope.follow_pitch");
    v.oscilloscope.phosphor_beam_energy = getFloat("oscilloscope.phosphor_beam_energy");
    v.oscilloscope.phosphor_db_lower_bound = getFloat("oscilloscope.phosphor_db_lower_bound");
    v.oscilloscope.phosphor_db_mid_point = getFloat("oscilloscope.phosphor_db_mid_point");
    v.oscilloscope.phosphor_db_upper_bound = getFloat("oscilloscope.phosphor_db_upper_bound");
    v.oscilloscope.phosphor_decay_constant = getFloat("oscilloscope.phosphor_decay_constant");
    v.oscilloscope.phosphor_beam_size = getFloat("oscilloscope.phosphor_beam_size");
    v.oscilloscope.phosphor_line_blur_spread = getFloat("oscilloscope.phosphor_line_blur_spread");
    v.oscilloscope.phosphor_line_width = getFloat("oscilloscope.phosphor_line_width");
    v.oscilloscope.phosphor_beam_speed_multiplier = getFloat("oscilloscope.phosphor_beam_speed_multiplier");

    v.lissajous.max_points = getInt("lissajous.max_points");
    v.lissajous.phosphor_tension = getFloat("lissajous.phosphor_tension");
    v.lissajous.enable_splines = getBool("lissajous.enable_splines");
    v.lissajous.spline_segments = getInt("lissajous.spline_segments");
    v.lissajous.enable_phosphor = getBool("lissajous.enable_phosphor");
    v.lissajous.phosphor_spline_density = getInt("lissajous.phosphor_spline_density");
    v.lissajous.phosphor_db_lower_bound = getFloat("lissajous.phosphor_db_lower_bound");
    v.lissajous.phosphor_db_mid_point = getFloat("lissajous.phosphor_db_mid_point");
    v.lissajous.phosphor_db_upper_bound = getFloat("lissajous.phosphor_db_upper_bound");
    v.lissajous.phosphor_decay_constant = getFloat("lissajous.phosphor_decay_constant");
    v.lissajous.phosphor_beam_size = getFloat("lissajous.phosphor_beam_size");
    v.lissajous.phosphor_line_blur_spread = getFloat("lissajous.phosphor_line_blur_spread");
    v.lissajous.phosphor_line_width = getFloat("lissajous.phosphor_line_width");
    v.lissajous.phosphor_beam_energy = getFloat("lissajous.phosphor_beam_energy");
    v.lissajous.phosphor_beam_speed_multiplier = getFloat("lissajous.phosphor_beam_speed_multiplier");

    v.fft.font = getString("font.default_font");
    v.fft.min_freq = getFloat("fft.fft_min_freq");
    v.fft.max_freq = getFloat("fft.fft_max_freq");
    v.fft.sample_rate = getFloat("audio.sample_rate");
    v.fft.slope_correction_db = getFloat("fft.fft_slope_correction_db");
    v.fft.min_db = getFloat("fft.fft_min_db");
    v.fft.max_db = getFloat("fft.fft_max_db");
    v.fft.stereo_mode = getString("fft.stereo_mode");
    v.fft.note_key_mode = getString("fft.note_key_mode");
    v.fft.enable_phosphor = getBool("fft.enable_phosphor");
    v.fft.phosphor_beam_energy = getFloat("fft.phosphor_beam_energy");
    v.fft.phosphor_db_lower_bound = getFloat("fft.phosphor_db_lower_bound");
    v.fft.phosphor_db_mid_point = getFloat("fft.phosphor_db_mid_point");
    v.fft.phosphor_db_upper_bound = getFloat("fft.phosphor_db_upper_bound");
    v.fft.phosphor_decay_constant = getFloat("fft.phosphor_decay_constant");
    v.fft.phosphor_beam_size = getFloat("fft.phosphor_beam_size");
    v.fft.phosphor_line_blur_spread = getFloat("fft.phosphor_line_blur_spread");
    v.fft.phosphor_line_width = getFloat("fft.phosphor_line_width");
    v.fft.phosphor_beam_speed_multiplier = getFloat("fft.phosphor_beam_speed_multiplier");

    v.spectrogram.time_window = getFloat("spectrogram.time_window");
    v.spectrogram.min_db = getFloat("spectrogram.min_db");
    v.spectrogram.max_db = getFloat("spectrogram.max_db");
    v.spectrogram.interpolation = getBool("spectrogram.interpolation");
    v.spectrogram.frequency_scale = getString("spectrogram.frequency_scale");
    v.spectrogram.min_freq = getFloat("spectrogram.min_freq");
    v.spectrogram.max_freq = getFloat("spectrogram.max_freq");

    v.audio.smoothing_factor = getFloat("fft.fft_smoothing_factor");
    v.audio.rise_speed = getFloat("fft.fft_rise_speed");
    v.audio.fall_speed = getFloat("fft.fft_fall_speed");
    v.audio.hover_fall_speed = getFloat("fft.fft_hover_fall_speed");
    v.audio.silence_threshold = getFloat("audio.silence_threshold");
    v.audio.sample_rate = getFloat("audio.sample_rate");
    v.audio.fft_size = getInt("fft.fft_size");
    v.audio.fft_skip_frames = getInt("audio.fft_skip_frames");
    v.audio.buffer_size = getInt("audio.buffer_size");
    v.audio.display_samples = getInt("audio.display_samples");
    v.audio.channels = getInt("audio.channels");
    v.audio.engine = getString("audio.engine");

    v.visualizers.spectrogram_order = getInt("visualizers.spectrogram_order");
    v.visualizers.spectrogram_popout = getBool("visualizers.spectrogram_popout");
    v.visualizers.lissajous_order = getInt("visualizers.lissajous_order");
    v.visualizers.lissajous_popout = getBool("visualizers.lissajous_popout");
    v.visualizers.oscilloscope_order = getInt("visualizers.oscilloscope_order");
    v.visualizers.oscilloscope_popout = getBool("visualizers.oscilloscope_popout");
    v.visualizers.fft_order = getInt("visualizers.fft_order");
    v.visualizers.fft_popout = getBool("visualizers.fft_popout");

    v.window.default_width = getInt("window.default_width");
    v.window.default_height = getInt("window.default_height");
    v.window.default_theme = getString("window.default_theme");

    v.pulseaudio.default_source = getString("pulseaudio.default_source");
    v.pulseaudio.buffer_size = getInt("pulseaudio.buffer_size");

    v.pipewire.default_source = getString("pipewire.default_source");
    v.pipewire.buffer_size = getInt("pipewire.buffer_size");
    v.pipewire.ring_multiplier = getInt("pipewire.ring_multiplier");

    v.debug.log_fps = getBool("debug.log_fps");
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

// Helper to traverse dot-separated keys
nlohmann::json& Config::getJsonRef(const std::string& key) {
  nlohmann::json* ref = &configData;
  std::stringstream ss(key);
  std::string segment;
  while (std::getline(ss, segment, '.')) {
    if (!ref->contains(segment) || (*ref)[segment].is_null()) {
      // Gracefully handle a missing value by returning a reference to a static
      // null object.  This lets callers decide how to fall back while issuing a
      // one-off warning.
      static nlohmann::json nullJson;
      std::cerr << "Warning: config key missing: " << key << std::endl;
      return nullJson;
    }
    ref = &(*ref)[segment];
  }
  return *ref;
}

int Config::getInt(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_number_integer()) {
      std::cerr << "Warning: config key '" << key << "' is not an integer (cached), using 0" << std::endl;
      configCache[key] = 0;
      return 0;
    }
    return it->second.get<int>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_number_integer()) {
    std::cerr << "Warning: config key '" << key << "' expected integer, using 0" << std::endl;
    configCache[key] = 0;
    return 0;
  }
  configCache[key] = val;
  return val.get<int>();
}

float Config::getFloat(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_number()) {
      std::cerr << "Warning: config key '" << key << "' is not a number (cached), using 0.0" << std::endl;
      configCache[key] = 0.0f;
      return 0.0f;
    }
    return it->second.get<float>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_number()) {
    std::cerr << "Warning: config key '" << key << "' expected number, using 0.0" << std::endl;
    configCache[key] = 0.0f;
    return 0.0f;
  }
  configCache[key] = val;
  return val.get<float>();
}

bool Config::getBool(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_boolean()) {
      std::cerr << "Warning: config key '" << key << "' is not a boolean (cached), using false" << std::endl;
      configCache[key] = false;
      return false;
    }
    return it->second.get<bool>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_boolean()) {
    std::cerr << "Warning: config key '" << key << "' expected boolean, using false" << std::endl;
    configCache[key] = false;
    return false;
  }
  configCache[key] = val;
  return val.get<bool>();
}

std::string Config::getString(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_string()) {
      std::cerr << "Warning: config key '" << key << "' is not a string (cached), using empty string" << std::endl;
      configCache[key] = "";
      return "";
    }
    return it->second.get<std::string>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_string()) {
    std::cerr << "Warning: config key '" << key << "' expected string, using empty string" << std::endl;
    configCache[key] = "";
    return "";
  }
  configCache[key] = val;
  return val.get<std::string>();
}

nlohmann::json Config::get(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    return it->second;
  }
  nlohmann::json& val = getJsonRef(key);
  configCache[key] = val;
  return val;
}

size_t Config::getVersion() { return configVersion; }

const ConfigValues& Config::values() { return g_cachedValues; }