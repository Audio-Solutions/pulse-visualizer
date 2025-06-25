#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// Static working buffers and phosphor context
std::vector<std::pair<float, float>> LissajousVisualizer::points;
std::vector<std::pair<float, float>> LissajousVisualizer::densePath;
std::vector<float> LissajousVisualizer::originalDistances;
static Graphics::Phosphor::PhosphorContext* lissajousPhosphorContext = nullptr;

// Static variable to track last read position
static size_t lastReadPos = 0;
static bool firstRun = true;

// HSV conversion helpers (local to this translation unit)
namespace {
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
  float max = std::max({r, g, b});
  float min = std::min({r, g, b});
  float delta = max - min;
  v = max;
  s = (max > 0.0f) ? (delta / max) : 0.0f;
  if (delta == 0.0f) {
    h = 0.0f;
  } else if (max == r) {
    h = 60.0f * fmodf(((g - b) / delta), 6.0f);
  } else if (max == g) {
    h = 60.0f * (((b - r) / delta) + 2.0f);
  } else {
    h = 60.0f * (((r - g) / delta) + 4.0f);
  }
  if (h < 0.0f)
    h += 360.0f;
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  float rp = 0.0f, gp = 0.0f, bp = 0.0f;
  if (h >= 0.0f && h < 60.0f) {
    rp = c;
    gp = x;
  } else if (h >= 60.0f && h < 120.0f) {
    rp = x;
    gp = c;
  } else if (h >= 120.0f && h < 180.0f) {
    gp = c;
    bp = x;
  } else if (h >= 180.0f && h < 240.0f) {
    gp = x;
    bp = c;
  } else if (h >= 240.0f && h < 300.0f) {
    rp = x;
    bp = c;
  } else {
    rp = c;
    bp = x;
  }
  r = rp + m;
  g = gp + m;
  b = bp + m;
}

} // namespace

void LissajousVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the Lissajous visualizer
  Graphics::setupViewport(position, 0, width, width, audioData.windowHeight);

  const auto& lis = Config::values().lissajous;
  const auto& colors = Theme::ThemeManager::colors();

  // Initialize phosphor context if needed
  if (lis.enable_phosphor && !lissajousPhosphorContext) {
    lissajousPhosphorContext = Graphics::Phosphor::createPhosphorContext("lissajous");
  } else if (!lis.enable_phosphor && lissajousPhosphorContext) {
    Graphics::Phosphor::destroyPhosphorContext(lissajousPhosphorContext);
    lissajousPhosphorContext = nullptr;
  }

  if (audioData.hasValidPeak && !audioData.bufferMid.empty() && !audioData.bufferSide.empty()) {
    // Calculate how much new data is available
    size_t currentWritePos = audioData.writePos;
    size_t newDataCount = 0;

    if (firstRun) {
      // On first run, read the most recent lis.max_points samples
      newDataCount = lis.max_points;
      lastReadPos = (currentWritePos + audioData.bufferSize - lis.max_points) % audioData.bufferSize;
      firstRun = false;
    } else {
      // Calculate new samples since last read
      if (currentWritePos >= lastReadPos) {
        newDataCount = currentWritePos - lastReadPos;
      } else {
        // Handle wrap-around
        newDataCount = (audioData.bufferSize - lastReadPos) + currentWritePos;
      }

      // Limit to maximum of lis.max_points to avoid excessive data
      newDataCount = std::min(newDataCount, static_cast<size_t>(lis.max_points));
    }

    // If no new data, just draw the current result
    if (newDataCount == 0) {
      if (lis.enable_phosphor && lissajousPhosphorContext) {
        // Draw current phosphor state without processing (just colormap existing energy)
        GLuint phosphorTexture = Graphics::Phosphor::drawCurrentPhosphorState(lissajousPhosphorContext, width, width,
                                                                              colors.background, colors.lissajous, lis.enable_phosphor_grain);

        if (phosphorTexture) {
          Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, width);
        }
      } else {
        // Standard non-phosphor rendering
        std::vector<std::pair<float, float>> displayPoints = points;
        if (lis.enable_splines && points.size() >= 4) {
          displayPoints = Graphics::generateCatmullRomSpline(points, lis.spline_segments, lis.phosphor_tension);
        }

        // Draw the Lissajous curve as antialiased lines
        Graphics::drawAntialiasedLines(displayPoints, colors.lissajous, 2.0f);
      }
      return;
    }

    // Resize cached vector to exact size needed
    points.resize(newDataCount);

    // Pre-compute scaling factors
    const float halfWidth = width * 0.5f;
    const float offsetX = halfWidth;

    // Read from circular buffers and reconstruct left/right channels
    for (size_t i = 0; i < newDataCount; ++i) {
      // Calculate position in circular buffer - read from lastReadPos forward
      size_t readPos = (lastReadPos + i) % audioData.bufferSize;

      // Reconstruct left/right from mid/side
      float mid = audioData.bufferMid[readPos];
      float side = audioData.bufferSide[readPos];
      float sampleLeft = mid + side;
      float sampleRight = mid - side;

      // Convert to screen coordinates (fixed scaling)
      float x = offsetX + sampleLeft * halfWidth;
      float y = (sampleRight + 1.0f) * halfWidth;
      points[i] = {x, y};
    }

    // Update lastReadPos for next frame
    lastReadPos = currentWritePos;

    if (lis.enable_phosphor && lissajousPhosphorContext) {
      // Generate high-density spline points for phosphor simulation
      if (lis.enable_splines && points.size() >= 4) {
        densePath = Graphics::generateCatmullRomSpline(points, lis.phosphor_spline_density, lis.phosphor_tension);
      } else if (points.size() >= 2) {
        // Linear interpolation with high density for non-spline mode
        int totalSegments = std::max(1, lis.phosphor_spline_density / static_cast<int>(points.size()));
        densePath.resize(points.size() * totalSegments);

        for (size_t i = 1; i < points.size(); ++i) {
          const auto& p1 = points[i - 1];
          const auto& p2 = points[i];

          // Interpolate with high density
          const float segmentScale = 1.0f / totalSegments;
          for (int j = 0; j < totalSegments; ++j) {
            float t = static_cast<float>(j) * segmentScale;
            float x = p1.first + t * (p2.first - p1.first);
            float y = p1.second + t * (p2.second - p1.second);
            densePath[i * totalSegments + j] = {x, y};
          }
        }
      }

      if (!densePath.empty()) {
        // Prepare intensity and dwell time data
        std::vector<float> intensityLinear;
        std::vector<float> dwellTimes;
        intensityLinear.reserve(densePath.size());
        dwellTimes.reserve(densePath.size());

        for (size_t i = 1; i < densePath.size(); ++i) {
          const auto& p1 = densePath[i - 1];
          const auto& p2 = densePath[i];

          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segLen = sqrtf(dx * dx + dy * dy);
          float deltaT = (1.0f / audioData.sampleRate);
          float totalEnergy = lis.phosphor_beam_energy * deltaT / segLen / lis.phosphor_spline_density;

          // Store linear energy and dwell time
          intensityLinear.push_back(totalEnergy);
          dwellTimes.push_back(deltaT);
        }

        // Render phosphor splines using high-level interface
        GLuint phosphorTexture = Graphics::Phosphor::renderPhosphorSplines(
            lissajousPhosphorContext, densePath, intensityLinear, dwellTimes, width, width, audioData.dt, 1.0f,
            colors.background, colors.lissajous, lis.phosphor_beam_size, lis.phosphor_line_blur_spread,
            lis.phosphor_line_width, lis.phosphor_decay_slow, lis.phosphor_decay_fast, lis.phosphor_age_threshold,
            lis.phosphor_range_factor, lis.enable_phosphor_grain);

        // Draw the phosphor result
        if (phosphorTexture) {
          Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, width);
        }
      }
    } else {
      // Standard non-phosphor rendering
      std::vector<std::pair<float, float>> displayPoints = points;
      if (lis.enable_splines && points.size() >= 4) {
        displayPoints = Graphics::generateCatmullRomSpline(points, lis.spline_segments, lis.phosphor_tension);
      }

      // Draw the Lissajous curve as antialiased lines
      Graphics::drawAntialiasedLines(displayPoints, colors.lissajous, 2.0f);
    }
  }
}

int LissajousVisualizer::getPosition() const { return position; }
void LissajousVisualizer::setPosition(int pos) { position = pos; }
int LissajousVisualizer::getWidth() const { return width; }
void LissajousVisualizer::setWidth(int w) { width = w; }
int LissajousVisualizer::getRightEdge(const AudioData&) const { return position + width; }