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
extern bool broken;

enum Rotation { ROTATION_0 = 0, ROTATION_90 = 1, ROTATION_180 = 2, ROTATION_270 = 3 };

/**
 * @brief Application configuration options structure
 */
struct Options {
  struct Oscilloscope {
    float beam_multiplier = 1.0f;
    bool flip_x = false;
    Rotation rotation = ROTATION_0;
    float window = 50.0f;

    struct Pitch {
      bool follow = true;
      std::string type = "zero_crossing";
      std::string alignment = "center";
      int cycles = 3;
      float min_cycle_time = 16.0f;
    } pitch;

    struct Lowpass {
      bool enabled = false;
      float cutoff = 200.0f;
      int order = 4;
    } lowpass;

    struct Bandpass {
      float bandwidth = 10.0f;
      float sidelobe = 60.0f;
    } bandpass;
  } oscilloscope;

  struct Lissajous {
    float beam_multiplier = 1.0f;
    float readback_multiplier = 1.0f;
    std::string mode = "none";
    Rotation rotation = ROTATION_0;
  } lissajous;

  struct FFT {
    float beam_multiplier = 1.0f;
    Rotation rotation = ROTATION_0;
    bool flip_x = false;
    bool markers = true;
    int size = 4096;
    float slope = 3.0f;
    std::string key = "sharp";
    std::string mode = "midside";

    struct Limits {
      float max_db = 0.0f;
      float max_freq = 22000.0f;
      float min_db = -60.0f;
      float min_freq = 10.0f;
    } limits;

    struct Smoothing {
      bool enabled = true;
      float fall_speed = 50.0f;
      float hover_fall_speed = 10.0f;
      float rise_speed = 500.0f;
    } smoothing;

    struct CQT {
      int bins_per_octave = 60;
      bool enabled = true;
    } cqt;

    struct Sphere {
      bool enabled = true;
      float max_freq = 5000.0f;
      float base_radius = 0.1f;
    } sphere;
  } fft;

  struct Spectrogram {
    float window = 2.0f;
    bool interpolation = true;
    std::string frequency_scale = "log";

    struct Limits {
      float max_db = -10.0f;
      float max_freq = 22000.0f;
      float min_db = -60.0f;
      float min_freq = 20.0f;
    } limits;
  } spectrogram;

  struct Audio {
    float silence_threshold = -100.0f;
    float sample_rate = 44100.0f;
    float gain_db = 0.0f;
    std::string engine = "auto";
    std::string device = "auto";
  } audio;

  std::map<std::string, std::vector<std::string>> visualizers;

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

    struct Beam {
      float energy = 90.0f;
      bool rainbow = false;
      float width = 0.5f;
      float tension = 0.5f;
    } beam;

    struct Blur {
      float spread = 128.0f;
      float range = 2.0f;
      float near_intensity = 0.6f;
      float far_intensity = 0.8f;
    } blur;

    struct Decay {
      float fast = 40.0f;
      float slow = 6.0f;
      int threshold = 14;
    } decay;

    struct Screen {
      float curvature = 0.1f;
      float gap = 0.03f;
      float vignette = 0.3f;
      float chromatic_aberration = 0.008f;
      float grain = 0.1f;
    } screen;
  } phosphor;

  struct LUFS {
    std::string mode = "momentary";
    std::string scale = "linear";
    std::string label = "off";
  } lufs;

  struct VU {
    float window = 100.0f;
    std::string style = "digital";
    float calibration_db = 0.0f;
    std::string scale = "linear";
    float needle_width = 2.0f;

    struct Momentum {
      bool enabled = true;
      float damping_ratio = 10.0f;
      float spring_constant = 500.0f;
    } momentum;
  } vu;

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

/**
 * @brief Read a value from the configuration into an output reference.
 * @tparam T Target value type
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, const std::string& path, T& out);

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
 * @brief Save configuration to file
 * @return true if saved properly, false otherwise
 */
bool save();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

/**
 * @brief Get the installation directory
 * @return Installation directory
 */
std::string getInstallDir();
} // namespace Config