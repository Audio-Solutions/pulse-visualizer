/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace Spectrogram {

// Spectrogram window
WindowManager::VisualizerWindow* window;

/**
 * @brief Normalize decibel value to 0-1 range for spectrogram
 * @param db Decibel value to normalize
 * @return Normalized value between 0 and 1
 */
float normalize(float db) {
  float norm = (db - Config::options.spectrogram.limits.min_db) /
               (Config::options.spectrogram.limits.max_db - Config::options.spectrogram.limits.min_db);
  return std::clamp(norm, 0.f, 1.f);
}

/**
 * @brief Map spectrum data to visualization format
 * @param in Input spectrum data
 * @return Mapped spectrum data
 */
std::vector<float>& mapSpectrum(const std::vector<float>& in) {
  static std::vector<float> spectrum;
  spectrum.resize(SDLWindow::windowSizes[window->sdlWindow].second);

  // Calculate frequency mapping parameters
  float logMin = log10f(Config::options.fft.limits.min_freq);
  float logMax = log10f(Config::options.fft.limits.max_freq);
  float logRange = logMax - logMin;
  float freqRange = Config::options.fft.limits.max_freq - Config::options.fft.limits.min_freq;

  // Map each pixel row to a frequency bin
  for (size_t i = 0; i < spectrum.size(); i++) {
    float normalized = static_cast<float>(i) / static_cast<float>(spectrum.size() - 1);
    float logFreq = logMin + normalized * logRange;
    float linFreq = Config::options.fft.limits.min_freq + normalized * freqRange;
    float target = powf(10.f, Config::options.spectrogram.frequency_scale == "log" ? logFreq : linFreq);

    // Find corresponding frequency bins
    size_t bin1, bin2;
    if (Config::options.fft.cqt.enabled) {
      std::tie(bin1, bin2) = DSP::ConstantQ::find(target);
    } else {
      bin1 = target / (Config::options.audio.sample_rate * 0.5f / in.size());
      bin2 = bin1 + 1;
    }

    // Interpolate between bins if necessary
    if (bin1 < in.size()) {
      if (bin2 < in.size() && bin1 != bin2) {
        if (Config::options.fft.cqt.enabled) {
          float f1 = DSP::ConstantQ::frequencies[bin1];
          float f2 = DSP::ConstantQ::frequencies[bin2];
          float frac = (target - f1) / (f2 - f1);
          spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
        } else {
          float frac = static_cast<float>(target) / (Config::options.audio.sample_rate * 0.5f / in.size()) - bin1;
          spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
        }
      } else {
        spectrum[i] = in[bin1];
      }
    }
  }

  return spectrum;
}

void render() {
  // Select the window for rendering
  SDLWindow::selectWindow(window->sdlWindow);

  // Set viewport for rendering
  WindowManager::setViewport(window->x, window->width, SDLWindow::windowSizes[window->sdlWindow].second);

  static size_t current = 0;

  // Calculate time interval for new columns
  float interval = Config::options.spectrogram.window / static_cast<float>(window->width);
  static float accumulator = 0;
  accumulator += WindowManager::dt;

  // Add new column when interval is reached
  if (accumulator > interval) {
    accumulator -= interval;
    std::vector<float>& spectrum = mapSpectrum(DSP::fftMidRaw);

    // Prepare column data for rendering
    static std::vector<float> columnData;
    columnData.resize(SDLWindow::windowSizes[window->sdlWindow].second * 3);

    // Initialize column with background color
    for (size_t i = 0; i < SDLWindow::windowSizes[window->sdlWindow].second; ++i) {
      columnData[i * 3 + 0] = Theme::colors.background[0];
      columnData[i * 3 + 1] = Theme::colors.background[1];
      columnData[i * 3 + 2] = Theme::colors.background[2];
    }

    // Choose rendering color
    bool monochrome = true;
    float* color = Theme::colors.color;
    if (Theme::colors.spectrogram_main[3] > FLT_EPSILON) {
      color = Theme::colors.spectrogram_main;
    } else if (Theme::colors.spectrogram_low != 0 && Theme::colors.spectrogram_high != 0) {
      monochrome = false;
    }

    float lowRgba[4], highRgba[4];
    for (size_t i = 0; i < spectrum.size(); i++) {
      float mag = spectrum[i];

      if (mag > FLT_EPSILON) {
        float dB = 20.f * log10f(mag);
        float intens = normalize(dB);

        if (intens > FLT_EPSILON) {
          float intensColor[4];
          if (monochrome)
            Theme::mix(Theme::colors.background, color, intensColor, intens);
          else {
            float hsva[4] = {0.f, 1.f, 1.f, 1.f};
            if (intens < 0.5f) {
              hsva[0] = Theme::colors.spectrogram_low;
              float rgba[4];
              Graphics::hsvaToRgba(hsva, rgba);
              Theme::mix(Theme::colors.background, rgba, intensColor, intens * 2.f);
            } else {
              hsva[0] = lerp(Theme::colors.spectrogram_low, Theme::colors.spectrogram_high, (intens - 0.5f) * 2.f);
              Graphics::hsvaToRgba(hsva, intensColor);
            }
          }

          columnData[i * 3 + 0] = intensColor[0];
          columnData[i * 3 + 1] = intensColor[1];
          columnData[i * 3 + 2] = intensColor[2];
        }
      }
    }

    if (current > window->width)
      current = window->width - 1;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, current, 0, 1, SDLWindow::windowSizes[window->sdlWindow].second, GL_RGB, GL_FLOAT,
                    columnData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    current = (current + 1) % window->width;
  }

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  float colWidth = 1.f / window->width;
  float currentU = static_cast<float>(current) * colWidth;
  float part1 = (1.f - currentU) * window->width;
  float part2 = currentU * window->width;

  if (part1 > 0.f) {
    glBegin(GL_QUADS);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(part1, 0.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(part1, SDLWindow::windowSizes[window->sdlWindow].second);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(0.0f, SDLWindow::windowSizes[window->sdlWindow].second);
    glEnd();
  }

  if (part2 > 0.f) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(part1, 0.0f);
    glTexCoord2f(currentU, 0.0f);
    glVertex2f(window->width, 0.0f);
    glTexCoord2f(currentU, 1.0f);
    glVertex2f(window->width, SDLWindow::windowSizes[window->sdlWindow].second);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(part1, SDLWindow::windowSizes[window->sdlWindow].second);
    glEnd();
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

} // namespace Spectrogram