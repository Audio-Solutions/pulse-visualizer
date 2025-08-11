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

namespace Config {

enum Rotation { ROTATION_0 = 0, ROTATION_90 = 1, ROTATION_180 = 2, ROTATION_270 = 3 };

/**
 * @brief Application configuration options structure
 */
struct Options {
  struct Oscilloscope {
    bool follow_pitch = true;
    std::string alignment = "left";
    std::string alignment_type = "peak";
    bool limit_cycles = false;
    int cycles = 1;
    float min_cycle_time = 16.0f;
    float time_window = 64.0f;
    float beam_multiplier = 1.0f;
    bool enable_lowpass = false;
    Rotation rotation = ROTATION_0;
    bool flip_x = false;
  } oscilloscope;

  struct BandpassFilter {
    float bandwidth = 50.0f;
    std::string bandwidth_type = "hz";
    int order = 8;
  } bandpass_filter;

  struct Lissajous {
    bool enable_splines = true;
    float beam_multiplier = 1.0f;
    float readback_multiplier = 1.0f;
    std::string mode = "none";
    Rotation rotation = ROTATION_0;
  } lissajous;

  struct FFT {
    float min_freq = 10.0f;
    float max_freq = 20000.0f;
    float sample_rate = 44100.0f;
    float slope_correction_db = 4.5f;
    float min_db = -60.0f;
    float max_db = 10.0f;
    std::string stereo_mode = "midside";
    std::string note_key_mode = "sharp";
    bool enable_cqt = false;
    int cqt_bins_per_octave = 24;
    bool enable_smoothing = false;
    float rise_speed = 500.0f;
    float fall_speed = 50.0f;
    float hover_fall_speed = 10.0f;
    int size = 4096;
    float beam_multiplier = 1.0f;
    bool frequency_markers = false;
    Rotation rotation = ROTATION_0;
    bool flip_x = false;
  } fft;

  struct Spectrogram {
    float time_window = 2.0f;
    float min_db = -80.0f;
    float max_db = -10.0f;
    bool interpolation = true;
    std::string frequency_scale = "log";
    float min_freq = 20.0f;
    float max_freq = 20000.0f;
  } spectrogram;

  struct Audio {
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    float gain_db = 0.0f;
    std::string engine = "auto";
    std::string device = "auto";
  } audio;

  std::vector<std::string> visualizers;

  struct Window {
    int default_width = 1080;
    int default_height = 200;
    std::string theme = "mocha.txt";
    int fps_limit = 240;
    bool decorations = true;
    bool always_on_top = false;
  } window;

  struct Debug {
    bool log_fps = false;
    bool show_bandpassed = false;
  } debug;

  struct Phosphor {
    bool enabled = false;
    float near_blur_intensity = 0.2f;
    float far_blur_intensity = 0.6f;
    float beam_energy = 100.0f;
    float decay_slow = 10.0f;
    float decay_fast = 100.0f;
    float line_blur_spread = 16.0f;
    float line_width = 2.0f;
    int age_threshold = 256;
    float range_factor = 5.0f;
    bool enable_grain = true;
    float tension = 0.5f;
    bool enable_curved_screen = false;
    float screen_curvature = 0.3f;
    float screen_gap = 0.05f;
    float grain_strength = 0.5f;
    float vignette_strength = 0.3f;
    float chromatic_aberration_strength = 0.003f;
    bool colorbeam = false;
  } phosphor;

  struct LUFS {
    std::string mode = "momentary";
    std::string scale = "linear";
    std::string label = "off";
  } lufs;

  struct VU {
    float time_window = 0.3f;
    std::string style = "digital";
    float calibration_db = 0.0f;
    std::string scale = "linear";
    bool enable_momentum = false;
    float spring_constant = 0.01f;
    float damping_ratio = 0.1f;
    float needle_width = 5.0f;
  } vu;

  struct Lowpass {
    float cutoff = 20000.0f;
    int order = 1;
  } lowpass;

  std::string font;
};

extern Options options;

#ifdef __linux__
extern int inotifyFd;
extern int inotifyWatch;
#endif

/**
 * @brief Copy default configuration files to user directory if they don't exist
 */
void copyFiles();

/**
 * @brief Get a nested node from YAML configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired node
 * @return Optional YAML node if found
 */
std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path);

template <typename T> T get(const YAML::Node& root, const std::string& path);

/**
 * @brief Load configuration from file
 */
void load();

/**
 * @brief Check if configuration file has been modified and reload if necessary
 * @return true if configuration was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

} // namespace Config