#pragma once
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace Theme {
// Color structure to hold RGBA values
struct Color {
  float r, g, b, a;

  // Default constructor
  constexpr Color() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}

  constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

  // Convert to array for OpenGL functions
  constexpr std::array<float, 4> toArray() const { return {r, g, b, a}; }

  // Convert from 0-255 range to 0-1 range
  static Color fromRGB255(int r, int g, int b, int a = 255) {
    return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
  }
};

// Unified theme colors structure
struct ThemeColors {
  // Primary colors (OpenGL-ready RGBA float[4])
  float background[4]; // bg
  float grid[4];       // accent (used for grid lines)
  float visualizer[4]; // color (primary visualizer color)
  float splitter[4];   // accent (used for splitters)
  float text[4];       // text
  float accent[4];     // accent

  // Visualizer-specific colors
  float lissajous[4];       // color (or oscilloscope_main if available)
  float oscilloscope[4];    // oscilloscope_main (or color fallback)
  float waveform[4];        // waveform (or color fallback)
  float spectrum[4];        // spectrum_analyzer_main (or color fallback)
  float spectrogramLow[4];  // spectrogram_low (or bg fallback)
  float spectrogramHigh[4]; // spectrogram_high (or color fallback)

  // Extended visualizer colors (maintaining compatibility)
  float selection[4];                      // selection
  float bgaccent[4];                       // bgaccent
  float waveformLow[4];                    // waveform_low
  float waveformMid[4];                    // waveform_mid
  float waveformHigh[4];                   // waveform_high
  float oscilloscopeBg[4];                 // oscilloscope_bg
  float stereometer[4];                    // stereometer
  float stereometerLow[4];                 // stereometer_low
  float stereometerMid[4];                 // stereometer_mid
  float stereometerHigh[4];                // stereometer_high
  float spectrumAnalyzerSecondary[4];      // spectrum_analyzer_secondary
  float spectrumAnalyzerFrequencyLines[4]; // spectrum_analyzer_frequency_lines
  float spectrumAnalyzerReferenceLine[4];  // spectrum_analyzer_reference_line
  float spectrumAnalyzerThresholdLine[4];  // spectrum_analyzer_threshold_line
  float spectrogramMain[4];                // spectrogram_main
  float colorBarsLow[4];                   // color_bars_low
  float colorBarsHigh[4];                  // color_bars_high
  float colorBarsMain[4];                  // color_bars_main
  float errorBar[4];                       // error_bar

  // Additional properties
  int rgbWaveformOpacityWithHistory;
  int waveformHueLow;
  int waveformHueHigh;
  bool invertNoiseBrightness;
  int colorBarsOpacity;

  // Initialize with default colors
  void initializeDefaults() {
    // Initialize all colors to black/transparent
    for (int i = 0; i < 4; ++i) {
      background[i] = visualizer[i] = grid[i] = splitter[i] = text[i] = accent[i] = 0.0f;
      lissajous[i] = oscilloscope[i] = waveform[i] = spectrum[i] = 0.0f;
      spectrogramLow[i] = spectrogramHigh[i] = selection[i] = bgaccent[i] = 0.0f;
      waveformLow[i] = waveformMid[i] = waveformHigh[i] = oscilloscopeBg[i] = 0.0f;
      stereometer[i] = stereometerLow[i] = stereometerMid[i] = stereometerHigh[i] = 0.0f;
      spectrumAnalyzerSecondary[i] = spectrumAnalyzerFrequencyLines[i] = 0.0f;
      spectrumAnalyzerReferenceLine[i] = spectrumAnalyzerThresholdLine[i] = 0.0f;
      spectrogramMain[i] = colorBarsLow[i] = colorBarsHigh[i] = colorBarsMain[i] = errorBar[i] = 0.0f;
    }

    // Set alpha to 1.0 for all colors
    background[3] = visualizer[3] = grid[3] = splitter[3] = text[3] = accent[3] = 1.0f;
    lissajous[3] = oscilloscope[3] = waveform[3] = spectrum[3] = 1.0f;
    spectrogramLow[3] = spectrogramHigh[3] = selection[3] = bgaccent[3] = 1.0f;
    waveformLow[3] = waveformMid[3] = waveformHigh[3] = oscilloscopeBg[3] = 1.0f;
    stereometer[3] = stereometerLow[3] = stereometerMid[3] = stereometerHigh[3] = 1.0f;
    spectrumAnalyzerSecondary[3] = spectrumAnalyzerFrequencyLines[3] = 1.0f;
    spectrumAnalyzerReferenceLine[3] = spectrumAnalyzerThresholdLine[3] = 1.0f;
    spectrogramMain[3] = colorBarsLow[3] = colorBarsHigh[3] = colorBarsMain[3] = errorBar[3] = 1.0f;

    // Initialize properties
    rgbWaveformOpacityWithHistory = 170;
    waveformHueLow = 180;
    waveformHueHigh = 540;
    invertNoiseBrightness = false;
    colorBarsOpacity = 255;
  }

  // Helper to set color from Color object
  void setColor(float dest[4], const Color& src) {
    dest[0] = src.r;
    dest[1] = src.g;
    dest[2] = src.b;
    dest[3] = src.a;
  }
};

// Theme manager class to handle theme switching and access
class ThemeManager {
public:
  static void initialize();
  static void setTheme(const std::string& name);
  static bool loadThemeFromFile(const std::string& filename);

  // Load theme from config
  static bool loadThemeFromConfig();

  // Theme file change detection
  static bool reloadIfChanged();

  // Version tracking
  static size_t getVersion();

  // Convenience methods to access current theme colors (returning Color objects for compatibility)
  static Color getBackground();
  static Color getGrid();
  static Color getVisualizer();
  static Color getSplitter();
  static Color getText();
  static Color getAccent();
  static Color getWarning();
  static Color getError();
  static Color getSuccess();
  static Color getLissajous();
  static Color getOscilloscope();
  static Color getWaveform();
  static Color getSpectrum();
  static Color getSpectrogramLow();
  static Color getSpectrogramHigh();
  static bool invertNoiseBrightness();

  // Access central cached colors (primary interface)
  static const ThemeColors& colors();

private:
  static void parseThemeValue(const std::string& key, const std::string& value, ThemeColors& theme);
  static void applyFallbacks(ThemeColors& theme);
  static std::optional<Color> parseColor(const std::string& value);
  static void setColorFromArray(float dest[4], const Color& src);
  static bool validatePrimaryColors(const ThemeColors& theme, const std::string& filename);

  static ThemeColors currentTheme;
  static std::unordered_map<std::string, ThemeColors> availableThemes;
  static std::string currentThemePath;
  static time_t lastThemeMTime;
  static size_t lastConfigVersion;
  static size_t themeVersion;
};
} // namespace Theme