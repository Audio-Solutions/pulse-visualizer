/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "common.hpp"

namespace Theme {

/**
 * @brief Color scheme structure containing all theme colors
 */
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

  float vu_main[4] = {0};
  float vu_caution[4] = {0};
  float vu_clip[4] = {0};

  float phosphor_border[4] = {0};
};

extern Colors colors;

#ifdef __linux__
extern int themeInotifyFd;
extern int themeInotifyWatch;
extern std::string currentThemePath;
#endif

/**
 * @brief Apply alpha transparency to a color
 * @param color Input color array (RGBA)
 * @param alpha Alpha value to apply
 * @return Pointer to temporary color with applied alpha
 */
float* alpha(float* color, float alpha);

/**
 * @brief Linear interpolation between two colors
 * @param a First color array
 * @param b Second color array
 * @param out Output color array
 * @param t Interpolation factor (0.0 to 1.0)
 */
void mix(float* a, float* b, float* out, float t);

/**
 * @brief Load a theme from file
 */
void load();

/**
 * @brief Check if theme file has been modified and reload if necessary
 * @return true if theme was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

} // namespace Theme