#pragma once

#include "audio_data.hpp"

#include <string>
#include <utility>
#include <vector>

// Abstract base class for all visualizers
class VisualizerBase {
public:
  VisualizerBase() = default;
  virtual ~VisualizerBase() = default;

  // Prevent copy, allow move
  VisualizerBase(const VisualizerBase&) = delete;
  VisualizerBase& operator=(const VisualizerBase&) = delete;
  VisualizerBase(VisualizerBase&&) = default;
  VisualizerBase& operator=(VisualizerBase&&) = default;

  virtual void draw(const AudioData& audioData, int sizeOrWidth) = 0;
  virtual int getRightEdge(const AudioData& audioData) const = 0;
  virtual int getPosition() const { return 0; }
  virtual void setPosition(int) {}
  virtual int getWidth() const { return 0; }
  virtual void setWidth(int) {}
};

// Lissajous visualizer with spline helpers
class LissajousVisualizer : public VisualizerBase {
public:
  void draw(const AudioData& audioData, int lissajousSize) override;
  int getRightEdge(const AudioData& audioData) const override;
  int getPosition() const override;
  void setPosition(int pos) override;
  int getWidth() const override;
  void setWidth(int width) override;

private:
  int position = 0;
  int width = 0;

  // Cached config values
  static int lissajous_points;
  static float phosphor_tension;
  static bool enable_splines;
  static int spline_segments;
  static bool enable_phosphor;
  static int phosphor_spline_density;
  static float phosphor_max_beam_speed;
  static float phosphor_persistence_time;
  static float phosphor_decay_rate;
  static float phosphor_intensity_scale;
  static size_t lastConfigVersion;

  // Cached theme colors
  static float cachedLissajousColor[4];
  static size_t lastThemeVersion;

  // Cached vectors
  static std::vector<std::pair<float, float>> points;
  static std::vector<std::pair<float, float>> densePath;
  static std::vector<float> originalDistances;

  // Helper methods
  void updateCachedValues();

  // Catmull-Rom spline interpolation
  std::vector<std::pair<float, float>>
  generateCatmullRomSpline(const std::vector<std::pair<float, float>>& controlPoints, int segmentsPerSegment = 10);
  // Calculate cumulative distance along a path of points
  std::vector<float> calculateCumulativeDistances(const std::vector<std::pair<float, float>>& points);
};

// Oscilloscope visualizer
class OscilloscopeVisualizer : public VisualizerBase {
public:
  void draw(const AudioData& audioData, int scopeWidth) override;
  int getRightEdge(const AudioData& audioData) const override;
  int getPosition() const override;
  void setPosition(int pos) override;
  int getWidth() const override;
  void setWidth(int width) override;

private:
  int position = 0;
  int width = 0;

  // Cached config values
  static float amplitude_scale;
  static std::string gradient_mode;
  static bool enable_phosphor;
  static int phosphor_spline_density;
  static float phosphor_max_beam_speed;
  static float phosphor_intensity_scale;
  static size_t lastConfigVersion;

  // Cached theme colors
  static float cachedOscilloscopeColor[4];
  static float cachedBackgroundColor[4];
  static size_t lastThemeVersion;

  // Helper methods
  void updateCachedValues();
};

// FFT visualizer
class FFTVisualizer : public VisualizerBase {
public:
  void draw(const AudioData& audioData, int fftWidth) override;
  int getRightEdge(const AudioData& audioData) const override;
  int getPosition() const override;
  void setPosition(int pos) override;
  int getWidth() const override;
  void setWidth(int width) override;

private:
  int position = 0;
  int width = 0;

  // Cached config values
  static std::string font;
  static float minFreq;
  static float maxFreq;
  static float sampleRate;
  static float slope_k;
  static float fft_display_min_db;
  static float fft_display_max_db;
  static std::string stereo_mode;
  static size_t lastConfigVersion;

  // Cached theme colors
  static float cachedSpectrumColor[4];
  static float cachedBackgroundColor[4];
  static float cachedGridColor[4];
  static float cachedTextColor[4];
  static size_t lastThemeVersion;

  // Cached calculations
  static float logMinFreq;
  static float logMaxFreq;
  static float logFreqRange;
  static float dbRange;

  // Helper methods
  void updateCachedValues();
};