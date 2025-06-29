#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/glew.h>
#include <GL/glu.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// Define to use raw signal instead of bandpassed signal for oscilloscope display (bandpassed signal is for debugging)
#define SCOPE_USE_RAW_SIGNAL

// Static working buffers and phosphor context
static std::vector<std::pair<float, float>> scopePoints;
static Graphics::Phosphor::PhosphorContext* scopePhosphorContext = nullptr;

void OscilloscopeVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the oscilloscope
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  const auto& osc = Config::values().oscilloscope;
  const auto& colors = Theme::ThemeManager::colors();

  // Initialize phosphor context if needed
  if (osc.enable_phosphor && !scopePhosphorContext) {
    scopePhosphorContext = Graphics::Phosphor::createPhosphorContext("oscilloscope");
  } else if (!osc.enable_phosphor && scopePhosphorContext) {
    Graphics::Phosphor::destroyPhosphorContext(scopePhosphorContext);
    scopePhosphorContext = nullptr;
  }

  // Calculate how much new data came from the buffer since last frame
  static size_t lastWritePos = 0;
  static bool firstCall = true;

  size_t readCount;
  if (firstCall) {
    readCount = audioData.displaySamples; // On first call, assume we need to draw displaySamples
    firstCall = false;
  } else {
    readCount = (audioData.writePos + audioData.bufferSize - lastWritePos) % audioData.bufferSize;
  }
  lastWritePos = audioData.writePos;

  if (readCount == 0 && audioData.hasValidPeak) {
    if (osc.enable_phosphor && scopePhosphorContext) {
      GLuint phosphorTexture = Graphics::Phosphor::drawCurrentPhosphorState(
          scopePhosphorContext, width, audioData.windowHeight, colors.background, colors.oscilloscope,
          osc.enable_phosphor_grain);

      if (phosphorTexture) {
        Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, audioData.windowHeight);
      }
    } else {
      Graphics::drawAntialiasedLines(scopePoints, colors.oscilloscope, 2.0f);
    }
    return;
  }

  // Calculate the target position for phase correction
  size_t targetPos = (audioData.writePos + audioData.bufferSize - audioData.displaySamples) % audioData.bufferSize;

  // Use pre-computed bandpassed data for zero crossing detection
  size_t searchRange = static_cast<size_t>(audioData.samplesPerCycle);
  size_t zeroCrossPos = targetPos;
  if (osc.follow_pitch && !audioData.bandpassedMid.empty() && audioData.pitchConfidence > 0.5f) {
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
  float phaseOffset =
      static_cast<float>((targetPos + audioData.bufferSize - zeroCrossPos) % audioData.bufferSize) + threeQuarterCycle;

  // Calculate the start position for reading samples, adjusted for phase
  size_t startPos = (audioData.writePos + audioData.bufferSize - audioData.displaySamples) % audioData.bufferSize;
  if (osc.follow_pitch && audioData.samplesPerCycle > 0.0f && audioData.pitchConfidence > 0.5f) {
    // Move backward by the phase offset
    startPos = (startPos + audioData.bufferSize - static_cast<size_t>(phaseOffset)) % audioData.bufferSize;
  }

  // Re-use static vector for waveform points
  scopePoints.clear();
  scopePoints.reserve(audioData.displaySamples);

  // Pre-compute scale factors
  const float amplitudeScale = audioData.windowHeight * osc.amplitude_scale;
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
    scopePoints.push_back({x, y});
  }

  // Draw the waveform line using OpenGL only if there's a valid peak
  if (audioData.hasValidPeak) {
    if (osc.enable_phosphor && scopePhosphorContext) {
      // --- Phosphor rendering using Graphics::Phosphor high-level interface ---
      if (!scopePoints.empty()) {
        // Prepare intensity and dwell time data for oscilloscope beam
        std::vector<float> intensityLinear;
        std::vector<float> dwellTimes;

        intensityLinear.reserve(scopePoints.size());
        dwellTimes.reserve(scopePoints.size());

        for (size_t i = 1; i < scopePoints.size(); ++i) {
          const auto& p1 = scopePoints[i - 1];
          const auto& p2 = scopePoints[i];

          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segLen = std::max(sqrtf(dx * dx + dy * dy), 1e-12f);
          float deltaT = 1.0f / audioData.sampleRate;
          float intensity = osc.phosphor_beam_energy * deltaT / segLen;

          intensityLinear.push_back(intensity);
          dwellTimes.push_back(deltaT);
        }

        // Render phosphor splines using high-level interface
        GLuint phosphorTexture = Graphics::Phosphor::renderPhosphorSplines(
            scopePhosphorContext, scopePoints, intensityLinear, dwellTimes, width, audioData.windowHeight, audioData.dt,
            1.0f, colors.background, colors.oscilloscope, osc.phosphor_beam_size, osc.phosphor_line_blur_spread,
            osc.phosphor_line_width, osc.phosphor_decay_slow, osc.phosphor_decay_fast, osc.phosphor_age_threshold,
            osc.phosphor_range_factor, osc.enable_phosphor_grain);

        // Draw the phosphor result
        if (phosphorTexture) {
          Graphics::Phosphor::drawPhosphorResult(phosphorTexture, width, audioData.windowHeight);
        }

        // --- Draw gradient overlays directly on screen (preserve current gradient functionality) ---
        if (osc.gradient_mode == "horizontal" || osc.gradient_mode == "vertical") {
          // Use standard alpha blending for gradient fill
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

          if (osc.gradient_mode == "horizontal") {
            const float invCenterY = 1.0f / centerY;
            const float gradientScale = 0.3f; // Reduced scale for overlay

            glBegin(GL_TRIANGLES);
            for (size_t i = 1; i < scopePoints.size(); ++i) {
              const auto& p1 = scopePoints[i - 1];
              const auto& p2 = scopePoints[i];

              // Gradient factors
              float dist1 = std::min(std::abs(p1.second - centerY) * invCenterY, 1.0f);
              float dist2 = std::min(std::abs(p2.second - centerY) * invCenterY, 1.0f);
              float grad1 = dist1 * gradientScale;
              float grad2 = dist2 * gradientScale;

              float fill1[4] = {colors.oscilloscope[0] * grad1, colors.oscilloscope[1] * grad1,
                                colors.oscilloscope[2] * grad1, grad1 * 0.5f}; // Reduced alpha

              float fill2[4] = {colors.oscilloscope[0] * grad2, colors.oscilloscope[1] * grad2,
                                colors.oscilloscope[2] * grad2, grad2 * 0.5f}; // Reduced alpha

              float bgTransparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};

              // Triangle 1
              glColor4fv(bgTransparent);
              glVertex2f(p1.first, centerY);
              glColor4fv(fill1);
              glVertex2f(p1.first, p1.second);
              glColor4fv(bgTransparent);
              glVertex2f(p2.first, centerY);

              // Triangle 2
              glColor4fv(fill1);
              glVertex2f(p1.first, p1.second);
              glColor4fv(fill2);
              glVertex2f(p2.first, p2.second);
              glColor4fv(bgTransparent);
              glVertex2f(p2.first, centerY);
            }
            glEnd();
          } else { // vertical
            const float invCenterY = 1.0f / centerY;
            const float gradientScale = 0.15f; // Reduced scale for overlay

            glBegin(GL_QUAD_STRIP);
            for (const auto& point : scopePoints) {
              float dist = std::min(std::abs(point.second - centerY) * invCenterY, 1.0f);
              float grad = dist * gradientScale;

              float fill[4] = {colors.oscilloscope[0] * grad, colors.oscilloscope[1] * grad,
                               colors.oscilloscope[2] * grad, grad * 0.5f}; // Reduced alpha

              glColor4fv(fill);
              glVertex2f(point.first, point.second);
              glVertex2f(point.first, centerY);
            }
            glEnd();
          }
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
      }
    } else {
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      // Standard non-phosphor rendering
      if (osc.gradient_mode == "horizontal") {
        // Pre-compute gradient constants
        const float invCenterY = 1.0f / centerY;
        const float gradientScale = 0.3f;

        // Horizontal gradient: intensity based on distance from center line outward
        glBegin(GL_TRIANGLES);
        for (size_t i = 1; i < scopePoints.size(); ++i) {
          const auto& p1 = scopePoints[i - 1];
          const auto& p2 = scopePoints[i];

          // Calculate gradient for p1
          float distance1 = std::abs(p1.second - centerY) * invCenterY;
          distance1 = std::min(distance1, 1.0f);
          float gradientIntensity1 = distance1 * gradientScale;
          float fillColor1[4] = {
              colors.oscilloscope[0] * gradientIntensity1 + colors.background[0] * (1.0f - gradientIntensity1),
              colors.oscilloscope[1] * gradientIntensity1 + colors.background[1] * (1.0f - gradientIntensity1),
              colors.oscilloscope[2] * gradientIntensity1 + colors.background[2] * (1.0f - gradientIntensity1),
              colors.oscilloscope[3] * gradientIntensity1 + colors.background[3] * (1.0f - gradientIntensity1)};

          // Calculate gradient for p2
          float distance2 = std::abs(p2.second - centerY) * invCenterY;
          distance2 = std::min(distance2, 1.0f);
          float gradientIntensity2 = distance2 * gradientScale;
          float fillColor2[4] = {
              colors.oscilloscope[0] * gradientIntensity2 + colors.background[0] * (1.0f - gradientIntensity2),
              colors.oscilloscope[1] * gradientIntensity2 + colors.background[1] * (1.0f - gradientIntensity2),
              colors.oscilloscope[2] * gradientIntensity2 + colors.background[2] * (1.0f - gradientIntensity2),
              colors.oscilloscope[3] * gradientIntensity2 + colors.background[3] * (1.0f - gradientIntensity2)};

          // Triangle 1: (p1.x, center) -> (p1.x, p1.y) -> (p2.x, center)
          glColor4fv(colors.background);
          glVertex2f(p1.first, centerY);
          glColor4fv(fillColor1);
          glVertex2f(p1.first, p1.second);
          glColor4fv(colors.background);
          glVertex2f(p2.first, centerY);

          // Triangle 2: (p1.x, p1.y) -> (p2.x, p2.y) -> (p2.x, center)
          glColor4fv(fillColor1);
          glVertex2f(p1.first, p1.second);
          glColor4fv(fillColor2);
          glVertex2f(p2.first, p2.second);
          glColor4fv(colors.background);
          glVertex2f(p2.first, centerY);
        }
        glEnd();
      } else if (osc.gradient_mode == "vertical") {
        // Pre-compute gradient constants
        const float invCenterY = 1.0f / centerY;
        const float gradientScale = 0.15f;

        // Vertical gradient: intensity based on distance from center line
        glBegin(GL_QUAD_STRIP);
        for (const auto& point : scopePoints) {
          // Calculate distance from center line
          float distanceFromCenter = std::abs(point.second - centerY) * invCenterY;
          distanceFromCenter = std::min(distanceFromCenter, 1.0f);

          // Create gradient color based on distance
          float gradientIntensity = distanceFromCenter * gradientScale;
          float fillColor[4] = {
              colors.oscilloscope[0] * gradientIntensity + colors.background[0] * (1.0f - gradientIntensity),
              colors.oscilloscope[1] * gradientIntensity + colors.background[1] * (1.0f - gradientIntensity),
              colors.oscilloscope[2] * gradientIntensity + colors.background[2] * (1.0f - gradientIntensity),
              colors.oscilloscope[3] * gradientIntensity + colors.background[3] * (1.0f - gradientIntensity)};

          glColor4fv(fillColor);
          glVertex2f(point.first, point.second);
          glVertex2f(point.first, centerY);
        }
        glEnd();
      }

      Graphics::drawAntialiasedLines(scopePoints, colors.oscilloscope, 2.0f);
    }
  }
}

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }