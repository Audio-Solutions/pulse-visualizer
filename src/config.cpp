#include "include/config.hpp"

namespace Config {

// Global configuration options
Options options;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

void copyFiles() {
  const char* home = getenv("HOME");
  if (!home) {
    std::cerr << "Warning: HOME environment variable not set, cannot setup user config" << std::endl;
    return;
  }

  // Create user directories
  std::string userCfgDir = std::string(home) + "/.config/pulse-visualizer";
  std::string userThemeDir = userCfgDir + "/themes";
  std::string userFontDir = std::string(home) + "/.local/share/fonts/JetBrainsMono";

  std::filesystem::create_directories(userCfgDir);
  std::filesystem::create_directories(userThemeDir);
  std::filesystem::create_directories(userFontDir);

  // Copy configuration template if it doesn't exist
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

  // Copy theme files
  std::string themeSource = std::string(PULSE_DATA_DIR) + "/themes";
  if (std::filesystem::exists(themeSource) && !std::filesystem::exists(userThemeDir + "/_TEMPLATE.txt")) {
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

  // Copy font file
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

std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path) {
  if (root[path].IsDefined())
    return root[path];

  // Handle nested paths with dot notation
  size_t dotIndex = path.find('.');
  if (dotIndex == std::string::npos)
    return std::nullopt;

  std::string section = path.substr(0, dotIndex);
  std::string sub = path.substr(dotIndex + 1);

  if (!root[section].IsDefined())
    return std::nullopt;

  return getNode(root[section], sub);
}

/**
 * @brief Get a value from the configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @return Value of type T
 */
template <typename T> T get(const YAML::Node& root, const std::string& path) {
  auto node = getNode(root, path);
  if (node.has_value())
    return node.value().as<T>();

  std::cerr << path << " is missing." << std::endl;

  return T {};
}

/**
 * @brief Get a boolean value from the configuration with better type conversion
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @return Value of type bool
 */
template <> bool get<bool>(const YAML::Node& root, const std::string& path) {
  auto node = getNode(root, path);
  if (!node.has_value())
    return false;

  const YAML::Node& value = node.value();

  try {
    return value.as<bool>();
  } catch (...) {
    try {
      std::string str = value.as<std::string>();
      if (str == "true" || str == "yes" || str == "on")
        return true;
      if (str == "false" || str == "no" || str == "off")
        return false;
      std::cerr << "Converting " << path << " to bool failed" << std::endl;
      return false;
    } catch (...) {
      try {
        return value.as<int>() != 0;
      } catch (...) {
        std::cerr << "Converting  " << path << " to bool failed" << std::endl;
        return false;
      }
    }
  }
}

/**
 * @brief Get a vector of strings from the configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @return Value of type std::vector<std::string>
 */
template <> std::vector<std::string> get<std::vector<std::string>>(const YAML::Node& root, const std::string& path) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsSequence())
    return std::vector<std::string> {};

  std::vector<std::string> result;
  for (const auto& item : node.value()) {
    result.push_back(item.as<std::string>());
  }
  return result;
}

void load() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData = YAML::LoadFile(path);

#ifdef __linux__
  // Setup file watching for configuration changes
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyWatch != -1)
      inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = inotify_add_watch(inotifyFd, path.c_str(), IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
  }
#endif

  // Load visualizer window layouts
  options.visualizers = get<std::vector<std::string>>(configData, "visualizers");

  // Load oscilloscope configuration
  options.oscilloscope.follow_pitch = get<bool>(configData, "oscilloscope.follow_pitch");
  options.oscilloscope.alignment = get<std::string>(configData, "oscilloscope.alignment");
  options.oscilloscope.alignment_type = get<std::string>(configData, "oscilloscope.alignment_type");
  options.oscilloscope.limit_cycles = get<bool>(configData, "oscilloscope.limit_cycles");
  options.oscilloscope.cycles = get<int>(configData, "oscilloscope.cycles");
  options.oscilloscope.min_cycle_time = get<float>(configData, "oscilloscope.min_cycle_time");
  options.oscilloscope.time_window = get<float>(configData, "oscilloscope.time_window");
  options.oscilloscope.beam_multiplier = get<float>(configData, "oscilloscope.beam_multiplier");

  // Load bandpass filter configuration
  options.bandpass_filter.bandwidth = get<float>(configData, "bandpass_filter.bandwidth");
  options.bandpass_filter.bandwidth_type = get<std::string>(configData, "bandpass_filter.bandwidth_type");
  options.bandpass_filter.order = get<int>(configData, "bandpass_filter.order");

  // Load Lissajous configuration
  options.lissajous.enable_splines = get<bool>(configData, "lissajous.enable_splines");
  options.lissajous.spline_segments = get<int>(configData, "lissajous.spline_segments");
  options.lissajous.beam_multiplier = get<float>(configData, "lissajous.beam_multiplier");
  options.lissajous.readback_multiplier = get<float>(configData, "lissajous.readback_multiplier");

  // Load FFT configuration
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

  // Load spectrogram configuration
  options.spectrogram.time_window = get<float>(configData, "spectrogram.time_window");
  options.spectrogram.min_db = get<float>(configData, "spectrogram.min_db");
  options.spectrogram.max_db = get<float>(configData, "spectrogram.max_db");
  options.spectrogram.interpolation = get<bool>(configData, "spectrogram.interpolation");
  options.spectrogram.frequency_scale = get<std::string>(configData, "spectrogram.frequency_scale");
  options.spectrogram.min_freq = get<float>(configData, "spectrogram.min_freq");
  options.spectrogram.max_freq = get<float>(configData, "spectrogram.max_freq");

  // Load audio configuration
  options.audio.silence_threshold = get<float>(configData, "audio.silence_threshold");
  options.audio.sample_rate = get<float>(configData, "audio.sample_rate");
  options.audio.gain_db = get<float>(configData, "audio.gain_db");
  options.audio.engine = get<std::string>(configData, "audio.engine");
  options.audio.device = get<std::string>(configData, "audio.device");

  // Load window configuration
  options.window.default_width = get<int>(configData, "window.default_width");
  options.window.default_height = get<int>(configData, "window.default_height");
  options.window.theme = get<std::string>(configData, "window.theme");
  options.window.fps_limit = get<int>(configData, "window.fps_limit");

  // Load debug configuration
  options.debug.log_fps = get<bool>(configData, "debug.log_fps");
  options.debug.show_bandpassed = get<bool>(configData, "debug.show_bandpassed");

  // Load phosphor effect configuration
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

  // Load font configuration
  options.font = get<std::string>(configData, "font");
}

bool reload() {
#ifdef __linux__
  // Check for file changes using inotify
  if (inotifyFd != -1 && inotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(inotifyFd, buf, sizeof(buf));
    if (len > 0) {
      load();
      return true;
    }
  }
#else
  // Check for file changes using stat (non-Linux systems)
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

void cleanup() {
#ifdef __linux__
  if (inotifyWatch != -1 && inotifyFd != -1) {
    inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = -1;
  }
  if (inotifyFd != -1) {
    close(inotifyFd);
    inotifyFd = -1;
  }
#endif
}

template int get<int>(const YAML::Node&, const std::string&);
template float get<float>(const YAML::Node&, const std::string&);
template std::string get<std::string>(const YAML::Node&, const std::string&);

} // namespace Config