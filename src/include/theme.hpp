#pragma once
#include "common.hpp"

namespace Theme {
struct Colors {
  float color[4] = {0};
  float selection[4] = {0};
  float text[4] = {0};
  float accent[4] = {0};
  float background[4] = {0};
  float bgaccent[4] = {0};

  float waveform[4] = {0};

  float rgb_waveform_opacity_with_history = 0;
  float history_low[4] = {0};
  float history_mid[4] = {0};
  float history_high[4] = {0};

  float waveform_low[4] = {0};
  float waveform_mid[4] = {0};
  float waveform_high[4] = {0};

  float oscilloscope_main[4] = {0};
  float oscilloscope_bg[4] = {0};

  float stereometer[4] = {0};

  float stereometer_low[4] = {0};
  float stereometer_mid[4] = {0};
  float stereometer_high[4] = {0};

  float spectrum_analyzer_main[4] = {0};
  float spectrum_analyzer_secondary[4] = {0};
  float spectrum_analyzer_frequency_lines[4] = {0};
  float spectrum_analyzer_reference_line[4] = {0};
  float spectrum_analyzer_threshold_line[4] = {0};

  float spectrogram_low = 0;
  float spectrogram_high = 0;
  float color_bars_low = 0;
  float color_bars_high = 0;
  float color_bars_opacity = 0;

  float spectrogram_main[4] = {0};
  float color_bars_main[4] = {0};

  float loudness_main[4] = {0};
  float loudness_text[4] = {0};
};

extern Colors colors;

#ifdef __linux__
extern int themeInotifyFd;
extern int themeInotifyWatch;
extern std::string currentThemePath;
#endif

float* alpha(float* color, float alpha);
void mix(float* a, float* b, float* out, float t);
void load(const std::string& name);
bool reload();

} // namespace Theme