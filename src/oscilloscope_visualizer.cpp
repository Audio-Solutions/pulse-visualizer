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

// --- Texture-based persistence resources and helpers (local to this TU) ---
namespace {
// GL resources
GLuint persistenceFBO = 0;
GLuint persistenceTex[2] = {0, 0};
int currentTexIdx = 0;
int texWidth = 0;
int texHeight = 0;

// Simple fade shader reused from lissajous implementation
GLuint fadeProgram = 0;

void ensureFadeProgram() {
  if (fadeProgram)
    return;

  fadeProgram = Graphics::createShaderProgram("shaders/fade.vert", "shaders/fade.frag");
  if (fadeProgram == 0) {
    std::cerr << "Failed to create fade shader program" << std::endl;
    return;
  }

  // Set vertex attribute location
  glBindAttribLocation(fadeProgram, 0, "pos");
}

void createPersistenceResources(int w, int h, const float* bgColor) {
  bool needsReinit = (w != texWidth || h != texHeight || !persistenceTex[0] || !persistenceTex[1] || !persistenceFBO);
  if (!needsReinit)
    return;

  texWidth = w;
  texHeight = h;

  if (persistenceTex[0]) {
    glDeleteTextures(2, persistenceTex);
    persistenceTex[0] = persistenceTex[1] = 0;
  }
  if (persistenceFBO) {
    glDeleteFramebuffers(1, &persistenceFBO);
    persistenceFBO = 0;
  }

  glGenTextures(2, persistenceTex);
  glGenFramebuffers(1, &persistenceFBO);

  std::vector<unsigned char> pixels(texWidth * texHeight * 4);
  for (int i = 0; i < texWidth * texHeight; ++i) {
    pixels[i * 4 + 0] = static_cast<unsigned char>(bgColor[0] * 255);
    pixels[i * 4 + 1] = static_cast<unsigned char>(bgColor[1] * 255);
    pixels[i * 4 + 2] = static_cast<unsigned char>(bgColor[2] * 255);
    pixels[i * 4 + 3] = 255;
  }

  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, persistenceTex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  currentTexIdx = 0;
}

void drawFullscreenTexturedQuad(int w, int h) {
  glBegin(GL_QUADS);
  glTexCoord2f(0.f, 0.f);
  glVertex2f(0.f, 0.f);
  glTexCoord2f(1.f, 0.f);
  glVertex2f(static_cast<float>(w), 0.f);
  glTexCoord2f(1.f, 1.f);
  glVertex2f(static_cast<float>(w), static_cast<float>(h));
  glTexCoord2f(0.f, 1.f);
  glVertex2f(0.f, static_cast<float>(h));
  glEnd();
}
} // namespace

void OscilloscopeVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the oscilloscope
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  const auto& osc = Config::values().oscilloscope;
  const auto& colors = Theme::ThemeManager::colors();

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

  // Re-use a static vector so we don't allocate every frame.  Capacity grows to
  // the maximum seen and is retained for future calls.
  static thread_local std::vector<std::pair<float, float>> waveformPoints;
  waveformPoints.clear();
  waveformPoints.reserve(audioData.displaySamples);

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
    waveformPoints.push_back({x, y});
  }

  // Draw the waveform line using OpenGL only if there's a valid peak
  if (audioData.hasValidPeak) {
    if (osc.enable_phosphor) {
      // --- VBO & texture persistence path ---
      if (!waveformPoints.empty()) {
        // Build VBO friendly buffers
        std::vector<float> vertices;
        std::vector<float> colorsRGBA;
        vertices.reserve(waveformPoints.size() * 4);
        colorsRGBA.reserve(waveformPoints.size() * 8);

        const float invWidth = 1.0f / width;
        const float referenceWidth = 200.0f;
        const float sizeScale = static_cast<float>(width) / referenceWidth;
        const float scaledMaxBeamSpeed = osc.phosphor_max_beam_speed * sizeScale;

        for (size_t i = 1; i < waveformPoints.size(); ++i) {
          const auto& p1 = waveformPoints[i - 1];
          const auto& p2 = waveformPoints[i];

          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segLen = sqrtf(dx * dx + dy * dy);
          float beamSpeed = segLen * invWidth;
          if (beamSpeed > scaledMaxBeamSpeed)
            continue;

          float speedIntensity = 1.0f / (1.0f + beamSpeed * 50.0f);
          float intensity = osc.phosphor_intensity_scale * speedIntensity;
          intensity = std::min(intensity, 1.0f);
          if (intensity < 0.001f)
            continue;

          float r = colors.oscilloscope[0] * intensity;
          float g = colors.oscilloscope[1] * intensity;
          float b = colors.oscilloscope[2] * intensity;

          vertices.push_back(p1.first);
          vertices.push_back(p1.second);
          colorsRGBA.push_back(r);
          colorsRGBA.push_back(g);
          colorsRGBA.push_back(b);
          colorsRGBA.push_back(intensity);

          vertices.push_back(p2.first);
          vertices.push_back(p2.second);
          colorsRGBA.push_back(r);
          colorsRGBA.push_back(g);
          colorsRGBA.push_back(b);
          colorsRGBA.push_back(intensity);
        }

        // Prepare persistence resources
        createPersistenceResources(width, audioData.windowHeight, colors.background);
        int srcIdx = currentTexIdx;
        int dstIdx = 1 - currentTexIdx;

        // Render decay pass to FBO
        glBindFramebuffer(GL_FRAMEBUFFER, persistenceFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, persistenceTex[dstIdx], 0);
        glViewport(0, 0, width, audioData.windowHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, 0, audioData.windowHeight, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        ensureFadeProgram();
        float dtDecay = std::clamp(Config::values().oscilloscope.texture_decay, 0.0f, 1.0f);
        glUseProgram(fadeProgram);
        glUniform1f(glGetUniformLocation(fadeProgram, "decay"), dtDecay);
        glUniform3f(glGetUniformLocation(fadeProgram, "bgColor"), colors.background[0], colors.background[1],
                    colors.background[2]);
        glUniform3f(glGetUniformLocation(fadeProgram, "targetRGB"), colors.oscilloscope[0], colors.oscilloscope[1],
                    colors.oscilloscope[2]);
        glUniform2f(glGetUniformLocation(fadeProgram, "texSize"), static_cast<float>(width),
                    static_cast<float>(audioData.windowHeight));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, persistenceTex[srcIdx]);
        glUniform1i(glGetUniformLocation(fadeProgram, "texPrev"), 0);

        glDisable(GL_BLEND);
        drawFullscreenTexturedQuad(width, audioData.windowHeight);
        glUseProgram(0);

        // Draw current beam
        if (!vertices.empty()) {
          glEnable(GL_BLEND);
          if (Theme::ThemeManager::invertNoiseBrightness()) {
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
          } else {
            glBlendFunc(GL_ONE, GL_ONE);
          }
          Graphics::drawColoredLineSegments(vertices, colorsRGBA, 2.0f);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // Restore default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

        // Present persistence texture
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, persistenceTex[dstIdx]);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        drawFullscreenTexturedQuad(width, audioData.windowHeight);
        glDisable(GL_TEXTURE_2D);

        // --- Draw gradient inside persistence FBO, scaled so it blends without bleaching ---
        if (osc.gradient_mode == "horizontal" || osc.gradient_mode == "vertical") {
          const float scaleFactor = 1.0f - dtDecay * (osc.gradient_mode == "horizontal" ? 1.0f : 0.5f);

          // Use standard alpha blending for gradient fill so colors properly interpolate toward the (already
          // decayed) background contained in the persistence texture.  Additive blending made the gradient
          // accumulate brightness instead of fading toward the background, resulting in a washed-out look.
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

          if (osc.gradient_mode == "horizontal") {
            const float invCenterY = 1.0f / centerY;
            const float gradientScale = 1.0f;

            glBegin(GL_TRIANGLES);
            for (size_t i = 1; i < waveformPoints.size(); ++i) {
              const auto& p1 = waveformPoints[i - 1];
              const auto& p2 = waveformPoints[i];

              // Gradient factors
              float dist1 = std::min(std::abs(p1.second - centerY) * invCenterY, 1.0f);
              float dist2 = std::min(std::abs(p2.second - centerY) * invCenterY, 1.0f);
              float grad1 = dist1 * gradientScale;
              float grad2 = dist2 * gradientScale;

              float fill1[4] = {(colors.oscilloscope[0] * grad1 + colors.background[0]) * scaleFactor,
                                (colors.oscilloscope[1] * grad1 + colors.background[1]) * scaleFactor,
                                (colors.oscilloscope[2] * grad1 + colors.background[2]) * scaleFactor,
                                grad1 * scaleFactor};

              float fill2[4] = {(colors.oscilloscope[0] * grad2 + colors.background[0]) * scaleFactor,
                                (colors.oscilloscope[1] * grad2 + colors.background[1]) * scaleFactor,
                                (colors.oscilloscope[2] * grad2 + colors.background[2]) * scaleFactor,
                                grad2 * scaleFactor};

              float bgScaled[4] = {colors.background[0] * scaleFactor, colors.background[1] * scaleFactor,
                                   colors.background[2] * scaleFactor, 0.0f};

              // Triangle 1
              glColor4fv(bgScaled);
              glVertex2f(p1.first, centerY);
              glColor4fv(fill1);
              glVertex2f(p1.first, p1.second);
              glColor4fv(bgScaled);
              glVertex2f(p2.first, centerY);

              // Triangle 2
              glColor4fv(fill1);
              glVertex2f(p1.first, p1.second);
              glColor4fv(fill2);
              glVertex2f(p2.first, p2.second);
              glColor4fv(bgScaled);
              glVertex2f(p2.first, centerY);
            }
            glEnd();
          } else { // vertical
            const float invCenterY = 1.0f / centerY;
            const float gradientScale = 0.5f;

            glBegin(GL_QUAD_STRIP);
            for (const auto& point : waveformPoints) {
              float dist = std::min(std::abs(point.second - centerY) * invCenterY, 1.0f);
              float grad = dist * gradientScale;

              float fill[4] = {(colors.oscilloscope[0] * grad + colors.background[0] * (1.0f - grad)) * scaleFactor,
                               (colors.oscilloscope[1] * grad + colors.background[1] * (1.0f - grad)) * scaleFactor,
                               (colors.oscilloscope[2] * grad + colors.background[2] * (1.0f - grad)) * scaleFactor,
                               grad * scaleFactor};

              float bgScaled[4] = {colors.background[0] * scaleFactor, colors.background[1] * scaleFactor,
                                   colors.background[2] * scaleFactor, 0.0f};

              glColor4fv(fill);
              glVertex2f(point.first, point.second);
              glVertex2f(point.first, centerY);
            }
            glEnd();
          }
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        currentTexIdx = dstIdx;
      }
    } else {
      // Standard non-phosphor rendering
      if (osc.gradient_mode == "horizontal") {
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
        for (const auto& point : waveformPoints) {
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

      Graphics::drawAntialiasedLines(waveformPoints, colors.oscilloscope, 2.0f);
    }
  }
}

int OscilloscopeVisualizer::getPosition() const { return position; }
void OscilloscopeVisualizer::setPosition(int pos) { position = pos; }
int OscilloscopeVisualizer::getWidth() const { return width; }
void OscilloscopeVisualizer::setWidth(int w) { width = w; }
int OscilloscopeVisualizer::getRightEdge(const AudioData&) const { return position + width; }