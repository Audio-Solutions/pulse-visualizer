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

// Lissajous visualizer with spline helpers
// Catmull-Rom spline interpolation
std::vector<std::pair<float, float>>
LissajousVisualizer::generateCatmullRomSpline(const std::vector<std::pair<float, float>>& controlPoints,
                                              int segmentsPerSegment) {
  // Not enough points for spline
  if (controlPoints.size() < 4) {
    return controlPoints;
  }

  std::vector<std::pair<float, float>> splinePoints;

  // Catmull-Rom matrix coefficients using configurable tension
  const float t = Config::getFloat("lissajous.phosphor_tension");

  for (size_t i = 1; i < controlPoints.size() - 2; ++i) {
    const auto& p0 = controlPoints[i - 1];
    const auto& p1 = controlPoints[i];
    const auto& p2 = controlPoints[i + 1];
    const auto& p3 = controlPoints[i + 2];

    // Generate segments between p1 and p2
    for (int j = 0; j <= segmentsPerSegment; ++j) {
      float u = static_cast<float>(j) / segmentsPerSegment;
      float u2 = u * u;
      float u3 = u2 * u;

      // Catmull-Rom blending functions
      float b0 = -t * u3 + 2.0f * t * u2 - t * u;
      float b1 = (2.0f - t) * u3 + (t - 3.0f) * u2 + 1.0f;
      float b2 = (t - 2.0f) * u3 + (3.0f - 2.0f * t) * u2 + t * u;
      float b3 = t * u3 - t * u2;

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
  // width is set by splitter/layout logic
  // Set up the viewport for the Lissajous visualizer
  Graphics::setupViewport(position, 0, width, width, audioData.windowHeight);

  // --- Local static config cache ---
  static float phosphor_tension;
  static bool enable_splines;
  static int spline_segments;
  static bool enable_phosphor;
  static int phosphor_spline_density;
  static float phosphor_max_beam_speed;
  static float phosphor_persistence_time;
  static float phosphor_decay_rate;
  static float phosphor_intensity_scale;
  static size_t lastConfigVersion;

  if (Config::getVersion() != lastConfigVersion) {
    phosphor_tension = Config::getFloat("lissajous.phosphor_tension");
    enable_splines = Config::getBool("lissajous.enable_splines");
    spline_segments = Config::getInt("lissajous.spline_segments");
    enable_phosphor = Config::getBool("lissajous.enable_phosphor");
    phosphor_spline_density = Config::getInt("lissajous.phosphor_spline_density");
    phosphor_max_beam_speed = Config::getFloat("lissajous.phosphor_max_beam_speed");
    phosphor_persistence_time = Config::getFloat("lissajous.phosphor_persistence_time");
    phosphor_decay_rate = Config::getFloat("lissajous.phosphor_decay_rate");
    phosphor_intensity_scale = Config::getFloat("lissajous.phosphor_intensity_scale");
    lastConfigVersion = Config::getVersion();
  }

  if (!audioData.lissajousPoints.empty() && audioData.hasValidPeak) {
    std::vector<std::pair<float, float>> points;
    points.reserve(audioData.lissajousPoints.size());

    // Convert points to screen coordinates (fixed scaling)
    for (const auto& point : audioData.lissajousPoints) {
      float x = position + (point.first + 1.0f) * width / 2;
      float y = (point.second + 1.0f) * width / 2;
      points.push_back({x, y});
    }

    if (enable_phosphor) {
      // Generate high-density spline points for phosphor simulation
      std::vector<std::pair<float, float>> densePath;

      if (enable_splines && points.size() >= 4) {
        // Calculate segments per original segment based on density
        int segmentsPerSegment = std::max(10, phosphor_spline_density / static_cast<int>(points.size()));
        densePath = generateCatmullRomSpline(points, segmentsPerSegment);
      } else if (points.size() >= 2) {
        // Linear interpolation with high density for non-spline mode
        densePath.reserve(phosphor_spline_density);
        for (size_t i = 1; i < points.size(); ++i) {
          const auto& p1 = points[i - 1];
          const auto& p2 = points[i];

          // Interpolate with high density
          int segmentPoints = std::max(1, phosphor_spline_density / static_cast<int>(points.size()));
          for (int j = 0; j < segmentPoints; ++j) {
            float t = static_cast<float>(j) / segmentPoints;
            float x = p1.first + t * (p2.first - p1.first);
            float y = p1.second + t * (p2.second - p1.second);
            densePath.push_back({x, y});
          }
        }
      }

      if (!densePath.empty()) {
        // Pre-calculate original point distances for intensity mapping
        std::vector<float> originalDistances;
        if (enable_splines && points.size() >= 4) {
          originalDistances.reserve(points.size() - 1);
          for (size_t i = 1; i < points.size(); ++i) {
            float dx = points[i].first - points[i - 1].first;
            float dy = points[i].second - points[i - 1].second;
            float dist = sqrtf(dx * dx + dy * dy) / width; // Normalized distance
            originalDistances.push_back(dist);
          }
        }

        // Enable additive blending for phosphor effect
        glEnable(GL_BLEND);
        if (Theme::ThemeManager::invertNoiseBrightness()) {
          glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        } else {
          glBlendFunc(GL_ONE, GL_ONE);
        }

        // Get the Lissajous color
        const auto& lissajousColor = Theme::ThemeManager::getLissajous();

        // Render lines based on accumulated intensity
        std::vector<std::pair<float, float>> renderPath;
        renderPath.reserve(densePath.size());

        for (size_t i = 1; i < densePath.size(); ++i) {
          const auto& p1 = densePath[i - 1];
          const auto& p2 = densePath[i];

          // Check beam speed for this segment before rendering
          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segmentLength = sqrtf(dx * dx + dy * dy);
          float beamSpeed = segmentLength / width; // Normalized to screen width

          // Scale max beam speed with screen size for consistent behavior
          // Use a reference size for normalization
          const float referenceWidth = 200.0f;
          float sizeScale = static_cast<float>(width) / referenceWidth;
          float scaledMaxBeamSpeed = phosphor_max_beam_speed * sizeScale;

          // Skip segments where beam moves too fast (like real oscilloscope)
          if (beamSpeed > scaledMaxBeamSpeed) {
            continue;
          }

          // Calculate intensity based on local point density around this segment
          // This gives more consistent brightness along smooth curves
          float intensity = phosphor_intensity_scale;

          // Scale intensity based on spline density
          if (enable_splines && points.size() >= 4) {
            float splineDensityRatio = static_cast<float>(densePath.size()) / static_cast<float>(points.size());
            float densityScale = 1.0f / sqrtf(splineDensityRatio);
            intensity *= densityScale;

            // Map this spline segment back to original curve region for speed-based intensity
            float normalizedSplinePos = static_cast<float>(i) / densePath.size();
            size_t originalSegmentIndex = static_cast<size_t>(normalizedSplinePos * (originalDistances.size() - 1));
            originalSegmentIndex = std::min(originalSegmentIndex, originalDistances.size() - 1);

            // Use the original curve speed for intensity modulation
            float originalSpeed = originalDistances[originalSegmentIndex];
            float speedIntensity = 1.0f / (1.0f + originalSpeed * 50.0f); // Higher multiplier for more effect
            intensity *= speedIntensity;
          } else {
            // For non-spline mode, use direct segment speed
            float speedIntensity = 1.0f / (1.0f + beamSpeed * 10.0f);
            intensity *= speedIntensity;
          }

          // Apply phosphor persistence decay based on position along path
          float normalizedPosition = static_cast<float>(i) / densePath.size();
          float timeFactor = expf(-phosphor_decay_rate * normalizedPosition * phosphor_persistence_time);
          intensity *= timeFactor;

          // Clamp intensity
          intensity = std::min(intensity, 1.0f);

          if (intensity > 0.001f) {
            // Calculate color with intensity
            float color[4] = {lissajousColor.r * intensity, lissajousColor.g * intensity, lissajousColor.b * intensity,
                              intensity};

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
      if (enable_splines && points.size() >= 4) {
        displayPoints = generateCatmullRomSpline(points, spline_segments);
      }

      // Draw the Lissajous curve as antialiased lines
      const auto& lissajousColor = Theme::ThemeManager::getLissajous();
      float color[4] = {lissajousColor.r, lissajousColor.g, lissajousColor.b, 1.0f};
      Graphics::drawAntialiasedLines(displayPoints, color, 2.0f);
    }
  }
}

int LissajousVisualizer::getPosition() const { return position; }
void LissajousVisualizer::setPosition(int pos) { position = pos; }
int LissajousVisualizer::getWidth() const { return width; }
void LissajousVisualizer::setWidth(int w) { width = w; }
int LissajousVisualizer::getRightEdge(const AudioData&) const { return position + width; }