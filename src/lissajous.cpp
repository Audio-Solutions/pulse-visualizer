#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
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

    float left = DSP::bufferMid[idx] + DSP::bufferSide[idx];
    float right = DSP::bufferMid[idx] - DSP::bufferSide[idx];

    // Basic mapping to screen coordinates
    float x = (1.f + right) * window->width / 2.f;
    float y = (1.f + left) * window->width / 2.f;
    points[i] = {x, y};
  }

  // Apply spline smoothing if enabled
  if (Config::options.lissajous.enable_splines)
    points = Spline::generate<10>(points, {1.f, 0.f}, {window->width - 1, window->width});

  // Apply stretch mode if enabled
  const std::string& mode = Config::options.lissajous.mode;
  bool isStretchMode = (mode == "rotate" || mode == "pulsar" || mode == "circle" || mode == "black_hole");
  bool isCircleMode = (mode == "circle" || mode == "pulsar" || mode == "black_hole");
  bool isPulsarMode = (mode == "pulsar" || mode == "black_hole");

  if (isStretchMode) {
    float halfW = window->width / 2.0f;

    // Pre-compute pulsar mode constants
    // This scales the input such that the point (1, 0) ends up at the origin (0, 0)
    // And scales the output such that the point near origin (1e-6, 0) ends up at (1, 0)
    // Singularity scales the logarythmic space to make the points distribute more to the outer edge
    // Transformation is nullified at exactly 1/e, below makes it a pulsar, above it makes a black hole
    float k = 0.0f, kPost = 0.0f, singularity = 1.0f / M_E + (mode == "pulsar" ? -1e-3f : 1e-3f);
    if (isPulsarMode) {

      // Scale k calculation
      k = (std::exp(-1.0f) - singularity) / (mode == "black_hole" ? std::sqrt(2.0f) / 2.0f : std::sqrt(2.0f));

      // Nonlinear pre-transform
      float nxRef = (mode == "black_hole" ? 1.f : 1e-6f) * std::sqrt(2.0f) * k;

      // Calculate d and s for logarithmic scaling
      float dRef = std::abs(nxRef);
      float sRef = -(std::log(dRef + singularity) + 1.0f) / dRef;

      // Compute post-scale factor
      kPost = 1.0f / std::abs(nxRef * sRef);
    }

    for (auto& point : points) {
      float nx = (point.first - halfW) / halfW;
      float ny = (point.second - halfW) / halfW;

      if (isCircleMode) {
        // Circle transform with Density-constant linear mapping
        float r, theta;
        if (fabs(nx) > fabs(ny)) {
          r = nx;
          theta = M_PI_4 * (ny / nx);
        } else {
          r = ny;
          theta = M_PI_2 - M_PI_4 * (nx / ny);
        }

        nx = r * cosf(theta) * M_SQRT2;
        ny = r * sinf(theta) * M_SQRT2;
      }
      if (isPulsarMode) {
        // Apply normalized scaling to current point
        nx *= k;
        ny *= k;

        // Funny math
        float d = std::sqrt(nx * nx + ny * ny);
        float s = -(std::log(d + singularity) + 1.0f) / d;
        nx = nx * s * kPost * M_SQRT2;
        ny = ny * s * kPost * M_SQRT2;
      }

      // Apply rotation
      float sx = halfW + nx * halfW;
      float sy = halfW + ny * halfW;
      float dx = sx - halfW;
      float dy = sy - halfW;
      float rx = (dx * M_SQRT1_2 - dy * M_SQRT1_2) * M_SQRT1_2;
      float ry = (dx * M_SQRT1_2 + dy * M_SQRT1_2) * M_SQRT1_2;
      point.first = halfW + rx;
      point.second = halfW + ry;
    }
  }

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
              (Config::options.lissajous.enable_splines ? 10 : 1);

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
    // Select the window for rendering
    SDLWindow::selectWindow(window->sdlWindow);

    // Render simple lines if phosphor is disabled
    if (DSP::pitchDB > Config::options.audio.silence_threshold)
      Graphics::drawLines(window, points, Theme::colors.color);
  }
}

} // namespace Lissajous