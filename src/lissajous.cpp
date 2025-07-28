#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/spline.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace Lissajous {

std::vector<std::pair<float, float>> points;
WindowManager::VisualizerWindow* window;

void render() {
  static size_t lastWritePos = DSP::writePos;
  size_t readCount = ((DSP::writePos - lastWritePos + DSP::bufferSize) % DSP::bufferSize) *
                     (1.f + Config::options.lissajous.readback_multiplier);

  if (readCount == 0)
    return;

  lastWritePos = DSP::writePos;

  points.resize(readCount);

  size_t start = (DSP::bufferSize + DSP::writePos - readCount) % DSP::bufferSize;
  for (size_t i = 0; i < readCount; i++) {
    size_t idx = (start + i) % DSP::bufferSize;

    float left = DSP::bufferMid[idx] + DSP::bufferSide[idx];
    float right = DSP::bufferMid[idx] - DSP::bufferSide[idx];

    float x = (1.f + left) * window->width / 2.f;
    float y = (1.f + right) * window->width / 2.f;

    points[i] = {x, y};
  }

  if (Config::options.lissajous.enable_splines)
    points = Spline::generate(points, Config::options.lissajous.spline_segments, {1.f, 0.f},
                              {window->width - 1, window->width});

  std::vector<float> vertexData;

  float* color = Theme::colors.color;

  if (Config::options.phosphor.enabled) {
    vertexData.reserve(points.size() * 4);
    std::vector<float> energies;
    energies.reserve(points.size());

    constexpr float REF_AREA = 200.f * 200.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA * (window->width * window->width);

    energy *= Config::options.lissajous.beam_multiplier / (1.f + Config::options.lissajous.readback_multiplier) /
              Config::options.phosphor.spline_density;

    float dt = 1.f / Config::options.audio.sample_rate;

    for (size_t i = 0; i < points.size() - 1; i++) {
      const auto& p1 = points[i];
      const auto& p2 = points[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float len = std::max(1e-12f, sqrtf(dx * dx + dy * dy));
      float totalE = energy * (dt / len);

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < points.size(); i++) {
      vertexData.push_back(points[i].first);
      vertexData.push_back(points[i].second);
      vertexData.push_back(i < energies.size() ? energies[i] : 0);
      vertexData.push_back(0);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, points, energies, color, DSP::pitchDB > Config::options.audio.silence_threshold);
    window->draw();
  } else {
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, color);
  }
}

} // namespace Lissajous