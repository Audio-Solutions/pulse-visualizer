#include "theme.hpp"

#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

namespace Theme {

// Helper function to expand user path (same as in config.cpp)
std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

// Define static members
ThemeColors ThemeManager::currentTheme;
std::unordered_map<std::string, ThemeColors> ThemeManager::availableThemes;
std::string ThemeManager::currentThemePath;
time_t ThemeManager::lastThemeMTime = 0;
size_t ThemeManager::lastConfigVersion = 0;
size_t ThemeManager::themeVersion = 0;

void ThemeManager::initialize() {
  if (availableThemes.empty()) {
    // Initialize with defaults and load theme from config
    currentTheme.initializeDefaults();
    bool success = loadThemeFromConfig();
    lastConfigVersion = Config::getVersion();
  }
}

void ThemeManager::setTheme(const std::string& name) {
  auto it = availableThemes.find(name);
  if (it != availableThemes.end()) {
    currentTheme = it->second;
    themeVersion++; // Increment version when theme changes
  }
}

size_t ThemeManager::getVersion() { return themeVersion; }

bool ThemeManager::loadThemeFromConfig() {
  try {
    std::string themeName = Config::values().window.theme;
    return loadThemeFromFile(themeName);
  } catch (...) {
    // Fallback to mocha theme if config fails
    return loadThemeFromFile("mocha.txt");
  }
}

bool ThemeManager::reloadIfChanged() {
  // First check if config has changed
  size_t currentConfigVersion = Config::getVersion();
  if (currentConfigVersion != lastConfigVersion) {
    // Config has changed, reload theme from config
    bool success = loadThemeFromConfig();
    if (success) {
      lastConfigVersion = currentConfigVersion;
      themeVersion++; // Increment version on successful reload
    }
    return success;
  }

  // Then check if theme file has changed
  if (currentThemePath.empty()) {
    return false;
  }

  struct stat st;
  if (stat(currentThemePath.c_str(), &st) != 0) {
    std::cerr << "Failed to stat theme file: " << currentThemePath << std::endl;
    return false;
  }

  if (st.st_mtime != lastThemeMTime) {
    // Theme file has changed, reload it
    std::string themeName = currentThemePath.substr(currentThemePath.find_last_of('/') + 1);
    if (themeName.find(".txt") != std::string::npos) {
      themeName = themeName.substr(0, themeName.find(".txt"));
    }
    bool success = loadThemeFromFile(themeName);
    if (success) {
      themeVersion++; // Increment version on successful reload
    }
    return success;
  }

  return false;
}

bool ThemeManager::loadThemeFromFile(const std::string& filename) {
  std::string filepath = "~/.config/pulse-visualizer/themes/" + filename;
  if (filename.find(".txt") == std::string::npos) {
    filepath += ".txt";
  }

  std::string expanded = expandUserPath(filepath);
  std::ifstream file(expanded);
  if (!file.is_open()) {
    std::cerr << "Failed to open theme file: " << expanded << std::endl;
    return false;
  }

  // Store the current theme path and modification time
  currentThemePath = expanded;
  struct stat st;
  if (stat(expanded.c_str(), &st) == 0) {
    lastThemeMTime = st.st_mtime;
  }

  ThemeColors theme;
  theme.initializeDefaults();
  std::string line;

  while (std::getline(file, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#' || line.substr(0, 2) == "##") {
      continue;
    }

    // Parse key-value pairs
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos)
      continue;

    std::string key = line.substr(0, colonPos);
    std::string value = line.substr(colonPos + 1);

    // Trim whitespace
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    parseThemeValue(key, value, theme);
  }

  // Apply fallbacks for missing optional colors
  applyFallbacks(theme);

  // Validate that all primary colors are present
  if (!validatePrimaryColors(theme, filename)) {
    return false;
  }

  // Register the theme
  std::string themeName = filename.substr(0, filename.find('.'));
  availableThemes[themeName] = theme;

  // Set as current theme
  currentTheme = theme;
  themeVersion++; // Increment version when theme is loaded

  return true;
}

bool ThemeManager::validatePrimaryColors(const ThemeColors& theme, const std::string& filename) {
  std::vector<std::string> missingColors;

  // Check if primary colors were set (not still at default values with alpha == 0.0f)
  // We check alpha channel instead of RGB since black is a valid color choice
  if (theme.visualizer[3] == 0.0f) {
    missingColors.push_back("color");
  }
  if (theme.selection[3] == 0.0f) {
    missingColors.push_back("selection");
  }
  if (theme.text[3] == 0.0f) {
    missingColors.push_back("text");
  }
  if (theme.accent[3] == 0.0f) {
    missingColors.push_back("accent");
  }
  if (theme.background[3] == 0.0f) {
    missingColors.push_back("bg");
  }
  if (theme.bgaccent[3] == 0.0f) {
    missingColors.push_back("bgaccent");
  }

  if (!missingColors.empty()) {
    std::cerr << "Theme file '" << filename << "' is missing required primary colors:" << std::endl;
    for (const auto& color : missingColors) {
      std::cerr << "  - " << color << std::endl;
    }
    std::cerr << "Primary colors (color, selection, text, accent, bg, bgaccent) are required." << std::endl;
    return false;
  }

  return true;
}

// Convenience methods to access current theme colors (returning Color objects for compatibility)
Color ThemeManager::getBackground() {
  return Color(currentTheme.background[0], currentTheme.background[1], currentTheme.background[2],
               currentTheme.background[3]);
}
Color ThemeManager::getGrid() {
  return Color(currentTheme.grid[0], currentTheme.grid[1], currentTheme.grid[2], currentTheme.grid[3]);
}
Color ThemeManager::getVisualizer() {
  return Color(currentTheme.visualizer[0], currentTheme.visualizer[1], currentTheme.visualizer[2],
               currentTheme.visualizer[3]);
}
Color ThemeManager::getSplitter() {
  return Color(currentTheme.splitter[0], currentTheme.splitter[1], currentTheme.splitter[2], currentTheme.splitter[3]);
}
Color ThemeManager::getText() {
  return Color(currentTheme.text[0], currentTheme.text[1], currentTheme.text[2], currentTheme.text[3]);
}
Color ThemeManager::getAccent() {
  return Color(currentTheme.accent[0], currentTheme.accent[1], currentTheme.accent[2], currentTheme.accent[3]);
}
Color ThemeManager::getWarning() {
  return Color(currentTheme.visualizer[0], currentTheme.visualizer[1], currentTheme.visualizer[2],
               currentTheme.visualizer[3]);
}
Color ThemeManager::getError() {
  return Color(currentTheme.visualizer[0], currentTheme.visualizer[1], currentTheme.visualizer[2],
               currentTheme.visualizer[3]);
}
Color ThemeManager::getSuccess() {
  return Color(currentTheme.visualizer[0], currentTheme.visualizer[1], currentTheme.visualizer[2],
               currentTheme.visualizer[3]);
}
Color ThemeManager::getLissajous() {
  return Color(currentTheme.lissajous[0], currentTheme.lissajous[1], currentTheme.lissajous[2],
               currentTheme.lissajous[3]);
}
Color ThemeManager::getOscilloscope() {
  return Color(currentTheme.oscilloscope[0], currentTheme.oscilloscope[1], currentTheme.oscilloscope[2],
               currentTheme.oscilloscope[3]);
}
Color ThemeManager::getWaveform() {
  return Color(currentTheme.waveform[0], currentTheme.waveform[1], currentTheme.waveform[2], currentTheme.waveform[3]);
}
Color ThemeManager::getSpectrum() {
  return Color(currentTheme.spectrum[0], currentTheme.spectrum[1], currentTheme.spectrum[2], currentTheme.spectrum[3]);
}
Color ThemeManager::getSpectrogramLow() {
  return Color(currentTheme.spectrogramLow[0], currentTheme.spectrogramLow[1], currentTheme.spectrogramLow[2],
               currentTheme.spectrogramLow[3]);
}
Color ThemeManager::getSpectrogramHigh() {
  return Color(currentTheme.spectrogramHigh[0], currentTheme.spectrogramHigh[1], currentTheme.spectrogramHigh[2],
               currentTheme.spectrogramHigh[3]);
}
bool ThemeManager::invertNoiseBrightness() { return currentTheme.invertNoiseBrightness; }

const ThemeColors& ThemeManager::colors() { return currentTheme; }

void ThemeManager::setColorFromArray(float dest[4], const Color& src) {
  dest[0] = src.r;
  dest[1] = src.g;
  dest[2] = src.b;
  dest[3] = src.a;
}

void ThemeManager::applyFallbacks(ThemeColors& theme) {
  // Apply fallbacks: copy from primary visualizer color to oscilloscope/lissajous
  // These were set during parsing, so if they're still (0,0,0,1), apply fallbacks

  // Check if oscilloscope color is still default black, use visualizer color
  if (theme.oscilloscope[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.oscilloscope[i] = theme.visualizer[i];
    }
  }

  // Check if lissajous color is still default black, use visualizer color
  if (theme.lissajous[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.lissajous[i] = theme.visualizer[i];
    }
  }

  // Apply other fallbacks as needed
  if (theme.waveform[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.waveform[i] = theme.visualizer[i];
    }
  }

  if (theme.spectrum[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.spectrum[i] = theme.visualizer[i];
    }
  }

  if (theme.spectrogramLow[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.spectrogramLow[i] = theme.background[i];
    }
  }

  if (theme.spectrogramHigh[3] == 0.0f) {
    for (int i = 0; i < 4; ++i) {
      theme.spectrogramHigh[i] = theme.visualizer[i];
    }
  }
}

void ThemeManager::parseThemeValue(const std::string& key, const std::string& value, ThemeColors& theme) {
  // Parse a color and store it in the destination array
  auto setColorIfValid = [&](float dest[4]) {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.setColor(dest, color.value());
    }
  };

  // Primary colors
  if (key == "color") {
    setColorIfValid(theme.visualizer);
  } else if (key == "selection") {
    setColorIfValid(theme.selection);
  } else if (key == "text") {
    setColorIfValid(theme.text);
  } else if (key == "accent") {
    setColorIfValid(theme.accent);
    // Also set grid and splitter to accent
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.setColor(theme.grid, color.value());
      theme.setColor(theme.splitter, color.value());
    }
  } else if (key == "bg") {
    setColorIfValid(theme.background);
  } else if (key == "bgaccent") {
    setColorIfValid(theme.bgaccent);
  }

  // Visualizer-specific colors
  else if (key == "oscilloscope_main") {
    setColorIfValid(theme.oscilloscope);
  } else if (key == "oscilloscope_bg") {
    setColorIfValid(theme.oscilloscopeBg);
  } else if (key == "waveform") {
    setColorIfValid(theme.waveform);
  } else if (key == "waveform_low") {
    setColorIfValid(theme.waveformLow);
  } else if (key == "waveform_mid") {
    setColorIfValid(theme.waveformMid);
  } else if (key == "waveform_high") {
    setColorIfValid(theme.waveformHigh);
  } else if (key == "spectrum_analyzer_main") {
    setColorIfValid(theme.spectrum);
  } else if (key == "spectrum_analyzer_secondary") {
    setColorIfValid(theme.spectrumAnalyzerSecondary);
  } else if (key == "spectrum_analyzer_frequency_lines") {
    setColorIfValid(theme.spectrumAnalyzerFrequencyLines);
  } else if (key == "spectrum_analyzer_reference_line") {
    setColorIfValid(theme.spectrumAnalyzerReferenceLine);
  } else if (key == "spectrum_analyzer_threshold_line") {
    setColorIfValid(theme.spectrumAnalyzerThresholdLine);
  } else if (key == "spectrogram_low") {
    setColorIfValid(theme.spectrogramLow);
  } else if (key == "spectrogram_high") {
    setColorIfValid(theme.spectrogramHigh);
  } else if (key == "spectrogram_main") {
    setColorIfValid(theme.spectrogramMain);
  } else if (key == "lissajous") {
    setColorIfValid(theme.lissajous);
  }

  // Extended colors
  else if (key == "stereometer") {
    setColorIfValid(theme.stereometer);
  } else if (key == "stereometer_low") {
    setColorIfValid(theme.stereometerLow);
  } else if (key == "stereometer_mid") {
    setColorIfValid(theme.stereometerMid);
  } else if (key == "stereometer_high") {
    setColorIfValid(theme.stereometerHigh);
  } else if (key == "color_bars_low") {
    setColorIfValid(theme.colorBarsLow);
  } else if (key == "color_bars_high") {
    setColorIfValid(theme.colorBarsHigh);
  } else if (key == "color_bars_main") {
    setColorIfValid(theme.colorBarsMain);
  } else if (key == "error_bar") {
    setColorIfValid(theme.errorBar);
  }

  // Properties
  else if (key == "rgb_waveform_opacity_with_history") {
    try {
      theme.rgbWaveformOpacityWithHistory = std::stoi(value);
    } catch (...) {
    }
  } else if (key == "waveform_hue_low") {
    try {
      theme.waveformHueLow = std::stoi(value);
    } catch (...) {
    }
  } else if (key == "waveform_hue_high") {
    try {
      theme.waveformHueHigh = std::stoi(value);
    } catch (...) {
    }
  } else if (key == "waveform_invert_noise_brightness") {
    try {
      theme.invertNoiseBrightness = (std::stoi(value) == 1);
    } catch (...) {
    }
  } else if (key == "color_bars_opacity") {
    try {
      theme.colorBarsOpacity = std::stoi(value);
    } catch (...) {
    }
  }
}

std::optional<Color> ThemeManager::parseColor(const std::string& value) {
  std::istringstream iss(value);
  std::string token;
  std::vector<int> components;

  while (std::getline(iss, token, ',')) {
    try {
      components.push_back(std::stoi(token));
    } catch (...) {
      return {};
    }
  }

  if (components.size() >= 3) {
    int a = components.size() >= 4 ? components[3] : 255;
    return Color::fromRGB255(components[0], components[1], components[2], a);
  }

  return {};
}

} // namespace Theme