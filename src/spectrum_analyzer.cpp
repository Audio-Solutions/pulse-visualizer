#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace SpectrumAnalyzer {

// Spectrum analyzer data and window
std::vector<std::pair<float, float>> pointsMain;
std::vector<std::pair<float, float>> pointsAlt;
WindowManager::VisualizerWindow* window;

void render() {
  // Calculate logarithmic frequency scale
  float logMin = log(Config::options.fft.min_freq);
  float logMax = log(Config::options.fft.max_freq);

  // Set viewport for rendering
  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  // Draw frequency markers if enabled
  if (!Config::options.phosphor.enabled && Config::options.fft.frequency_markers) {
    auto drawLogLine = [&](float f) {
      if (f < Config::options.fft.min_freq || f > Config::options.fft.max_freq)
        return;
      float logX = (log(f) - logMin) / (logMax - logMin);
      float x = logX * window->width;
      Graphics::drawLine(x, 0, x, SDLWindow::height, Theme::colors.accent, 1.f);
    };

    // Draw frequency lines for each decade
    for (int decade = 1; decade <= 20000; decade *= 10) {
      for (int mult = 1; mult < 10; ++mult) {
        int freq = mult * decade;
        if (freq < Config::options.fft.min_freq)
          continue;
        if (freq > Config::options.fft.max_freq)
          break;
        drawLogLine(static_cast<float>(freq));
      }
    }
  }

  // Choose between smoothed and raw FFT data
  const std::vector<float>& inMain = Config::options.fft.enable_smoothing ? DSP::fftMid : DSP::fftMidRaw;
  const std::vector<float>& inAlt = Config::options.fft.enable_smoothing ? DSP::fftSide : DSP::fftSideRaw;

  // Prepare point arrays
  pointsMain.clear();
  pointsAlt.clear();
  pointsMain.resize(inMain.size());
  pointsAlt.resize(inAlt.size());

  // Generate main spectrum points
  for (size_t bin = 0; bin < inMain.size(); bin++) {
    // Calculate frequency for this bin
    float f;
    if (Config::options.fft.enable_cqt)
      f = DSP::ConstantQ::frequencies[bin];
    else
      f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inMain.size());

    // Convert to logarithmic X coordinate
    float logX = (log(f) - logMin) / (logMax - logMin);
    float x = logX * window->width;
    float mag = inMain[bin];

    // Apply slope correction
    float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);
    float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
    mag *= gain;

    // Convert to decibels and map to Y coordinate
    float dB = 20.f * log10f(mag + 1e-12f);
    float y = (dB - Config::options.fft.min_db) / (Config::options.fft.max_db - Config::options.fft.min_db) *
              SDLWindow::height;
    pointsMain[bin] = {x, y};
  }

  // Generate alternative spectrum points (for stereo visualization)
  for (size_t bin = 0; bin < inAlt.size() && !Config::options.phosphor.enabled; bin++) {
    // Calculate frequency for this bin
    float f;
    if (Config::options.fft.enable_cqt)
      f = DSP::ConstantQ::frequencies[bin];
    else
      f = static_cast<float>(bin) * (Config::options.audio.sample_rate / inAlt.size());

    // Convert to logarithmic X coordinate
    float logX = (log(f) - logMin) / (logMax - logMin);
    float x = logX * window->width;
    float mag = inAlt[bin];

    // Apply slope correction
    float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);
    float gain = powf(f * 1.0f / (440.0f * 2.0f), k);
    mag *= gain;

    // Convert to decibels and map to Y coordinate
    float dB = 20.f * log10f(mag + 1e-12f);
    float y = (dB - Config::options.fft.min_db) / (Config::options.fft.max_db - Config::options.fft.min_db) *
              SDLWindow::height;
    pointsAlt[bin] = {x, y};
  }

  // Choose rendering colors
  float* color = Theme::colors.color;
  if (Theme::colors.spectrum_analyzer_main[3] > 1e-6f)
    color = Theme::colors.spectrum_analyzer_main;

  float* colorAlt = Theme::alpha(Theme::colors.color, 0.5f);
  if (Theme::colors.spectrum_analyzer_secondary[3] > 1e-6f)
    colorAlt = Theme::colors.spectrum_analyzer_secondary;

  // Render with phosphor effect if enabled
  if (Config::options.phosphor.enabled) {
    std::vector<float> vectorData;
    std::vector<float> energies;
    energies.reserve(pointsMain.size());
    vectorData.reserve(pointsMain.size() * 4);

    constexpr float REF_AREA = 400.f * 300.f;
    float energy = Config::options.phosphor.beam_energy / REF_AREA *
                   (Config::options.fft.enable_cqt ? window->width * SDLWindow::height : 400.f * 50.f);

    energy *= Config::options.fft.beam_multiplier * WindowManager::dt / 0.016f;

    float dt = 1.0f / Config::options.audio.sample_rate;

    for (size_t i = 0; i < pointsMain.size() - 1; i++) {
      const auto& p1 = pointsMain[i];
      const auto& p2 = pointsMain[i + 1];

      float dx = p2.first - p1.first;
      float dy = p2.second - p1.second;
      float segLen = std::max(sqrtf(dx * dx + dy * dy), 1e-12f);

      float totalE = energy * (dt / (Config::options.fft.enable_cqt ? segLen : 1.f / sqrtf(dx))) * 2.f;

      energies.push_back(totalE);
    }

    for (size_t i = 0; i < pointsMain.size(); i++) {
      vectorData.push_back(pointsMain[i].first);
      vectorData.push_back(pointsMain[i].second);
      vectorData.push_back(i < energies.size() ? energies[i] : 0);
      vectorData.push_back(0);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, window->phosphor.vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, vectorData.size() * sizeof(float), vectorData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    Graphics::Phosphor::render(window, pointsMain, energies, color);
    window->draw();
  } else {
    Graphics::drawLines(window, pointsAlt, colorAlt);
    Graphics::drawLines(window, pointsMain, color);
  }

  // Handle hover functionality and pitch display
  static std::string noteNamesSharp[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static std::string noteNamesFlat[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  std::string* noteNames = (Config::options.fft.note_key_mode == "sharp") ? noteNamesSharp : noteNamesFlat;

  char overlay[128];
  float x = 10.0f;
  float y = SDLWindow::height - 20.0f;

  // Convert mouse coordinates to window-relative coordinates
  float mouseXRel = SDLWindow::mouseX - window->x;
  float mouseYRel = SDLWindow::mouseY;

  if (window->hovering && mouseXRel >= 0 && mouseXRel < window->width && mouseYRel >= 0 &&
      mouseYRel < SDLWindow::height) {
    // Check if mouse is within window bounds
    // Draw crosshair
    glColor4fv(color);
    glLineWidth(2.0f);
    glBegin(GL_LINES);

    // Draw horizontal line from left edge to right edge
    glVertex2f(0, mouseYRel);
    glVertex2f(window->width, mouseYRel);

    // Draw vertical line from top edge to bottom edge
    glVertex2f(mouseXRel, 0);
    glVertex2f(mouseXRel, SDLWindow::height);

    glEnd();

    // Convert coordinates to frequency and dB
    float logMin = log(Config::options.fft.min_freq);
    float logMax = log(Config::options.fft.max_freq);
    float logX = mouseXRel / window->width;
    float logFreq = logMin + logX * (logMax - logMin);
    float freq = exp(logFreq);

    float normalizedY = mouseYRel / SDLWindow::height;
    float dB = Config::options.fft.min_db + normalizedY * (Config::options.fft.max_db - Config::options.fft.min_db);
    float k = Config::options.fft.slope_correction_db / 20.f / log10f(2.f);
    float gain = powf(freq * 1.0f / (440.0f * 2.0f), -k);
    dB += 20.f * log10f(gain);

    // Get note information and display
    auto [note, octave, cents] = DSP::toNote(freq, noteNames);
    snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", dB, freq, note.c_str(), octave,
             cents);
    Graphics::Font::drawText(overlay, x, y, 14.f, Theme::colors.text);
  } else if (DSP::pitchDB > Config::options.audio.silence_threshold) {
    auto [note, octave, cents] = DSP::toNote(DSP::pitch, noteNames);
    snprintf(overlay, sizeof(overlay), "%6.2f dB  |  %7.2f Hz  |  %s%d %+d Cents", DSP::pitchDB, DSP::pitch,
             note.c_str(), octave, cents);
    Graphics::Font::drawText(overlay, x, y, 14.f, Theme::colors.text);
  }
}

} // namespace SpectrumAnalyzer