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
  // Calculate number of samples to display
  size_t samples = Config::options.oscilloscope.time_window * Config::options.audio.sample_rate / 1000;
  if (Config::options.oscilloscope.limit_cycles) {
    samples = Config::options.audio.sample_rate / DSP::pitch * Config::options.oscilloscope.cycles;
    samples = std::max(samples, static_cast<size_t>(Config::options.oscilloscope.min_cycle_time *
                                                    Config::options.audio.sample_rate / 1000.0f));
  }

  // Calculate target position in buffer
  size_t target = (DSP::writePos + DSP::bufferSize - samples);
  size_t range = Config::options.audio.sample_rate / DSP::pitch * 2.f;
  size_t zeroCross = target;

  // Align to zero crossings if pitch following is enabled
  if (Config::options.oscilloscope.follow_pitch) {
    if (Config::options.oscilloscope.alignment == "center")
      target = (target + samples / 2) % DSP::bufferSize;
    else if (Config::options.oscilloscope.alignment == "right")
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
    if (Config::options.oscilloscope.alignment_type == "peak")
      phaseOffset += Config::options.audio.sample_rate / DSP::pitch * 0.75f;
    target = (DSP::writePos + DSP::bufferSize - phaseOffset - samples) % DSP::bufferSize;
  }

  // Generate oscilloscope points
  points.resize(samples);
  const float scale = static_cast<float>(window->width) / samples;
  for (size_t i = 0; i < samples; i++) {
    size_t pos = (target + i) % DSP::bufferSize;
    float x = static_cast<float>(i) * scale;
    float y;

    // Choose between bandpassed or raw audio data
    if (Config::options.debug.show_bandpassed) [[unlikely]]
      y = SDLWindow::height * 0.5f + DSP::bandpassed[pos] * 0.5f * SDLWindow::height;
    else if (Config::options.oscilloscope.enable_lowpass)
      y = SDLWindow::height * 0.5f + DSP::lowpassed[pos] * 0.5f * SDLWindow::height;
    else
      y = SDLWindow::height * 0.5f + DSP::bufferMid[pos] * 0.5f * SDLWindow::height;

    points[i] = {x, y};
  }

  // Choose rendering color
  float* color = Theme::colors.color;
  if (Theme::colors.oscilloscope_main[3] > 1e-6f)
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
    float energy = Config::options.phosphor.beam_energy / REF_AREA * (window->width * SDLWindow::height);
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
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexColorBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexColors.size() * sizeof(float), vertexColors.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Render phosphor effect
    Graphics::Phosphor::render(window, points, DSP::pitchDB > Config::options.audio.silence_threshold, color);
    window->draw();
  } else {
    // Select the window for rendering
    SDLWindow::selectWindow(window->sdlWindow);

    // Render simple lines if phosphor is disabled
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, color);
  }
}

} // namespace Oscilloscope