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
  const auto& debug = Config::values().debug;
  const auto& phos = Config::values().phosphor;
  const auto& colors = Theme::ThemeManager::colors();
  const auto& audio = Config::values().audio;

  // Initialize phosphor context if needed
  if (osc.enable_phosphor && !scopePhosphorContext) {
    scopePhosphorContext = Graphics::Phosphor::createPhosphorContext("oscilloscope");
  } else if (!osc.enable_phosphor && scopePhosphorContext) {
    Graphics::Phosphor::destroyPhosphorContext(scopePhosphorContext);
    scopePhosphorContext = nullptr;
  }

  size_t displaySamples = audioData.displaySamples;
  if (osc.limit_cycles) {
    displaySamples = audioData.samplesPerCycle * osc.cycles;

    // Limit to at least min_samples
    displaySamples = std::max(displaySamples, static_cast<size_t>(osc.min_samples));
  }

  // Calculate the target position for phase correction
  size_t targetPos = (audioData.writePos + audioData.bufferSize - displaySamples) % audioData.bufferSize;

  // Use pre-computed bandpassed data for zero crossing detection
  size_t searchRange = static_cast<size_t>(audioData.samplesPerCycle) * 2;
  size_t zeroCrossPos = targetPos;
  if (osc.follow_pitch && !audioData.bandpassedMid.empty() && audioData.pitchConfidence > 0.5f &&
      audioData.hasValidPeak) {

    // Adjust target position based on alignment
    if (osc.alignment == "left") {
      // Search backward for the nearest positive zero crossing
      targetPos = (audioData.writePos + audioData.bufferSize - displaySamples) % audioData.bufferSize;
    } else if (osc.alignment == "center") {
      // Search backward from center position
      targetPos = (audioData.writePos + audioData.bufferSize - displaySamples / 2) % audioData.bufferSize;
    } else if (osc.alignment == "right") {
      // Search backward from right edge (newest data)
      targetPos = (audioData.writePos + audioData.bufferSize - 1) % audioData.bufferSize;
    }

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

  // Calculate phase offset to align
  float phaseOffset = static_cast<float>((targetPos + audioData.bufferSize - zeroCrossPos) % audioData.bufferSize);

  if (osc.alignment_type == "peak") {
    phaseOffset += audioData.samplesPerCycle * 0.75f;
  } else if (osc.alignment_type == "zero_crossing") {
    // do nothing, zero crossing is already aligned
  }

  // Calculate the start position for reading samples, adjusted for phase
  size_t startPos = (audioData.writePos + audioData.bufferSize - displaySamples) % audioData.bufferSize;
  if (osc.follow_pitch && audioData.samplesPerCycle > 0.0f && audioData.pitchConfidence > 0.5f &&
      audioData.hasValidPeak) {
    // Move backward by the phase offset
    startPos = (startPos + audioData.bufferSize - static_cast<size_t>(phaseOffset)) % audioData.bufferSize;
  }

  // Re-use static vector for waveform points
  scopePoints.clear();
  scopePoints.reserve(displaySamples);

  // Pre-compute scale factors
  const float widthScale = static_cast<float>(width - 2) / displaySamples;
  const float centerY = audioData.windowHeight * 0.5f;

  for (size_t i = 0; i < displaySamples + 1; i++) {
    size_t pos = (startPos + i) % audioData.bufferSize;
    float x = static_cast<float>(i) * widthScale + 1.0f;

    float y = 0.0f;
    if (debug.show_bandpassed) {
      y = centerY + audioData.bandpassedMid[pos] * audioData.windowHeight * 0.5f;
    } else {
      y = centerY + audioData.bufferMid[pos] * audioData.windowHeight * 0.5f;
    }

    // Clamp y values to screen bounds
    y = std::max(0.0f, std::min(y, static_cast<float>(audioData.windowHeight - 2)));

    scopePoints.push_back({x, y});
  }

  // Draw the waveform line using OpenGL
  const bool isSilent = !audioData.hasValidPeak || audioData.peakDb < audio.silence_threshold;

  if (osc.enable_phosphor && isSilent && scopePhosphorContext) {
    std::vector<std::pair<float, float>> emptyPoints;
    std::vector<float> emptyIntensity;
    std::vector<float> emptyDwell;
    GLuint tex = Graphics::Phosphor::renderPhosphorSplines(
        scopePhosphorContext, emptyPoints, emptyIntensity, emptyDwell, width, audioData.windowHeight,
        audioData.getAudioDeltaTime(), 1.0f, colors.background, colors.oscilloscope);
    if (tex)
      Graphics::Phosphor::drawPhosphorResult(tex, width, audioData.windowHeight);
    return;
  }

  if (osc.enable_phosphor && scopePhosphorContext) {
    // --- Phosphor rendering using Graphics::Phosphor high-level interface ---
    if (!scopePoints.empty()) {
      // Prepare intensity and dwell time data for oscilloscope beam
      std::vector<float> intensityLinear;
      std::vector<float> dwellTimes;

      intensityLinear.reserve(scopePoints.size());
      dwellTimes.reserve(scopePoints.size());

      // Normalize beam energy over area
      float ref = 400.0f * 400.0f;
      float beamEnergy = phos.beam_energy / ref * (width * audioData.windowHeight);

      // Normalize beam energy over display samples
      beamEnergy = beamEnergy / displaySamples * audioData.displaySamples;

      for (size_t i = 1; i < scopePoints.size(); ++i) {
        const auto& p1 = scopePoints[i - 1];
        const auto& p2 = scopePoints[i];

        float dx = p2.first - p1.first;
        float dy = p2.second - p1.second;
        float segLen = std::max(sqrtf(dx * dx + dy * dy), 1e-12f);
        float deltaT = 1.0f / audioData.sampleRate;
        float intensity = beamEnergy * (deltaT / segLen);

        intensityLinear.push_back(intensity);
        dwellTimes.push_back(deltaT);
      }

      // Render phosphor splines using high-level interface
      GLuint phosphorTexture = Graphics::Phosphor::renderPhosphorSplines(
          scopePhosphorContext, scopePoints, intensityLinear, dwellTimes, width, audioData.windowHeight,
          audioData.getAudioDeltaTime(), 1.0f, colors.background, colors.oscilloscope);

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

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }

void OscilloscopeVisualizer::invalidatePhosphorContext() {
  if (scopePhosphorContext) {
    // Force the phosphor context to recreate its textures by destroying and recreating it
    Graphics::Phosphor::destroyPhosphorContext(scopePhosphorContext);
    scopePhosphorContext = nullptr;
  }
}