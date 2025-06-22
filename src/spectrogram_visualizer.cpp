#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <vector>

void SpectrogramVisualizer::initializeTexture(int targetWidth, int targetHeight) {
  if (spectrogramTexture == 0) {
    glGenTextures(1, &spectrogramTexture);
  }

  if (textureWidth != targetWidth || textureHeight != targetHeight) {
    // Save old texture data if it exists
    std::vector<float> oldTextureData;
    int oldWidth = textureWidth;
    int oldHeight = textureHeight;
    int oldColumn = currentColumn;

    if (textureWidth > 0 && textureHeight > 0) {
      oldTextureData.resize(textureWidth * textureHeight * 3);
      glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, oldTextureData.data());
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Update dimensions
    textureWidth = targetWidth;
    textureHeight = targetHeight;

    // Create new texture data filled with background color
    const auto& colors = Theme::ThemeManager::colors();
    std::vector<float> backgroundData(textureWidth * textureHeight * 3);
    for (size_t i = 0; i < backgroundData.size(); i += 3) {
      backgroundData[i] = colors.background[0];     // R
      backgroundData[i + 1] = colors.background[1]; // G
      backgroundData[i + 2] = colors.background[2]; // B
    }

    // Copy old data to new texture if it exists
    if (!oldTextureData.empty() && oldWidth > 0 && oldHeight > 0) {
      // Calculate how much data to preserve based on new dimensions
      int copyWidth = std::min(oldWidth, textureWidth);
      int copyHeight = std::min(oldHeight, textureHeight);

      // Copy row by row, preserving the circular buffer structure
      for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
          // Calculate source and destination indices
          int srcIdx = (y * oldWidth + x) * 3;
          int dstIdx = (y * textureWidth + x) * 3;

          // Copy RGB values
          backgroundData[dstIdx] = oldTextureData[srcIdx];
          backgroundData[dstIdx + 1] = oldTextureData[srcIdx + 1];
          backgroundData[dstIdx + 2] = oldTextureData[srcIdx + 2];
        }
      }

      // Adjust current column position for new width
      if (textureWidth != oldWidth) {
        currentColumn = std::min(oldColumn, textureWidth - 1);
      }
    } else {
      // First time initialization
      currentColumn = 0;
    }

    const auto& scfg = Config::values().spectrogram;
    glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_RGB, GL_FLOAT, backgroundData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, scfg.interpolation ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, scfg.interpolation ? GL_LINEAR : GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

float SpectrogramVisualizer::dbToColor(float db) const {
  const auto& scfg = Config::values().spectrogram;
  float normalized = (db - scfg.min_db) / (scfg.max_db - scfg.min_db);
  return std::max(0.0f, std::min(1.0f, normalized));
}

void SpectrogramVisualizer::rgbToHsv(float r, float g, float b, float& h, float& s, float& v) const {
  float max = std::max({r, g, b});
  float min = std::min({r, g, b});
  float delta = max - min;

  v = max;
  s = (max > 0.0f) ? (delta / max) : 0.0f;

  if (delta == 0.0f) {
    h = 0.0f; // Undefined, but set to 0
  } else if (max == r) {
    h = 60.0f * fmod(((g - b) / delta), 6.0f);
  } else if (max == g) {
    h = 60.0f * (((b - r) / delta) + 2.0f);
  } else { // max == b
    h = 60.0f * (((r - g) / delta) + 4.0f);
  }

  if (h < 0.0f) {
    h += 360.0f;
  }
}

void SpectrogramVisualizer::hsvToRgb(float h, float s, float v, float& r, float& g, float& b) const {
  float c = v * s;
  float x = c * (1.0f - std::abs(fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  if (h >= 0.0f && h < 60.0f) {
    r = c;
    g = x;
    b = 0.0f;
  } else if (h >= 60.0f && h < 120.0f) {
    r = x;
    g = c;
    b = 0.0f;
  } else if (h >= 120.0f && h < 180.0f) {
    r = 0.0f;
    g = c;
    b = x;
  } else if (h >= 180.0f && h < 240.0f) {
    r = 0.0f;
    g = x;
    b = c;
  } else if (h >= 240.0f && h < 300.0f) {
    r = x;
    g = 0.0f;
    b = c;
  } else {
    r = c;
    g = 0.0f;
    b = x;
  }

  r += m;
  g += m;
  b += m;
}

void SpectrogramVisualizer::mapFrequencyToSpectrum(const std::vector<float>& fftMagnitudes,
                                                   std::vector<float>& spectrum, float sampleRate) const {
  spectrum.resize(textureHeight);
  std::fill(spectrum.begin(), spectrum.end(), 0.0f);

  if (fftMagnitudes.empty())
    return;

  const auto& scfg = Config::values().spectrogram;
  float nyquist = sampleRate * 0.5f;
  float binWidth = nyquist / (fftMagnitudes.size() - 1);

  if (scfg.frequency_scale == "log") {
    // Logarithmic frequency mapping
    float logMinFreq = log10f(std::max(scfg.min_freq, 1.0f));
    float logMaxFreq = log10f(std::min(scfg.max_freq, nyquist));
    float logRange = logMaxFreq - logMinFreq;

    for (size_t i = 0; i < spectrum.size(); ++i) {
      // Map display bin to frequency
      float normalizedPos = static_cast<float>(i) / static_cast<float>(spectrum.size() - 1);
      float logFreq = logMinFreq + normalizedPos * logRange;
      float targetFreq = powf(10.0f, logFreq);

      // Find corresponding FFT bin
      float exactBin = targetFreq / binWidth;
      size_t bin1 = static_cast<size_t>(exactBin);
      size_t bin2 = bin1 + 1;

      if (bin1 < fftMagnitudes.size()) {
        if (bin2 < fftMagnitudes.size()) {
          // Interpolate between bins
          float fraction = exactBin - bin1;
          spectrum[i] = fftMagnitudes[bin1] * (1.0f - fraction) + fftMagnitudes[bin2] * fraction;
        } else {
          spectrum[i] = fftMagnitudes[bin1];
        }
      }
    }
  } else {
    // Linear frequency mapping
    float freqRange = std::min(scfg.max_freq, nyquist) - scfg.min_freq;

    for (size_t i = 0; i < spectrum.size(); ++i) {
      // Map display bin to frequency
      float normalizedPos = static_cast<float>(i) / static_cast<float>(spectrum.size() - 1);
      float targetFreq = scfg.min_freq + normalizedPos * freqRange;

      // Find corresponding FFT bin
      float exactBin = targetFreq / binWidth;
      size_t bin1 = static_cast<size_t>(exactBin);
      size_t bin2 = bin1 + 1;

      if (bin1 < fftMagnitudes.size()) {
        if (bin2 < fftMagnitudes.size()) {
          // Interpolate between bins
          float fraction = exactBin - bin1;
          spectrum[i] = fftMagnitudes[bin1] * (1.0f - fraction) + fftMagnitudes[bin2] * fraction;
        } else {
          spectrum[i] = fftMagnitudes[bin1];
        }
      }
    }
  }
}

void SpectrogramVisualizer::updateSpectrogramColumn(const AudioData& audioData) {
  if (audioData.fftMagnitudesMid.empty()) {
    return;
  }

  // Use the chosen FFT data with frequency mapping
  std::vector<float> spectrum;

  // First map frequencies according to the chosen scale (log or linear)
  // This handles the frequency spacing and interpolation
  mapFrequencyToSpectrum(audioData.fftMagnitudesMid, spectrum, audioData.sampleRate);

  // Prepare column data for texture update - start with background color
  const auto& colors = Theme::ThemeManager::colors();
  std::vector<float> columnData(textureHeight * 3);

  // Fill with background color first
  for (size_t i = 0; i < columnData.size(); i += 3) {
    columnData[i] = colors.background[0];     // R
    columnData[i + 1] = colors.background[1]; // G
    columnData[i + 2] = colors.background[2]; // B
  }

  // Then overlay spectrum data
  for (size_t i = 0; i < static_cast<size_t>(textureHeight) && i < spectrum.size(); ++i) {
    // Convert magnitude to dB
    float magnitude = spectrum[i];

    if (magnitude > 1e-9f) {
      float db = 20.0f * log10f(magnitude);
      // Convert dB to color intensity
      float intensity = dbToColor(db);

      if (intensity > 0.0f) {
        // Convert low and high colors to HSV
        float lowH, lowS, lowV;
        float highH, highS, highV;
        rgbToHsv(colors.spectrogramLow[0], colors.spectrogramLow[1], colors.spectrogramLow[2], lowH, lowS, lowV);
        rgbToHsv(colors.spectrogramHigh[0], colors.spectrogramHigh[1], colors.spectrogramHigh[2], highH, highS, highV);

        // Interpolate hue - force going through the warmer colors (reds/oranges)
        // For purple (~270°) to yellow (~60°), we want: purple → red → orange → yellow
        float interpH;
        if (lowH > highH) {
          // If low hue is greater than high hue, go through 360°/0° (through reds)
          float totalPath = (360.0f - lowH) + highH;
          float currentPath = totalPath * intensity;
          if (currentPath <= (360.0f - lowH)) {
            interpH = lowH + currentPath;
          } else {
            interpH = currentPath - (360.0f - lowH);
          }
        } else {
          // Standard interpolation
          interpH = lowH + (highH - lowH) * intensity;
        }

        // Normalize to [0, 360)
        while (interpH < 0.0f)
          interpH += 360.0f;
        while (interpH >= 360.0f)
          interpH -= 360.0f;

        // Interpolate saturation and value
        float interpS = lowS * (1.0f - intensity) + highS * intensity;
        float interpV = lowV * (1.0f - intensity) + highV * intensity;

        // Convert back to RGB
        float r, g, b;
        hsvToRgb(interpH, interpS, interpV, r, g, b);

        // Blend interpolated color over background
        size_t pixelOffset = i * 3;
        columnData[pixelOffset] = colors.background[0] * (1.0f - intensity) + r * intensity;
        columnData[pixelOffset + 1] = colors.background[1] * (1.0f - intensity) + g * intensity;
        columnData[pixelOffset + 2] = colors.background[2] * (1.0f - intensity) + b * intensity;
      }
    }
  }

  // Update texture using circular buffer approach
  glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, currentColumn, 0, 1, textureHeight, GL_RGB, GL_FLOAT, columnData.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  // Advance column
  currentColumn = (currentColumn + 1) % textureWidth;
}

void SpectrogramVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the spectrogram
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // Initialize texture if needed
  initializeTexture(width, audioData.windowHeight);

  // Update spectrogram data based on time window
  const auto& scfg = Config::values().spectrogram;
  static auto lastUpdate = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count();

  // Calculate update interval based on time window and texture width
  // time_window (seconds) should represent the total time span of the texture width
  float updateIntervalMs = (scfg.time_window * 1000.0f) / static_cast<float>(textureWidth);

  if (elapsed >= static_cast<long>(updateIntervalMs)) {
    updateSpectrogramColumn(audioData);
    lastUpdate = now;
  }

  // Draw spectrogram texture with scrolling effect
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  float drawHeight = static_cast<float>(audioData.windowHeight);

  // Calculate texture coordinates for scrolling effect
  float colWidth = 1.0f / textureWidth;
  float currentU = static_cast<float>(currentColumn) * colWidth;

  // Draw the texture in two parts to create seamless scrolling
  // Part 1: From current column to end of texture
  float part1Width = (1.0f - currentU) * width;
  if (part1Width > 0.0f) {
    glBegin(GL_QUADS);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(part1Width, 0.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(part1Width, drawHeight);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(0.0f, drawHeight);
    glEnd();
  }

  // Part 2: From start of texture to current column
  float part2Width = currentU * width;
  if (part2Width > 0.0f) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(part1Width, 0.0f);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(width, 0.0f);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(width, drawHeight);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(part1Width, drawHeight);
    glEnd();
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

int SpectrogramVisualizer::getPosition() const { return position; }
void SpectrogramVisualizer::setPosition(int pos) { position = pos; }
int SpectrogramVisualizer::getWidth() const { return width; }
void SpectrogramVisualizer::setWidth(int w) { width = w; }
int SpectrogramVisualizer::getRightEdge(const AudioData&) const { return position + width; }