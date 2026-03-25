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

namespace SpectrumAnalyzer {

class SpectrumAnalyzerVisualizer : public WindowManager::VisualizerWindow {
public:
  std::vector<std::pair<float, float>> pointsMain;
  std::vector<std::pair<float, float>> pointsAlt;
  std::vector<float> energyCompensation;

  SpectrumAnalyzerVisualizer() {
    id = "spectrum_analyzer";
    displayName = "Spectrum Analyzer";
  }

  void configure() override {
    if (Config::options.fft.sphere.enabled)
      aspectRatio = 1.0f;
  }

  void render() override;
};

std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer() {
  return std::make_shared<SpectrumAnalyzerVisualizer>();
}

float hzToMel(float f) { return 2595.0f * log10f(1.0f + f / 700.0f); }

// Helper to map a bin index to an X coordinate
float binToX(size_t bin, float width, float minFreq, float maxFreq, float logMin, float logMax, float melMin,
             float melMax) {
  float f = Config::options.fft.cqt.enabled
                ? DSP::ConstantQ::frequencies[bin]
                : static_cast<float>(bin) * (Config::options.audio.sample_rate / Config::options.fft.size);

  if (Config::options.fft.frequency_scale == "log") {
    float norm = (logf(f) - logMin) / (logMax - logMin);
    return norm * width;
  } else if (Config::options.fft.frequency_scale == "mel") {
    float norm = (hzToMel(f) - melMin) / (melMax - melMin);
    return norm * width;
  } else {
    float norm = (f - minFreq) / (maxFreq - minFreq);
    return norm * width;
  }
}

void SpectrumAnalyzerVisualizer::render() {

  // Choose between smoothed and raw FFT data
  const std::vector<float>& inMain = Config::options.fft.smoothing.enabled ? DSP::fftMid : DSP::fftMidRaw;
  const std::vector<float>& inAlt = Config::options.fft.smoothing.enabled ? DSP::fftSide : DSP::fftSideRaw;

  float minFreq;
  float maxFreq;

  if (Config::options.fft.cqt.enabled) {
    minFreq = DSP::ConstantQ::frequencies.front();
    maxFreq = DSP::ConstantQ::frequencies.back();
  } else {
    minFreq = Config::options.audio.sample_rate / Config::options.fft.size;
    maxFreq = inMain.size() * (Config::options.audio.sample_rate / Config::options.fft.size);
  }

  const float logMin = log(minFreq);
  const float logMax = log(maxFreq);
  const float melMin = hzToMel(minFreq);
  const float melMax = hzToMel(maxFreq);

  const int binOffset = Config::options.fft.cqt.enabled ? 0 : 1;

  // Set viewport for rendering
  WindowManager::setViewport(bounds);

  // Draw frequency markers if enabled
  if (!Config::options.phosphor.enabled && Config::options.fft.markers) {
    auto drawLogLine = [&](float f) {
      if (f < Config::options.fft.limits.min_freq || f > Config::options.fft.limits.max_freq)
        return;

      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = roundf(logX * (Config::options.fft.rotation == Config::ROTATION_90 ||
                                       Config::options.fft.rotation == Config::ROTATION_270
                                   ? static_cast<float>(bounds.h)
                                   : static_cast<float>(bounds.w)));
      float height =
          Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
              ? bounds.w
              : bounds.h;

      // Apply rotation transformation to marker coordinates
      float x1, y1, x2, y2;
      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        x1 = x2 = x;
        y1 = 0;
        y2 = height;
        break;
      case Config::ROTATION_90:
        x1 = x2 = bounds.w - height;
        y1 = y2 = x;
        break;
      case Config::ROTATION_180:
        x1 = x2 = bounds.w - x;
        y1 = 0;
        y2 = height;
        break;
      case Config::ROTATION_270:
        x1 = x2 = height;
        y1 = y2 = bounds.h - x;
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

  // Prepare point arrays
  pointsMain.clear();
  pointsAlt.clear();
  pointsMain.resize(inMain.size());
  // Optional per-point depth factors (used for shading in sphere mode)
  std::vector<float> depthsMain;

  // Generate main spectrum points
  if (Config::options.fft.sphere.enabled && Config::options.phosphor.enabled) {
    float cx = bounds.w / 2.0f;
    float cy = bounds.h / 2.0f;
    float minSize = static_cast<float>(std::min(bounds.w, bounds.h));

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
      float binHz = Config::options.audio.sample_rate / std::max(static_cast<float>(Config::options.fft.size), 1.0f);
      maxBinMain = static_cast<size_t>(std::floor(fMax / std::max(binHz, FLT_EPSILON)));
      if (maxBinMain >= inMain.size())
        maxBinMain = inMain.size() - 1;
    }

    semiMain.reserve(maxBinMain + 1);
    for (size_t bin = binOffset; bin <= maxBinMain; bin++) {
      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / Config::options.fft.size);

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
    float height =
        Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
            ? bounds.w
            : bounds.h;

    if (Config::options.phosphor.enabled)
      energyCompensation.resize(inMain.size());

    const float width =
        (Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
             ? static_cast<float>(bounds.h)
             : static_cast<float>(bounds.w));
    static float prevX;
    for (size_t bin = binOffset; bin < inMain.size(); bin++) {
      float x = binToX(bin, width, minFreq, maxFreq, logMin, logMax, melMin, melMax);
      if (bin == binOffset) {
        prevX = -binToX(bin + 1, width, minFreq, maxFreq, logMin, logMax, melMin, melMax);
      }
      if (Config::options.phosphor.enabled)
        energyCompensation[bin] = x - prevX;
      prevX = x;

      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / Config::options.fft.size);

      float mag = inMain[bin];
      float k = Config::options.fft.slope / 20.f / log10f(2.f);
      float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
      mag *= gain;

      float dB = 20.f * log10f(mag + FLT_EPSILON);
      float y = (dB - Config::options.fft.limits.min_db) /
                (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db) * height;

      if (Config::options.fft.flip_x)
        y = height - y;

      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        pointsMain[bin] = {x, y};
        break;
      case Config::ROTATION_90:
        pointsMain[bin] = {bounds.w - y, x};
        break;
      case Config::ROTATION_180:
        pointsMain[bin] = {bounds.w - x, bounds.h - y};
        break;
      case Config::ROTATION_270:
        pointsMain[bin] = {y, bounds.h - x};
        break;
      }
    }

    // Generate alternative spectrum points (for stereo visualization)
    if (!Config::options.phosphor.enabled)
      pointsAlt.resize(inAlt.size());

    for (size_t bin = binOffset; bin < inAlt.size() && !Config::options.phosphor.enabled; bin++) {
      float x = binToX(bin, width, minFreq, maxFreq, logMin, logMax, melMin, melMax);
      float f;
      if (Config::options.fft.cqt.enabled)
        f = DSP::ConstantQ::frequencies[bin];
      else
        f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inAlt.size());
      float mag = inAlt[bin];
      float k = Config::options.fft.slope / 20.f / log10f(2.f);
      float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
      mag *= gain;
      float dB = 20.f * log10f(mag + FLT_EPSILON);
      float y = (dB - Config::options.fft.limits.min_db) /
                (Config::options.fft.limits.max_db - Config::options.fft.limits.min_db) * height;
      if (Config::options.fft.flip_x)
        y = height - y;
      switch (Config::options.fft.rotation) {
      case Config::ROTATION_0:
        pointsAlt[bin] = {x, y};
        break;
      case Config::ROTATION_90:
        pointsAlt[bin] = {bounds.w - y, x};
        break;
      case Config::ROTATION_180:
        pointsAlt[bin] = {bounds.w - x, bounds.h - y};
        break;
      case Config::ROTATION_270:
        pointsAlt[bin] = {y, bounds.h - x};
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

    // Calculate frame energy
    float energy = Config::options.phosphor.beam.energy;
    energy *= Config::options.fft.beam_multiplier;
    energy /= pointsMain.size();
    energy /= 800.0f * 800.0f;
    energy *= bounds.w * bounds.h;

    if (Config::options.fft.cqt.enabled)
      energy /= 4.0; // magic constant woo

    // Build energy vector
    for (size_t i = 0; i < pointsMain.size() - 1; i++)
      energies.push_back(energy * energyCompensation[i]);

    for (size_t i = binOffset; i < pointsMain.size() - 1; i++) {
      vectorData.push_back(pointsMain[i].first);
      vectorData.push_back(pointsMain[i].second);
      vectorData.push_back(energies[i]);
      vectorData.push_back(0);

      // Calculate direction-based gradient using HSV
      if (Config::options.phosphor.beam.rainbow) {
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
      } else {
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
        vertexColors.push_back(1.0f);
      }
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vectorData.size() * sizeof(float), vectorData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SDLWindow::vertexColorBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexColors.size() * sizeof(float), vertexColors.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(this, pointsMain, true, color);
    draw();
  } else {
    Graphics::drawLines(this, pointsAlt, colorAlt);
    Graphics::drawLines(this, pointsMain, color);
  }

  if (!Config::options.fft.readout_header)
    return;

  // Handle hover functionality and pitch display
  static std::string noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static std::string noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  std::string* noteNames = (Config::options.fft.key == "sharp") ? noteNamesSharp : noteNamesFlat;

  char overlay[128];
  float x = 10.0f;
  float y = bounds.h - 20.0f;

  auto drawNoteInfo = [&](float freq, float dB) {
    auto [note, octave, cents] = DSP::toNote(freq, noteNames);
    snprintf(overlay, sizeof(overlay), "%6.2f dB | %8.2f Hz | %-2s%2d | %3d Cents", dB, freq, note.c_str(), octave,
             cents);
    Graphics::Font::drawText(overlay, x, y, 14.f, Theme::colors.text);
  };

  // Convert mouse coordinates to window-relative coordinates
  float mouseXRel = 0.0; // state->mousePos.first - x;
  float mouseYRel = 0.0; // state->mousePos.second;

  bool showCursor = hovering && !Config::options.fft.sphere.enabled && Config::options.fft.cursor &&
                    !Config::options.phosphor.enabled;

  if (showCursor) {
    // Check if mouse is within window bounds
    // Draw crosshair
    glColor4fv(color);
    glLineWidth(2.0f);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    float vertices[] = {0, mouseYRel, (float)bounds.w, mouseYRel, mouseXRel, 0, mouseXRel, (float)bounds.h};
    glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));
    glDrawArrays(GL_LINES, 0, 4);
    glDisable(GL_LINE_SMOOTH);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Transform mouse coordinates back to original coordinate system
    float originalX, originalY;
    switch (Config::options.fft.rotation) {
    case Config::ROTATION_0:
      originalX = mouseXRel;
      originalY = mouseYRel;
      break;
    case Config::ROTATION_90:
      originalX = mouseYRel;
      originalY = bounds.w - mouseXRel;
      break;
    case Config::ROTATION_180:
      originalX = bounds.w - mouseXRel;
      originalY = bounds.h - mouseYRel;
      break;
    case Config::ROTATION_270:
      originalX = bounds.h - mouseYRel;
      originalY = mouseXRel;
      break;
    }

    // Convert coordinates to frequency and dB
    float logMin = log(Config::options.fft.limits.min_freq);
    float logMax = log(Config::options.fft.limits.max_freq);
    float effectiveWidth =
        Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
            ? bounds.h
            : bounds.w;
    float effectiveHeight =
        Config::options.fft.rotation == Config::ROTATION_90 || Config::options.fft.rotation == Config::ROTATION_270
            ? bounds.w
            : bounds.h;
    float logX = originalX / effectiveWidth;
    float logFreq = logMin + logX * (logMax - logMin);
    float freq = exp(logFreq);

    float normalizedY = originalY / effectiveHeight;
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