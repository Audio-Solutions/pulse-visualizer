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
#include "include/spline.hpp"
#include "include/theme.hpp"
#include "include/window_manager.hpp"

namespace Lissajous {
class LissajousVisualizer : public WindowManager::VisualizerWindow {
public:
  std::vector<std::pair<float, float>> points;

  LissajousVisualizer() {
    id = "lissajous";
    displayName = "Lissajous";
    aspectRatio = 1.0f;
  }

  void render() override;
};

std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer() { return std::make_shared<LissajousVisualizer>(); }

void LissajousVisualizer::render() {
  // Calculate how many samples to read based on buffer position
  static size_t prevWrite = 0;

  size_t readCount = (DSP::writePos + DSP::bufferSize - prevWrite) % DSP::bufferSize;
  prevWrite = DSP::writePos;

  // Sane limit to avoid lag (5x expected samples)
  if (readCount > Config::options.audio.sample_rate / Config::options.window.fps_limit * 5)
    readCount = Config::options.audio.sample_rate / Config::options.window.fps_limit * 5;

  // Compensate for gap created with the shader's line drawing algo and potentially the catmull-rom spline
  readCount++;

  if (Config::options.lissajous.spline.tension > FLT_EPSILON && Config::options.lissajous.spline.segments != 0) {
    readCount += 3;
  }

  // Resize points vector to hold new data
  points.resize(readCount);

  // Convert audio samples to Lissajous coordinates
  size_t start = (DSP::bufferSize + DSP::writePos - readCount) % DSP::bufferSize;
  for (size_t i = 0; i < readCount; i++) {
    size_t idx = (start + i) % DSP::bufferSize;

    float left = DSP::bufferMid[idx] + DSP::bufferSide[idx];
    float right = DSP::bufferMid[idx] - DSP::bufferSide[idx];

    // Basic mapping to screen coordinates
    float x = (1.f + left) * bounds.w / 2.f;
    float y = (1.f + right) * bounds.h / 2.f;
    switch (Config::options.lissajous.rotation) {
    case Config::ROTATION_0:
      points[i] = {x, y};
      break;
    case Config::ROTATION_90:
      points[i] = {bounds.w - y, x};
      break;
    case Config::ROTATION_180:
      points[i] = {bounds.w - x, bounds.h - y};
      break;
    case Config::ROTATION_270:
      points[i] = {y, bounds.h - x};
      break;
    }
  }

  // Apply spline smoothing
  if (Config::options.lissajous.spline.tension > FLT_EPSILON && Config::options.lissajous.spline.segments != 0)
    points =
        Spline::generate(points, Config::options.lissajous.spline.segments, Config::options.lissajous.spline.tension);

  // Apply stretch mode if enabled
  const std::string& mode = Config::options.lissajous.mode;
  bool isRadialWarpMode = (mode == "pulsar" || mode == "black_hole");
  bool isCircleMode = (isRadialWarpMode || mode == "circle");

  if (mode != "normal") {
    float halfW = bounds.w * 0.5;
    float invHalfW = 2.0f / bounds.w;

    float k = 0.0f;
    float kPost = 0.0f;
    float singularity = 1.0f / M_E + (mode == "pulsar" ? -1e-3f : 1e-3f);

    if (isRadialWarpMode) {
      k = (mode == "pulsar" ? -1e-3f : 2e-3f);
      float nxRef = k;
      if (mode == "pulsar")
        nxRef *= FLT_EPSILON;
      float dRef = std::abs(nxRef);
      float sRef = -(std::log(dRef + singularity) + 1.0f) / dRef;
      kPost = 1.0f / std::abs(nxRef * sRef);
    }

    for (auto& point : points) {
      float nx = (point.first - halfW) * invHalfW;
      float ny = (point.second - halfW) * invHalfW;

      // Circle transform
      if (isCircleMode) {
        float cx = nx * std::sqrt(1.0 - 0.5 * ny * ny);
        float cy = ny * std::sqrt(1.0 - 0.5 * nx * nx);
        nx = cx;
        ny = cy;
      }

      // Pulsar/Black Hole transform
      if (isRadialWarpMode) {
        nx *= k;
        ny *= k;
        float d = std::sqrt(nx * nx + ny * ny);
        float s = -(std::log(d + singularity) + 1.0f) / d;
        nx = nx * s * kPost;
        ny = ny * s * kPost;
      }

      // 45° rotation
      float sx = halfW + nx * halfW;
      float sy = halfW + ny * halfW;
      float dx = sx - halfW;
      float dy = sy - halfW;
      float scale = (mode == "rotate" ? 0.5f : M_SQRT1_2);
      float rx = (dx - dy) * scale;
      float ry = (dx + dy) * scale;
      point.first = halfW + rx;
      point.second = halfW + ry;
    }
  }

  // Render with phosphor effect if enabled
  if (Config::options.phosphor.enabled) {
    std::vector<float> vertexData;
    vertexData.reserve(points.size() * 4);
    std::vector<float> vertexColors;
    vertexColors.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    // Calculate frame energy
    float energy = Config::options.phosphor.beam.energy;
    energy *= Config::options.lissajous.beam_multiplier;
    energy /= points.size();
    energy /= 300.0f * 300.0f;
    energy *= bounds.w * bounds.h;

    if (Config::options.lissajous.spline.tension > FLT_EPSILON && Config::options.lissajous.spline.segments != 0)
      energy /= Config::options.lissajous.spline.segments;

    // Build energy vector
    for (size_t i = 0; i < points.size() - 1; i++)
      energies.push_back(energy);

    // Prepare vertex data for phosphor rendering
    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(energies[i]);
      vertexData.push_back(0);

      // Calculate direction-based gradient using HSV
      if (Config::options.phosphor.beam.rainbow) {
        float hue = 0.0f;
        float saturation = 0.6f;
        float value = 1.0f;

        if (i > 0) {
          const auto& prev = points[i - 1];
          const auto& curr = points[i];
          float dx = curr.first - prev.first;
          float dy = curr.second - prev.second;
          float angle = atan2f(dy, dx);

          // Convert angle to hue using full -π to π range
          // Map -π to π range to 0 to 1 hue range
          hue = (angle + M_PI) / (2.0f * M_PI) + 0.77f;
        } else if (i < points.size() - 1) {
          // For first point, use direction to next point
          const auto& curr = points[i];
          const auto& next = points[i + 1];
          float dx = next.first - curr.first;
          float dy = next.second - curr.second;
          float angle = atan2f(dy, dx);

          // Convert angle to hue using full -π to π range
          hue = (angle + M_PI) / (2.0f * M_PI) + 0.77f;
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
      } else {
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
      }
    }

    // Upload vertex data to GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexColorBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexColors.size() * sizeof(float), vertexColors.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Render phosphor effect
    Graphics::Phosphor::render(this, points, DSP::pitchDB > Config::options.audio.silence_threshold,
                               Theme::colors.color);
    draw();
  } else {
    // Select the window for rendering
    SDLWindow::selectWindow(group);

    // Render simple lines if phosphor is disabled
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(this, points, Theme::colors.color);
  }
}

} // namespace Lissajous