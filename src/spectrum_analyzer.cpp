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

namespace SpectrumAnalyzer {

// Spectrum analyzer data and window
std::vector<std::pair<float, float>> pointsMain;
std::vector<std::pair<float, float>> pointsAlt;
WindowManager::VisualizerWindow* window;

void render() {
  // Select the window for rendering
  SDLWindow::selectWindow(window->sdlWindow);

  // Calculate logarithmic frequency scale
  float logMin = log(Config::options.fft.limits.min_freq);
  float logMax = log(Config::options.fft.limits.max_freq);

  // Set viewport for rendering
  WindowManager::setViewport(window->x, window->width, SDLWindow::windowSizes[window->sdlWindow].second);

  // Draw frequency markers if enabled
  if (!Config::options.phosphor.enabled && Config::options.fft.markers) {
    auto drawLogLine = [&](float f) {
      if (f < Config::options.fft.limits.min_freq || f > Config::options.fft.limits.max_freq)
        return;
      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = roundf(logX * (Config::options.fft.rotation == Config::ROTATION_90 ||
                                       Config::options.fft.rotation == Config::ROTATION_270
                                   ? static_cast<float>(SDLWindow::windowSizes[window->sdlWindow].second)
                                   : static_cast<float>(window->width)));
      float height =
          Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
              ? window->width
              : SDLWindow::windowSizes[window->sdlWindow].second;

      // Apply rotation transformation to marker coordinates
      float x1, y1, x2, y2;
      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        x1 = x2 = x;
        y1 = 0;
        y2 = height;
        break;
      case Config::ROTATION_90:
        x1 = x2 = window->width - height;
        y1 = y2 = x;
        break;
      case Config::ROTATION_180:
        x1 = x2 = window->width - x;
        y1 = 0;
        y2 = height;
        break;
      case Config::ROTATION_270:
        x1 = x2 = height;
        y1 = y2 = SDLWindow::windowSizes[window->sdlWindow].second - x;
        break;
      }
      Graphics::drawLine(x1, y1, x2, y2, Theme::colors.accent, 1.f);
    };

    // Draw frequency lines for each decade
    for (int decade = 1; decade <= 20000; decade *= 10) {
      for (int mult = 1; mult < 10; ++mult) {
        int freq = mult * decade;
        if (freq < Config::options.fft.limits.min_freq)
          continue;
        if (freq > Config::options.fft.limits.max_freq)
          break;
        drawLogLine(static_cast<float>(freq));
      }
    }
  }

  // Choose between smoothed and raw FFT data
  const std::vector<float>& inMain = Config::options.fft.smoothing.enabled ? DSP::fftMid : DSP::fftMidRaw;
  const std::vector<float>& inAlt = Config::options.fft.smoothing.enabled ? DSP::fftSide : DSP::fftSideRaw;

  // Prepare point arrays
  pointsMain.clear();
  pointsAlt.clear();
  pointsMain.resize(inMain.size());
  // Optional per-point depth factors (used for shading in sphere mode)
  std::vector<float> depthsMain;

  // Generate main spectrum points
  if (Config::options.fft.sphere.enabled && Config::options.phosphor.enabled) {
    float cx = window->width / 2.0f;
    float cy = SDLWindow::windowSizes[window->sdlWindow].second / 2.0f;
    float minSize = static_cast<float>(std::min(window->width, SDLWindow::windowSizes[window->sdlWindow].second));

    // Place points on a circle and rotate along Y using per-bin phase; project with mild perspective.
    const float cameraDistance = 4.0f * minSize;

    // Align phases so the current main pitch has phase zero
    float refPhaseMid = 0.0f;
    bool haveRefPhase = false;
    if (DSP::pitchDB > Config::options.audio.silence_threshold && !DSP::fftMidPhase.empty()) {
      size_t refIdx = 0;
      if (Config::options.fft.cqt.enabled) {
        auto brk = DSP::ConstantQ::find(DSP::pitch);
        size_t i0 =
            std::min(brk.first, DSP::ConstantQ::frequencies.size() ? DSP::ConstantQ::frequencies.size() - 1 : 0);
        size_t i1 =
            std::min(brk.second, DSP::ConstantQ::frequencies.size() ? DSP::ConstantQ::frequencies.size() - 1 : 0);
        float f0 = (i0 < DSP::ConstantQ::frequencies.size()) ? DSP::ConstantQ::frequencies[i0] : 0.0f;
        float f1 = (i1 < DSP::ConstantQ::frequencies.size()) ? DSP::ConstantQ::frequencies[i1] : 0.0f;
        refIdx = (std::fabs(f0 - DSP::pitch) <= std::fabs(f1 - DSP::pitch)) ? i0 : i1;
      } else {
        size_t bins = inMain.size();
        float binHz = Config::options.audio.sample_rate / std::max(static_cast<float>(bins), FLT_EPSILON);
        refIdx = static_cast<size_t>(roundf(DSP::pitch / std::max(binHz, FLT_EPSILON)));
      }
      if (refIdx < DSP::fftMidPhase.size()) {
        refPhaseMid = DSP::fftMidPhase[refIdx];
        haveRefPhase = true;
      }
    }
    // Build right-side semicircle
    std::vector<std::pair<float, float>> semiMain;
    std::vector<float> semiMainDepth;
    // Determine max bin to include based on sphere_max_freq
    float fMin = std::max(Config::options.fft.limits.min_freq, 1.0f);
    float fMax = std::max(Config::options.fft.sphere.max_freq, fMin + 1.0f);
    float logMinSphere = logf(fMin);
    float logMaxSphere = logf(fMax);

    size_t maxBinMain;
    if (Config::options.fft.cqt.enabled) {
      size_t limit = std::min(inMain.size(), DSP::ConstantQ::frequencies.size());
      size_t k = 0;
      for (; k < limit; ++k) {
        if (DSP::ConstantQ::frequencies[k] > fMax)
          break;
      }
      maxBinMain = (k == 0) ? 0 : (k - 1);
    } else {
      float binHz = Config::options.audio.sample_rate / std::max(static_cast<float>(inMain.size()), 1.0f);
      maxBinMain = static_cast<size_t>(std::floor(fMax / std::max(binHz, FLT_EPSILON)));
      if (maxBinMain >= inMain.size())
        maxBinMain = inMain.size() - 1;
    }

    semiMain.reserve(maxBinMain + 1);
    for (size_t bin = 0; bin <= maxBinMain; bin++) {
      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inMain.size());

      float mag = inMain[bin];
      float k = Config::options.fft.slope / 20.f / log10f(2.f);
      float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
      mag *= gain;

      float dB = 20.f * log10f(mag + FLT_EPSILON);
      float norm = (dB - Config::options.fft.limits.min_db) /
                   (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db);
      float radius = Config::options.fft.sphere.base_radius * minSize +
                     norm * (0.5f - Config::options.fft.sphere.base_radius) * minSize;
      float frac = (logf(std::max(f, fMin)) - logMinSphere) / std::max(logMaxSphere - logMinSphere, FLT_EPSILON);
      frac = std::clamp(frac, 0.0f, 1.0f);    // 0..1
      float baseAngle = M_PI_2 + frac * M_PI; // right semicircle, top->bottom

      // 3D point on XY plane (Z=0)
      float x = radius * cos(baseAngle);
      float y = radius * sin(baseAngle);
      float z = 0.0f;

      // Rotate around Y-axis by phase aligned to main pitch (harmonic-locked)
      float phase = (bin < DSP::fftMidPhase.size()) ? DSP::fftMidPhase[bin] : 0.0f;
      if (haveRefPhase) {
        float pitchHz = std::max(DSP::pitch, 1.0f);
        int harmonicIndex = std::max(1, static_cast<int>(roundf(f / pitchHz)));
        phase -= static_cast<float>(harmonicIndex) * refPhaseMid;
      }
      // Normalize phase to [0, π]
      while (phase < 0.0f)
        phase += M_PI;
      phase = fmodf(phase, M_PI);
      float cosPhi = cosf(phase);
      float sinPhi = sinf(phase);
      float xr = x * cosPhi - z * sinPhi;
      float yr = y;
      float zr = x * sinPhi + z * cosPhi;

      // Perspective projection with focal length
      float denomProj = (zr + cameraDistance);
      float scale = cameraDistance / std::max(denomProj, FLT_EPSILON);
      float sx = cx + xr * scale;
      float sy = cy + yr * scale;
      semiMain.push_back({sx, sy});
      // Depth factor: 0 at far back (zr == -radius), 1 at halfway/front (zr >= 0)
      float radiusSafe = std::max(radius, FLT_EPSILON);
      float br = zr < 0.0f ? std::clamp(1.0f + zr / radiusSafe, 0.0f, 1.0f) : 1.0f;
      semiMainDepth.push_back(br);
    }

    // Compose full orb
    pointsMain.clear();
    pointsMain.reserve(semiMain.size() * 2);
    depthsMain.clear();
    depthsMain.reserve(semiMain.size() * 2);
    for (const auto& p : semiMain)
      pointsMain.push_back(p);
    for (const auto d : semiMainDepth)
      depthsMain.push_back(d);
    for (size_t i = semiMain.size(); i-- > 0;) {
      auto p = semiMain[i];
      float mx = 2.0f * cx - p.first;
      pointsMain.push_back({mx, p.second});
      depthsMain.push_back(semiMainDepth[i]);
    }
  } else {
    for (size_t bin = 0; bin < inMain.size(); bin++) {
      // Calculate frequency for this bin
      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inMain.size());

      // Convert to logarithmic X coordinate
      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = logX * (Config::options.fft.rotation == Config::ROTATION_90 ||
                                Config::options.fft.rotation == Config::ROTATION_270
                            ? static_cast<float>(SDLWindow::windowSizes[window->sdlWindow].second)
                            : static_cast<float>(window->width));
      float mag = inMain[bin];

      // Apply slope correction
      float k = Config::options.fft.slope / 20.f / log10f(2.f);
      float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
      mag *= gain;

      // Convert to decibels and map to Y coordinate
      float dB = 20.f * log10f(mag + FLT_EPSILON);
      float height =
          Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
              ? window->width
              : SDLWindow::windowSizes[window->sdlWindow].second;
      float y = (dB - Config::options.fft.limits.min_db) /
                (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db) * height;

      if (Config::options.fft.flip_x)
        y = height - y;

      // Apply rotation transformation
      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        pointsMain[bin] = {x, y};
        break;
      case Config::ROTATION_90:
        pointsMain[bin] = {window->width - y, x};
        break;
      case Config::ROTATION_180:
        pointsMain[bin] = {window->width - x, SDLWindow::windowSizes[window->sdlWindow].second - y};
        break;
      case Config::ROTATION_270:
        pointsMain[bin] = {y, SDLWindow::windowSizes[window->sdlWindow].second - x};
        break;
      }
    }

    // Generate alternative spectrum points (for stereo visualization)
    if (!Config::options.phosphor.enabled)
      pointsAlt.resize(inAlt.size());
    for (size_t bin = 0; bin < inAlt.size() && !Config::options.phosphor.enabled; bin++) {
      // Calculate frequency for this bin
      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inAlt.size());

      // Convert to logarithmic X coordinate
      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = logX * (Config::options.fft.rotation == Config::ROTATION_90 ||
                                Config::options.fft.rotation == Config::ROTATION_270
                            ? static_cast<float>(SDLWindow::windowSizes[window->sdlWindow].second)
                            : static_cast<float>(window->width));
      float mag = inAlt[bin];

      // Apply slope correction
      float k = Config::options.fft.slope / 20.f / log10f(2.f);
      float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
      mag *= gain;

      // Convert to decibels and map to Y coordinate
      float dB = 20.f * log10f(mag + FLT_EPSILON);
      float height =
          Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
              ? window->width
              : SDLWindow::windowSizes[window->sdlWindow].second;
      float y = (dB - Config::options.fft.limits.min_db) /
                (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db) * height;

      if (Config::options.fft.flip_x)
        y = height - y;

      // Apply rotation transformation
      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        pointsAlt[bin] = {x, y};
        break;
      case Config::ROTATION_90:
        pointsAlt[bin] = {window->width - y, x};
        break;
      case Config::ROTATION_180:
        pointsAlt[bin] = {window->width - x, SDLWindow::windowSizes[window->sdlWindow].second - y};
        break;
      case Config::ROTATION_270:
        pointsAlt[bin] = {y, SDLWindow::windowSizes[window->sdlWindow].second - x};
        break;
      }
    }
  }

  // Choose rendering colors
  float* color = Theme::colors.color;
  if (Theme::colors.spectrum_analyzer_main[3] > FLT_EPSILON)
    color = Theme::colors.spectrum_analyzer_main;

  float* colorAlt = Theme::alpha(Theme::colors.color, 0.5f);
  if (Theme::colors.spectrum_analyzer_secondary[3] > FLT_EPSILON)
    colorAlt = Theme::colors.spectrum_analyzer_secondary;

  // Render with phosphor effect if enabled
  if (Config::options.phosphor.enabled) {
    std::vector<float> vectorData;
    std::vector<float> vertexColors;
    std::vector<float> energies;
    energies.reserve(pointsMain.size());
    vectorData.reserve(pointsMain.size() * 4);

    constexpr float REF_AREA = 400.f * 300.f;
    float energy = Config::options.phosphor.beam.energy / REF_AREA *
                   (Config::options.fft.cqt.enabled ? window->width * SDLWindow::windowSizes[window->sdlWindow].second
                                                    : 400.f * 50.f);

    energy *= Config::options.fft.beam_multiplier * WindowManager::dt / 0.016f;

    float dt = 1.0f / Config::options.audio.sample_rate;

    for (size_t i = 0; i < pointsMain.size() - 1; i++) {
      const auto& p1 = pointsMain[i];
      const auto& p2 = pointsMain[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float segLen = std::max(sqrtf(dx * dx + dy * dy), FLT_EPSILON);

      float totalE = energy * (dt / (Config::options.fft.cqt.enabled ? segLen : 1.f / sqrtf(dx))) * 2.f;

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < pointsMain.size(); i++) {
      vectorData.push_back(pointsMain[i].first);
      vectorData.push_back(pointsMain[i].second);
      vectorData.push_back(i < energies.size() ? energies[i] : 0);
      vectorData.push_back(0);

      // Calculate direction-based gradient using HSV
      float hue = 0.0f;
      float saturation = 0.6f;
      // Depth-based brightness boost for sphere mode
      float value = Config::options.fft.sphere.enabled && i < depthsMain.size()
                        ? std::clamp(0.6f + 0.6f * depthsMain[i], 0.0f, 1.0f)
                        : 1.0f;

      if (i > 0) {
        const auto& prev = pointsMain[i - 1];
        const auto& curr = pointsMain[i];
        float dx = curr.first - prev.first;
        float dy = curr.second - prev.second;
        float angle = atan2f(dy, dx);

        if (!Config::options.fft.sphere.enabled) {
          // Convert angle to base hue: up = 0°, right = 0.5°, down = 1.0°
          hue = (angle + M_PI_2) / M_PI;

          // squish hue from 0.0, 1.0 towards 0.5 using exponential function
          float squish = 0.5f + (hue - 0.5f) * (0.5f + 0.5f * powf(fabsf(hue - 0.5f), 2.0f));
          hue = squish + 0.77f;
        } else {
          hue = (angle + M_PI) / (2.0f * M_PI) + 0.77f;
        }
      } else if (i < pointsMain.size() - 1) {
        // For first point, use direction to next point
        const auto& curr = pointsMain[i];
        const auto& next = pointsMain[i + 1];
        float dx = next.first - curr.first;
        float dy = next.second - curr.second;
        float angle = atan2f(dy, dx);

        if (!Config::options.fft.sphere.enabled) {
          // Convert angle to base hue: up = 0°, right = 0.5°, down = 1.0°
          hue = (angle + M_PI_2) / M_PI;

          // squish hue from 0.0, 1.0 towards 0.5 using exponential function
          float squish = 0.5f + (hue - 0.5f) * (0.5f + 0.5f * powf(fabsf(hue - 0.5f), 2.0f));
          hue = squish + 0.77f;
        } else {
          hue = (angle + M_PI) / (2.0f * M_PI) + 0.77f;
        }
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

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vectorData.size() * sizeof(float), vectorData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexColorBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexColors.size() * sizeof(float), vertexColors.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, pointsMain, true, color);
    window->draw();
  } else {
    Graphics::drawLines(window, pointsAlt, colorAlt);
    Graphics::drawLines(window, pointsMain, color);
  }

  // Handle hover functionality and pitch display
  static std::string noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static std::string noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  std::string* noteNames = (Config::options.fft.key == "sharp") ? noteNamesSharp : noteNamesFlat;

  char overlay[128];
  float x = 10.0f;
  float y = SDLWindow::windowSizes[window->sdlWindow].second - 20.0f;

  auto drawNoteInfo = [&](float freq, float dB) {
    auto [note, octave, cents] = DSP::toNote(freq, noteNames);
    snprintf(overlay, sizeof(overlay), "%6.2f dB | %8.2f Hz | %-2s%2d | %3d Cents", dB, freq, note.c_str(), octave,
             cents);
    Graphics::Font::drawText(overlay, x, y, 14.f, Theme::colors.text, window->sdlWindow);
  };

  // Convert mouse coordinates to window-relative coordinates
  float mouseXRel = SDLWindow::mousePos[window->sdlWindow].first - window->x;
  float mouseYRel = SDLWindow::mousePos[window->sdlWindow].second;

  if (SDLWindow::focused[window->sdlWindow] && mouseXRel >= 0 && mouseXRel < window->width && mouseYRel >= 0 &&
      mouseYRel < SDLWindow::windowSizes[window->sdlWindow].second && !Config::options.fft.sphere.enabled) {
    // Check if mouse is within window bounds
    // Draw crosshair
    glColor4fv(color);
    glLineWidth(2.0f);
    glBegin(GL_LINES);

    // Draw horizontal line from left edge to right edge
    glVertex2f(0, mouseYRel);
    glVertex2f(window->width, mouseYRel);

    // Draw vertical line from top edge to bottom edge
    glVertex2f(mouseXRel, 0);
    glVertex2f(mouseXRel, SDLWindow::windowSizes[window->sdlWindow].second);

    glEnd();

    // Transform mouse coordinates back to unrotated coordinate system
    float unrotatedX, unrotatedY;
    switch (Config::options.fft.rotation) {
    case Config::ROTATION_0:
      unrotatedX = mouseXRel;
      unrotatedY = mouseYRel;
      break;
    case Config::ROTATION_90:
      unrotatedX = mouseYRel;
      unrotatedY = window->width - mouseXRel;
      break;
    case Config::ROTATION_180:
      unrotatedX = window->width - mouseXRel;
      unrotatedY = SDLWindow::windowSizes[window->sdlWindow].second - mouseYRel;
      break;
    case Config::ROTATION_270:
      unrotatedX = SDLWindow::windowSizes[window->sdlWindow].second - mouseYRel;
      unrotatedY = mouseXRel;
      break;
    }

    // Convert coordinates to frequency and dB
    float logMin = log(Config::options.fft.limits.min_freq);
    float logMax = log(Config::options.fft.limits.max_freq);
    float effectiveWidth =
        Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
            ? SDLWindow::windowSizes[window->sdlWindow].second
            : window->width;
    float effectiveHeight =
        Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
            ? window->width
            : SDLWindow::windowSizes[window->sdlWindow].second;
    float logX = unrotatedX / effectiveWidth;
    float logFreq = logMin + logX * (logMax - logMin);
    float freq = exp(logFreq);

    float normalizedY = unrotatedY / effectiveHeight;
    float dB = Config::options.fft.limits.min_db +
               normalizedY * (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db);
    float k = Config::options.fft.slope / 20.f / log10f(2.f);
    float gain = powf(freq * 1.0f / (440.0f * 2.0f), -k);
    dB += 20.f * log10f(gain);

    drawNoteInfo(freq, dB);
  } else if (DSP::pitchDB > Config::options.audio.silence_threshold) {
    drawNoteInfo(DSP::pitch, DSP::pitchDB);
  }
}

} // namespace SpectrumAnalyzer