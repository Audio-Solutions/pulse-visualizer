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

// Computed caches (no theme colors needed - using centralized access)

// Computed values from config (cached for performance)
float FFTVisualizer::logMinFreq = 0.0f;
float FFTVisualizer::logMaxFreq = 0.0f;
float FFTVisualizer::logFreqRange = 0.0f;
float FFTVisualizer::dbRange = 0.0f;
const char** FFTVisualizer::noteNames = nullptr;
size_t FFTVisualizer::lastConfigVersion = 0;

void FFTVisualizer::updateCaches() {
  if (Config::getVersion() != lastConfigVersion) {
    const auto& fcfg = Config::values().fft;

    // Set note names based on key mode
    static const char* noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    noteNames = (fcfg.note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;

    lastConfigVersion = Config::getVersion();

    // Pre-compute logarithmic values
    logMinFreq = log(fcfg.min_freq);
    logMaxFreq = log(fcfg.max_freq);
    logFreqRange = logMaxFreq - logMinFreq;
    dbRange = fcfg.display_max_db - fcfg.display_min_db;
  }
}

void FFTVisualizer::draw(const AudioData& audioData, int) {
  // Set up the viewport for the FFT visualizer
  Graphics::setupViewport(position, 0, width, audioData.windowHeight, audioData.windowHeight);

  // Update cached values if needed
  updateCaches();

  const auto& fcfg = Config::values().fft;
  const auto& colors = Theme::ThemeManager::colors();

  if (!audioData.fftMagnitudesMid.empty() && !audioData.fftMagnitudesSide.empty()) {
    const int fftSize = (audioData.fftMagnitudesMid.size() - 1) * 2;

    // Draw frequency scale lines using OpenGL
    auto drawFreqLine = [&](float freq) {
      if (freq < fcfg.min_freq || freq > fcfg.max_freq)
        return;
      float logX = (log(freq) - logMinFreq) / logFreqRange;
      float x = logX * width;
      Graphics::drawAntialiasedLine(x, 0, x, audioData.windowHeight, colors.grid, 1.0f);
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
    for (float freq = 10000.0f; freq <= fcfg.max_freq; freq *= 10.0f) {
      drawFreqLine(freq);
    }

    // Draw frequency labels for 100Hz, 1kHz, 10kHz
    auto drawFreqLabel = [&](float freq, const char* label) {
      float logX = (log(freq) - logMinFreq) / logFreqRange;
      float x = logX * width;
      float y = 8.0f;

      // Draw background rectangle
      float textWidth = 40.0f;
      float textHeight = 12.0f;
      float padding = 4.0f;

      Graphics::drawFilledRect(x - textWidth / 2 - padding, y - textHeight / 2 - padding, textWidth + padding * 2,
                               textHeight + padding * 2, colors.background);

      // Draw the text
      Graphics::drawText(label, x - textWidth / 2, y, 10.0f, colors.grid, fcfg.font.c_str());
    };
    drawFreqLabel(100.0f, "100 Hz");
    drawFreqLabel(1000.0f, "1 kHz");
    drawFreqLabel(10000.0f, "10 kHz");

    // Determine which data to use based on FFT mode
    const std::vector<float>* mainMagnitudes = nullptr;
    const std::vector<float>* alternateMagnitudes = nullptr;

    // Temporary vectors for computed LEFTRIGHT mode data
    std::vector<float> leftMagnitudes, rightMagnitudes;

    // Pre-compute mode comparison for performance
    bool isLeftRightMode = (fcfg.stereo_mode == "leftright");

    if (isLeftRightMode) {
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

    // Pre-compute constants for magnitude processing
    const float freqScale = fcfg.sample_rate / fftSize;
    const float invLogFreqRange = 1.0f / logFreqRange;
    const float invDbRange = 1.0f / dbRange;
    const float gainBase = 1.0f / (440.0f * 2.0f); // Pre-compute for slope correction

    // Re-use static vectors to avoid a heap allocation every frame.  These are
    // cleared but their capacity persists for the next call.
    static thread_local std::vector<std::pair<float, float>> alternateFFTPoints;
    static thread_local std::vector<std::pair<float, float>> fftPoints;

    alternateFFTPoints.clear();
    fftPoints.clear();

    alternateFFTPoints.reserve(alternateMagnitudes->size());
    fftPoints.reserve(mainMagnitudes->size());

    for (size_t bin = 1; bin < alternateMagnitudes->size(); ++bin) {
      float freq = static_cast<float>(bin) * freqScale;
      if (freq < fcfg.min_freq || freq > fcfg.max_freq)
        continue;
      float logX = (log(freq) - logMinFreq) * invLogFreqRange;
      float x = logX * width;
      float magnitude = (*alternateMagnitudes)[bin];

      // Apply slope correction with pre-computed values
      float slope_k = fcfg.slope_correction_db / 20.0f / log10f(2.0f);
      float gain = powf(freq * gainBase, slope_k);
      magnitude *= gain;

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using cached display limits
      float y = (dB - fcfg.display_min_db) * invDbRange * audioData.windowHeight;
      alternateFFTPoints.push_back({x, y});
    }

    // Draw the Alternate FFT curve using centralized colors
    constexpr float alternateFFTAlpha = 0.3f;
    float alternateFFTColorArray[4] = {
        colors.spectrum[0] * alternateFFTAlpha + colors.background[0] * (1.0f - alternateFFTAlpha),
        colors.spectrum[1] * alternateFFTAlpha + colors.background[1] * (1.0f - alternateFFTAlpha),
        colors.spectrum[2] * alternateFFTAlpha + colors.background[2] * (1.0f - alternateFFTAlpha),
        colors.spectrum[3] * alternateFFTAlpha + colors.background[3] * (1.0f - alternateFFTAlpha)};
    Graphics::drawAntialiasedLines(alternateFFTPoints, alternateFFTColorArray, 2.0f);

    // Draw Main FFT using pre-computed smoothed values
    for (size_t bin = 1; bin < mainMagnitudes->size(); ++bin) {
      float freq = static_cast<float>(bin) * freqScale;
      if (freq < fcfg.min_freq || freq > fcfg.max_freq)
        continue;
      float logX = (log(freq) - logMinFreq) * invLogFreqRange;
      float x = logX * width;
      float magnitude = (*mainMagnitudes)[bin];

      // Apply slope correction with pre-computed values
      float slope_k = fcfg.slope_correction_db / 20.0f / log10f(2.0f);
      float gain = powf(freq * gainBase, slope_k);
      magnitude *= gain;

      // Convert to dB using proper scaling
      float dB = 20.0f * log10f(magnitude + 1e-9f);

      // Map dB to screen coordinates using cached display limits
      float y = (dB - fcfg.display_min_db) * invDbRange * audioData.windowHeight;
      fftPoints.push_back({x, y});
    }

    // Draw the Main FFT curve using centralized colors
    Graphics::drawAntialiasedLines(fftPoints, colors.spectrum, 2.0f);

    // Draw crosshair and show mouse position info when hovering
    if (hovering) {
      // Draw crosshair lines
      Graphics::drawAntialiasedLine(mouseX, 0, mouseX, audioData.windowHeight, colors.spectrum, 2.0f);
      Graphics::drawAntialiasedLine(0, mouseY, width, mouseY, colors.spectrum, 2.0f);

      // Calculate frequency and dB from mouse position
      float frequency, actualDB;
      calculateFrequencyAndDB(mouseX, mouseY, audioData.windowHeight, frequency, actualDB);

      // Convert frequency to note
      std::string noteName;
      int octave, cents;
      freqToNote(frequency, noteName, octave, cents);

      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", actualDB, frequency,
               noteName.c_str(), octave, cents);
      float overlayX = 10.0f;
      float overlayY = audioData.windowHeight - 20.0f;

      // Draw overlay text using centralized colors
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, colors.text, fcfg.font.c_str());
    } else if (audioData.hasValidPeak) {
      // Show pitch detector data when not hovering
      char overlay[128];
      snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", audioData.peakDb,
               audioData.peakFreq, audioData.peakNote.c_str(), audioData.peakOctave, audioData.peakCents);
      float overlayX = 10.0f;
      float overlayY = audioData.windowHeight - 20.0f;

      // Draw overlay text using centralized colors
      Graphics::drawText(overlay, overlayX, overlayY, 14.0f, colors.text, fcfg.font.c_str());
    }
  }
}

int FFTVisualizer::getPosition() const { return position; }
void FFTVisualizer::setPosition(int pos) { position = pos; }
int FFTVisualizer::getWidth() const { return width; }
void FFTVisualizer::setWidth(int w) { width = w; }
int FFTVisualizer::getRightEdge(const AudioData&) const { return position + width; }

// Mouse tracking methods
void FFTVisualizer::updateMousePosition(float mouseX, float mouseY) {
  this->mouseX = mouseX;
  this->mouseY = mouseY;
}

void FFTVisualizer::setHovering(bool hovering) { this->hovering = hovering; }

bool FFTVisualizer::isHovering() const { return hovering; }

void FFTVisualizer::calculateFrequencyAndDB(float x, float y, float windowHeight, float& frequency,
                                            float& actualDB) const {
  const auto& fcfg = Config::values().fft;

  // Convert X coordinate to frequency
  float logX = x / width;
  frequency = exp(logX * logFreqRange + logMinFreq);

  // Convert Y coordinate to displayed dB (slope-corrected)
  float displayed_dB = (y / windowHeight) * dbRange + fcfg.display_min_db;

  // Remove slope correction to get actual dB
  const float gainBase = 1.0f / (440.0f * 2.0f);
  float slope_k = fcfg.slope_correction_db / 20.0f / log10f(2.0f);
  float slope_correction_dB = 20.0f * slope_k * log10f(frequency * gainBase);
  actualDB = displayed_dB - slope_correction_dB;
}

void FFTVisualizer::freqToNote(float freq, std::string& noteName, int& octave, int& cents) const {
  if (freq <= 0.0f || !noteNames) {
    noteName = "-";
    octave = 0;
    cents = 0;
    return;
  }

  // Calculate MIDI note number (A4 = 440Hz = MIDI 69)
  float midi = 69.0f + 12.0f * log2f(freq / 440.0f);

  // Handle out of range values
  if (midi < 0.0f || midi > 127.0f) {
    noteName = "-";
    octave = 0;
    cents = 0;
    return;
  }

  int midiInt = (int)roundf(midi);
  int noteIdx = (midiInt + 1200) % 12; // +1200 for negative safety
  octave = midiInt / 12 - 1;
  noteName = noteNames[noteIdx];
  cents = (int)roundf((midi - midiInt) * 100.0f);
}