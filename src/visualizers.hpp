#pragma once

#include "audio_data.hpp"

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
};