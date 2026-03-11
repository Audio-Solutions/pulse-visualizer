/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
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
#include "include/window_manager.hpp"

#include <algorithm>
#include <cmath>

namespace Spectrogram {

class SpectrogramVisualizer : public WindowManager::VisualizerWindow {
public:
  SpectrogramVisualizer() {
    id = "spectrogram";
    displayName = "Spectrogram";
  }

  std::vector<float>& mapSpectrum(const std::vector<float>& in, const std::vector<float>& phase, float frameDt);
  void render() override;
};

std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer() {
  return std::make_shared<SpectrogramVisualizer>();
}

/**
 * @brief Map spectrum data to visualization format
 * @param in Input spectrum data
 * @return Mapped spectrum data
 */
std::vector<float>& SpectrogramVisualizer::mapSpectrum(const std::vector<float>& in, const std::vector<float>& phase,
                                                       float frameDt) {
  static std::vector<float> spectrum;
  spectrum.assign(bounds.h, 0.0f);

  const bool useLogScale = Config::options.spectrogram.frequency_scale == "log";
  const bool useCqt = Config::options.fft.cqt.enabled;
  const float sampleRate = Config::options.audio.sample_rate;
  const float safeInputSize = std::max(static_cast<float>(in.size()), 1.0f);
  const float halfBinHz = sampleRate * 0.5f / safeInputSize;
  const float fullBinHz = sampleRate / safeInputSize;

  // Calculate frequency mapping parameters
  float logMin = log10f(Config::options.fft.limits.min_freq);
  float logMax = log10f(Config::options.fft.limits.max_freq);
  float logRange = logMax - logMin;
  float freqRange = Config::options.fft.limits.max_freq - Config::options.fft.limits.min_freq;

  if (!Config::options.spectrogram.iterative_reassignment) {
    // Standard spectrogram mapping with interpolation
    for (size_t i = 0; i < spectrum.size(); ++i) {
      float normalized = static_cast<float>(i) / static_cast<float>(spectrum.size() - 1);
      float logFreq = logMin + normalized * logRange;
      float linFreq = Config::options.fft.limits.min_freq + normalized * freqRange;
      float target = useLogScale ? powf(10.f, logFreq) : linFreq;

      size_t bin1, bin2;
      if (useCqt) {
        std::tie(bin1, bin2) = DSP::ConstantQ::find(target);
      } else {
        bin1 = target / halfBinHz;
        bin2 = bin1 + 1;
      }

      if (bin1 < in.size()) {
        if (bin2 < in.size() && bin1 != bin2) {
          if (useCqt) {
            float f1 = DSP::ConstantQ::frequencies[bin1];
            float f2 = DSP::ConstantQ::frequencies[bin2];
            float frac = (target - f1) / std::max(f2 - f1, FLT_EPSILON);
            spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
          } else {
            float frac = target / halfBinHz - static_cast<float>(bin1);
            spectrum[i] = in[bin1] * (1.f - frac) + in[bin2] * frac;
          }
        } else {
          spectrum[i] = in[bin1];
        }
      }
    }

    return spectrum;
  }

  // Iterative reassignment mode: hard-assign significant local peaks to corrected frequency rows.
  static std::vector<float> lastPhase;
  static bool haveLastPhase = false;
  if (lastPhase.size() != phase.size()) {
    lastPhase = phase;
    haveLastPhase = false;
  }

  const size_t sourceBins = std::min(in.size(), phase.size());
  const float safeDt = std::max(frameDt, 1e-4f);
  const float ampThreshold = powf(10.0f, Config::options.spectrogram.limits.min_db / 20.0f);

  auto clampFreq = [&](float f) {
    return std::clamp(f, Config::options.fft.limits.min_freq, Config::options.fft.limits.max_freq);
  };

  auto toRow = [&](float freq) {
    float normalized = 0.0f;
    if (useLogScale) {
      normalized = (log10f(clampFreq(freq)) - logMin) / std::max(logRange, FLT_EPSILON);
    } else {
      normalized = (clampFreq(freq) - Config::options.fft.limits.min_freq) / std::max(freqRange, FLT_EPSILON);
    }
    float pos = normalized * static_cast<float>(spectrum.size() - 1);
    return static_cast<size_t>(std::clamp(std::lround(pos), 0l, static_cast<long>(spectrum.size() - 1)));
  };

  for (size_t k = 0; k < sourceBins; ++k) {
    float mag = in[k];
    if (mag <= ampThreshold)
      continue;

    if (k > 0 && k + 1 < sourceBins && (in[k] < in[k - 1] || in[k] < in[k + 1]))
      continue;

    float nominalFreq = 0.0f;
    if (useCqt) {
      if (k >= DSP::ConstantQ::frequencies.size())
        continue;
      nominalFreq = DSP::ConstantQ::frequencies[k];
    } else {
      nominalFreq = static_cast<float>(k) * fullBinHz;
    }

    if (nominalFreq < Config::options.fft.limits.min_freq || nominalFreq > Config::options.fft.limits.max_freq)
      continue;

    float reassignedFreq = nominalFreq;
    if (haveLastPhase) {
      float expected = static_cast<float>(2.0 * M_PI) * nominalFreq * safeDt;
      float delta = phase[k] - lastPhase[k] - expected;
      while (delta > static_cast<float>(M_PI))
        delta -= static_cast<float>(2.0 * M_PI);
      while (delta < static_cast<float>(-M_PI))
        delta += static_cast<float>(2.0 * M_PI);

      float correctionHz = delta / (static_cast<float>(2.0 * M_PI) * safeDt);
      float bandwidthHz;
      if (useCqt && k > 0 && k + 1 < DSP::ConstantQ::frequencies.size()) {
        bandwidthHz = 0.5f * ((DSP::ConstantQ::frequencies[k] - DSP::ConstantQ::frequencies[k - 1]) +
                              (DSP::ConstantQ::frequencies[k + 1] - DSP::ConstantQ::frequencies[k]));
      } else {
        bandwidthHz = fullBinHz;
      }

      float maxCorrection = std::max(1.5f * bandwidthHz, 1.0f);
      correctionHz = std::clamp(correctionHz, -maxCorrection, maxCorrection);
      reassignedFreq = nominalFreq + correctionHz;
      reassignedFreq = clampFreq(reassignedFreq);
    }

    size_t center;
    if (useCqt) {
      auto [b1, b2] = DSP::ConstantQ::find(reassignedFreq);
      center = std::min(b1, sourceBins - 1);
      if (b2 < sourceBins && in[b2] > in[center])
        center = b2;
    } else {
      float bin = reassignedFreq / fullBinHz;
      center = static_cast<size_t>(std::clamp(static_cast<int>(std::lround(bin)), 0, static_cast<int>(sourceBins - 1)));
    }

    size_t l = center > 0 ? center - 1 : center;
    size_t r = std::min(center + 1, sourceBins - 1);
    float wl = in[l];
    float wc = in[center];
    float wr = in[r];
    float sum = wl + wc + wr;
    if (sum > FLT_EPSILON) {
      auto binToFreq = [&](size_t idx) {
        if (useCqt)
          return DSP::ConstantQ::frequencies[idx];
        return static_cast<float>(idx) * fullBinHz;
      };

      float centroidFreq = (binToFreq(l) * wl + binToFreq(center) * wc + binToFreq(r) * wr) / sum;
      reassignedFreq = clampFreq(0.5f * reassignedFreq + 0.5f * centroidFreq);
    }

    size_t row = toRow(reassignedFreq);
    spectrum[row] = std::max(spectrum[row], mag);

    if (row > 0)
      spectrum[row - 1] = std::max(spectrum[row - 1], mag * 0.2f);
    if (row + 1 < spectrum.size())
      spectrum[row + 1] = std::max(spectrum[row + 1], mag * 0.2f);
  }

  lastPhase = phase;
  haveLastPhase = true;

  return spectrum;
}

void SpectrogramVisualizer::render() {
  auto* window = this;

  static size_t current = 0;

  // Ensure current is within bounds when texture dimensions change
  if (current >= bounds.w)
    current = 0;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);
  glColor4f(1.f, 1.f, 1.f, 1.f);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  // Pace column emission so full texture width represents spectrogram.window seconds.
  const size_t textureWidth = bounds.w;
  const float interval =
      textureWidth > 0 ? std::max(Config::options.spectrogram.window, 1e-3f) / static_cast<float>(textureWidth) : 0.0f;

  static float columnAccumulator = 0.0f;
  static float phaseDtAccumulator = 0.0f;
  columnAccumulator += WindowManager::dt;
  phaseDtAccumulator += WindowManager::dt;

  size_t columnsToWrite = 0;
  if (interval > FLT_EPSILON) {
    columnsToWrite = static_cast<size_t>(columnAccumulator / interval);
    if (columnsToWrite > 0)
      columnAccumulator -= interval * static_cast<float>(columnsToWrite);
  }

  if (textureWidth > 0 && columnsToWrite > 0) {
    columnsToWrite = std::min(columnsToWrite, textureWidth);
    std::vector<float>& spectrum = mapSpectrum(DSP::fftMidRaw, DSP::fftMidPhase, std::max(phaseDtAccumulator, 1e-4f));
    phaseDtAccumulator = 0.0f;

    // Prepare column data for rendering
    static std::vector<float> columnData;
    columnData.resize(bounds.h * 4);

    // Initialize column with background color (transparent)
    for (size_t i = 0; i < bounds.h; ++i) {
      columnData[i * 4 + 0] = Theme::colors.background[0];
      columnData[i * 4 + 1] = Theme::colors.background[1];
      columnData[i * 4 + 2] = Theme::colors.background[2];
      columnData[i * 4 + 3] = 0.0f;
    }

    // Choose rendering color
    bool monochrome = true;
    float* color = Theme::colors.color;
    if (Theme::colors.spectrogram_main[3] > FLT_EPSILON) {
      color = Theme::colors.spectrogram_main;
    } else if (Theme::colors.spectrogram_low != 0 && Theme::colors.spectrogram_high != 0) {
      monochrome = false;
    }

    for (size_t i = 0; i < spectrum.size(); ++i) {
      float mag = spectrum[i];

      if (mag > FLT_EPSILON) {
        float dB = 20.f * log10f(mag);
        float intens =
            std::clamp((dB - Config::options.spectrogram.limits.min_db) /
                           (Config::options.spectrogram.limits.max_db - Config::options.spectrogram.limits.min_db),
                       0.f, 1.f);

        if (intens > FLT_EPSILON) {
          float intensColor[4];
          if (monochrome) {
            Theme::mix(Theme::colors.background, color, intensColor, intens);
          } else {
            float hsva[4] = {0.f, 1.f, 1.f, 1.f};
            if (intens < 0.5f) {
              hsva[0] = Theme::colors.spectrogram_low;
              float rgba[4];
              Graphics::hsvaToRgba(hsva, rgba);
              Theme::mix(Theme::colors.background, rgba, intensColor, intens * 2.f);
            } else {
              float t = (intens - 0.5f) * 2.f;
              hsva[0] =
                  Theme::colors.spectrogram_low + (Theme::colors.spectrogram_high - Theme::colors.spectrogram_low) * t;
              Graphics::hsvaToRgba(hsva, intensColor);
            }
          }

          columnData[i * 4 + 0] = intensColor[0];
          columnData[i * 4 + 1] = intensColor[1];
          columnData[i * 4 + 2] = intensColor[2];
          columnData[i * 4 + 3] = 1.0f;
        }
      }
    }

    if (current >= textureWidth)
      current = textureWidth - 1;

    for (size_t col = 0; col < columnsToWrite; ++col) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, current, 0, 1, bounds.h, GL_RGBA, GL_FLOAT, columnData.data());
      current = (current + 1) % textureWidth;
    }
  }

  float currentU = static_cast<float>(current) / bounds.w;
  float part1 = (1.f - currentU) * bounds.w;
  float part2 = currentU * bounds.w;

  std::vector<float> vertexData;
  vertexData.reserve(32);

  if (part1 > 0.f) {
    float vertices[] = {0.0f, 0.0f,  currentU,        0.0f, 0.0f, (float)bounds.h, currentU,
                        1.0f, part1, (float)bounds.h, 1.0f, 1.0f, part1,           0.0f,
                        1.0f, 0.0f};
    vertexData.insert(vertexData.end(), vertices, vertices + 16);
  }

  if (part2 > 0.f) {
    float vertices[] = {part1,
                        0.0f,
                        0.0f,
                        0.0f,
                        part1,
                        (float)bounds.h,
                        0.0f,
                        1.0f,
                        (float)bounds.w,
                        (float)bounds.h,
                        currentU,
                        1.0f,
                        (float)bounds.w,
                        0.0f,
                        currentU,
                        0.0f};
    vertexData.insert(vertexData.end(), vertices, vertices + 16);
  }

  if (!vertexData.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(vertexData.size() / 4));
    glDisable(GL_BLEND);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

} // namespace Spectrogram