#pragma once
#include "common.hpp"
#include "window_manager.hpp"

namespace Lissajous {
extern std::vector<std::pair<float, float>> points;
extern WindowManager::VisualizerWindow* window;

void render();
} // namespace Lissajous

namespace Oscilloscope {
extern std::vector<std::pair<float, float>> points;
extern WindowManager::VisualizerWindow* window;

void render();
} // namespace Oscilloscope

namespace SpectrumAnalyzer {
extern std::vector<std::pair<float, float>> pointsMain;
extern std::vector<std::pair<float, float>> pointsAlt;
extern WindowManager::VisualizerWindow* window;

void render();
} // namespace SpectrumAnalyzer

namespace Spectrogram {
extern WindowManager::VisualizerWindow* window;

float normalize(float db);
std::pair<size_t, size_t> find(const std::vector<float>& freqs, float f);
std::vector<float>& mapSpectrum(const std::vector<float>& in);
void render();
} // namespace Spectrogram