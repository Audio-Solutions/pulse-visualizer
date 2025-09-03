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

namespace Oscilloscope {

// Oscilloscope data and window
std::vector<std::pair<float, float>> points;
WindowManager::VisualizerWindow* window;

void render() {
  if (!window)
    return;

  auto& state = SDLWindow::states[window->group];

  // Calculate number of samples to display
  size_t samples = Config::options.oscilloscope.window * Config::options.audio.sample_rate / 1000;
  if (Config::options.oscilloscope.pitch.cycles > 0) {
    samples = Config::options.audio.sample_rate / DSP::pitch * Config::options.oscilloscope.pitch.cycles;
    samples = std::max(samples, static_cast<size_t>(Config::options.oscilloscope.pitch.min_cycle_time *
                                                    Config::options.audio.sample_rate / 1000.0f));
  }

  // Calculate target position in buffer
  const size_t fir_delay = DSP::FIR::bandpass_filter.order / 2;
  size_t target = (DSP::writePos + DSP::bufferSize - samples - fir_delay);
  size_t range = Config::options.audio.sample_rate / DSP::pitch * 2.f;
  size_t zeroCross = target;

  // Align to zero crossings if pitch following is enabled
  if (Config::options.oscilloscope.pitch.follow) {
    if (Config::options.oscilloscope.pitch.alignment == "center")
      target = (target + samples / 2) % DSP::bufferSize;
    else if (Config::options.oscilloscope.pitch.alignment == "right")
      target = (target + samples) % DSP::bufferSize;

    // Find zero crossing point
    for (uint32_t i = 0; i < range && i < DSP::bufferSize; i++) {
      size_t pos = (target + DSP::bufferSize - i) % DSP::bufferSize;
      size_t prev = (pos + DSP::bufferSize - 1) % DSP::bufferSize;
      if (DSP::bandpassed[prev] < 0.f && DSP::bandpassed[pos] >= 0.f) {
        zeroCross = pos;
        break;
      }
    }

    // Apply phase offset for peak alignment
    size_t phaseOffset = (target + DSP::bufferSize - zeroCross) % DSP::bufferSize;
    if (Config::options.oscilloscope.pitch.type == "peak")
      phaseOffset += Config::options.audio.sample_rate / DSP::pitch * 0.75f;
    target = (DSP::writePos + DSP::bufferSize - phaseOffset - samples) % DSP::bufferSize;
  }

  // Generate oscilloscope points
  points.resize(samples);
  const float scale = (Config::options.oscilloscope.rotation == Config::ROTATION_90 ||
                               Config::options.oscilloscope.rotation == Config::ROTATION_270
                           ? static_cast<float>(state.windowSizes.second)
                           : static_cast<float>(window->width)) /
                      samples;

  float height = Config::options.oscilloscope.rotation == Config::ROTATION_90 ||
                         Config::options.oscilloscope.rotation == Config::ROTATION_270
                     ? window->width
                     : state.windowSizes.second;

  for (size_t i = 0; i < samples; i++) {
    size_t pos = (target + i - fir_delay) % DSP::bufferSize;
    float x = static_cast<float>(i) * scale;
    float y;

    // Choose between bandpassed or raw audio data
    if (Config::options.debug.show_bandpassed) [[unlikely]]
      y = height * 0.5f + DSP::bandpassed[pos] * 0.5f * height - 0.5f;
    else if (Config::options.oscilloscope.lowpass.enabled)
      y = height * 0.5f +
          DSP::lowpassed[(pos + DSP::FIR::bandpass_filter.order / 4) % DSP::bufferSize] * 0.5f * height - 0.5f;
    else
      y = height * 0.5f + DSP::bufferMid[pos] * 0.5f * height - 0.5f;

    if (Config::options.oscilloscope.flip_x)
      y = height - y;

    switch (Config::options.oscilloscope.rotation) {
    case Config::ROTATION_0:
      points[i] = {x, y};
      break;
    case Config::ROTATION_90:
      points[i] = {window->width - y, x};
      break;
    case Config::ROTATION_180:
      points[i] = {window->width - x, state.windowSizes.second - y};
      break;
    case Config::ROTATION_270:
      points[i] = {y, state.windowSizes.second - x};
      break;
    }
  }

  // Choose rendering color
  float* color = Theme::colors.color;
  if (Theme::colors.oscilloscope_main[3] > FLT_EPSILON)
    color = Theme::colors.oscilloscope_main;

  // Render with phosphor effect if enabled
  if (Config::options.phosphor.enabled) {
    std::vector<float> vertexData;
    vertexData.reserve(points.size() * 4);
    std::vector<float> vertexColors;
    vertexColors.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    // Calculate energy for phosphor effect
    constexpr float REF_AREA = 300.f * 300.f;
    float energy = Config::options.phosphor.beam.energy / REF_AREA * (window->width * state.windowSizes.second);
    energy *= Config::options.oscilloscope.beam_multiplier / samples * 2048 * WindowManager::dt / 0.016f;

    // Calculate energy for each line segment
    for (size_t i = 0; i < points.size() - 1; i++) {
      const auto& p1 = points[i];
      const auto& p2 = points[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float len = sqrtf(dx * dx + dy * dy);
      float totalE = energy * ((1.f / Config::options.audio.sample_rate) / len);

      energies.push_back(totalE);
    }

    // Prepare vertex data for phosphor rendering
    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(i < energies.size() ? energies[i] : 0);
      vertexData.push_back(0);

      // Calculate direction-based gradient using HSV
      float hue = 0.0f;
      float saturation = 0.6f;
      float value = 1.0f;

      if (i > 0) {
        const auto& prev = points[i - 1];
        const auto& curr = points[i];
        float dx = curr.first - prev.first;
        float dy = curr.second - prev.second;
        float angle = atan2f(dy, dx);

        // Convert angle to base hue: up = 0°, right = 0.5°, down = 1.0°
        hue = (angle + M_PI_2) / M_PI;

        // squish hue from 0.0, 1.0 towards 0.5 using exponential function
        float squish = 0.5f + (hue - 0.5f) * (0.5f + 0.5f * powf(fabsf(hue - 0.5f), 2.0f));
        hue = squish + 0.77f;

      } else if (i < points.size() - 1) {
        // For first point, use direction to next point
        const auto& curr = points[i];
        const auto& next = points[i + 1];
        float dx = next.first - curr.first;
        float dy = next.second - curr.second;
        float angle = atan2f(dy, dx);

        // Convert angle to base hue: up = 0°, right = 0.5°, down = 1.0°
        hue = (angle + M_PI_2) / M_PI;

        // squish hue from 0.0, 1.0 towards 0.5 using exponential function
        float squish = 0.5f + (hue - 0.5f) * (0.5f + 0.5f * powf(fabsf(hue - 0.5f), 2.0f));
        hue = squish + 0.77f;
      }

      // Convert HSV to RGB using existing functions
      float hsva[4] = {hue, saturation, value, 1.0f};
      float rgba[4];
      Graphics::hsvaToRgba(hsva, rgba);

      float r = rgba[0];
      float g = rgba[1];
      float b = rgba[2];
      vertexColors.push_back(r);
      vertexColors.push_back(g);
      vertexColors.push_back(b);
      vertexColors.push_back(1.0f);
    }

    // Upload vertex data to GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexColorBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexColors.size() * sizeof(float), vertexColors.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Render phosphor effect
    Graphics::Phosphor::render(window, points, DSP::pitchDB > Config::options.audio.silence_threshold, color);
    window->draw();
  } else {
    // Select the window for rendering
    SDLWindow::selectWindow(window->group);

    // Render simple lines if phosphor is disabled
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, color);
  }
}

} // namespace Oscilloscope