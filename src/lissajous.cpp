#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/spline.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace Lissajous {

// Lissajous figure data and window
std::vector<std::pair<float, float>> points;
WindowManager::VisualizerWindow* window;

void render() {
  // Calculate how many samples to read based on buffer position
  static size_t lastWritePos = DSP::writePos;
  size_t readCount = ((DSP::writePos - lastWritePos + DSP::bufferSize) % DSP::bufferSize) *
                     (1.f + Config::options.lissajous.readback_multiplier);

  if (readCount == 0)
    return;

  lastWritePos = DSP::writePos;

  // Resize points vector to hold new data
  points.resize(readCount);

  // Convert audio samples to Lissajous coordinates
  size_t start = (DSP::bufferSize + DSP::writePos - readCount) % DSP::bufferSize;
  for (size_t i = 0; i < readCount; i++) {
    size_t idx = (start + i) % DSP::bufferSize;

    // Convert mid/side to left/right channels
    float left = DSP::bufferMid[idx] + DSP::bufferSide[idx];
    float right = DSP::bufferMid[idx] - DSP::bufferSide[idx];

    // Map to screen coordinates
    float x = (1.f + left) * window->width / 2.f;
    float y = (1.f + right) * window->width / 2.f;

    points[i] = {x, y};
  }

  // Apply spline smoothing if enabled
  if (Config::options.lissajous.enable_splines)
    points = Spline::generate(points, Config::options.lissajous.spline_segments, {1.f, 0.f},
                              {window->width - 1, window->width});

  // Render with phosphor effect if enabled
  if (Config::options.phosphor.enabled) {
    std::vector<float> vertexData;
    vertexData.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    // Calculate energy per segment for phosphor effect
    constexpr float REF_AREA = 200.f * 200.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA * (window->width * window->width);

    energy *= Config::options.lissajous.beam_multiplier / (1.f + Config::options.lissajous.readback_multiplier) /
              Config::options.phosphor.spline_density;

    float dt = 1.f / Config::options.audio.sample_rate;

    // Calculate energy for each line segment
    for (size_t i = 0; i < points.size() - 1; i++) {
      const auto& p1 = points[i];
      const auto& p2 = points[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float len = std::max(1e-12f, sqrtf(dx * dx + dy * dy));
      float totalE = energy * (dt / len);

      energies.push_back(totalE);
    }

    // Prepare vertex data for phosphor rendering
    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(i < energies.size() ? energies[i] : 0);
      vertexData.push_back(0);
    }

    // Upload vertex data to GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Render phosphor effect
    Graphics::Phosphor::render(window, points, energies, Theme::colors.color,
                               DSP::pitchDB > Config::options.audio.silence_threshold);
    window->draw();
  } else {
    // Render simple lines if phosphor is disabled
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, Theme::colors.color);
  }
}

} // namespace Lissajous