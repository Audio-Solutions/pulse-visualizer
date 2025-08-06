#pragma once
#include "common.hpp"
#include "window_manager.hpp"

namespace Lissajous {

// Lissajous figure data and window
extern std::vector<std::pair<float, float>> points;
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Render the Lissajous figure visualization
 */
void render();

} // namespace Lissajous

namespace Oscilloscope {

// Oscilloscope data and window
extern std::vector<std::pair<float, float>> points;
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Render the oscilloscope visualization
 */
void render();

} // namespace Oscilloscope

namespace SpectrumAnalyzer {

// Spectrum analyzer data and window
extern std::vector<std::pair<float, float>> pointsMain;
extern std::vector<std::pair<float, float>> pointsAlt;
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Render the spectrum analyzer visualization
 */
void render();
} // namespace SpectrumAnalyzer

namespace Spectrogram {

// Spectrogram window
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Normalize decibel value to 0-1 range
 * @param db Decibel value to normalize
 * @return Normalized value between 0 and 1
 */
float normalize(float db);

/**
 * @brief Find frequency range indices for given frequency
 * @param freqs Frequency array
 * @param f Target frequency
 * @return Pair of start and end indices
 */
std::pair<size_t, size_t> find(const std::vector<float>& freqs, float f);

/**
 * @brief Map spectrum data to visualization format
 * @param in Input spectrum data
 * @return Mapped spectrum data
 */
std::vector<float>& mapSpectrum(const std::vector<float>& in);

/**
 * @brief Render the spectrogram visualization
 */
void render();

} // namespace Spectrogram

namespace LUFS {

// LUFS window
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Render the LUFS visualization
 */
void render();
} // namespace LUFS

namespace VU {

// VU window
extern WindowManager::VisualizerWindow* window;

/**
 * @brief Render the VU visualization
 */
void render();
} // namespace VU