#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Static working buffers
std::vector<std::pair<float, float>> LissajousVisualizer::points;
std::vector<std::pair<float, float>> LissajousVisualizer::densePath;
std::vector<float> LissajousVisualizer::originalDistances;

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

  if (audioData.hasValidPeak && !audioData.bufferMid.empty() && !audioData.bufferSide.empty()) {
    // Resize cached vector to exact size needed
    points.resize(lis.points);

    // Pre-compute scaling factors
    const float halfWidth = width * 0.5f;
    const float offsetX = halfWidth;

    // Read from circular buffers and reconstruct left/right channels
    for (size_t i = 0; i < lis.points; ++i) {
      // Calculate position in circular buffer
      size_t readPos = (audioData.writePos + audioData.bufferSize - lis.points + i) % audioData.bufferSize;

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

    if (lis.enable_phosphor) {
      // Generate high-density spline points for phosphor simulation
      if (lis.enable_splines && points.size() >= 4) {
        // Calculate segments per original segment based on density
        int segmentsPerSegment = std::max(10, lis.phosphor_spline_density / static_cast<int>(points.size()));
        densePath = generateCatmullRomSpline(points, segmentsPerSegment, lis.phosphor_tension);
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
        // Pre-calculate original point distances for intensity mapping
        if (lis.enable_splines && points.size() >= 4) {
          originalDistances.resize(points.size() - 1);
          const float invWidth = 1.0f / width;

          for (size_t i = 1; i < points.size(); ++i) {
            float dx = points[i].first - points[i - 1].first;
            float dy = points[i].second - points[i - 1].second;
            float dist = sqrtf(dx * dx + dy * dy) * invWidth;
            originalDistances[i - 1] = dist;
          }
        }

        // Enable additive blending for phosphor effect
        glEnable(GL_BLEND);
        if (Theme::ThemeManager::invertNoiseBrightness()) {
          glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        } else {
          glBlendFunc(GL_ONE, GL_ONE);
        }

        // Pre-compute constants for phosphor calculations
        const float referenceWidth = 200.0f;
        const float sizeScale = static_cast<float>(width) / referenceWidth;
        const float scaledMaxBeamSpeed = lis.phosphor_max_beam_speed * sizeScale;
        const float invWidth = 1.0f / width;
        const float invDensePathSize = 1.0f / densePath.size();
        const float decayTimeProduct = lis.phosphor_decay_rate * lis.phosphor_persistence_time;

        for (size_t i = 1; i < densePath.size(); ++i) {
          const auto& p1 = densePath[i - 1];
          const auto& p2 = densePath[i];

          // Check beam speed for this segment before rendering
          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segmentLength = sqrtf(dx * dx + dy * dy);
          float beamSpeed = segmentLength * invWidth;

          // Skip segments where beam moves too fast
          if (beamSpeed > scaledMaxBeamSpeed) {
            continue;
          }

          // Calculate intensity based on local point density around this segment
          float intensity = lis.phosphor_intensity_scale;

          // Scale intensity based on spline density
          if (lis.enable_splines && points.size() >= 4) {
            float splineDensityRatio = static_cast<float>(densePath.size()) / static_cast<float>(points.size());
            float densityScale = 1.0f / sqrtf(splineDensityRatio);
            intensity *= densityScale;

            // Map this spline segment back to original curve region for speed-based intensity
            float normalizedSplinePos = static_cast<float>(i) * invDensePathSize;
            size_t originalSegmentIndex = static_cast<size_t>(normalizedSplinePos * (originalDistances.size() - 1));
            originalSegmentIndex = std::min(originalSegmentIndex, originalDistances.size() - 1);

            // Use the original curve speed for intensity modulation
            float originalSpeed = originalDistances[originalSegmentIndex];
            float speedIntensity = 1.0f / (1.0f + originalSpeed * 50.0f);
            intensity *= speedIntensity;
          } else {
            // For non-spline mode, use direct segment speed
            float speedIntensity = 1.0f / (1.0f + beamSpeed * 10.0f);
            intensity *= speedIntensity;
          }

          // Apply phosphor persistence decay based on position along path
          float normalizedPosition = static_cast<float>(i) * invDensePathSize;
          float timeFactor = expf(-decayTimeProduct * normalizedPosition);
          intensity *= timeFactor;

          // Clamp intensity
          intensity = std::min(intensity, 1.0f);

          if (intensity > 0.001f) {
            // Calculate color with intensity
            float color[4] = {colors.lissajous[0] * intensity, colors.lissajous[1] * intensity,
                              colors.lissajous[2] * intensity, intensity};

            // Draw antialiased line segment
            Graphics::drawAntialiasedLine(p1.first, p1.second, p2.first, p2.second, color, 2.0f);
          }
        }

        // Reset blending
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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