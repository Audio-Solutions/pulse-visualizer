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

void FFTVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the FFT visualizer
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // --- Local static config cache ---
  static std::string font;
  static float minFreq;
  static float maxFreq;
  static float sampleRate;
  static float slope_k;
  static float fft_display_min_db;
  static float fft_display_max_db;
  static std::string stereo_mode;
  static size_t lastConfigVersion;

  if (Config::getVersion() != lastConfigVersion) {
    font = Config::getString("font.default_font");
    minFreq = Config::getFloat("fft.fft_min_freq");
    maxFreq = Config::getFloat("fft.fft_max_freq");
    sampleRate = Config::getFloat("audio.sample_rate");
    slope_k = Config::getFloat("fft.fft_slope_correction_db") / 20.0f / log10f(2.0f);
    fft_display_min_db = Config::getFloat("fft.fft_display_min_db");
    fft_display_max_db = Config::getFloat("fft.fft_display_max_db");
    stereo_mode = Config::getString("fft.stereo_mode");
    lastConfigVersion = Config::getVersion();
  }

  if (!audioData.fftMagnitudesMid.empty() && !audioData.fftMagnitudesSide.empty()) {
    const int fftSize = (audioData.fftMagnitudesMid.size() - 1) * 2;

    // Draw frequency scale lines using OpenGL
    auto drawFreqLine = [&](float freq) {
      if (freq < minFreq || freq > maxFreq)
        return;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      const auto& gridColor = Theme::ThemeManager::getGrid();
      float gridColorArray[4] = {gridColor.r, gridColor.g, gridColor.b, gridColor.a};
      Graphics::drawAntialiasedLine(x, 0, x, audioData.windowHeight, gridColorArray, 1.0f);
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
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float y = 8.0f;

      // Draw background rectangle
      float textWidth = 40.0f;  // Approximate width for the labels
      float textHeight = 12.0f; // Height of the text
      float padding = 4.0f;     // Padding around text

      const auto& bgColor = Theme::ThemeManager::getBackground();
      float bgColorArray[4] = {bgColor.r, bgColor.g, bgColor.b, bgColor.a};
      Graphics::drawFilledRect(x - textWidth / 2 - padding, y - textHeight / 2 - padding, textWidth + padding * 2,
                               textHeight + padding * 2, bgColorArray);

      // Draw the text
      const auto& gridColor = Theme::ThemeManager::getGrid();
      float gridColorArray[4] = {gridColor.r, gridColor.g, gridColor.b, gridColor.a};
      Graphics::drawText(label, x - textWidth / 2, y, 10.0f, gridColorArray, font.c_str());
    };
    drawFreqLabel(100.0f, "100 Hz");
    drawFreqLabel(1000.0f, "1 kHz");
    drawFreqLabel(10000.0f, "10 kHz");

    // Get the spectrum color for both FFT curves
    const auto& spectrumColor = Theme::ThemeManager::getSpectrum();

    // Determine which data to use based on FFT mode
    const std::vector<float>* mainMagnitudes = nullptr;
    const std::vector<float>* alternateMagnitudes = nullptr;

    // Temporary vectors for computed LEFTRIGHT mode data
    std::vector<float> leftMagnitudes, rightMagnitudes;

    if (stereo_mode == "leftright") {
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

    // Draw Alternate FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> alternateFFTPoints;
    alternateFFTPoints.reserve(alternateMagnitudes->size());

    for (size_t bin = 1; bin < alternateMagnitudes->size(); ++bin) {
      float freq = (float)bin * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float magnitude = (*alternateMagnitudes)[bin];

      // Apply slope correction
      float gain = powf(freq / 440.0f, slope_k); // Use A440 as 0dB reference
      magnitude *= gain / 2.0f;                  // Hann window

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using config display limits
      float y = (dB - fft_display_min_db) / (fft_display_max_db - fft_display_min_db) * audioData.windowHeight;
      alternateFFTPoints.push_back({x, y});
    }

    // Draw the Alternate FFT curve using OpenGL (darken the color towards the background color)
    constexpr float alternateFFTAlpha = 0.3f; // Maybe make this a config option?
    const auto& backgroundColor = Theme::ThemeManager::getBackground();
    float alternateFFTColorArray[4] = {
        spectrumColor.r * alternateFFTAlpha + backgroundColor.r * (1.0f - alternateFFTAlpha),
        spectrumColor.g * alternateFFTAlpha + backgroundColor.g * (1.0f - alternateFFTAlpha),
        spectrumColor.b * alternateFFTAlpha + backgroundColor.b * (1.0f - alternateFFTAlpha),
        spectrumColor.a * alternateFFTAlpha + backgroundColor.a * (1.0f - alternateFFTAlpha)};
    Graphics::drawAntialiasedLines(alternateFFTPoints, alternateFFTColorArray, 2.0f);

    // Draw Main FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> fftPoints;
    fftPoints.reserve(mainMagnitudes->size());

    for (size_t bin = 1; bin < mainMagnitudes->size(); ++bin) {
      float freq = (float)bin * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float magnitude = (*mainMagnitudes)[bin];

      // Apply slope correction
      float gain = powf(freq / 440.0f, slope_k); // Use A440 as 0dB reference
      magnitude *= gain / 2.0f;                  // Hann window

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using config display limits
      float y = (dB - fft_display_min_db) / (fft_display_max_db - fft_display_min_db) * audioData.windowHeight;
      fftPoints.push_back({x, y});
    }

    // Draw the Main FFT curve using OpenGL
    float spectrumColorArray[4] = {spectrumColor.r, spectrumColor.g, spectrumColor.b, spectrumColor.a};
    Graphics::drawAntialiasedLines(fftPoints, spectrumColorArray, 2.0f);

    // Only show note text if we have a valid peak
    if (audioData.hasValidPeak) {
      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", audioData.peakDb,
               audioData.peakFreq, audioData.peakNote.c_str(), audioData.peakOctave, audioData.peakCents);
      float overlayX = 10.0f; // Relative to FFT viewport
      float overlayY = audioData.windowHeight - 20.0f;

      // Now draw overlay text on top of everything
      const auto& textColor = Theme::ThemeManager::getText();
      float textColorArray[4] = {textColor.r, textColor.g, textColor.b, textColor.a};
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, textColorArray, font.c_str());
    }
  }
}

int FFTVisualizer::getPosition() const { return position; }
void FFTVisualizer::setPosition(int pos) { position = pos; }
int FFTVisualizer::getWidth() const { return width; }
void FFTVisualizer::setWidth(int w) { width = w; }
int FFTVisualizer::getRightEdge(const AudioData&) const { return position + width; }