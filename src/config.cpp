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
 * @brief Read a value from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, const std::string& path, T& out) {
  auto node = getNode(root, path);
  if (node.has_value()) {
    try {
      T value = node.value().as<T>();
      out = value;
      return;
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
      return;
    }
  }

  broken = true;
  LOG_ERROR(path + " is missing.");
}

/**
 * @brief Read a boolean from the configuration with permissive conversions.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <> void get<bool>(const YAML::Node& root, const std::string& path, bool& out) {
  auto node = getNode(root, path);
  if (!node.has_value()) {
    broken = true;
    return;
  }

  const YAML::Node& value = node.value();

  try {
    out = value.as<bool>();
    return;
  } catch (...) {
    try {
      std::string str = value.as<std::string>();
      if (str == "true" || str == "yes" || str == "on") {
        out = true;
        return;
      }
      if (str == "false" || str == "no" || str == "off") {
        out = false;
        return;
      }
      LOG_ERROR(std::string("Converting ") + path + " to bool failed");
      broken = true;
      return;
    } catch (...) {
      try {
        out = (value.as<int>() != 0);
        return;
      } catch (...) {
        LOG_ERROR(std::string("Converting  ") + path + " to bool failed");
        broken = true;
        return;
      }
    }
  }
}

/**
 * @brief Read a vector of strings from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed sequence
 */
template <>
void get<std::vector<std::string>>(const YAML::Node& root, const std::string& path, std::vector<std::string>& out) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsSequence()) {
    broken = true;
    return;
  }

  std::vector<std::string> result;
  for (const auto& item : node.value()) {
    result.push_back(item.as<std::string>());
  }
  out = std::move(result);
}

/**
 * @brief Read a map<string, vector<string>> from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed mapping
 */
template <>
void get<std::map<std::string, std::vector<std::string>>>(const YAML::Node& root, const std::string& path,
                                                          std::map<std::string, std::vector<std::string>>& out) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsMap()) {
    broken = true;
    return;
  }

  std::map<std::string, std::vector<std::string>> result;
  for (const auto& item : node.value()) {
    std::vector<std::string> arr;
    get<std::vector<std::string>>(root, path + "." + item.first.as<std::string>(), arr);
    result[item.first.as<std::string>()] = std::move(arr);
  }
  out = std::move(result);
}

/**
 * @brief Read a Rotation (backed by int) from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed enum value
 */
template <> void get<Rotation>(const YAML::Node& root, const std::string& path, Rotation& out) {
  int tmp = static_cast<int>(out);
  get<int>(root, path, tmp);
  out = static_cast<Rotation>(tmp);
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
  get<std::map<std::string, std::vector<std::string>>>(configData, "visualizers", options.visualizers);

  // Load Oscilloscope configuration
  get<float>(configData, "oscilloscope.beam_multiplier", options.oscilloscope.beam_multiplier);
  get<bool>(configData, "oscilloscope.flip_x", options.oscilloscope.flip_x);
  get<Rotation>(configData, "oscilloscope.rotation", options.oscilloscope.rotation);
  get<float>(configData, "oscilloscope.window", options.oscilloscope.window);

  // Oscilloscope Pitch
  get<bool>(configData, "oscilloscope.pitch.follow", options.oscilloscope.pitch.follow);
  get<std::string>(configData, "oscilloscope.pitch.type", options.oscilloscope.pitch.type);
  get<std::string>(configData, "oscilloscope.pitch.alignment", options.oscilloscope.pitch.alignment);
  get<int>(configData, "oscilloscope.pitch.cycles", options.oscilloscope.pitch.cycles);
  get<float>(configData, "oscilloscope.pitch.min_cycle_time", options.oscilloscope.pitch.min_cycle_time);

  // Oscilloscope Lowpass
  get<bool>(configData, "oscilloscope.lowpass.enabled", options.oscilloscope.lowpass.enabled);
  get<float>(configData, "oscilloscope.lowpass.cutoff", options.oscilloscope.lowpass.cutoff);
  get<int>(configData, "oscilloscope.lowpass.order", options.oscilloscope.lowpass.order);

  // Oscilloscope Bandpass
  get<float>(configData, "oscilloscope.bandpass.bandwidth", options.oscilloscope.bandpass.bandwidth);
  get<float>(configData, "oscilloscope.bandpass.sidelobe", options.oscilloscope.bandpass.sidelobe);

  // Load Lissajous configuration
  get<float>(configData, "lissajous.beam_multiplier", options.lissajous.beam_multiplier);
  get<float>(configData, "lissajous.readback_multiplier", options.lissajous.readback_multiplier);
  get<std::string>(configData, "lissajous.mode", options.lissajous.mode);
  get<Rotation>(configData, "lissajous.rotation", options.lissajous.rotation);

  // Load FFT configuration
  get<float>(configData, "fft.beam_multiplier", options.fft.beam_multiplier);
  get<Rotation>(configData, "fft.rotation", options.fft.rotation);
  get<bool>(configData, "fft.flip_x", options.fft.flip_x);
  get<bool>(configData, "fft.markers", options.fft.markers);
  get<int>(configData, "fft.size", options.fft.size);
  get<float>(configData, "fft.slope", options.fft.slope);
  get<std::string>(configData, "fft.key", options.fft.key);
  get<std::string>(configData, "fft.mode", options.fft.mode);

  // FFT Limits
  get<float>(configData, "fft.limits.max_db", options.fft.limits.max_db);
  get<float>(configData, "fft.limits.max_freq", options.fft.limits.max_freq);
  get<float>(configData, "fft.limits.min_db", options.fft.limits.min_db);
  get<float>(configData, "fft.limits.min_freq", options.fft.limits.min_freq);

  // FFT Smoothing
  get<bool>(configData, "fft.smoothing.enabled", options.fft.smoothing.enabled);
  get<float>(configData, "fft.smoothing.fall_speed", options.fft.smoothing.fall_speed);
  get<float>(configData, "fft.smoothing.hover_fall_speed", options.fft.smoothing.hover_fall_speed);
  get<float>(configData, "fft.smoothing.rise_speed", options.fft.smoothing.rise_speed);

  // FFT CQT
  get<int>(configData, "fft.cqt.bins_per_octave", options.fft.cqt.bins_per_octave);
  get<bool>(configData, "fft.cqt.enabled", options.fft.cqt.enabled);

  // FFT Sphere
  get<bool>(configData, "fft.sphere.enabled", options.fft.sphere.enabled);
  get<float>(configData, "fft.sphere.max_freq", options.fft.sphere.max_freq);
  get<float>(configData, "fft.sphere.base_radius", options.fft.sphere.base_radius);

  // Load Spectrogram configuration
  get<float>(configData, "spectrogram.window", options.spectrogram.window);
  get<bool>(configData, "spectrogram.interpolation", options.spectrogram.interpolation);
  get<std::string>(configData, "spectrogram.frequency_scale", options.spectrogram.frequency_scale);
  get<float>(configData, "spectrogram.limits.max_db", options.spectrogram.limits.max_db);
  get<float>(configData, "spectrogram.limits.max_freq", options.spectrogram.limits.max_freq);
  get<float>(configData, "spectrogram.limits.min_db", options.spectrogram.limits.min_db);
  get<float>(configData, "spectrogram.limits.min_freq", options.spectrogram.limits.min_freq);

  // Load Audio configuration
  get<float>(configData, "audio.silence_threshold", options.audio.silence_threshold);
  get<float>(configData, "audio.sample_rate", options.audio.sample_rate);
  get<float>(configData, "audio.gain_db", options.audio.gain_db);
  get<std::string>(configData, "audio.engine", options.audio.engine);
  get<std::string>(configData, "audio.device", options.audio.device);

  // Load window configuration
  get<int>(configData, "window.default_width", options.window.default_width);
  get<int>(configData, "window.default_height", options.window.default_height);
  get<std::string>(configData, "window.theme", options.window.theme);
  get<int>(configData, "window.fps_limit", options.window.fps_limit);
  get<bool>(configData, "window.decorations", options.window.decorations);
  get<bool>(configData, "window.always_on_top", options.window.always_on_top);

  // Load Debug configuration
  get<bool>(configData, "debug.log_fps", options.debug.log_fps);
  get<bool>(configData, "debug.show_bandpassed", options.debug.show_bandpassed);

  // Load Phosphor effect configuration
  get<bool>(configData, "phosphor.enabled", options.phosphor.enabled);

  // Phosphor Beam
  get<float>(configData, "phosphor.beam.energy", options.phosphor.beam.energy);
  get<bool>(configData, "phosphor.beam.rainbow", options.phosphor.beam.rainbow);
  get<float>(configData, "phosphor.beam.width", options.phosphor.beam.width);
  get<float>(configData, "phosphor.beam.tension", options.phosphor.beam.tension);

  // Phosphor Blur
  get<float>(configData, "phosphor.blur.spread", options.phosphor.blur.spread);
  get<float>(configData, "phosphor.blur.range", options.phosphor.blur.range);
  get<float>(configData, "phosphor.blur.near_intensity", options.phosphor.blur.near_intensity);
  get<float>(configData, "phosphor.blur.far_intensity", options.phosphor.blur.far_intensity);

  // Phosphor Decay
  get<float>(configData, "phosphor.decay.fast", options.phosphor.decay.fast);
  get<float>(configData, "phosphor.decay.slow", options.phosphor.decay.slow);
  get<int>(configData, "phosphor.decay.threshold", options.phosphor.decay.threshold);

  // Phosphor Screen
  get<float>(configData, "phosphor.screen.curvature", options.phosphor.screen.curvature);
  get<float>(configData, "phosphor.screen.gap", options.phosphor.screen.gap);
  get<float>(configData, "phosphor.screen.vignette", options.phosphor.screen.vignette);
  get<float>(configData, "phosphor.screen.chromatic_aberration", options.phosphor.screen.chromatic_aberration);
  get<float>(configData, "phosphor.screen.grain", options.phosphor.screen.grain);

  // Load LUFS configuration
  get<std::string>(configData, "lufs.mode", options.lufs.mode);
  get<std::string>(configData, "lufs.scale", options.lufs.scale);
  get<std::string>(configData, "lufs.label", options.lufs.label);

  // Load VU configuration
  get<float>(configData, "vu.window", options.vu.window);
  get<std::string>(configData, "vu.style", options.vu.style);
  get<float>(configData, "vu.calibration_db", options.vu.calibration_db);
  get<std::string>(configData, "vu.scale", options.vu.scale);
  get<bool>(configData, "vu.momentum.enabled", options.vu.momentum.enabled);
  get<float>(configData, "vu.momentum.spring_constant", options.vu.momentum.spring_constant);
  get<float>(configData, "vu.momentum.damping_ratio", options.vu.momentum.damping_ratio);
  get<float>(configData, "vu.needle_width", options.vu.needle_width);

  // Load font configuration
  get<std::string>(configData, "font", options.font);

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

  // Debug
  root["debug"]["log_fps"] = options.debug.log_fps;
  root["debug"]["show_bandpassed"] = options.debug.show_bandpassed;

  // FFT
  root["fft"]["beam_multiplier"] = options.fft.beam_multiplier;
  root["fft"]["rotation"] = static_cast<int>(options.fft.rotation);
  root["fft"]["flip_x"] = options.fft.flip_x;
  root["fft"]["markers"] = options.fft.markers;
  root["fft"]["size"] = options.fft.size;
  root["fft"]["slope"] = options.fft.slope;
  root["fft"]["key"] = options.fft.key;
  root["fft"]["mode"] = options.fft.mode;

  // FFT Limits
  root["fft"]["limits"]["max_db"] = options.fft.limits.max_db;
  root["fft"]["limits"]["max_freq"] = options.fft.limits.max_freq;
  root["fft"]["limits"]["min_db"] = options.fft.limits.min_db;
  root["fft"]["limits"]["min_freq"] = options.fft.limits.min_freq;

  // FFT Smoothing
  root["fft"]["smoothing"]["enabled"] = options.fft.smoothing.enabled;
  root["fft"]["smoothing"]["fall_speed"] = options.fft.smoothing.fall_speed;
  root["fft"]["smoothing"]["hover_fall_speed"] = options.fft.smoothing.hover_fall_speed;
  root["fft"]["smoothing"]["rise_speed"] = options.fft.smoothing.rise_speed;

  // FFT CQT
  root["fft"]["cqt"]["bins_per_octave"] = options.fft.cqt.bins_per_octave;
  root["fft"]["cqt"]["enabled"] = options.fft.cqt.enabled;

  // FFT Sphere
  root["fft"]["sphere"]["enabled"] = options.fft.sphere.enabled;
  root["fft"]["sphere"]["max_freq"] = options.fft.sphere.max_freq;
  root["fft"]["sphere"]["base_radius"] = options.fft.sphere.base_radius;

  // Font
  root["font"] = options.font;

  // Lissajous
  root["lissajous"]["beam_multiplier"] = options.lissajous.beam_multiplier;
  root["lissajous"]["mode"] = options.lissajous.mode;
  root["lissajous"]["readback_multiplier"] = options.lissajous.readback_multiplier;
  root["lissajous"]["rotation"] = static_cast<int>(options.lissajous.rotation);

  // LUFS
  root["lufs"]["label"] = options.lufs.label;
  root["lufs"]["mode"] = options.lufs.mode;
  root["lufs"]["scale"] = options.lufs.scale;

  // Oscilloscope
  root["oscilloscope"]["beam_multiplier"] = options.oscilloscope.beam_multiplier;
  root["oscilloscope"]["flip_x"] = options.oscilloscope.flip_x;
  root["oscilloscope"]["rotation"] = static_cast<int>(options.oscilloscope.rotation);
  root["oscilloscope"]["window"] = options.oscilloscope.window;

  // Oscilloscope Pitch
  root["oscilloscope"]["pitch"]["follow"] = options.oscilloscope.pitch.follow;
  root["oscilloscope"]["pitch"]["type"] = options.oscilloscope.pitch.type;
  root["oscilloscope"]["pitch"]["alignment"] = options.oscilloscope.pitch.alignment;
  root["oscilloscope"]["pitch"]["cycles"] = options.oscilloscope.pitch.cycles;
  root["oscilloscope"]["pitch"]["min_cycle_time"] = options.oscilloscope.pitch.min_cycle_time;

  // Oscilloscope Lowpass
  root["oscilloscope"]["lowpass"]["enabled"] = options.oscilloscope.lowpass.enabled;
  root["oscilloscope"]["lowpass"]["cutoff"] = options.oscilloscope.lowpass.cutoff;
  root["oscilloscope"]["lowpass"]["order"] = options.oscilloscope.lowpass.order;

  // Oscilloscope Bandpass
  root["oscilloscope"]["bandpass"]["bandwidth"] = options.oscilloscope.bandpass.bandwidth;
  root["oscilloscope"]["bandpass"]["sidelobe"] = options.oscilloscope.bandpass.sidelobe;

  // Phosphor
  root["phosphor"]["enabled"] = options.phosphor.enabled;

  // Phosphor Beam
  root["phosphor"]["beam"]["energy"] = options.phosphor.beam.energy;
  root["phosphor"]["beam"]["rainbow"] = options.phosphor.beam.rainbow;
  root["phosphor"]["beam"]["width"] = options.phosphor.beam.width;
  root["phosphor"]["beam"]["tension"] = options.phosphor.beam.tension;

  // Phosphor Blur
  root["phosphor"]["blur"]["spread"] = options.phosphor.blur.spread;
  root["phosphor"]["blur"]["range"] = options.phosphor.blur.range;
  root["phosphor"]["blur"]["near_intensity"] = options.phosphor.blur.near_intensity;
  root["phosphor"]["blur"]["far_intensity"] = options.phosphor.blur.far_intensity;

  // Phosphor Decay
  root["phosphor"]["decay"]["fast"] = options.phosphor.decay.fast;
  root["phosphor"]["decay"]["slow"] = options.phosphor.decay.slow;
  root["phosphor"]["decay"]["threshold"] = options.phosphor.decay.threshold;

  // Phosphor Screen
  root["phosphor"]["screen"]["curvature"] = options.phosphor.screen.curvature;
  root["phosphor"]["screen"]["gap"] = options.phosphor.screen.gap;
  root["phosphor"]["screen"]["vignette"] = options.phosphor.screen.vignette;
  root["phosphor"]["screen"]["chromatic_aberration"] = options.phosphor.screen.chromatic_aberration;
  root["phosphor"]["screen"]["grain"] = options.phosphor.screen.grain;

  // Spectrogram
  root["spectrogram"]["frequency_scale"] = options.spectrogram.frequency_scale;
  root["spectrogram"]["interpolation"] = options.spectrogram.interpolation;
  root["spectrogram"]["window"] = options.spectrogram.window;
  root["spectrogram"]["limits"]["max_db"] = options.spectrogram.limits.max_db;
  root["spectrogram"]["limits"]["max_freq"] = options.spectrogram.limits.max_freq;
  root["spectrogram"]["limits"]["min_db"] = options.spectrogram.limits.min_db;
  root["spectrogram"]["limits"]["min_freq"] = options.spectrogram.limits.min_freq;

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
  root["vu"]["scale"] = options.vu.scale;
  root["vu"]["style"] = options.vu.style;
  root["vu"]["window"] = options.vu.window;
  root["vu"]["momentum"]["enabled"] = options.vu.momentum.enabled;
  root["vu"]["momentum"]["damping_ratio"] = options.vu.momentum.damping_ratio;
  root["vu"]["momentum"]["spring_constant"] = options.vu.momentum.spring_constant;
  root["vu"]["needle_width"] = options.vu.needle_width;

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

template void get<int>(const YAML::Node&, const std::string&, int&);
template void get<float>(const YAML::Node&, const std::string&, float&);
template void get<std::string>(const YAML::Node&, const std::string&, std::string&);

} // namespace Config