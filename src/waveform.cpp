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
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/window_manager.hpp"

namespace Waveform {

class WaveformVisualizer : public WindowManager::VisualizerWindow {
public:
  WaveformVisualizer() {
    id = "waveform";
    displayName = "Waveform";
  }

  void render(SDLWindow::State* state) override;
};

std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer() { return std::make_shared<WaveformVisualizer>(); }

void WaveformVisualizer::render(SDLWindow::State* state) {
  auto* window = this;

  SDLWindow::selectWindow(window->group);

  WindowManager::setViewport(window->x, window->phosphor.textureWidth, state->windowSizes.second);

  const size_t textureWidth = window->phosphor.textureWidth;
  const size_t textureHeight = window->phosphor.textureHeight;
  if (textureWidth == 0 || textureHeight == 0)
    return;

  static size_t current = 0;
  static float columnAccumulator = 0.0f;
  if (current >= textureWidth)
    current = 0;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, window->phosphor.outputTexture);

  const float columnInterval =
      std::max(Config::options.waveform.window, FLT_EPSILON) / static_cast<float>(textureWidth);

  columnAccumulator += WindowManager::dt;

  size_t columnsToWrite = 0;
  if (columnInterval > FLT_EPSILON) {
    columnsToWrite = static_cast<size_t>(columnAccumulator / columnInterval);
    if (columnsToWrite > 0)
      columnAccumulator -= columnInterval * static_cast<float>(columnsToWrite);
  }

  if (columnsToWrite > 0) {
    columnsToWrite = std::min(columnsToWrite, textureWidth);

    const float* baseColor = Theme::colors.waveform[3] > FLT_EPSILON ? Theme::colors.waveform : Theme::colors.color;
    std::array<float, 3> columnColorA = {baseColor[0], baseColor[1], baseColor[2]};
    std::array<float, 3> columnColorB = {baseColor[0], baseColor[1], baseColor[2]};
    if (Theme::colors.waveform_low[3] > FLT_EPSILON && Theme::colors.waveform_mid[3] > FLT_EPSILON &&
        Theme::colors.waveform_high[3] > FLT_EPSILON) {
      auto computeTraceColor = [&](const std::vector<float>& fftData, std::array<float, 3>& outColor) {
        if (fftData.empty())
          return;

        const float lowMidSplit = std::clamp(Config::options.waveform.low_mid_split_hz,
                                             Config::options.fft.limits.min_freq, Config::options.fft.limits.max_freq);
        const float midHighSplit = std::clamp(Config::options.waveform.mid_high_split_hz, lowMidSplit + 1.0f,
                                              Config::options.fft.limits.max_freq);

        const float slopeK = Config::options.waveform.slope / 20.f / std::log10(2.f);
        constexpr float slopeRefHz = 440.0f * 2.0f;

        float low = 0.0f;
        float mid = 0.0f;
        float high = 0.0f;

        for (size_t i = 0; i < fftData.size(); ++i) {
          float freq = 0.0f;
          if (Config::options.fft.cqt.enabled) {
            if (i >= DSP::ConstantQ::frequencies.size())
              break;
            freq = DSP::ConstantQ::frequencies[i];
          } else {
            freq = static_cast<float>(i) * (Config::options.audio.sample_rate / std::max(1, Config::options.fft.size));
          }

          if (freq < Config::options.fft.limits.min_freq || freq > Config::options.fft.limits.max_freq)
            continue;

          float gain = std::pow(freq / slopeRefHz, slopeK);
          float magnitude = fftData[i] * gain;
          float energy = magnitude * magnitude;

          if (freq < lowMidSplit)
            low += energy;
          else if (freq < midHighSplit)
            mid += energy;
          else
            high += energy;
        }

        float sum = low + mid + high;
        if (sum <= FLT_EPSILON)
          return;

        low /= sum;
        mid /= sum;
        high /= sum;

        outColor[0] = low * Theme::colors.waveform_low[0] + mid * Theme::colors.waveform_mid[0] +
                      high * Theme::colors.waveform_high[0];
        outColor[1] = low * Theme::colors.waveform_low[1] + mid * Theme::colors.waveform_mid[1] +
                      high * Theme::colors.waveform_high[1];
        outColor[2] = low * Theme::colors.waveform_low[2] + mid * Theme::colors.waveform_mid[2] +
                      high * Theme::colors.waveform_high[2];
      };

      computeTraceColor(DSP::fftMidRaw, columnColorA);
      if (Config::options.waveform.mode != "mono")
        computeTraceColor(DSP::fftSideRaw, columnColorB);
    }

    size_t sampleCountPerColumn = static_cast<size_t>(
        std::max(1.0f, std::round(Config::options.audio.sample_rate *
                                  std::max(columnInterval, 1.0f / Config::options.audio.sample_rate))));

    static std::vector<float> columnData;
    columnData.resize(textureHeight * 3);

    auto drawMinMaxEnvelopeColumn = [&](int rowMin, int rowMax, size_t startIndex, size_t sampleCount, bool secondary,
                                        const float* color) {
      if (sampleCount == 0 || rowMax < rowMin)
        return;

      float minValue = INFINITY;
      float maxValue = -INFINITY;

      for (size_t i = 0; i < sampleCount; ++i) {
        size_t idx = (startIndex + i) % DSP::bufferSize;
        float mid = DSP::bufferMid[idx];
        float side = DSP::bufferSide[idx];

        float value = mid;
        if (Config::options.waveform.mode != "mono" && Config::options.fft.mode == "leftright")
          value = secondary ? (mid - side) : (mid + side);
        else if (Config::options.waveform.mode != "mono")
          value = secondary ? side : mid;

        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
      }

      if (minValue > maxValue)
        return;

      float yMinMapped = static_cast<float>(rowMin) +
                         (std::clamp(minValue, -1.0f, 1.0f) + 1.0f) * 0.5f * static_cast<float>(rowMax - rowMin);
      float yMaxMapped = static_cast<float>(rowMin) +
                         (std::clamp(maxValue, -1.0f, 1.0f) + 1.0f) * 0.5f * static_cast<float>(rowMax - rowMin);

      int yMin =
          static_cast<int>(std::clamp(std::lround(yMinMapped), static_cast<long>(rowMin), static_cast<long>(rowMax)));
      int yMax =
          static_cast<int>(std::clamp(std::lround(yMaxMapped), static_cast<long>(rowMin), static_cast<long>(rowMax)));
      if (yMin > yMax)
        std::swap(yMin, yMax);

      for (int y = yMin; y <= yMax; ++y) {
        size_t offset = static_cast<size_t>(y) * 3;
        columnData[offset + 0] = color[0];
        columnData[offset + 1] = color[1];
        columnData[offset + 2] = color[2];
      }
    };

    for (size_t col = 0; col < columnsToWrite; ++col) {
      for (size_t i = 0; i < textureHeight; ++i) {
        columnData[i * 3 + 0] = Theme::colors.background[0];
        columnData[i * 3 + 1] = Theme::colors.background[1];
        columnData[i * 3 + 2] = Theme::colors.background[2];
      }

      size_t endIndex = (DSP::writePos + DSP::bufferSize -
                         (columnsToWrite * sampleCountPerColumn - ((col + 1) * sampleCountPerColumn))) %
                        DSP::bufferSize;
      size_t startIndex = (endIndex + DSP::bufferSize - sampleCountPerColumn) % DSP::bufferSize;

      if (Config::options.waveform.mode != "mono") {
        drawMinMaxEnvelopeColumn(static_cast<int>(textureHeight / 2), static_cast<int>(textureHeight - 1), startIndex,
                                 sampleCountPerColumn, false, columnColorA.data());
        drawMinMaxEnvelopeColumn(0, static_cast<int>(std::max<size_t>(textureHeight / 2, 1) - 1), startIndex,
                                 sampleCountPerColumn, true, columnColorB.data());
      } else {
        drawMinMaxEnvelopeColumn(0, static_cast<int>(textureHeight - 1), startIndex, sampleCountPerColumn, false,
                                 columnColorA.data());
      }

      glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(current), 0, 1, static_cast<GLsizei>(textureHeight), GL_RGB,
                      GL_FLOAT, columnData.data());
      current = (current + 1) % textureWidth;
    }
  }

  glColor4f(1.f, 1.f, 1.f, 1.f);

  float currentU = static_cast<float>(current) / static_cast<float>(textureWidth);
  float part1 = (1.f - currentU) * static_cast<float>(textureWidth);
  float part2 = currentU * static_cast<float>(textureWidth);

  std::vector<float> vertexData;
  vertexData.reserve(32);

  if (part1 > 0.f) {
    float vertices[] = {0.0f,     0.0f,
                        currentU, 0.0f,
                        0.0f,     static_cast<float>(state->windowSizes.second),
                        currentU, 1.0f,
                        part1,    static_cast<float>(state->windowSizes.second),
                        1.0f,     1.0f,
                        part1,    0.0f,
                        1.0f,     0.0f};
    vertexData.insert(vertexData.end(), vertices, vertices + 16);
  }

  if (part2 > 0.f) {
    float vertices[] = {part1,
                        0.0f,
                        0.0f,
                        0.0f,
                        part1,
                        static_cast<float>(state->windowSizes.second),
                        0.0f,
                        1.0f,
                        static_cast<float>(window->phosphor.textureWidth),
                        static_cast<float>(state->windowSizes.second),
                        currentU,
                        1.0f,
                        static_cast<float>(window->phosphor.textureWidth),
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
    glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(vertexData.size() / 4));
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

} // namespace Waveform
