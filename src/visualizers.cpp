#include "visualizers.hpp"

#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Define to use raw signal instead of bandpassed signal for oscilloscope display (bandpassed signal is for debugging)
#define SCOPE_USE_RAW_SIGNAL

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

void OscilloscopeVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the oscilloscope
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // --- Local static config cache ---
  static float amplitude_scale;
  static std::string gradient_mode;
  static bool enable_phosphor;
  static int phosphor_spline_density;
  static float phosphor_max_beam_speed;
  static float phosphor_intensity_scale;
  static size_t lastConfigVersion;

  if (Config::getVersion() != lastConfigVersion) {
    amplitude_scale = Config::getFloat("oscilloscope.amplitude_scale");
    gradient_mode = Config::getString("oscilloscope.gradient_mode");
    enable_phosphor = Config::getBool("oscilloscope.enable_phosphor");
    phosphor_spline_density = Config::getInt("oscilloscope.phosphor_spline_density");
    phosphor_max_beam_speed = Config::getFloat("oscilloscope.phosphor_max_beam_speed");
    phosphor_intensity_scale = Config::getFloat("oscilloscope.phosphor_intensity_scale");
    lastConfigVersion = Config::getVersion();
  }

  if (audioData.availableSamples > 0) {
    // Calculate the target position for phase correction
    size_t targetPos = (audioData.writePos + audioData.bufferSize - audioData.displaySamples) % audioData.bufferSize;

    // Use pre-computed bandpassed data for zero crossing detection
    size_t searchRange = static_cast<size_t>(audioData.samplesPerCycle);
    size_t zeroCrossPos = targetPos;
    if (!audioData.bandpassedMid.empty() && audioData.pitchConfidence > 0.5f) {
      // Search backward for the nearest positive zero crossing in bandpassed signal
      for (size_t i = 1; i < searchRange && i < audioData.bandpassedMid.size(); i++) {
        size_t pos = (targetPos + audioData.bufferSize - i) % audioData.bufferSize;
        size_t prevPos = (pos + audioData.bufferSize - 1) % audioData.bufferSize;
        float currentSample = audioData.bandpassedMid[pos];
        float prevSample = audioData.bandpassedMid[prevPos];
        if (prevSample < 0.0f && currentSample >= 0.0f) {
          zeroCrossPos = pos;
          break;
        }
      }
    }

    // Calculate phase offset to align with peak (3/4 cycle after zero crossing)
    float threeQuarterCycle = audioData.samplesPerCycle * 0.75f;
    float phaseOffset = static_cast<float>((targetPos + audioData.bufferSize - zeroCrossPos) % audioData.bufferSize) +
                        threeQuarterCycle;

    // Calculate the start position for reading samples, adjusted for phase
    size_t startPos = (audioData.writePos + audioData.bufferSize - audioData.displaySamples) % audioData.bufferSize;
    if (audioData.samplesPerCycle > 0.0f && audioData.pitchConfidence > 0.5f) {
      // Move backward by the phase offset
      startPos = (startPos + audioData.bufferSize - static_cast<size_t>(phaseOffset)) % audioData.bufferSize;
    }

    // Create waveform points
    std::vector<std::pair<float, float>> waveformPoints;
    waveformPoints.reserve(audioData.displaySamples);

    float amplitudeScale = audioData.windowHeight * amplitude_scale;
    for (size_t i = 0; i < audioData.displaySamples; i++) {
      size_t pos = (startPos + i) % audioData.bufferSize;
      float x = (static_cast<float>(i) * width) / audioData.displaySamples;
#ifdef SCOPE_USE_RAW_SIGNAL
      float y = audioData.windowHeight / 2 + audioData.bufferMid[pos] * amplitudeScale;
#else
      float y = audioData.windowHeight / 2 + audioData.bandpassedMid[pos] * amplitudeScale;
#endif
      waveformPoints.push_back({x, y});
    }

    // Create fill color by blending oscilloscope color with background
    const auto& oscilloscopeColor = Theme::ThemeManager::getOscilloscope();
    const auto& backgroundColor = Theme::ThemeManager::getBackground();

    // Draw the waveform line using OpenGL only if there's a valid peak
    if (audioData.hasValidPeak) {
      if (enable_phosphor) {
        // First draw the gradient background (same as non-phosphor mode)
        if (gradient_mode == "horizontal") {
          // Horizontal gradient: intensity based on distance from center line outward
          // Draw filled area using triangles from zero line to waveform
          glBegin(GL_TRIANGLES);
          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            const auto& p1 = waveformPoints[i - 1];
            const auto& p2 = waveformPoints[i];

            // Calculate gradient for p1
            float distance1 = std::abs(p1.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distance1 = std::min(distance1, 1.0f);
            float gradientIntensity1 = distance1 * 0.3f; // Higher 30% intensity as its less noticeable
            float fillColor1[4] = {
                oscilloscopeColor.r * gradientIntensity1 + backgroundColor.r * (1.0f - gradientIntensity1),
                oscilloscopeColor.g * gradientIntensity1 + backgroundColor.g * (1.0f - gradientIntensity1),
                oscilloscopeColor.b * gradientIntensity1 + backgroundColor.b * (1.0f - gradientIntensity1),
                oscilloscopeColor.a * gradientIntensity1 + backgroundColor.a * (1.0f - gradientIntensity1)};

            // Calculate gradient for p2
            float distance2 = std::abs(p2.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distance2 = std::min(distance2, 1.0f);
            float gradientIntensity2 = distance2 * 0.3f;
            float fillColor2[4] = {
                oscilloscopeColor.r * gradientIntensity2 + backgroundColor.r * (1.0f - gradientIntensity2),
                oscilloscopeColor.g * gradientIntensity2 + backgroundColor.g * (1.0f - gradientIntensity2),
                oscilloscopeColor.b * gradientIntensity2 + backgroundColor.b * (1.0f - gradientIntensity2),
                oscilloscopeColor.a * gradientIntensity2 + backgroundColor.a * (1.0f - gradientIntensity2)};

            // Center color (always background)
            float centerColor[4] = {backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a};

            // Triangle 1: (p1.x, center) -> (p1.x, p1.y) -> (p2.x, center)
            glColor4fv(centerColor);
            glVertex2f(p1.first, audioData.windowHeight / 2);
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(centerColor);
            glVertex2f(p2.first, audioData.windowHeight / 2);

            // Triangle 2: (p1.x, p1.y) -> (p2.x, p2.y) -> (p2.x, center)
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(fillColor2);
            glVertex2f(p2.first, p2.second);
            glColor4fv(centerColor);
            glVertex2f(p2.first, audioData.windowHeight / 2);
          }
          glEnd();
        } else if (gradient_mode == "vertical") {
          // Vertical gradient: intensity based on distance from center line
          glBegin(GL_QUAD_STRIP);
          for (const auto& point : waveformPoints) {
            // Calculate distance from center line (0.0 = at center, 1.0 = at edge)
            float distanceFromCenter =
                std::abs(point.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distanceFromCenter = std::min(distanceFromCenter, 1.0f);

            // Create gradient color based on distance
            float gradientIntensity = distanceFromCenter * 0.15f; // Max 15% oscilloscope color
            float fillColor[4] = {
                oscilloscopeColor.r * gradientIntensity + backgroundColor.r * (1.0f - gradientIntensity),
                oscilloscopeColor.g * gradientIntensity + backgroundColor.g * (1.0f - gradientIntensity),
                oscilloscopeColor.b * gradientIntensity + backgroundColor.b * (1.0f - gradientIntensity),
                oscilloscopeColor.a * gradientIntensity + backgroundColor.a * (1.0f - gradientIntensity)};

            glColor4fv(fillColor);
            glVertex2f(point.first, point.second);
            glVertex2f(point.first, audioData.windowHeight / 2); // Zero line
          }
          glEnd();
        }

        // Now draw the phosphor effect on top of the gradient
        // Generate high-density points for phosphor simulation
        std::vector<std::pair<float, float>> densePath;
        densePath.reserve(phosphor_spline_density);

        for (size_t i = 1; i < waveformPoints.size(); ++i) {
          const auto& p1 = waveformPoints[i - 1];
          const auto& p2 = waveformPoints[i];

          // Interpolate with high density
          int segmentPoints = std::max(1, phosphor_spline_density / static_cast<int>(waveformPoints.size()));
          for (int j = 0; j < segmentPoints; ++j) {
            float t = static_cast<float>(j) / segmentPoints;
            float x = p1.first + t * (p2.first - p1.first);
            float y = p1.second + t * (p2.second - p1.second);
            densePath.push_back({x, y});
          }
        }

        if (!densePath.empty()) {
          // Pre-calculate original point distances for intensity mapping
          std::vector<float> originalDistances;
          originalDistances.reserve(waveformPoints.size() - 1);
          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            float dx = waveformPoints[i].first - waveformPoints[i - 1].first;
            float dy = waveformPoints[i].second - waveformPoints[i - 1].second;
            float dist = sqrtf(dx * dx + dy * dy) / width; // Normalized distance
            originalDistances.push_back(dist);
          }

          // Enable additive blending for phosphor effect
          glEnable(GL_BLEND);
          if (Theme::ThemeManager::invertNoiseBrightness()) {
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
          } else {
            glBlendFunc(GL_ONE, GL_ONE);
          }

          // Get the oscilloscope color
          const auto& oscilloscopeColor = Theme::ThemeManager::getOscilloscope();

          for (size_t i = 1; i < densePath.size(); ++i) {
            const auto& p1 = densePath[i - 1];
            const auto& p2 = densePath[i];

            // Check beam speed for this segment before rendering
            float dx = p2.first - p1.first;
            float dy = p2.second - p1.second;
            float segmentLength = sqrtf(dx * dx + dy * dy);
            float beamSpeed = segmentLength / width; // Normalized to screen width

            // Scale max beam speed with screen size for consistent behavior
            const float referenceWidth = 200.0f;
            float sizeScale = static_cast<float>(width) / referenceWidth;
            float scaledMaxBeamSpeed = phosphor_max_beam_speed * sizeScale;

            // Skip segments where beam moves too fast (like real oscilloscope)
            if (beamSpeed > scaledMaxBeamSpeed) {
              continue;
            }

            // Calculate intensity based on local point density
            float intensity = phosphor_intensity_scale;

            // Scale intensity based on density
            float splineDensityRatio = static_cast<float>(densePath.size()) / static_cast<float>(waveformPoints.size());
            float densityScale = 1.0f / sqrtf(splineDensityRatio);
            intensity *= densityScale;

            // Map this segment back to original curve region for speed-based intensity
            float normalizedPos = static_cast<float>(i) / densePath.size();
            size_t originalSegmentIndex = static_cast<size_t>(normalizedPos * (originalDistances.size() - 1));
            originalSegmentIndex = std::min(originalSegmentIndex, originalDistances.size() - 1);

            // Use the original curve speed for intensity modulation
            // For oscilloscope, this represents how fast the signal is changing (derivative)
            float originalSpeed = originalDistances[originalSegmentIndex];
            float speedIntensity = 1.0f / (1.0f + originalSpeed * 50.0f);
            intensity *= speedIntensity;

            // Clamp intensity
            intensity = std::min(intensity, 1.0f);

            if (intensity > 0.001f) {
              // Calculate color with intensity
              float color[4] = {oscilloscopeColor.r * intensity, oscilloscopeColor.g * intensity,
                                oscilloscopeColor.b * intensity, intensity};

              // Draw antialiased line segment
              Graphics::drawAntialiasedLine(p1.first, p1.second, p2.first, p2.second, color, 2.0f);
            }
          }

          // Reset blending
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
      } else {
        // Standard non-phosphor rendering
        // Handle different gradient modes
        if (gradient_mode == "horizontal") {
          // Horizontal gradient: intensity based on distance from center line outward
          // Draw filled area using triangles from zero line to waveform
          glBegin(GL_TRIANGLES);
          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            const auto& p1 = waveformPoints[i - 1];
            const auto& p2 = waveformPoints[i];

            // Calculate gradient for p1
            float distance1 = std::abs(p1.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distance1 = std::min(distance1, 1.0f);
            float gradientIntensity1 = distance1 * 0.3f; // Higher 30% intensity as its less noticeable
            float fillColor1[4] = {
                oscilloscopeColor.r * gradientIntensity1 + backgroundColor.r * (1.0f - gradientIntensity1),
                oscilloscopeColor.g * gradientIntensity1 + backgroundColor.g * (1.0f - gradientIntensity1),
                oscilloscopeColor.b * gradientIntensity1 + backgroundColor.b * (1.0f - gradientIntensity1),
                oscilloscopeColor.a * gradientIntensity1 + backgroundColor.a * (1.0f - gradientIntensity1)};

            // Calculate gradient for p2
            float distance2 = std::abs(p2.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distance2 = std::min(distance2, 1.0f);
            float gradientIntensity2 = distance2 * 0.3f;
            float fillColor2[4] = {
                oscilloscopeColor.r * gradientIntensity2 + backgroundColor.r * (1.0f - gradientIntensity2),
                oscilloscopeColor.g * gradientIntensity2 + backgroundColor.g * (1.0f - gradientIntensity2),
                oscilloscopeColor.b * gradientIntensity2 + backgroundColor.b * (1.0f - gradientIntensity2),
                oscilloscopeColor.a * gradientIntensity2 + backgroundColor.a * (1.0f - gradientIntensity2)};

            // Center color (always background)
            float centerColor[4] = {backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a};

            // Triangle 1: (p1.x, center) -> (p1.x, p1.y) -> (p2.x, center)
            glColor4fv(centerColor);
            glVertex2f(p1.first, audioData.windowHeight / 2);
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(centerColor);
            glVertex2f(p2.first, audioData.windowHeight / 2);

            // Triangle 2: (p1.x, p1.y) -> (p2.x, p2.y) -> (p2.x, center)
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(fillColor2);
            glVertex2f(p2.first, p2.second);
            glColor4fv(centerColor);
            glVertex2f(p2.first, audioData.windowHeight / 2);
          }
          glEnd();
        } else if (gradient_mode == "vertical") {
          // Vertical gradient: intensity based on distance from center line (current implementation)
          glBegin(GL_QUAD_STRIP);
          for (const auto& point : waveformPoints) {
            // Calculate distance from center line (0.0 = at center, 1.0 = at edge)
            float distanceFromCenter =
                std::abs(point.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
            distanceFromCenter = std::min(distanceFromCenter, 1.0f);

            // Create gradient color based on distance
            float gradientIntensity = distanceFromCenter * 0.15f; // Max 15% oscilloscope color
            float fillColor[4] = {
                oscilloscopeColor.r * gradientIntensity + backgroundColor.r * (1.0f - gradientIntensity),
                oscilloscopeColor.g * gradientIntensity + backgroundColor.g * (1.0f - gradientIntensity),
                oscilloscopeColor.b * gradientIntensity + backgroundColor.b * (1.0f - gradientIntensity),
                oscilloscopeColor.a * gradientIntensity + backgroundColor.a * (1.0f - gradientIntensity)};

            glColor4fv(fillColor);
            glVertex2f(point.first, point.second);
            glVertex2f(point.first, audioData.windowHeight / 2); // Zero line
          }
          glEnd();
        }

        float oscilloscopeColorArray[4] = {oscilloscopeColor.r, oscilloscopeColor.g, oscilloscopeColor.b,
                                           oscilloscopeColor.a};
        Graphics::drawAntialiasedLines(waveformPoints, oscilloscopeColorArray, 2.0f);
      }
    }
  }
}

void FFTVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the FFT visualizer
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // --- Local static config cache ---
  static std::string font;
  static float minFreq;
  static float maxFreq;
  static float sampleRate;
  static float slope_k;
  static float fft_display_min_db;
  static float fft_display_max_db;
  static std::string stereo_mode;
  static size_t lastConfigVersion;

  if (Config::getVersion() != lastConfigVersion) {
    font = Config::getString("font.default_font");
    minFreq = Config::getFloat("fft.fft_min_freq");
    maxFreq = Config::getFloat("fft.fft_max_freq");
    sampleRate = Config::getFloat("audio.sample_rate");
    slope_k = Config::getFloat("fft.fft_slope_correction_db") / 20.0f / log10f(2.0f);
    fft_display_min_db = Config::getFloat("fft.fft_display_min_db");
    fft_display_max_db = Config::getFloat("fft.fft_display_max_db");
    stereo_mode = Config::getString("fft.stereo_mode");
    lastConfigVersion = Config::getVersion();
  }

  if (!audioData.fftMagnitudesMid.empty() && !audioData.fftMagnitudesSide.empty()) {
    const int fftSize = (audioData.fftMagnitudesMid.size() - 1) * 2;

    // Draw frequency scale lines using OpenGL
    auto drawFreqLine = [&](float freq) {
      if (freq < minFreq || freq > maxFreq)
        return;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      const auto& gridColor = Theme::ThemeManager::getGrid();
      float gridColorArray[4] = {gridColor.r, gridColor.g, gridColor.b, gridColor.a};
      Graphics::drawAntialiasedLine(x, 0, x, audioData.windowHeight, gridColorArray, 1.0f);
    };

    // Draw log-decade lines: 1,2,3,...9,10 per decade, from 100Hz up to maxFreq
    // Add 10-100Hz decade
    for (int mult = 1; mult < 10; ++mult) {
      float freq = 10.0f * mult;
      drawFreqLine(freq);
    }
    drawFreqLine(100.0f);
    for (int decade = 2; decade <= 5; ++decade) {
      float base = powf(10.0f, decade);
      for (int mult = 1; mult < 10; ++mult) {
        float freq = base * mult;
        drawFreqLine(freq);
      }
    }
    for (float freq = 10000.0f; freq <= maxFreq; freq *= 10.0f) {
      drawFreqLine(freq);
    }

    // Draw frequency labels for 100Hz, 1kHz, 10kHz
    auto drawFreqLabel = [&](float freq, const char* label) {
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float y = 8.0f;

      // Draw background rectangle
      float textWidth = 40.0f;  // Approximate width for the labels
      float textHeight = 12.0f; // Height of the text
      float padding = 4.0f;     // Padding around text

      const auto& bgColor = Theme::ThemeManager::getBackground();
      float bgColorArray[4] = {bgColor.r, bgColor.g, bgColor.b, bgColor.a};
      Graphics::drawFilledRect(x - textWidth / 2 - padding, y - textHeight / 2 - padding, textWidth + padding * 2,
                               textHeight + padding * 2, bgColorArray);

      // Draw the text
      const auto& gridColor = Theme::ThemeManager::getGrid();
      float gridColorArray[4] = {gridColor.r, gridColor.g, gridColor.b, gridColor.a};
      Graphics::drawText(label, x - textWidth / 2, y, 10.0f, gridColorArray, font.c_str());
    };
    drawFreqLabel(100.0f, "100 Hz");
    drawFreqLabel(1000.0f, "1 kHz");
    drawFreqLabel(10000.0f, "10 kHz");

    // Get the spectrum color for both FFT curves
    const auto& spectrumColor = Theme::ThemeManager::getSpectrum();

    // Determine which data to use based on FFT mode
    const std::vector<float>* mainMagnitudes = nullptr;
    const std::vector<float>* alternateMagnitudes = nullptr;

    // Temporary vectors for computed LEFTRIGHT mode data
    std::vector<float> leftMagnitudes, rightMagnitudes;

    if (stereo_mode == "leftright") {
      // LEFTRIGHT mode: Main = Mid - Side (Left), Alternate = Mid + Side (Right)
      leftMagnitudes.resize(audioData.smoothedMagnitudesMid.size());
      rightMagnitudes.resize(audioData.smoothedMagnitudesMid.size());

      for (size_t i = 0; i < audioData.smoothedMagnitudesMid.size(); ++i) {
        leftMagnitudes[i] = audioData.smoothedMagnitudesMid[i] - audioData.smoothedMagnitudesSide[i];
        rightMagnitudes[i] = audioData.smoothedMagnitudesMid[i] + audioData.smoothedMagnitudesSide[i];
      }

      mainMagnitudes = &leftMagnitudes;
      alternateMagnitudes = &rightMagnitudes;
    } else {
      // MIDSIDE mode: Main = Mid, Alternate = Side
      mainMagnitudes = &audioData.smoothedMagnitudesMid;
      alternateMagnitudes = &audioData.smoothedMagnitudesSide;
    }

    // Draw Alternate FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> alternateFFTPoints;
    alternateFFTPoints.reserve(alternateMagnitudes->size());

    for (size_t bin = 1; bin < alternateMagnitudes->size(); ++bin) {
      float freq = (float)bin * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float magnitude = (*alternateMagnitudes)[bin];

      // Apply slope correction
      float gain = powf(freq / 440.0f, slope_k); // Use A440 as 0dB reference
      magnitude *= gain / 2.0f;                  // Hann window

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using config display limits
      float y = (dB - fft_display_min_db) / (fft_display_max_db - fft_display_min_db) * audioData.windowHeight;
      alternateFFTPoints.push_back({x, y});
    }

    // Draw the Alternate FFT curve using OpenGL (darken the color towards the background color)
    constexpr float alternateFFTAlpha = 0.3f; // Maybe make this a config option?
    const auto& backgroundColor = Theme::ThemeManager::getBackground();
    float alternateFFTColorArray[4] = {
        spectrumColor.r * alternateFFTAlpha + backgroundColor.r * (1.0f - alternateFFTAlpha),
        spectrumColor.g * alternateFFTAlpha + backgroundColor.g * (1.0f - alternateFFTAlpha),
        spectrumColor.b * alternateFFTAlpha + backgroundColor.b * (1.0f - alternateFFTAlpha),
        spectrumColor.a * alternateFFTAlpha + backgroundColor.a * (1.0f - alternateFFTAlpha)};
    Graphics::drawAntialiasedLines(alternateFFTPoints, alternateFFTColorArray, 2.0f);

    // Draw Main FFT using pre-computed smoothed values
    std::vector<std::pair<float, float>> fftPoints;
    fftPoints.reserve(mainMagnitudes->size());

    for (size_t bin = 1; bin < mainMagnitudes->size(); ++bin) {
      float freq = (float)bin * sampleRate / fftSize;
      if (freq < minFreq || freq > maxFreq)
        continue;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * width;
      float magnitude = (*mainMagnitudes)[bin];

      // Apply slope correction
      float gain = powf(freq / 440.0f, slope_k); // Use A440 as 0dB reference
      magnitude *= gain / 2.0f;                  // Hann window

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using config display limits
      float y = (dB - fft_display_min_db) / (fft_display_max_db - fft_display_min_db) * audioData.windowHeight;
      fftPoints.push_back({x, y});
    }

    // Draw the Main FFT curve using OpenGL
    float spectrumColorArray[4] = {spectrumColor.r, spectrumColor.g, spectrumColor.b, spectrumColor.a};
    Graphics::drawAntialiasedLines(fftPoints, spectrumColorArray, 2.0f);

    // Only show note text if we have a valid peak
    if (audioData.hasValidPeak) {
      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", audioData.peakDb,
               audioData.peakFreq, audioData.peakNote.c_str(), audioData.peakOctave, audioData.peakCents);
      float overlayX = 10.0f; // Relative to FFT viewport
      float overlayY = audioData.windowHeight - 20.0f;

      // Now draw overlay text on top of everything
      const auto& textColor = Theme::ThemeManager::getText();
      float textColorArray[4] = {textColor.r, textColor.g, textColor.b, textColor.a};
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, textColorArray, font.c_str());
    }
  }
}

int LissajousVisualizer::getPosition() const { return position; }
void LissajousVisualizer::setPosition(int pos) { position = pos; }
int LissajousVisualizer::getWidth() const { return width; }
void LissajousVisualizer::setWidth(int w) { width = w; }
int LissajousVisualizer::getRightEdge(const AudioData&) const { return position + width; }

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }

int FFTVisualizer::getPosition() const { return position; }
void FFTVisualizer::setPosition(int pos) { position = pos; }
int FFTVisualizer::getWidth() const { return width; }
void FFTVisualizer::setWidth(int w) { width = w; }
int FFTVisualizer::getRightEdge(const AudioData&) const { return position + width; }