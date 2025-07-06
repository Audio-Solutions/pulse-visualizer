#pragma once

#include "audio_data.hpp"

#include <GL/glew.h>
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

  // Aspect ratio constraint methods
  void setAspectRatioConstraint(float ratio, bool enforce = true) {
    aspectRatio = ratio;
    enforceAspectRatio = enforce;
  }

  float getAspectRatio() const { return aspectRatio; }
  bool shouldEnforceAspectRatio() const { return enforceAspectRatio; }

  // Calculate required width based on height and aspect ratio
  int getRequiredWidth(int height) const {
    if (!enforceAspectRatio)
      return 0;
    return static_cast<int>(height * aspectRatio);
  }

protected:
  float aspectRatio = 1.0f;        // Default 1:1 aspect ratio
  bool enforceAspectRatio = false; // Default: no enforcement
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
  void invalidatePhosphorContext();

private:
  int position = 0;
  int width = 0;

  // Cached vectors
  static std::vector<std::pair<float, float>> points;
  static std::vector<std::pair<float, float>> densePath;
  static std::vector<float> originalDistances;
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
  void invalidatePhosphorContext();

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
  void invalidatePhosphorContext();

  // Mouse tracking methods
  void updateMousePosition(float mouseX, float mouseY);
  void setHovering(bool hovering);
  bool isHovering() const;

private:
  int position = 0;
  int width = 0;

  // Mouse tracking
  float mouseX = 0.0f;
  float mouseY = 0.0f;
  bool hovering = false;

  // Cached config values
  static const char** noteNames;
  static size_t lastConfigVersion;

  // Cached calculations
  static float logMinFreq;
  static float logMaxFreq;
  static float logFreqRange;
  static float dbRange;

  // Helper methods
  void updateCaches();
  void calculateFrequencyAndDB(float x, float y, float windowHeight, float& frequency, float& actualDB) const;
  void freqToNote(float freq, std::string& noteName, int& octave, int& cents) const;

  // Cached vectors for FFT points
  static std::vector<std::pair<float, float>> fftPoints;
  static std::vector<std::pair<float, float>> alternateFFTPoints;
};

// Spectrogram visualizer (normal + high quality reassigned)
class SpectrogramVisualizer : public VisualizerBase {
public:
  SpectrogramVisualizer() = default;
  ~SpectrogramVisualizer() = default;

  void draw(const AudioData& audioData, int ignored) override;
  int getRightEdge(const AudioData& audioData) const override;
  int getPosition() const override;
  void setPosition(int pos) override;
  int getWidth() const override;
  void setWidth(int w) override;

private:
  int position = 0;
  int width = 0;

  // OpenGL texture
  GLuint spectrogramTexture = 0;
  int textureWidth = 0;
  int textureHeight = 0;
  int currentColumn = 0;

  // Internal helpers (no cached values needed - using centralized config and theme access)
  void initializeTexture(int targetWidth, int targetHeight);
  void updateSpectrogramColumn(const AudioData& audioData);
  void mapFrequencyToSpectrum(const std::vector<float>& fftMagnitudes, std::vector<float>& spectrum,
                              float sampleRate) const;
  void mapCQTToSpectrum(const std::vector<float>& cqtMagnitudes, const std::vector<float>& cqtFrequencies,
                        std::vector<float>& spectrum) const;

  // Color conversion helpers
  float dbToColor(float db) const;
  void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) const;
  void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) const;
};