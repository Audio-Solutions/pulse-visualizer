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
  const float t = Config::Lissajous::PHOSPHOR_TENSION;

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

  if (!audioData.lissajousPoints.empty() && audioData.hasValidPeak) {
    std::vector<std::pair<float, float>> points;
    points.reserve(audioData.lissajousPoints.size());

    // Convert points to screen coordinates (fixed scaling)
    for (const auto& point : audioData.lissajousPoints) {
      float x = position + (point.first + 1.0f) * width / 2;
      float y = (point.second + 1.0f) * width / 2;
      points.push_back({x, y});
    }

    // Generate spline points if enabled, otherwise use original points
    std::vector<std::pair<float, float>> displayPoints = points;
    if (Config::Lissajous::ENABLE_SPLINES && points.size() >= 4) {
      displayPoints = generateCatmullRomSpline(points, Config::Lissajous::SPLINE_SEGMENTS);
    }

    if (Config::Lissajous::ENABLE_PHOSPHOR) {
      // Calculate cumulative distances for length-based darkening
      std::vector<float> cumulativeDistances = calculateCumulativeDistances(displayPoints);
      float totalLength = cumulativeDistances.back();

      // Enable additive blending for phosphor effect
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);

      // Get the Lissajous color
      const auto& lissajousColor = Theme::ThemeManager::getLissajous();

      // Draw spline segments with length-based darkening
      glBegin(GL_LINES);
      for (size_t i = 1; i < displayPoints.size(); ++i) {
        const auto& p1 = displayPoints[i - 1];
        const auto& p2 = displayPoints[i];

        // Calculate distance from start of path
        float distanceFromStart = cumulativeDistances[i];

        // Normalize distance to 0-1 range
        float normalizedDistance = totalLength > 0.0f ? distanceFromStart / totalLength : 0.0f;

        // Calculate brightness factor based on distance (longer paths get brighter)
        // Use configurable parameters for phosphor effect
        float brightnessFactor = normalizedDistance * (Config::Lissajous::PHOSPHOR_MAX_BRIGHTNESS -
                                                       Config::Lissajous::PHOSPHOR_MIN_BRIGHTNESS) +
                                 Config::Lissajous::PHOSPHOR_MIN_BRIGHTNESS;
        brightnessFactor = std::clamp(brightnessFactor, Config::Lissajous::PHOSPHOR_MIN_BRIGHTNESS,
                                      Config::Lissajous::PHOSPHOR_MAX_BRIGHTNESS);

        // Tail fade: 1.0 at head, PHOSPHOR_FADE_FACTOR at tail
        float tailFade = Config::Lissajous::PHOSPHOR_FADE_FACTOR +
                         (1.0f - Config::Lissajous::PHOSPHOR_FADE_FACTOR) * (1.0f - float(i) / displayPoints.size());
        float finalBrightness = brightnessFactor * tailFade;

        float color[4] = {lissajousColor.r * finalBrightness, lissajousColor.g * finalBrightness,
                          lissajousColor.b * finalBrightness, finalBrightness};

        glColor4fv(color);
        glVertex2f(p1.first, p1.second);
        glVertex2f(p2.first, p2.second);
      }
      glEnd();

      // Reset blending
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
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

  if (audioData.availableSamples > 0) {
    // Calculate the target position for phase correction
    size_t targetPos = (audioData.writePos + audioData.BUFFER_SIZE - audioData.DISPLAY_SAMPLES) % audioData.BUFFER_SIZE;

    // Use pre-computed bandpassed data for zero crossing detection
    size_t searchRange = static_cast<size_t>(audioData.samplesPerCycle);
    size_t zeroCrossPos = targetPos;
    if (!audioData.bandpassedMid.empty() && audioData.pitchConfidence > 0.5f) {
      // Search backward for the nearest positive zero crossing in bandpassed signal
      for (size_t i = 1; i < searchRange && i < audioData.bandpassedMid.size(); i++) {
        size_t pos = (targetPos + audioData.BUFFER_SIZE - i) % audioData.BUFFER_SIZE;
        size_t prevPos = (pos + audioData.BUFFER_SIZE - 1) % audioData.BUFFER_SIZE;
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
    float phaseOffset = static_cast<float>((targetPos + audioData.BUFFER_SIZE - zeroCrossPos) % audioData.BUFFER_SIZE) +
                        threeQuarterCycle;

    // Calculate the start position for reading samples, adjusted for phase
    size_t startPos = (audioData.writePos + audioData.BUFFER_SIZE - audioData.DISPLAY_SAMPLES) % audioData.BUFFER_SIZE;
    if (audioData.samplesPerCycle > 0.0f && audioData.pitchConfidence > 0.5f) {
      // Move backward by the phase offset
      startPos = (startPos + audioData.BUFFER_SIZE - static_cast<size_t>(phaseOffset)) % audioData.BUFFER_SIZE;
    }

    // Create waveform points
    std::vector<std::pair<float, float>> waveformPoints;
    waveformPoints.reserve(audioData.DISPLAY_SAMPLES);

    float amplitudeScale = audioData.windowHeight * Config::Oscilloscope::AMPLITUDE_SCALE;
    for (size_t i = 0; i < audioData.DISPLAY_SAMPLES; i++) {
      size_t pos = (startPos + i) % audioData.BUFFER_SIZE;
      float x = (static_cast<float>(i) * width) / audioData.DISPLAY_SAMPLES;
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
      // Handle different gradient modes
      switch (Config::Oscilloscope::GRADIENT_MODE) {
      case Config::Oscilloscope::OFF:
        // No gradient fill - just draw the waveform line
        break;

      case Config::Oscilloscope::HORIZONTAL: {
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
        break;
      }

      case Config::Oscilloscope::VERTICAL: {
        // Vertical gradient: intensity based on distance from center line (current implementation)
        glBegin(GL_QUAD_STRIP);
        for (const auto& point : waveformPoints) {
          // Calculate distance from center line (0.0 = at center, 1.0 = at edge)
          float distanceFromCenter = std::abs(point.second - audioData.windowHeight / 2) / (audioData.windowHeight / 2);
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
        break;
      }
      }

      float oscilloscopeColorArray[4] = {oscilloscopeColor.r, oscilloscopeColor.g, oscilloscopeColor.b,
                                         oscilloscopeColor.a};
      Graphics::drawAntialiasedLines(waveformPoints, oscilloscopeColorArray, 2.0f);
    }
  }
}

void FFTVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the FFT visualizer
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  if (!audioData.fftMagnitudesMid.empty() && !audioData.fftMagnitudesSide.empty()) {
    const float minFreq = Config::FFT::FFT_MIN_FREQ;
    const float maxFreq = Config::FFT::FFT_MAX_FREQ;
    const float sampleRate = Config::Audio::SAMPLE_RATE;
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
      Graphics::drawText(label, x - textWidth / 2, y, 10.0f, gridColorArray);
    };
    drawFreqLabel(100.0f, "100 Hz");
    drawFreqLabel(1000.0f, "1 kHz");
    drawFreqLabel(10000.0f, "10 kHz");

    // Slope correction factor for 4.5dB/octave
    const float slope_k = Config::FFT::FFT_SLOPE_CORRECTION_DB / 20.0f / log10f(2.0f);

    // Get the spectrum color for both FFT curves
    const auto& spectrumColor = Theme::ThemeManager::getSpectrum();

    // Determine which data to use based on FFT mode
    const std::vector<float>* mainMagnitudes = nullptr;
    const std::vector<float>* alternateMagnitudes = nullptr;

    // Temporary vectors for computed LEFTRIGHT mode data
    std::vector<float> leftMagnitudes, rightMagnitudes;

    if (Config::FFT::MODE == Config::FFT::LEFTRIGHT) {
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
      float dB_min = Config::FFT::FFT_DISPLAY_MIN_DB;
      float dB_max = Config::FFT::FFT_DISPLAY_MAX_DB;
      float y = (dB - dB_min) / (dB_max - dB_min) * audioData.windowHeight;
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
      float dB_min = Config::FFT::FFT_DISPLAY_MIN_DB;
      float dB_max = Config::FFT::FFT_DISPLAY_MAX_DB;
      float y = (dB - dB_min) / (dB_max - dB_min) * audioData.windowHeight;
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
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, textColorArray);
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