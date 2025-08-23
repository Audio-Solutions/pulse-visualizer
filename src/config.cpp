/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
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

#include "include/config.hpp"

namespace Config {

bool broken = false;

// Global configuration options
Options options;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

std::string getInstallDir() {
#ifdef _WIN32
  char buffer[MAX_PATH];
  DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
  if (length == 0)
    return std::string();

  std::string path(buffer);
  size_t pos = path.find_last_of("\\/");
  if (pos != std::string::npos)
    return path.substr(0, pos);
  return std::string();
#else
  return std::string(PULSE_DATA_DIR);
#endif
}

void copyFiles() {
#ifdef _WIN32
  const char* home = getenv("USERPROFILE");
#else
  const char* home = getenv("HOME");
#endif
  if (!home) {
#ifdef _WIN32
    LOG_ERROR("Warning: USERPROFILE environment variable not set, cannot setup user config");
#else
    LOG_ERROR("Warning: HOME environment variable not set, cannot setup user config");
#endif
    return;
  }

  // Create user directories
  std::string userCfgDir = std::string(home) + "/.config/pulse-visualizer";
  std::string userThemeDir = userCfgDir + "/themes";
  std::string userFontDir = std::string(home) + "/.local/share/fonts/JetBrainsMono";

  std::filesystem::create_directories(userCfgDir);
  std::filesystem::create_directories(userThemeDir);
  std::filesystem::create_directories(userFontDir);

  std::string installDir = getInstallDir();

  // Copy configuration template if it doesn't exist
  if (!std::filesystem::exists(userCfgDir + "/config.yml")) {
    std::string cfgSource = installDir + "/config.yml.template";
    if (std::filesystem::exists(cfgSource)) {
      try {
        std::filesystem::copy_file(cfgSource, userCfgDir + "/config.yml");
        LOG_DEBUG(std::string("Created user config file: ") + userCfgDir + "/config.yml");
      } catch (const std::exception& e) {
        LOG_ERROR(std::string("Warning: Failed to copy config template: ") + e.what());
      }
    }
  }

  // Copy theme files
  std::string themeSource = installDir + "/themes";
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
      LOG_DEBUG(std::string("Copied themes to: ") + userThemeDir);
    } catch (const std::exception& e) {
      LOG_ERROR(std::string("Warning: Failed to copy themes: ") + e.what());
    }
  }

  // Copy font file
  std::string fontSource = installDir + "/fonts/JetBrainsMonoNerdFont-Medium.ttf";
  std::string userFontFile = userFontDir + "/JetBrainsMonoNerdFont-Medium.ttf";
  if (std::filesystem::exists(fontSource)) {
    try {
      if (!std::filesystem::exists(userFontFile)) {
        std::filesystem::copy_file(fontSource, userFontFile);
        LOG_DEBUG(std::string("Copied font to: ") + userFontFile);
      }
    } catch (const std::exception& e) {
      LOG_ERROR(std::string("Warning: Failed to copy font: ") + e.what());
    }
  }
}

std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path) {
  if (root[path].IsDefined())
    return root[path];

  // Handle nested paths with dot notation
  size_t dotIndex = path.find('.');
  if (dotIndex == std::string::npos) {
    broken = true;
    return std::nullopt;
  }

  std::string section = path.substr(0, dotIndex);
  std::string sub = path.substr(dotIndex + 1);

  if (!root[section].IsDefined()) {
    broken = true;
    return std::nullopt;
  }

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
  if (node.has_value()) {
    try {
      return node.value().as<T>();
    } catch (std::exception) {
      std::string typeStr = "unknown";
      if (typeid(T) == typeid(std::string))
        typeStr = "string";
      else if (typeid(T) == typeid(int))
        typeStr = "int";
      else if (typeid(T) == typeid(float))
        typeStr = "float";
      else if (typeid(T) == typeid(Rotation))
        typeStr = "Rotation";
      broken = true;
      LOG_ERROR(std::string("Converting ") + path + " to " + typeStr + " failed");
      return T {};
    }
  }

  broken = true;
  LOG_ERROR(path + " is missing.");

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
  if (!node.has_value()) {
    broken = true;
    return false;
  }

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
      LOG_ERROR(std::string("Converting ") + path + " to bool failed");
      broken = true;
      return false;
    } catch (...) {
      try {
        return value.as<int>() != 0;
      } catch (...) {
        LOG_ERROR(std::string("Converting  ") + path + " to bool failed");
        broken = true;
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
  if (!node.has_value() || !node.value().IsSequence()) {
    broken = true;
    return std::vector<std::string> {};
  }

  std::vector<std::string> result;
  for (const auto& item : node.value()) {
    result.push_back(item.as<std::string>());
  }
  return result;
}

/**
 * @brief Get a map of strings to vectors of strings from the configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @return Value of type std::map<std::string, std::vector<std::string>>
 */
template <>
std::map<std::string, std::vector<std::string>>
get<std::map<std::string, std::vector<std::string>>>(const YAML::Node& root, const std::string& path) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsMap()) {
    broken = true;
    return std::map<std::string, std::vector<std::string>> {};
  }

  std::map<std::string, std::vector<std::string>> result;
  for (const auto& item : node.value())
    result[item.first.as<std::string>()] =
        get<std::vector<std::string>>(root, path + "." + item.first.as<std::string>());
  return result;
}

/**
 * @brief Get a rotation value from the configuration from a int
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @return Value of type Rotation
 */
template <> Rotation get<Rotation>(const YAML::Node& root, const std::string& path) {
  return static_cast<Rotation>(get<int>(root, path));
}

void load() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData;

  // Handle YAML errors
  try {
    configData = YAML::LoadFile(path);
  } catch (YAML::BadFile e) {
    LOG_ERROR("Failed to load config file");
  } catch (YAML::ParserException e) {
    LOG_ERROR(std::string("Parser error when loading the config file: \"") + e.msg + "\" at " +
              std::to_string(e.mark.line + 1) + "(" + std::to_string(e.mark.column + 1) + ")");
    broken = true;
  }

#ifdef __linux__
  // Setup file watching for configuration changes
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyWatch != -1)
      inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = inotify_add_watch(inotifyFd, path.c_str(), IN_CLOSE_WRITE);
  }
#endif

  // If the config fails to load we still want the watch to be created
  if (configData.IsNull())
    return;

  // Load visualizer window layouts
  options.visualizers = get<std::map<std::string, std::vector<std::string>>>(configData, "visualizers");

  // Load oscilloscope configuration
  options.oscilloscope.follow_pitch = get<bool>(configData, "oscilloscope.follow_pitch");
  options.oscilloscope.alignment = get<std::string>(configData, "oscilloscope.alignment");
  options.oscilloscope.alignment_type = get<std::string>(configData, "oscilloscope.alignment_type");
  options.oscilloscope.limit_cycles = get<bool>(configData, "oscilloscope.limit_cycles");
  options.oscilloscope.cycles = get<int>(configData, "oscilloscope.cycles");
  options.oscilloscope.min_cycle_time = get<float>(configData, "oscilloscope.min_cycle_time");
  options.oscilloscope.time_window = get<float>(configData, "oscilloscope.time_window");
  options.oscilloscope.beam_multiplier = get<float>(configData, "oscilloscope.beam_multiplier");
  options.oscilloscope.enable_lowpass = get<bool>(configData, "oscilloscope.enable_lowpass");
  options.oscilloscope.rotation = get<Rotation>(configData, "oscilloscope.rotation");
  options.oscilloscope.flip_x = get<bool>(configData, "oscilloscope.flip_x");

  // Load bandpass filter configuration
  options.bandpass_filter.bandwidth = get<float>(configData, "bandpass_filter.bandwidth");
  options.bandpass_filter.sidelobe = get<float>(configData, "bandpass_filter.sidelobe");

  // Load Lissajous configuration
  options.lissajous.enable_splines = get<bool>(configData, "lissajous.enable_splines");
  options.lissajous.beam_multiplier = get<float>(configData, "lissajous.beam_multiplier");
  options.lissajous.readback_multiplier = get<float>(configData, "lissajous.readback_multiplier");
  options.lissajous.mode = get<std::string>(configData, "lissajous.mode");
  options.lissajous.rotation = get<Rotation>(configData, "lissajous.rotation");

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
  options.fft.rotation = get<Rotation>(configData, "fft.rotation");
  options.fft.flip_x = get<bool>(configData, "fft.flip_x");

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
  options.window.decorations = get<bool>(configData, "window.decorations");
  options.window.always_on_top = get<bool>(configData, "window.always_on_top");

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
  options.phosphor.line_blur_spread = get<float>(configData, "phosphor.line_blur_spread");
  options.phosphor.line_width = get<float>(configData, "phosphor.line_width");
  options.phosphor.age_threshold = get<int>(configData, "phosphor.age_threshold");
  options.phosphor.range_factor = get<float>(configData, "phosphor.range_factor");
  options.phosphor.enable_grain = get<bool>(configData, "phosphor.enable_grain");
  options.phosphor.tension = get<float>(configData, "phosphor.tension");
  options.phosphor.enable_curved_screen = get<bool>(configData, "phosphor.enable_curved_screen");
  options.phosphor.screen_curvature = get<float>(configData, "phosphor.screen_curvature");
  options.phosphor.screen_gap = get<float>(configData, "phosphor.screen_gap");
  options.phosphor.grain_strength = get<float>(configData, "phosphor.grain_strength");
  options.phosphor.vignette_strength = get<float>(configData, "phosphor.vignette_strength");
  options.phosphor.chromatic_aberration_strength = get<float>(configData, "phosphor.chromatic_aberration_strength");
  options.phosphor.colorbeam = get<bool>(configData, "phosphor.colorbeam");

  // Load LUFS configuration
  options.lufs.mode = get<std::string>(configData, "lufs.mode");
  options.lufs.scale = get<std::string>(configData, "lufs.scale");
  options.lufs.label = get<std::string>(configData, "lufs.label");

  // Load VU configuration
  options.vu.time_window = get<float>(configData, "vu.time_window");
  options.vu.style = get<std::string>(configData, "vu.style");
  options.vu.calibration_db = get<float>(configData, "vu.calibration_db");
  options.vu.scale = get<std::string>(configData, "vu.scale");
  options.vu.enable_momentum = get<bool>(configData, "vu.enable_momentum");
  options.vu.spring_constant = get<float>(configData, "vu.spring_constant");
  options.vu.damping_ratio = get<float>(configData, "vu.damping_ratio");
  options.vu.needle_width = get<float>(configData, "vu.needle_width");

  // Load lowpass configuration
  options.lowpass.cutoff = get<float>(configData, "lowpass.cutoff");
  options.lowpass.order = get<int>(configData, "lowpass.order");

  // Load font configuration
  options.font = get<std::string>(configData, "font");

  // If the config is broken, attempt to recover by merging defaults
  if (broken) {
    LOG_ERROR("Config is broken, attempting to recover...");
    save();
    load();
  }
}

void save() {
  broken = false;

  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");

  YAML::Node root;

  // Audio
  root["audio"]["device"] = options.audio.device;
  root["audio"]["engine"] = options.audio.engine;
  root["audio"]["gain_db"] = options.audio.gain_db;
  root["audio"]["sample_rate"] = options.audio.sample_rate;
  root["audio"]["silence_threshold"] = options.audio.silence_threshold;

  // Bandpass filter
  root["bandpass_filter"]["bandwidth"] = options.bandpass_filter.bandwidth;
  root["bandpass_filter"]["sidelobe"] = options.bandpass_filter.sidelobe;

  // Debug
  root["debug"]["log_fps"] = options.debug.log_fps;
  root["debug"]["show_bandpassed"] = options.debug.show_bandpassed;

  // FFT
  root["fft"]["beam_multiplier"] = options.fft.beam_multiplier;
  root["fft"]["cqt_bins_per_octave"] = options.fft.cqt_bins_per_octave;
  root["fft"]["enable_cqt"] = options.fft.enable_cqt;
  root["fft"]["enable_smoothing"] = options.fft.enable_smoothing;
  root["fft"]["fall_speed"] = options.fft.fall_speed;
  root["fft"]["flip_x"] = options.fft.flip_x;
  root["fft"]["frequency_markers"] = options.fft.frequency_markers;
  root["fft"]["hover_fall_speed"] = options.fft.hover_fall_speed;
  root["fft"]["max_db"] = options.fft.max_db;
  root["fft"]["max_freq"] = options.fft.max_freq;
  root["fft"]["min_db"] = options.fft.min_db;
  root["fft"]["min_freq"] = options.fft.min_freq;
  root["fft"]["note_key_mode"] = options.fft.note_key_mode;
  root["fft"]["rise_speed"] = options.fft.rise_speed;
  root["fft"]["rotation"] = static_cast<int>(options.fft.rotation);
  root["fft"]["size"] = options.fft.size;
  root["fft"]["slope_correction_db"] = options.fft.slope_correction_db;
  root["fft"]["stereo_mode"] = options.fft.stereo_mode;

  // Font
  root["font"] = options.font;

  // Lissajous
  root["lissajous"]["beam_multiplier"] = options.lissajous.beam_multiplier;
  root["lissajous"]["enable_splines"] = options.lissajous.enable_splines;
  root["lissajous"]["mode"] = options.lissajous.mode;
  root["lissajous"]["readback_multiplier"] = options.lissajous.readback_multiplier;
  root["lissajous"]["rotation"] = static_cast<int>(options.lissajous.rotation);

  // Lowpass
  root["lowpass"]["cutoff"] = options.lowpass.cutoff;
  root["lowpass"]["order"] = options.lowpass.order;

  // LUFS
  root["lufs"]["label"] = options.lufs.label;
  root["lufs"]["mode"] = options.lufs.mode;
  root["lufs"]["scale"] = options.lufs.scale;

  // Oscilloscope
  root["oscilloscope"]["alignment"] = options.oscilloscope.alignment;
  root["oscilloscope"]["alignment_type"] = options.oscilloscope.alignment_type;
  root["oscilloscope"]["beam_multiplier"] = options.oscilloscope.beam_multiplier;
  root["oscilloscope"]["cycles"] = options.oscilloscope.cycles;
  root["oscilloscope"]["enable_lowpass"] = options.oscilloscope.enable_lowpass;
  root["oscilloscope"]["flip_x"] = options.oscilloscope.flip_x;
  root["oscilloscope"]["follow_pitch"] = options.oscilloscope.follow_pitch;
  root["oscilloscope"]["limit_cycles"] = options.oscilloscope.limit_cycles;
  root["oscilloscope"]["min_cycle_time"] = options.oscilloscope.min_cycle_time;
  root["oscilloscope"]["rotation"] = static_cast<int>(options.oscilloscope.rotation);
  root["oscilloscope"]["time_window"] = options.oscilloscope.time_window;

  // Phosphor
  root["phosphor"]["age_threshold"] = options.phosphor.age_threshold;
  root["phosphor"]["beam_energy"] = options.phosphor.beam_energy;
  root["phosphor"]["chromatic_aberration_strength"] = options.phosphor.chromatic_aberration_strength;
  root["phosphor"]["colorbeam"] = options.phosphor.colorbeam;
  root["phosphor"]["decay_fast"] = options.phosphor.decay_fast;
  root["phosphor"]["decay_slow"] = options.phosphor.decay_slow;
  root["phosphor"]["enable_curved_screen"] = options.phosphor.enable_curved_screen;
  root["phosphor"]["enable_grain"] = options.phosphor.enable_grain;
  root["phosphor"]["enabled"] = options.phosphor.enabled;
  root["phosphor"]["far_blur_intensity"] = options.phosphor.far_blur_intensity;
  root["phosphor"]["grain_strength"] = options.phosphor.grain_strength;
  root["phosphor"]["line_blur_spread"] = options.phosphor.line_blur_spread;
  root["phosphor"]["line_width"] = options.phosphor.line_width;
  root["phosphor"]["near_blur_intensity"] = options.phosphor.near_blur_intensity;
  root["phosphor"]["range_factor"] = options.phosphor.range_factor;
  root["phosphor"]["screen_curvature"] = options.phosphor.screen_curvature;
  root["phosphor"]["screen_gap"] = options.phosphor.screen_gap;
  root["phosphor"]["tension"] = options.phosphor.tension;
  root["phosphor"]["vignette_strength"] = options.phosphor.vignette_strength;

  // Spectrogram
  root["spectrogram"]["frequency_scale"] = options.spectrogram.frequency_scale;
  root["spectrogram"]["interpolation"] = options.spectrogram.interpolation;
  root["spectrogram"]["max_db"] = options.spectrogram.max_db;
  root["spectrogram"]["max_freq"] = options.spectrogram.max_freq;
  root["spectrogram"]["min_db"] = options.spectrogram.min_db;
  root["spectrogram"]["min_freq"] = options.spectrogram.min_freq;
  root["spectrogram"]["time_window"] = options.spectrogram.time_window;

  // Visualizers
  {
    YAML::Node vis;
    for (const auto& [groupName, visualizerList] : options.visualizers) {
      YAML::Node arr(YAML::NodeType::Sequence);
      for (const auto& item : visualizerList)
        arr.push_back(item);
      vis[groupName] = arr;
    }
    root["visualizers"] = vis;
  }

  // VU
  root["vu"]["calibration_db"] = options.vu.calibration_db;
  root["vu"]["damping_ratio"] = options.vu.damping_ratio;
  root["vu"]["enable_momentum"] = options.vu.enable_momentum;
  root["vu"]["needle_width"] = options.vu.needle_width;
  root["vu"]["scale"] = options.vu.scale;
  root["vu"]["spring_constant"] = options.vu.spring_constant;
  root["vu"]["style"] = options.vu.style;
  root["vu"]["time_window"] = options.vu.time_window;

  // Window
  root["window"]["always_on_top"] = options.window.always_on_top;
  root["window"]["decorations"] = options.window.decorations;
  root["window"]["default_height"] = options.window.default_height;
  root["window"]["default_width"] = options.window.default_width;
  root["window"]["fps_limit"] = options.window.fps_limit;
  root["window"]["theme"] = options.window.theme;

  YAML::Emitter out;
  for (auto it = root.begin(); it != root.end(); ++it) {
    out << YAML::BeginMap;
    out << it->first << it->second;
    out << YAML::EndMap;
    out << YAML::Newline;
  }

  std::ofstream fout(path);
  if (!fout.is_open()) {
    LOG_ERROR("Failed to open config file for writing");
    return;
  }
  fout << out.c_str();
  fout.close();
}

bool reload() {
#ifdef __linux__
  // Check for file changes using inotify
  if (inotifyFd != -1 && inotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(inotifyFd, buf, sizeof(buf));
    if (len > 0) {
      for (int i = 0; i < len; i += sizeof(struct inotify_event)) {
        inotify_event* event = (inotify_event*)&buf[i];

        // check if the inotify got freed (file gets moved, deleted etc), kernel fires IN_IGNORED when that happens
        if ((event->mask & IN_IGNORED) == IN_IGNORED) {
          inotifyFd = -1;
          inotifyWatch = -1;
        }
      }

      load();
      return true;
    }
  }
#else
  // Check for file changes using stat (non-Linux systems)
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
#ifdef _WIN32
  struct _stat st;
  if (_stat(path.c_str(), &st) != 0) {
#else
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
#endif
    LOG_ERROR("Warning: could not stat config file.");
    return false;
  }
  static time_t lastConfigMTime = st.st_mtime;
  if (st.st_mtime != lastConfigMTime) {
    lastConfigMTime = st.st_mtime;
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