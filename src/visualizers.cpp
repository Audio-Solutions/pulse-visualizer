#include "visualizers.hpp"
#include "graphics.hpp"
#include "config.hpp"
#include "audio_processing.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>
#include <GL/gl.h>

// Define to use raw signal instead of bandpassed signal for oscilloscope display (bandpassed signal is for debugging)
#define SCOPE_USE_RAW_SIGNAL

namespace Visualizers {

void drawLissajous(const AudioData& audioData, int lissajousSize) {
  Graphics::setupViewport(0, 0, lissajousSize, lissajousSize, audioData.windowHeight);

  // Draw Lissajous curve using OpenGL
  if (!audioData.lissajousPoints.empty()) {
    std::vector<std::pair<float, float>> points;
    points.reserve(audioData.lissajousPoints.size());
    
    // Convert points to screen coordinates (fixed scaling)
    for (const auto& point : audioData.lissajousPoints) {
      float x = (point.first + 1.0f) * lissajousSize / 2;
      float y = (point.second + 1.0f) * lissajousSize / 2;
      points.push_back({x, y});
    }

    // Draw smooth lines between points using OpenGL
    Graphics::drawAntialiasedLines(points, Config::Colors::VISUALIZER, 2.0f);
  }
}

void drawOscilloscope(const AudioData& audioData, int scopeWidth) {
  int lissajousSize = audioData.windowHeight;
  Graphics::setupViewport(lissajousSize, 0, scopeWidth, audioData.windowHeight, audioData.windowHeight);

  if (audioData.availableSamples > 0) {
    // Calculate the target position for phase correction
    size_t targetPos = (audioData.writePos + audioData.BUFFER_SIZE - audioData.DISPLAY_SAMPLES) % audioData.BUFFER_SIZE;
    
    // Use pre-computed bandpassed data for zero crossing detection
    size_t searchRange = static_cast<size_t>(audioData.samplesPerCycle);
    size_t zeroCrossPos = targetPos;
    if (!audioData.bandpassed.empty() && audioData.pitchConfidence > 0.5f) {
      // Search backward for the nearest positive zero crossing in bandpassed signal
      for (size_t i = 1; i < searchRange && i < audioData.bandpassed.size(); i++) {
        size_t pos = (targetPos + audioData.BUFFER_SIZE - i) % audioData.BUFFER_SIZE;
        size_t prevPos = (pos + audioData.BUFFER_SIZE - 1) % audioData.BUFFER_SIZE;
        if (audioData.bandpassed[prevPos] < 0.0f && audioData.bandpassed[pos] >= 0.0f) {
          zeroCrossPos = pos;
          break;
        }
      }
    }
    
    // Calculate phase offset to align with peak (3/4 cycle after zero crossing)
    float threeQuarterCycle = audioData.samplesPerCycle * 0.75f;
    float phaseOffset = static_cast<float>((targetPos + audioData.BUFFER_SIZE - zeroCrossPos) % audioData.BUFFER_SIZE) + threeQuarterCycle;
    
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
      float x = (static_cast<float>(i) * scopeWidth) / audioData.DISPLAY_SAMPLES;
#ifdef SCOPE_USE_RAW_SIGNAL
      float y = audioData.windowHeight/2 + audioData.buffer[pos] * amplitudeScale;
#else
      float y = audioData.windowHeight/2 + audioData.bandpassed[pos] * amplitudeScale;
#endif
      waveformPoints.push_back({x, y});
    }
    
    // Create fill color by blending VISUALIZER and BACKGROUND
    float fillColor[4];
    for (int i = 0; i < 4; i++) {
      fillColor[i] = Config::Colors::VISUALIZER[i] * 0.15f + Config::Colors::BACKGROUND[i] * 0.85f;
    }
    
    // Draw filled area below waveform
    glBegin(GL_QUAD_STRIP);
    glColor4fv(fillColor);
    for (const auto& point : waveformPoints) {
      glVertex2f(point.first, point.second);
      glVertex2f(point.first, audioData.windowHeight/2);  // Zero line
    }
    glEnd();
    
    // Draw the waveform line using OpenGL
    Graphics::drawAntialiasedLines(waveformPoints, Config::Colors::VISUALIZER, 2.0f);
  }
}

void drawFFT(const AudioData& audioData, int fftWidth) {
  Graphics::setupViewport(audioData.windowWidth - fftWidth, 0, fftWidth, audioData.windowHeight, audioData.windowHeight);

  if (!audioData.fftMagnitudes.empty()) {
    const float minFreq = Config::Audio::FFT_MIN_FREQ;
    const float maxFreq = Config::Audio::FFT_MAX_FREQ;
    const float sampleRate = Config::Audio::SAMPLE_RATE;
    const int fftSize = (audioData.fftMagnitudes.size() - 1) * 2;

    // Draw frequency scale lines using OpenGL
    auto drawFreqLine = [&](float freq) {
      if (freq < minFreq || freq > maxFreq) return;
      float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
      float x = logX * fftWidth;
      Graphics::drawAntialiasedLine(x, 0, x, audioData.windowHeight, Config::Colors::GRID, 1.0f);
    };

    // Only show note text if we have a valid peak
    if (audioData.hasValidPeak) {
      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", 
               audioData.peakDb, audioData.peakFreq, audioData.peakNote.c_str(), 
               audioData.peakOctave, audioData.peakCents);
      float overlayX = 10.0f;  // Relative to FFT viewport
      float overlayY = audioData.windowHeight - 20.0f;

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
      
      // --- LABELS for 100Hz, 1kHz, 10kHz ---
      auto drawFreqLabel = [&](float freq, const char* label) {
        float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
        float x = logX * fftWidth;
        float y = 8.0f;
        
        // Draw background rectangle
        float textWidth = 40.0f;  // Approximate width for the labels
        float textHeight = 12.0f; // Height of the text
        float padding = 4.0f;     // Padding around text
        Graphics::drawFilledRect(x - textWidth/2 - padding, y - textHeight/2 - padding,
                               textWidth + padding*2, textHeight + padding*2, Config::Colors::BACKGROUND);
        
        // Draw the text
        Graphics::drawText(label, x - textWidth/2, y, 10.0f, Config::Colors::GRID);
      };
      drawFreqLabel(100.0f, "100 Hz");
      drawFreqLabel(1000.0f, "1 kHz");
      drawFreqLabel(10000.0f, "10 kHz");

      // Slope correction factor for 4.5dB/octave (NOTE: To be implemented into config)
      const float slope_k = 0.747f; // (4.5/20) / log10(2)

      // Draw FFT using pre-computed smoothed values
      std::vector<std::pair<float, float>> fftPoints;
      fftPoints.reserve(audioData.smoothedMagnitudes.size());
      for (size_t bin = 1; bin < audioData.smoothedMagnitudes.size(); ++bin) {
        float freq = (float)bin * sampleRate / fftSize;
        if (freq < minFreq || freq > maxFreq) continue;
        float logX = (log(freq) - log(minFreq)) / (log(maxFreq) - log(minFreq));
        float x = logX * fftWidth;
        float magnitude = audioData.smoothedMagnitudes[bin];
        float gain = powf(freq / minFreq, slope_k);
        magnitude *= gain;
        float dB = 20.0f * log10f(magnitude * 0.0002f + 1e-9f);
        float dB_min = Config::Audio::FFT_MIN_DB;
        float dB_max = Config::Audio::FFT_MAX_DB;
        float y = (dB - dB_min) / (dB_max - dB_min) * audioData.windowHeight;
        fftPoints.push_back({x, y});
      }
      // Draw the FFT curve using OpenGL
      Graphics::drawAntialiasedLines(fftPoints, Config::Colors::VISUALIZER, 2.0f);

      // Now draw overlay text on top of everything
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, Config::Colors::TEXT);
    }
  }
}

void drawSplitter(const AudioData& audioData, int splitterX) {
  Graphics::setupViewport(0, 0, audioData.windowWidth, audioData.windowHeight, audioData.windowHeight);

  // Draw splitter using OpenGL
  Graphics::drawAntialiasedLine(
    static_cast<float>(splitterX), 0, 
    static_cast<float>(splitterX), static_cast<float>(audioData.windowHeight), 
    Config::Colors::SPLITTER, 2.0f
  );
}

} 