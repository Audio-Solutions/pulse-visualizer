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

// Define to use raw signal instead of bandpassed signal for oscilloscope display (bandpassed signal is for debugging)
#define SCOPE_USE_RAW_SIGNAL

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

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }