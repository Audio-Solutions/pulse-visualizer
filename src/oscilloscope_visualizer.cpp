#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/gl.h>
#include <GL/glu.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Define to use raw signal instead of bandpassed signal for oscilloscope display (bandpassed signal is for debugging)
#define SCOPE_USE_RAW_SIGNAL

// Initialize static members
float OscilloscopeVisualizer::amplitude_scale = 0.0f;
std::string OscilloscopeVisualizer::gradient_mode;
bool OscilloscopeVisualizer::enable_phosphor = false;
int OscilloscopeVisualizer::phosphor_spline_density = 0;
float OscilloscopeVisualizer::phosphor_max_beam_speed = 0.0f;
float OscilloscopeVisualizer::phosphor_intensity_scale = 0.0f;
size_t OscilloscopeVisualizer::lastConfigVersion = 0;
float OscilloscopeVisualizer::cachedOscilloscopeColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float OscilloscopeVisualizer::cachedBackgroundColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
size_t OscilloscopeVisualizer::lastThemeVersion = 0;

void OscilloscopeVisualizer::updateCachedValues() {
  bool configChanged = Config::getVersion() != lastConfigVersion;
  bool themeChanged = Theme::ThemeManager::getVersion() != lastThemeVersion;

  if (configChanged) {
    amplitude_scale = Config::getFloat("oscilloscope.amplitude_scale");
    gradient_mode = Config::getString("oscilloscope.gradient_mode");
    enable_phosphor = Config::getBool("oscilloscope.enable_phosphor");
    phosphor_spline_density = Config::getInt("oscilloscope.phosphor_spline_density");
    phosphor_max_beam_speed = Config::getFloat("oscilloscope.phosphor_max_beam_speed");
    phosphor_intensity_scale = Config::getFloat("oscilloscope.phosphor_intensity_scale");
    lastConfigVersion = Config::getVersion();
  }

  if (themeChanged) {
    const auto& oscilloscopeColor = Theme::ThemeManager::getOscilloscope();
    cachedOscilloscopeColor[0] = oscilloscopeColor.r;
    cachedOscilloscopeColor[1] = oscilloscopeColor.g;
    cachedOscilloscopeColor[2] = oscilloscopeColor.b;
    cachedOscilloscopeColor[3] = oscilloscopeColor.a;

    const auto& backgroundColor = Theme::ThemeManager::getBackground();
    cachedBackgroundColor[0] = backgroundColor.r;
    cachedBackgroundColor[1] = backgroundColor.g;
    cachedBackgroundColor[2] = backgroundColor.b;
    cachedBackgroundColor[3] = backgroundColor.a;

    lastThemeVersion = Theme::ThemeManager::getVersion();
  }
}

void OscilloscopeVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the oscilloscope
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // Update cached values if needed
  updateCachedValues();

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

    // Pre-compute scale factors
    const float amplitudeScale = audioData.windowHeight * amplitude_scale;
    const float widthScale = static_cast<float>(width) / audioData.displaySamples;
    const float centerY = audioData.windowHeight * 0.5f;

    for (size_t i = 0; i < audioData.displaySamples; i++) {
      size_t pos = (startPos + i) % audioData.bufferSize;
      float x = static_cast<float>(i) * widthScale;
#ifdef SCOPE_USE_RAW_SIGNAL
      float y = centerY + audioData.bufferMid[pos] * amplitudeScale;
#else
      float y = centerY + audioData.bandpassedMid[pos] * amplitudeScale;
#endif
      waveformPoints.push_back({x, y});
    }

    // Draw the waveform line using OpenGL only if there's a valid peak
    if (audioData.hasValidPeak) {
      if (enable_phosphor) {
        // First draw the gradient background (same as non-phosphor mode)
        if (gradient_mode == "horizontal") {
          // Pre-compute gradient constants
          const float invCenterY = 1.0f / centerY;
          const float gradientScale = 0.3f;

          // Horizontal gradient: intensity based on distance from center line outward
          glBegin(GL_TRIANGLES);
          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            const auto& p1 = waveformPoints[i - 1];
            const auto& p2 = waveformPoints[i];

            // Calculate gradient for p1
            float distance1 = std::abs(p1.second - centerY) * invCenterY;
            distance1 = std::min(distance1, 1.0f);
            float gradientIntensity1 = distance1 * gradientScale;
            float fillColor1[4] = {cachedOscilloscopeColor[0] * gradientIntensity1 +
                                       cachedBackgroundColor[0] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[1] * gradientIntensity1 +
                                       cachedBackgroundColor[1] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[2] * gradientIntensity1 +
                                       cachedBackgroundColor[2] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[3] * gradientIntensity1 +
                                       cachedBackgroundColor[3] * (1.0f - gradientIntensity1)};

            // Calculate gradient for p2
            float distance2 = std::abs(p2.second - centerY) * invCenterY;
            distance2 = std::min(distance2, 1.0f);
            float gradientIntensity2 = distance2 * gradientScale;
            float fillColor2[4] = {cachedOscilloscopeColor[0] * gradientIntensity2 +
                                       cachedBackgroundColor[0] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[1] * gradientIntensity2 +
                                       cachedBackgroundColor[1] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[2] * gradientIntensity2 +
                                       cachedBackgroundColor[2] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[3] * gradientIntensity2 +
                                       cachedBackgroundColor[3] * (1.0f - gradientIntensity2)};

            // Triangle 1: (p1.x, center) -> (p1.x, p1.y) -> (p2.x, center)
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p1.first, centerY);
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p2.first, centerY);

            // Triangle 2: (p1.x, p1.y) -> (p2.x, p2.y) -> (p2.x, center)
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(fillColor2);
            glVertex2f(p2.first, p2.second);
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p2.first, centerY);
          }
          glEnd();
        } else if (gradient_mode == "vertical") {
          // Pre-compute gradient constants
          const float invCenterY = 1.0f / centerY;
          const float gradientScale = 0.15f;

          // Vertical gradient: intensity based on distance from center line
          glBegin(GL_QUAD_STRIP);
          for (const auto& point : waveformPoints) {
            // Calculate distance from center line
            float distanceFromCenter = std::abs(point.second - centerY) * invCenterY;
            distanceFromCenter = std::min(distanceFromCenter, 1.0f);

            // Create gradient color based on distance
            float gradientIntensity = distanceFromCenter * gradientScale;
            float fillColor[4] = {
                cachedOscilloscopeColor[0] * gradientIntensity + cachedBackgroundColor[0] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[1] * gradientIntensity + cachedBackgroundColor[1] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[2] * gradientIntensity + cachedBackgroundColor[2] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[3] * gradientIntensity + cachedBackgroundColor[3] * (1.0f - gradientIntensity)};

            glColor4fv(fillColor);
            glVertex2f(point.first, point.second);
            glVertex2f(point.first, centerY);
          }
          glEnd();
        }

        // Now draw the phosphor effect on top of the gradient
        if (!waveformPoints.empty()) {
          // Pre-calculate point distances for intensity mapping
          std::vector<float> segmentDistances;
          segmentDistances.reserve(waveformPoints.size() - 1);
          const float invWidth = 1.0f / width;

          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            float dx = waveformPoints[i].first - waveformPoints[i - 1].first;
            float dy = waveformPoints[i].second - waveformPoints[i - 1].second;
            float dist = sqrtf(dx * dx + dy * dy) * invWidth;
            segmentDistances.push_back(dist);
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
          const float scaledMaxBeamSpeed = phosphor_max_beam_speed * sizeScale;

          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            const auto& p1 = waveformPoints[i - 1];
            const auto& p2 = waveformPoints[i];

            // Check beam speed for this segment before rendering
            float dx = p2.first - p1.first;
            float dy = p2.second - p1.second;
            float segmentLength = sqrtf(dx * dx + dy * dy);
            float beamSpeed = segmentLength * invWidth;

            // Skip segments where beam moves too fast
            if (beamSpeed > scaledMaxBeamSpeed) {
              continue;
            }

            // Calculate intensity based on beam speed and signal change rate
            float intensity = phosphor_intensity_scale;
            float segmentSpeed = segmentDistances[i - 1];
            float speedIntensity = 1.0f / (1.0f + segmentSpeed * 50.0f);
            intensity *= speedIntensity;

            // Clamp intensity
            intensity = std::min(intensity, 1.0f);

            if (intensity > 0.001f) {
              // Calculate color with intensity
              float color[4] = {cachedOscilloscopeColor[0] * intensity, cachedOscilloscopeColor[1] * intensity,
                                cachedOscilloscopeColor[2] * intensity, intensity};

              // Draw antialiased line segment
              Graphics::drawAntialiasedLine(p1.first, p1.second, p2.first, p2.second, color, 2.0f);
            }
          }

          // Reset blending
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
      } else {
        // Standard non-phosphor rendering
        if (gradient_mode == "horizontal") {
          // Pre-compute gradient constants
          const float invCenterY = 1.0f / centerY;
          const float gradientScale = 0.3f;

          // Horizontal gradient: intensity based on distance from center line outward
          glBegin(GL_TRIANGLES);
          for (size_t i = 1; i < waveformPoints.size(); ++i) {
            const auto& p1 = waveformPoints[i - 1];
            const auto& p2 = waveformPoints[i];

            // Calculate gradient for p1
            float distance1 = std::abs(p1.second - centerY) * invCenterY;
            distance1 = std::min(distance1, 1.0f);
            float gradientIntensity1 = distance1 * gradientScale;
            float fillColor1[4] = {cachedOscilloscopeColor[0] * gradientIntensity1 +
                                       cachedBackgroundColor[0] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[1] * gradientIntensity1 +
                                       cachedBackgroundColor[1] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[2] * gradientIntensity1 +
                                       cachedBackgroundColor[2] * (1.0f - gradientIntensity1),
                                   cachedOscilloscopeColor[3] * gradientIntensity1 +
                                       cachedBackgroundColor[3] * (1.0f - gradientIntensity1)};

            // Calculate gradient for p2
            float distance2 = std::abs(p2.second - centerY) * invCenterY;
            distance2 = std::min(distance2, 1.0f);
            float gradientIntensity2 = distance2 * gradientScale;
            float fillColor2[4] = {cachedOscilloscopeColor[0] * gradientIntensity2 +
                                       cachedBackgroundColor[0] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[1] * gradientIntensity2 +
                                       cachedBackgroundColor[1] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[2] * gradientIntensity2 +
                                       cachedBackgroundColor[2] * (1.0f - gradientIntensity2),
                                   cachedOscilloscopeColor[3] * gradientIntensity2 +
                                       cachedBackgroundColor[3] * (1.0f - gradientIntensity2)};

            // Triangle 1: (p1.x, center) -> (p1.x, p1.y) -> (p2.x, center)
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p1.first, centerY);
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p2.first, centerY);

            // Triangle 2: (p1.x, p1.y) -> (p2.x, p2.y) -> (p2.x, center)
            glColor4fv(fillColor1);
            glVertex2f(p1.first, p1.second);
            glColor4fv(fillColor2);
            glVertex2f(p2.first, p2.second);
            glColor4fv(cachedBackgroundColor);
            glVertex2f(p2.first, centerY);
          }
          glEnd();
        } else if (gradient_mode == "vertical") {
          // Pre-compute gradient constants
          const float invCenterY = 1.0f / centerY;
          const float gradientScale = 0.15f;

          // Vertical gradient: intensity based on distance from center line
          glBegin(GL_QUAD_STRIP);
          for (const auto& point : waveformPoints) {
            // Calculate distance from center line
            float distanceFromCenter = std::abs(point.second - centerY) * invCenterY;
            distanceFromCenter = std::min(distanceFromCenter, 1.0f);

            // Create gradient color based on distance
            float gradientIntensity = distanceFromCenter * gradientScale;
            float fillColor[4] = {
                cachedOscilloscopeColor[0] * gradientIntensity + cachedBackgroundColor[0] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[1] * gradientIntensity + cachedBackgroundColor[1] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[2] * gradientIntensity + cachedBackgroundColor[2] * (1.0f - gradientIntensity),
                cachedOscilloscopeColor[3] * gradientIntensity + cachedBackgroundColor[3] * (1.0f - gradientIntensity)};

            glColor4fv(fillColor);
            glVertex2f(point.first, point.second);
            glVertex2f(point.first, centerY);
          }
          glEnd();
        }

        Graphics::drawAntialiasedLines(waveformPoints, cachedOscilloscopeColor, 2.0f);
      }
    }
  }
}

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }