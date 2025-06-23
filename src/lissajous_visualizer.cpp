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

// Catmull-Rom spline interpolation
std::vector<std::pair<float, float>>
LissajousVisualizer::generateCatmullRomSpline(const std::vector<std::pair<float, float>>& controlPoints,
                                              int segmentsPerSegment, float tension) {
  // Not enough points for spline
  if (controlPoints.size() < 4) {
    return controlPoints;
  }

  std::vector<std::pair<float, float>> splinePoints;
  splinePoints.reserve((controlPoints.size() - 3) * (segmentsPerSegment + 1));

  // Pre-compute tension value and segment scale
  const float segmentScale = 1.0f / segmentsPerSegment;

  for (size_t i = 1; i < controlPoints.size() - 2; ++i) {
    const auto& p0 = controlPoints[i - 1];
    const auto& p1 = controlPoints[i];
    const auto& p2 = controlPoints[i + 1];
    const auto& p3 = controlPoints[i + 2];

    // Generate segments between p1 and p2
    for (int j = 0; j <= segmentsPerSegment; ++j) {
      float u = static_cast<float>(j) * segmentScale;
      float u2 = u * u;
      float u3 = u2 * u;

      // Catmull-Rom blending functions
      float b0 = -tension * u3 + 2.0f * tension * u2 - tension * u;
      float b1 = (2.0f - tension) * u3 + (tension - 3.0f) * u2 + 1.0f;
      float b2 = (tension - 2.0f) * u3 + (3.0f - 2.0f * tension) * u2 + tension * u;
      float b3 = tension * u3 - tension * u2;

      float x = b0 * p0.first + b1 * p1.first + b2 * p2.first + b3 * p3.first;
      float y = b0 * p0.second + b1 * p1.second + b2 * p2.second + b3 * p3.second;

      splinePoints.push_back({x, y});
    }
  }

  return splinePoints;
}

// Calculate cumulative distance along a path of points
std::vector<float>
LissajousVisualizer::calculateCumulativeDistances(const std::vector<std::pair<float, float>>& points) {
  std::vector<float> distances;
  distances.reserve(points.size());

  float cumulativeDistance = 0.0f;
  distances.push_back(0.0f);

  for (size_t i = 1; i < points.size(); ++i) {
    const auto& p1 = points[i - 1];
    const auto& p2 = points[i];
    float dx = p2.first - p1.first;
    float dy = p2.second - p1.second;
    float segmentDistance = sqrtf(dx * dx + dy * dy);
    cumulativeDistance += segmentDistance;
    distances.push_back(cumulativeDistance);
  }

  return distances;
}

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
        GLuint phosphorTexture = Graphics::Phosphor::drawCurrentPhosphorState(
            lissajousPhosphorContext, width, width, colors.background, colors.lissajous, colors.text,
            lis.phosphor_db_lower_bound, lis.phosphor_db_mid_point, lis.phosphor_db_upper_bound);

        if (phosphorTexture) {
          Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, width);
        }
      } else {
        // Standard non-phosphor rendering
        std::vector<std::pair<float, float>> displayPoints = points;
        if (lis.enable_splines && points.size() >= 4) {
          displayPoints = generateCatmullRomSpline(points, lis.spline_segments, lis.phosphor_tension);
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
        densePath = generateCatmullRomSpline(points, lis.phosphor_spline_density, lis.phosphor_tension);
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

        // Calculate beam parameters for lissajous
        const float invWidth = 1.0f / width;

        for (size_t i = 1; i < densePath.size(); ++i) {
          const auto& p1 = densePath[i - 1];
          const auto& p2 = densePath[i];

          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segLen = sqrtf(dx * dx + dy * dy);
          float beamSpeed = segLen * invWidth * lis.phosphor_beam_speed_multiplier;

          // Calculate beam dwell time based on segment length and sample rate
          float dwelltime = segLen / (static_cast<float>(width) * audioData.sampleRate / newDataCount);

          // Much more aggressive speed-based intensity reduction for realistic CRT behavior
          // Very fast traces should nearly vanish
          float speedFactor = 1.0f / (1.0f + beamSpeed * 500.0f);
          float speedSquaredFactor = speedFactor * speedFactor;

          // Apply exponential falloff for extremely fast movements
          float exponentialSpeedFactor = expf(-beamSpeed * 50.0f);

          // Combine factors for realistic "vanishing" effect
          float combinedSpeedFactor = speedSquaredFactor * exponentialSpeedFactor;

          float intensity = combinedSpeedFactor;

          // Basic beam energy with much more dramatic speed dependency
          float beamEnergy = lis.phosphor_beam_energy * intensity;
          float totalSegmentEnergy = beamEnergy * dwelltime;

          // Store linear energy and dwell time
          intensityLinear.push_back(totalSegmentEnergy);
          dwellTimes.push_back(dwelltime);
        }

        // Calculate frame time for decay
        float deltaTime = static_cast<float>(newDataCount) / audioData.sampleRate;
        float pixelWidth = 1.0f;

        // Render phosphor splines using high-level interface
        GLuint phosphorTexture = Graphics::Phosphor::renderPhosphorSplines(
            lissajousPhosphorContext, densePath, intensityLinear, dwellTimes, width, width, deltaTime, pixelWidth,
            colors.background, colors.lissajous, colors.text, lis.phosphor_beam_size, lis.phosphor_db_lower_bound,
            lis.phosphor_db_mid_point, lis.phosphor_db_upper_bound, lis.phosphor_decay_constant,
            lis.phosphor_line_blur_spread, lis.phosphor_line_width);

        // Draw the phosphor result
        if (phosphorTexture) {
          Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, width);
        }
      }
    } else {
      // Standard non-phosphor rendering
      std::vector<std::pair<float, float>> displayPoints = points;
      if (lis.enable_splines && points.size() >= 4) {
        displayPoints = generateCatmullRomSpline(points, lis.spline_segments, lis.phosphor_tension);
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