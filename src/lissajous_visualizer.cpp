#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Static working buffers
std::vector<std::pair<float, float>> LissajousVisualizer::points;
std::vector<std::pair<float, float>> LissajousVisualizer::densePath;
std::vector<float> LissajousVisualizer::originalDistances;

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

// Persistence render-to-texture resources (shared)
GLuint persistenceFBO = 0;
GLuint persistenceTex[2] = {0, 0};
int currentTexIdx = 0;
int texWidth = 0;
int texHeight = 0;

// Shaders for separate fade and glow effects
GLuint fadeOnlyProgram = 0;
GLuint glowProgram = 0;

void ensureFadeOnlyProgram() {
  if (fadeOnlyProgram)
    return;

  fadeOnlyProgram = Graphics::createShaderProgram("shaders/fade_only.vert", "shaders/fade_only.frag");
  if (fadeOnlyProgram == 0) {
    std::cerr << "Failed to create fade-only shader program" << std::endl;
    return;
  }

  // Set vertex attribute location
  glBindAttribLocation(fadeOnlyProgram, 0, "pos");
}

void ensureGlowProgram() {
  if (glowProgram)
    return;

  glowProgram = Graphics::createShaderProgram("shaders/glow.vert", "shaders/glow.frag");
  if (glowProgram == 0) {
    std::cerr << "Failed to create glow shader program" << std::endl;
    return;
  }

  // Set vertex attribute location
  glBindAttribLocation(glowProgram, 0, "pos");
}

void createPersistenceResources(int w, int h, const float* bgColor) {
  bool needsReinit = (texWidth != w || texHeight != h || !persistenceTex[0] || !persistenceTex[1] || !persistenceFBO);

  if (!needsReinit) {
    return;
  }

  texWidth = w;
  texHeight = h;

  // Clean up existing resources if needed
  if (persistenceTex[0]) {
    glDeleteTextures(2, persistenceTex);
    persistenceTex[0] = persistenceTex[1] = 0;
  }
  if (persistenceFBO) {
    glDeleteFramebuffers(1, &persistenceFBO);
    persistenceFBO = 0;
  }

  // Create new resources
  glGenTextures(2, persistenceTex);
  glGenFramebuffers(1, &persistenceFBO);

  // Initialize textures with background color for proper persistence
  std::vector<unsigned char> bgPixels(texWidth * texHeight * 4);
  for (int i = 0; i < texWidth * texHeight; ++i) {
    bgPixels[i * 4 + 0] = static_cast<unsigned char>(bgColor[0] * 255);
    bgPixels[i * 4 + 1] = static_cast<unsigned char>(bgColor[1] * 255);
    bgPixels[i * 4 + 2] = static_cast<unsigned char>(bgColor[2] * 255);
    bgPixels[i * 4 + 3] = 255;
  }

  for (int i = 0; i < 2; ++i) {
    glBindTexture(GL_TEXTURE_2D, persistenceTex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, bgPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  // Reset texture index
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

        // Prepare VBO friendly buffers (2 floats per vertex, 4 floats per color)
        std::vector<float> vertices;
        vertices.reserve(densePath.size() * 4); // each segment adds 2 vertices (x,y)
        std::vector<float> colorsRGBA;
        colorsRGBA.reserve(densePath.size() * 8);

        // Convert base color to HSV once
        float baseH, baseS, baseV;
        rgbToHsv(colors.lissajous[0], colors.lissajous[1], colors.lissajous[2], baseH, baseS, baseV);

        for (size_t i = 1; i < densePath.size(); ++i) {
          const auto& p1 = densePath[i - 1];
          const auto& p2 = densePath[i];

          // Calculate beam movement
          float dx = p2.first - p1.first;
          float dy = p2.second - p1.second;
          float segmentLength = sqrtf(dx * dx + dy * dy);
          float beamSpeed = segmentLength * invWidth;

          // Realistic phosphor energy calculation
          // Base energy is proportional to signal amplitude (beam current)
          float signalAmplitude = std::min(1.0f, sqrtf(dx * dx + dy * dy) / (width * 0.1f));
          float baseEnergy = lis.phosphor_intensity_scale * (0.3f + 0.7f * signalAmplitude);

          // Configurable beam speed brightness modulation
          // Energy deposition is inversely proportional to beam speed (dwell time)
          float dwellTime = 1.0f;
          if (segmentLength > 0.001f) {
            // Use configurable curve and scale for beam speed modulation
            float normalizedSpeed = beamSpeed / (scaledMaxBeamSpeed * lis.beam_speed_brightness_scale);
            dwellTime = 1.0f / (1.0f + powf(normalizedSpeed, lis.beam_speed_brightness_curve));
            // Ensure minimum brightness to keep fast movements visible
            dwellTime = std::max(dwellTime, lis.min_beam_brightness);
          }

          // Time-based persistence decay (exponential)
          float normalizedPosition = static_cast<float>(i) * invDensePathSize;
          float ageFactor = expf(-normalizedPosition * lis.phosphor_decay_rate * 2.0f);

          // Spline density compensation - maintain energy conservation
          float densityFactor = 1.0f;
          if (lis.enable_splines && points.size() >= 4) {
            float splineDensityRatio = static_cast<float>(densePath.size()) / static_cast<float>(points.size());
            // Energy per segment should be reduced to maintain total energy
            densityFactor = 1.0f / sqrtf(std::max(1.0f, splineDensityRatio * 0.5f));
          }

          // Combine all energy factors
          float intensity = baseEnergy * dwellTime * ageFactor * densityFactor;

          // Apply realistic intensity curve with proper saturation
          intensity = std::min(intensity, 1.5f); // Allow slight overbrightness for bloom effect

          if (intensity <= 0.001f)
            continue;

          // HSV blending to transition through white at high intensity
          float outH = baseH;
          float outS;
          float outV;
          if (intensity > 0.5f) {
            float factor = (intensity - 0.5f) / 0.5f; // 0..1
            outS = baseS * (1.0f - factor);           // fade saturation towards 0 (white)
            outV = 1.0f;                              // full brightness
          } else {
            float factor = intensity / 0.5f; // 0..1
            outS = baseS;                    // keep base saturation
            outV = factor;                   // scale brightness
          }

          float r, g, b;
          hsvToRgb(outH, outS, outV, r, g, b);

          // Append to buffers (each vertex duplicated with same color)
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

        // --- Texture-based persistence with separate fade and glow passes ---
        // Ensure resources sized to current viewport (need additional texture for glow pass)
        createPersistenceResources(width, width, colors.background);

        int srcIdx = currentTexIdx;
        int tempIdx = 1 - currentTexIdx;
        int finalIdx = currentTexIdx; // We'll swap at the end

        // PASS 1: Apply fade-only to previous frame
        glBindFramebuffer(GL_FRAMEBUFFER, persistenceFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, persistenceTex[tempIdx], 0);

        // Set viewport for FBO
        glViewport(0, 0, width, width);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, 0, width, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        ensureFadeOnlyProgram();
        float dtDecay = std::clamp(Config::values().lissajous.texture_decay, 0.0f, 1.0f);

        glUseProgram(fadeOnlyProgram);
        glUniform1f(glGetUniformLocation(fadeOnlyProgram, "decay"), dtDecay);
        glUniform3f(glGetUniformLocation(fadeOnlyProgram, "bgColor"), colors.background[0], colors.background[1],
                    colors.background[2]);
        glUniform2f(glGetUniformLocation(fadeOnlyProgram, "texSize"), static_cast<float>(width),
                    static_cast<float>(width));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, persistenceTex[srcIdx]);
        glUniform1i(glGetUniformLocation(fadeOnlyProgram, "texPrev"), 0);

        glDisable(GL_BLEND);
        drawFullscreenTexturedQuad(width, width);
        glUseProgram(0);

        // PASS 2: Draw current beam additively onto faded frame
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

        // PASS 3: Apply glow effect to the combined result
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, persistenceTex[srcIdx], 0);

        ensureGlowProgram();
        glUseProgram(glowProgram);
        glUniform2f(glGetUniformLocation(glowProgram, "texSize"), static_cast<float>(width), static_cast<float>(width));
        glUniform1f(glGetUniformLocation(glowProgram, "glowIntensity"), 1.0f); // TODO: make configurable

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, persistenceTex[tempIdx]);
        glUniform1i(glGetUniformLocation(glowProgram, "texInput"), 0);

        glDisable(GL_BLEND);
        drawFullscreenTexturedQuad(width, width);
        glUseProgram(0);

        // Unbind FBO
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Restore original viewport and projection for main buffer
        Graphics::setupViewport(position, 0, width, width, audioData.windowHeight);

        // Draw final glowed texture to screen
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, persistenceTex[srcIdx]);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        drawFullscreenTexturedQuad(width, width);
        glDisable(GL_TEXTURE_2D);

        currentTexIdx = srcIdx;
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