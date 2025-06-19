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
ThemeDefinition ThemeManager::currentTheme;
std::unordered_map<std::string, ThemeDefinition> ThemeManager::availableThemes;
std::string ThemeManager::currentThemePath;
time_t ThemeManager::lastThemeMTime = 0;

void ThemeManager::initialize() {
  if (availableThemes.empty()) {
    // Load theme from config instead of loading all theme files
    loadThemeFromConfig();
  }
}

void ThemeManager::setTheme(const std::string& name) {
  auto it = availableThemes.find(name);
  if (it != availableThemes.end()) {
    currentTheme = it->second;
  }
}

const ThemeDefinition& ThemeManager::getCurrentTheme() { return currentTheme; }

void ThemeManager::registerTheme(const std::string& name, const ThemeDefinition& theme) {
  availableThemes[name] = theme;
}

bool ThemeManager::loadThemeFromConfig() {
  try {
    std::string themeName = Config::getString("window.default_theme");
    return loadThemeFromFile(themeName);
  } catch (...) {
    // Fallback to mocha theme if config fails
    return loadThemeFromFile("mocha.txt");
  }
}

bool ThemeManager::reloadIfChanged() {
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
      std::cerr << "Theme reloaded: " << themeName << std::endl;
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

  ThemeDefinition theme;
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

  return true;
}

bool ThemeManager::validatePrimaryColors(const ThemeDefinition& theme, const std::string& filename) {
  std::vector<std::string> missingColors;

  // Check if primary colors were parsed
  if (!theme.hasColor) {
    missingColors.push_back("color");
  }
  if (!theme.hasSelection) {
    missingColors.push_back("selection");
  }
  if (!theme.hasText) {
    missingColors.push_back("text");
  }
  if (!theme.hasAccent) {
    missingColors.push_back("accent");
  }
  if (!theme.hasBg) {
    missingColors.push_back("bg");
  }
  if (!theme.hasBgaccent) {
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

const ThemeDefinition* ThemeManager::getExtendedTheme(const std::string& name) {
  auto it = availableThemes.find(name);
  return it != availableThemes.end() ? &it->second : nullptr;
}

// Convenience methods to access current theme colors with proper mapping
const Color& ThemeManager::getBackground() { return currentTheme.bg; }
const Color& ThemeManager::getGrid() { return currentTheme.accent; }
const Color& ThemeManager::getVisualizer() { return currentTheme.color; }
const Color& ThemeManager::getSplitter() { return currentTheme.accent; }
const Color& ThemeManager::getText() { return currentTheme.text; }
const Color& ThemeManager::getAccent() { return currentTheme.accent; }
const Color& ThemeManager::getWarning() { return currentTheme.color; }
const Color& ThemeManager::getError() { return currentTheme.color; }
const Color& ThemeManager::getSuccess() { return currentTheme.color; }
const Color& ThemeManager::getLissajous() { return currentTheme.color; }
const Color& ThemeManager::getOscilloscope() { return currentTheme.oscilloscopeMain; }
const Color& ThemeManager::getWaveform() { return currentTheme.waveform; }
const Color& ThemeManager::getSpectrum() { return currentTheme.spectrumAnalyzerMain; }

void ThemeManager::applyFallbacks(ThemeDefinition& theme) {
  // Apply fallbacks for optional colors based on primary colors only if they weren't parsed
  if (!theme.hasWaveform) {
    theme.waveform = theme.color;
  }

  if (!theme.hasOscilloscopeMain) {
    theme.oscilloscopeMain = theme.color;
  }

  if (!theme.hasOscilloscopeBg) {
    theme.oscilloscopeBg = theme.bgaccent;
  }

  if (!theme.hasSpectrumAnalyzerMain) {
    theme.spectrumAnalyzerMain = theme.color;
  }

  if (!theme.hasSpectrumAnalyzerSecondary) {
    theme.spectrumAnalyzerSecondary = theme.accent;
  }

  if (!theme.hasSpectrumAnalyzerFrequencyLines) {
    theme.spectrumAnalyzerFrequencyLines = theme.accent;
  }

  if (!theme.hasSpectrumAnalyzerReferenceLine) {
    theme.spectrumAnalyzerReferenceLine = theme.accent;
  }

  if (!theme.hasSpectrumAnalyzerThresholdLine) {
    theme.spectrumAnalyzerThresholdLine = theme.color;
  }

  if (!theme.hasSpectrogramMain) {
    theme.spectrogramMain = theme.color;
  }

  if (!theme.hasColorBarsMain) {
    theme.colorBarsMain = theme.color;
  }

  if (!theme.hasErrorBar) {
    theme.errorBar = theme.color;
  }
}

void ThemeManager::parseThemeValue(const std::string& key, const std::string& value, ThemeDefinition& theme) {
  if (key == "credit") {
    // Remove quotes if present
    if (value.length() >= 2 && value[0] == '"' && value[value.length() - 1] == '"') {
      theme.credit = value.substr(1, value.length() - 2);
    } else {
      theme.credit = value;
    }
  } else if (key == "color") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.color = color.value();
      theme.hasColor = true;
    }
  } else if (key == "selection") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.selection = color.value();
      theme.hasSelection = true;
    }
  } else if (key == "text") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.text = color.value();
      theme.hasText = true;
    }
  } else if (key == "accent") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.accent = color.value();
      theme.hasAccent = true;
    }
  } else if (key == "bg") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.bg = color.value();
      theme.hasBg = true;
    }
  } else if (key == "bgaccent") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.bgaccent = color.value();
      theme.hasBgaccent = true;
    }
  } else if (key == "waveform") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.waveform = color.value();
      theme.hasWaveform = true;
    }
  } else if (key == "waveform_low") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.waveformLow = color.value();
      theme.hasWaveformLow = true;
    }
  } else if (key == "waveform_mid") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.waveformMid = color.value();
      theme.hasWaveformMid = true;
    }
  } else if (key == "waveform_high") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.waveformHigh = color.value();
      theme.hasWaveformHigh = true;
    }
  } else if (key == "oscilloscope_main") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.oscilloscopeMain = color.value();
      theme.hasOscilloscopeMain = true;
    }
  } else if (key == "oscilloscope_bg") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.oscilloscopeBg = color.value();
      theme.hasOscilloscopeBg = true;
    }
  } else if (key == "stereometer") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.stereometer = color.value();
      theme.hasStereometer = true;
    }
  } else if (key == "stereometer_low") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.stereometerLow = color.value();
      theme.hasStereometerLow = true;
    }
  } else if (key == "stereometer_mid") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.stereometerMid = color.value();
      theme.hasStereometerMid = true;
    }
  } else if (key == "stereometer_high") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.stereometerHigh = color.value();
      theme.hasStereometerHigh = true;
    }
  } else if (key == "spectrum_analyzer_main") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrumAnalyzerMain = color.value();
      theme.hasSpectrumAnalyzerMain = true;
    }
  } else if (key == "spectrum_analyzer_secondary") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrumAnalyzerSecondary = color.value();
      theme.hasSpectrumAnalyzerSecondary = true;
    }
  } else if (key == "spectrum_analyzer_frequency_lines") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrumAnalyzerFrequencyLines = color.value();
      theme.hasSpectrumAnalyzerFrequencyLines = true;
    }
  } else if (key == "spectrum_analyzer_reference_line") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrumAnalyzerReferenceLine = color.value();
      theme.hasSpectrumAnalyzerReferenceLine = true;
    }
  } else if (key == "spectrum_analyzer_threshold_line") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrumAnalyzerThresholdLine = color.value();
      theme.hasSpectrumAnalyzerThresholdLine = true;
    }
  } else if (key == "spectrogram_low") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrogramLow = color.value();
      theme.hasSpectrogramLow = true;
    }
  } else if (key == "spectrogram_high") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrogramHigh = color.value();
      theme.hasSpectrogramHigh = true;
    }
  } else if (key == "color_bars_low") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.colorBarsLow = color.value();
      theme.hasColorBarsLow = true;
    }
  } else if (key == "color_bars_high") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.colorBarsHigh = color.value();
      theme.hasColorBarsHigh = true;
    }
  } else if (key == "spectrogram_main") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.spectrogramMain = color.value();
      theme.hasSpectrogramMain = true;
    }
  } else if (key == "color_bars_main") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.colorBarsMain = color.value();
      theme.hasColorBarsMain = true;
    }
  } else if (key == "error_bar") {
    auto color = parseColor(value);
    if (color.has_value()) {
      theme.errorBar = color.value();
      theme.hasErrorBar = true;
    }
  } else if (key == "rgb_waveform_opacity_with_history") {
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
      theme.waveformInvertNoiseBrightness = std::stoi(value);
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