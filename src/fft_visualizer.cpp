#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Initialize static members
std::string FFTVisualizer::font;
float FFTVisualizer::minFreq = 0.0f;
float FFTVisualizer::maxFreq = 0.0f;
float FFTVisualizer::sampleRate = 0.0f;
float FFTVisualizer::slope_k = 0.0f;
float FFTVisualizer::fft_display_min_db = 0.0f;
float FFTVisualizer::fft_display_max_db = 0.0f;
std::string FFTVisualizer::stereo_mode;
size_t FFTVisualizer::lastConfigVersion = 0;
float FFTVisualizer::cachedSpectrumColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float FFTVisualizer::cachedBackgroundColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float FFTVisualizer::cachedGridColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float FFTVisualizer::cachedTextColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
size_t FFTVisualizer::lastThemeVersion = 0;
float FFTVisualizer::logMinFreq = 0.0f;
float FFTVisualizer::logMaxFreq = 0.0f;
float FFTVisualizer::logFreqRange = 0.0f;
float FFTVisualizer::dbRange = 0.0f;

void FFTVisualizer::updateCachedValues() {
  bool configChanged = Config::getVersion() != lastConfigVersion;
  bool themeChanged = Theme::ThemeManager::getVersion() != lastThemeVersion;

  if (configChanged) {
    font = Config::getString("font.default_font");
    minFreq = Config::getFloat("fft.fft_min_freq");
    maxFreq = Config::getFloat("fft.fft_max_freq");
    sampleRate = Config::getFloat("audio.sample_rate");
    slope_k = Config::getFloat("fft.fft_slope_correction_db") / 20.0f / log10f(2.0f);
    fft_display_min_db = Config::getFloat("fft.fft_display_min_db");
    fft_display_max_db = Config::getFloat("fft.fft_display_max_db");
    stereo_mode = Config::getString("fft.stereo_mode");
    lastConfigVersion = Config::getVersion();

    // Pre-compute logarithmic values
    logMinFreq = log(minFreq);
    logMaxFreq = log(maxFreq);
    logFreqRange = logMaxFreq - logMinFreq;
    dbRange = fft_display_max_db - fft_display_min_db;
  }

  if (themeChanged) {
    const auto& spectrumColor = Theme::ThemeManager::getSpectrum();
    cachedSpectrumColor[0] = spectrumColor.r;
    cachedSpectrumColor[1] = spectrumColor.g;
    cachedSpectrumColor[2] = spectrumColor.b;
    cachedSpectrumColor[3] = spectrumColor.a;

    const auto& backgroundColor = Theme::ThemeManager::getBackground();
    cachedBackgroundColor[0] = backgroundColor.r;
    cachedBackgroundColor[1] = backgroundColor.g;
    cachedBackgroundColor[2] = backgroundColor.b;
    cachedBackgroundColor[3] = backgroundColor.a;

    const auto& gridColor = Theme::ThemeManager::getGrid();
    cachedGridColor[0] = gridColor.r;
    cachedGridColor[1] = gridColor.g;
    cachedGridColor[2] = gridColor.b;
    cachedGridColor[3] = gridColor.a;

    const auto& textColor = Theme::ThemeManager::getText();
    cachedTextColor[0] = textColor.r;
    cachedTextColor[1] = textColor.g;
    cachedTextColor[2] = textColor.b;
    cachedTextColor[3] = textColor.a;

    lastThemeVersion = Theme::ThemeManager::getVersion();
  }
}

void FFTVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the FFT visualizer
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // Update cached values if needed
  updateCachedValues();

  if (!audioData.fftMagnitudesMid.empty() && !audioData.fftMagnitudesSide.empty()) {
    const int fftSize = (audioData.fftMagnitudesMid.size() - 1) * 2;

    // Draw frequency scale lines using OpenGL
    auto drawFreqLine = [&](float freq) {
      if (freq < minFreq || freq > maxFreq)
        return;
      float logX = (log(freq) - logMinFreq) / logFreqRange;
      float x = logX * width;
      Graphics::drawAntialiasedLine(x, 0, x, audioData.windowHeight, cachedGridColor, 1.0f);
    };

    // Draw log-decade lines: 1,2,3,...9,10 per decade, from 100Hz up to maxFreq
    // Add 10-100Hz decade
    for (int mult = 1; mult < 10; ++mult) {
      float freq = 10.0f * mult;
      drawFreqLine(freq);
    }
    drawFreqLine(100.0f);
    for (int decade = 2; decade <= 5; ++decade) {
      float base = powf(10.0f, decade);
      for (int mult = 1; mult < 10; ++mult) {
        float freq = base * mult;
        drawFreqLine(freq);
      }
    }
    for (float freq = 10000.0f; freq <= maxFreq; freq *= 10.0f) {
      drawFreqLine(freq);
    }

    // Draw frequency labels for 100Hz, 1kHz, 10kHz
    auto drawFreqLabel = [&](float freq, const char* label) {
      float logX = (log(freq) - logMinFreq) / logFreqRange;
      float x = logX * width;
      float y = 8.0f;

      // Draw background rectangle
      float textWidth = 40.0f;
      float textHeight = 12.0f;
      float padding = 4.0f;

      Graphics::drawFilledRect(x - textWidth / 2 - padding, y - textHeight / 2 - padding, textWidth + padding * 2,
                               textHeight + padding * 2, cachedBackgroundColor);

      // Draw the text
      Graphics::drawText(label, x - textWidth / 2, y, 10.0f, cachedGridColor, font.c_str());
    };
    drawFreqLabel(100.0f, "100 Hz");
    drawFreqLabel(1000.0f, "1 kHz");
    drawFreqLabel(10000.0f, "10 kHz");

    // Determine which data to use based on FFT mode
    const std::vector<float>* mainMagnitudes = nullptr;
    const std::vector<float>* alternateMagnitudes = nullptr;

    // Temporary vectors for computed LEFTRIGHT mode data
    std::vector<float> leftMagnitudes, rightMagnitudes;

    // Pre-compute mode comparison for performance
    bool isLeftRightMode = (stereo_mode == "leftright");

    if (isLeftRightMode) {
      // LEFTRIGHT mode: Main = Mid - Side (Left), Alternate = Mid + Side (Right)
      leftMagnitudes.resize(audioData.smoothedMagnitudesMid.size());
      rightMagnitudes.resize(audioData.smoothedMagnitudesMid.size());

      for (size_t i = 0; i < audioData.smoothedMagnitudesMid.size(); ++i) {
        leftMagnitudes[i] = audioData.smoothedMagnitudesMid[i] - audioData.smoothedMagnitudesSide[i];
        rightMagnitudes[i] = audioData.smoothedMagnitudesMid[i] + audioData.smoothedMagnitudesSide[i];
      }

      mainMagnitudes = &leftMagnitudes;
      alternateMagnitudes = &rightMagnitudes;
    } else {
      // MIDSIDE mode: Main = Mid, Alternate = Side
      mainMagnitudes = &audioData.smoothedMagnitudesMid;
      alternateMagnitudes = &audioData.smoothedMagnitudesSide;
    }

    // Pre-compute constants for magnitude processing
    const float freqScale = sampleRate / fftSize;
    const float invLogFreqRange = 1.0f / logFreqRange;
    const float invDbRange = 1.0f / dbRange;
    const float gainBase = 1.0f / (440.0f * 2.0f); // Pre-compute for slope correction

    // Draw Alternate FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> alternateFFTPoints;
    alternateFFTPoints.reserve(alternateMagnitudes->size());

    for (size_t bin = 1; bin < alternateMagnitudes->size(); ++bin) {
      float freq = static_cast<float>(bin) * freqScale;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - logMinFreq) * invLogFreqRange;
      float x = logX * width;
      float magnitude = (*alternateMagnitudes)[bin];

      // Apply slope correction with pre-computed values
      float gain = powf(freq * gainBase, slope_k);
      magnitude *= gain;

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using cached display limits
      float y = (dB - fft_display_min_db) * invDbRange * audioData.windowHeight;
      alternateFFTPoints.push_back({x, y});
    }

    // Draw the Alternate FFT curve using cached colors
    constexpr float alternateFFTAlpha = 0.3f;
    float alternateFFTColorArray[4] = {
        cachedSpectrumColor[0] * alternateFFTAlpha + cachedBackgroundColor[0] * (1.0f - alternateFFTAlpha),
        cachedSpectrumColor[1] * alternateFFTAlpha + cachedBackgroundColor[1] * (1.0f - alternateFFTAlpha),
        cachedSpectrumColor[2] * alternateFFTAlpha + cachedBackgroundColor[2] * (1.0f - alternateFFTAlpha),
        cachedSpectrumColor[3] * alternateFFTAlpha + cachedBackgroundColor[3] * (1.0f - alternateFFTAlpha)};
    Graphics::drawAntialiasedLines(alternateFFTPoints, alternateFFTColorArray, 2.0f);

    // Draw Main FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> fftPoints;
    fftPoints.reserve(mainMagnitudes->size());

    for (size_t bin = 1; bin < mainMagnitudes->size(); ++bin) {
      float freq = static_cast<float>(bin) * freqScale;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - logMinFreq) * invLogFreqRange;
      float x = logX * width;
      float magnitude = (*mainMagnitudes)[bin];

      // Apply slope correction with pre-computed values
      float gain = powf(freq * gainBase, slope_k);
      magnitude *= gain;

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using cached display limits
      float y = (dB - fft_display_min_db) * invDbRange * audioData.windowHeight;
      fftPoints.push_back({x, y});
    }

    // Draw the Main FFT curve using cached colors
    Graphics::drawAntialiasedLines(fftPoints, cachedSpectrumColor, 2.0f);

    // Only show note text if we have a valid peak
    if (audioData.hasValidPeak) {
      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", audioData.peakDb,
               audioData.peakFreq, audioData.peakNote.c_str(), audioData.peakOctave, audioData.peakCents);
      float overlayX = 10.0f;
      float overlayY = audioData.windowHeight - 20.0f;

      // Draw overlay text using cached colors
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, cachedTextColor, font.c_str());
    }
  }
}

int FFTVisualizer::getPosition() const { return position; }
void FFTVisualizer::setPosition(int pos) { position = pos; }
int FFTVisualizer::getWidth() const { return width; }
void FFTVisualizer::setWidth(int w) { width = w; }
int FFTVisualizer::getRightEdge(const AudioData&) const { return position + width; }