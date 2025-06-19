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

// Comprehensive theme structure with all possible colors
struct ThemeDefinition {
  // Primary colors (required)
  Color color;
  Color selection;
  Color text;
  Color accent;
  Color bg;
  Color bgaccent;

  // Optional colors with fallbacks to primaries
  Color waveform;
  Color waveformLow;
  Color waveformMid;
  Color waveformHigh;
  Color oscilloscopeMain;
  Color oscilloscopeBg;
  Color stereometer;
  Color stereometerLow;
  Color stereometerMid;
  Color stereometerHigh;
  Color spectrumAnalyzerMain;
  Color spectrumAnalyzerSecondary;
  Color spectrumAnalyzerFrequencyLines;
  Color spectrumAnalyzerReferenceLine;
  Color spectrumAnalyzerThresholdLine;
  Color spectrogramLow;
  Color spectrogramHigh;
  Color colorBarsLow;
  Color colorBarsHigh;
  Color spectrogramMain;
  Color colorBarsMain;
  Color errorBar;

  // Additional properties
  int rgbWaveformOpacityWithHistory;
  int waveformHueLow;
  int waveformHueHigh;
  int waveformInvertNoiseBrightness;
  int colorBarsOpacity;
  std::string credit;

  // Flags to track which optional colors were parsed
  bool hasWaveform;
  bool hasWaveformLow;
  bool hasWaveformMid;
  bool hasWaveformHigh;
  bool hasOscilloscopeMain;
  bool hasOscilloscopeBg;
  bool hasStereometer;
  bool hasStereometerLow;
  bool hasStereometerMid;
  bool hasStereometerHigh;
  bool hasSpectrumAnalyzerMain;
  bool hasSpectrumAnalyzerSecondary;
  bool hasSpectrumAnalyzerFrequencyLines;
  bool hasSpectrumAnalyzerReferenceLine;
  bool hasSpectrumAnalyzerThresholdLine;
  bool hasSpectrogramLow;
  bool hasSpectrogramHigh;
  bool hasColorBarsLow;
  bool hasColorBarsHigh;
  bool hasSpectrogramMain;
  bool hasColorBarsMain;
  bool hasErrorBar;

  // Flags to track which primary colors were parsed
  bool hasColor;
  bool hasSelection;
  bool hasText;
  bool hasAccent;
  bool hasBg;
  bool hasBgaccent;

  // Default constructor
  ThemeDefinition()
      : rgbWaveformOpacityWithHistory(170), waveformHueLow(180), waveformHueHigh(540), waveformInvertNoiseBrightness(0),
        colorBarsOpacity(255), hasWaveform(false), hasWaveformLow(false), hasWaveformMid(false), hasWaveformHigh(false),
        hasOscilloscopeMain(false), hasOscilloscopeBg(false), hasStereometer(false), hasStereometerLow(false),
        hasStereometerMid(false), hasStereometerHigh(false), hasSpectrumAnalyzerMain(false),
        hasSpectrumAnalyzerSecondary(false), hasSpectrumAnalyzerFrequencyLines(false),
        hasSpectrumAnalyzerReferenceLine(false), hasSpectrumAnalyzerThresholdLine(false), hasSpectrogramLow(false),
        hasSpectrogramHigh(false), hasColorBarsLow(false), hasColorBarsHigh(false), hasSpectrogramMain(false),
        hasColorBarsMain(false), hasErrorBar(false), hasColor(false), hasSelection(false), hasText(false),
        hasAccent(false), hasBg(false), hasBgaccent(false) {}
};

// Theme manager class to handle theme switching and access
class ThemeManager {
public:
  static void initialize();
  static void setTheme(const std::string& name);
  static const ThemeDefinition& getCurrentTheme();
  static void registerTheme(const std::string& name, const ThemeDefinition& theme);
  static bool loadThemeFromFile(const std::string& filename);
  static const ThemeDefinition* getExtendedTheme(const std::string& name);

  // Load theme from config
  static bool loadThemeFromConfig();

  // Theme file change detection
  static bool reloadIfChanged();

  // Convenience methods to access current theme colors
  static const Color& getBackground();
  static const Color& getGrid();
  static const Color& getVisualizer();
  static const Color& getSplitter();
  static const Color& getText();
  static const Color& getAccent();
  static const Color& getWarning();
  static const Color& getError();
  static const Color& getSuccess();
  static const Color& getLissajous();
  static const Color& getOscilloscope();
  static const Color& getWaveform();
  static const Color& getSpectrum();

private:
  static void parseThemeValue(const std::string& key, const std::string& value, ThemeDefinition& theme);
  static void applyFallbacks(ThemeDefinition& theme);
  static std::optional<Color> parseColor(const std::string& value);
  static bool validatePrimaryColors(const ThemeDefinition& theme, const std::string& filename);

  static ThemeDefinition currentTheme;
  static std::unordered_map<std::string, ThemeDefinition> availableThemes;
  static std::string currentThemePath;
  static time_t lastThemeMTime;
  static size_t lastConfigVersion;
};
} // namespace Theme