/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
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
#include "types.hpp"

namespace ConfigSchema {

/**
 * @brief Schema-level page classification used by config metadata.
 */
enum class SchemaPage {
  Oscilloscope,
  Lissajous,
  FFT,
  Spectrogram,
  Waveform,
  Audio,
  Window,
  Debug,
  Phosphor,
  LUFS,
  VU,
  Unknown,
};

/**
 * @brief Extract root segment of a dot-separated config path.
 */
constexpr std::string_view rootSegment(std::string_view path) {
  const size_t dot = path.find('.');
  if (dot == std::string_view::npos)
    return path;
  return path.substr(0, dot);
}

/**
 * @brief Extract second path segment as group key, or "misc".
 */
constexpr std::string_view groupKeyForPath(std::string_view path) {
  const size_t firstDot = path.find('.');
  if (firstDot == std::string_view::npos)
    return "misc";

  const size_t secondDot = path.find('.', firstDot + 1);
  if (secondDot == std::string_view::npos || secondDot == firstDot + 1)
    return "misc";

  return path.substr(firstDot + 1, secondDot - firstDot - 1);
}

/**
 * @brief Map root path segment to schema page.
 */
constexpr SchemaPage pageFromRoot(std::string_view root) {
  if (root == "oscilloscope")
    return SchemaPage::Oscilloscope;
  if (root == "lissajous")
    return SchemaPage::Lissajous;
  if (root == "fft")
    return SchemaPage::FFT;
  if (root == "spectrogram")
    return SchemaPage::Spectrogram;
  if (root == "waveform")
    return SchemaPage::Waveform;
  if (root == "audio")
    return SchemaPage::Audio;
  if (root == "window" || root == "font")
    return SchemaPage::Window;
  if (root == "debug")
    return SchemaPage::Debug;
  if (root == "phosphor")
    return SchemaPage::Phosphor;
  if (root == "lufs")
    return SchemaPage::LUFS;
  if (root == "vu")
    return SchemaPage::VU;
  return SchemaPage::Unknown;
}

/**
 * @brief Map full config path to schema page.
 */
constexpr SchemaPage pageForPath(std::string_view path) { return pageFromRoot(rootSegment(path)); }

/**
 * @brief A selectable UI choice value and display label.
 * @tparam T Stored value type
 */
template <typename T> struct Choice {
  T value;
  std::string_view label;
};

/**
 * @brief UI metadata for scalar configuration fields.
 * @tparam T Field value type
 */
template <typename T> struct FieldUi {
  bool hasRange = false;
  T min = {};
  T max = {};
  int precision = 1;
  bool zeroOff = false;
  bool useTick = false;
  std::span<const T> detents = {};
  std::span<const Choice<T>> choices = {};

  /**
   * @brief Create slider metadata with numeric range.
   */
  static constexpr FieldUi slider(T minValue, T maxValue, int precisionValue = 1, bool zeroOffValue = false) {
    FieldUi ui;
    ui.hasRange = true;
    ui.min = minValue;
    ui.max = maxValue;
    ui.precision = precisionValue;
    ui.zeroOff = zeroOffValue;
    return ui;
  }

  /**
   * @brief Create detented slider metadata.
   */
  static constexpr FieldUi detentSlider(std::span<const T> values, int precisionValue = 1) {
    FieldUi ui;
    ui.detents = values;
    ui.precision = precisionValue;
    return ui;
  }

  /**
   * @brief Create drop-down enum metadata.
   */
  static constexpr FieldUi enumDrop(std::span<const Choice<T>> values) {
    FieldUi ui;
    ui.choices = values;
    ui.useTick = false;
    return ui;
  }

  /**
   * @brief Create tick enum metadata.
   */
  static constexpr FieldUi enumTick(std::span<const Choice<T>> values) {
    FieldUi ui;
    ui.choices = values;
    ui.useTick = true;
    return ui;
  }
};

/**
 * @brief UI metadata specialization for string fields.
 */
template <> struct FieldUi<std::string> {
  int precision = 1;
  bool useTick = false;
  std::span<const Choice<std::string_view>> choices = {};

  /**
   * @brief Create drop-down enum metadata for strings.
   */
  static constexpr FieldUi enumDrop(std::span<const Choice<std::string_view>> values) {
    FieldUi ui;
    ui.choices = values;
    ui.useTick = false;
    return ui;
  }

  /**
   * @brief Create tick enum metadata for strings.
   */
  static constexpr FieldUi enumTick(std::span<const Choice<std::string_view>> values) {
    FieldUi ui;
    ui.choices = values;
    ui.useTick = true;
    return ui;
  }
};

/**
 * @brief Strongly typed schema entry for a scalar config field.
 * @tparam T Field value type
 */
template <typename T> struct ScalarField {
  std::string_view path;
  std::string_view label;
  std::string_view description;
  T& (*get)(Config::Options&);
  const T& (*getConst)(const Config::Options&);
  FieldUi<T> ui;
  SchemaPage page;
  std::string_view groupKey;
};

#define PV_SCHEMA_FIELD(type, member, ...)                                                                             \
  ([](std::string_view path, type& (*get)(Config::Options&), const type& (*getConst)(const Config::Options&),          \
      std::string_view label = {}, std::string_view description = {}, FieldUi<type> ui = {}) constexpr {               \
    return ScalarField<type> {                                                                                         \
        path, label.empty() ? path : label, description, get, getConst, ui, pageForPath(path), groupKeyForPath(path)}; \
  })(                                                                                                                  \
      #member, +[](Config::Options& opts) -> type& { return opts.member; },                                            \
      +[](const Config::Options& opts) -> const type& { return opts.member; } __VA_OPT__(, ) __VA_ARGS__)

// clang-format off
inline constexpr std::array fftSizeDetents = {
  128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

inline constexpr std::array rotationChoices = {
  Choice<Config::Rotation>{Config::Rotation::ROTATION_0,   "0deg"},
  Choice<Config::Rotation>{Config::Rotation::ROTATION_90,  "90deg"},
  Choice<Config::Rotation>{Config::Rotation::ROTATION_180, "180deg"},
  Choice<Config::Rotation>{Config::Rotation::ROTATION_270, "270deg"},
};

inline constexpr std::array audioSampleRateChoices = {
  Choice<float>{ 44100.f,  "44.1kHz"},
  Choice<float>{ 48000.f,    "48kHz"},
  Choice<float>{ 88200.f,  "88.2kHz"},
  Choice<float>{ 96000.f,    "96kHz"},
  Choice<float>{176400.f, "176.4kHz"},
  Choice<float>{192000.f,   "192kHz"},
  Choice<float>{352800.f, "352.8kHz"},
  Choice<float>{384000.f,   "384kHz"},
};

inline constexpr std::array oscAlignmentChoices = {
  Choice<std::string_view>{"left",   "Left"},
  Choice<std::string_view>{"center", "Center"},
  Choice<std::string_view>{"right",  "Right"},
};

inline constexpr std::array oscPitchTypeChoices = {
  Choice<std::string_view>{"peak",          "Peak"},
  Choice<std::string_view>{"zero_crossing", "Zero crossing"},
};

inline constexpr std::array lissajousModeChoices = {
  Choice<std::string_view>{"rotate",     "Rotate"},
  Choice<std::string_view>{"circle",     "Circle"},
  Choice<std::string_view>{"pulsar",     "Pulsar"},
  Choice<std::string_view>{"black_hole", "Black hole"},
  Choice<std::string_view>{"normal",     "Normal"},
};

inline constexpr std::span<const Choice<std::string_view>> noStringChoices = {};

inline constexpr std::array fftKeyChoices = {
  Choice<std::string_view>{"sharp", "Sharp"},
  Choice<std::string_view>{"flat",  "Flat"},
};

inline constexpr std::array fftStereoChoices = {
  Choice<std::string_view>{"midside",   "Mid/Side"},
  Choice<std::string_view>{"leftright", "Left/Right"},
};

inline constexpr std::array frequencyScaleOptions = {
  Choice<std::string_view>{"log",    "Logarithmic"},
  Choice<std::string_view>{"linear", "Linear"},
  Choice<std::string_view>{"mel",    "Mel"},
};

inline constexpr std::array waveformModeChoices = {
  Choice<std::string_view>{"mono",   "Mono"},
  Choice<std::string_view>{"stereo", "Stereo"},
};

inline constexpr std::array audioEngineChoices = {
  Choice<std::string_view>{"auto",       "Auto"},
  Choice<std::string_view>{"pipewire",   "PipeWire"},
  Choice<std::string_view>{"pulseaudio", "PulseAudio"},
  Choice<std::string_view>{"wasapi",     "WASAPI"},
};

inline constexpr std::array lufsModeChoices = {
  Choice<std::string_view>{"shortterm",  "Short-term"},
  Choice<std::string_view>{"momentary",  "Momentary"},
  Choice<std::string_view>{"integrated", "Integrated"},
};

inline constexpr std::array linearScaleChoices = {
  Choice<std::string_view>{"log",    "Logarithmic"},
  Choice<std::string_view>{"linear", "Linear"},
};

inline constexpr std::array lufsLabelChoices = {
  Choice<std::string_view>{"on",      "On"},
  Choice<std::string_view>{"off",     "Off"},
  Choice<std::string_view>{"compact", "Compact"},
};

inline constexpr std::array vuStyleChoices = {
  Choice<std::string_view>{"analog",  "Analog"},
  Choice<std::string_view>{"digital", "Digital"},
};

inline constexpr std::array boolFields = {
  PV_SCHEMA_FIELD(
    bool, oscilloscope.flip_x,
    "Invert Y Axis",
    "Mirror the oscilloscope vertically."),
  PV_SCHEMA_FIELD(
    bool, oscilloscope.edge_compression.enabled,
    "Enable Edge Compression",
    "Apply non-linear edge compression for a Wave Candy-like look."),
  PV_SCHEMA_FIELD(
    bool, oscilloscope.pitch.follow,
    "Follow Detected Pitch",
    "Phase-align the waveform to the detected pitch for a steadier trace."),
  PV_SCHEMA_FIELD(
    bool, waveform.midline,
    "Show Midline",
    "Draw a center pixel when the signal is effectively silent (min and max nearly equal)."),
  PV_SCHEMA_FIELD(
    bool, oscilloscope.lowpass.enabled,
    "Enable Low-Pass Filter",
    "Apply a low-pass filter before rendering the oscilloscope waveform."),

  PV_SCHEMA_FIELD(
    bool, fft.flip_x,
    "Invert Y Axis",
    "Mirror the spectrum vertically."),
  PV_SCHEMA_FIELD(
    bool, fft.markers,
    "Show Frequency Markers",
    "Display frequency marker lines and labels on the spectrum."),
  PV_SCHEMA_FIELD(
    bool, fft.cursor,
    "Show Hover Cursor",
    "Show a cursor and readout while hovering the spectrum."),
  PV_SCHEMA_FIELD(
    bool, fft.readout_header,
    "Show Readout Header",
    "Show peak level, peak frequency, nearest note, and cents offset for the strongest partial."),
  PV_SCHEMA_FIELD(
    bool, fft.smoothing.enabled,
    "Enable Peak Smoothing",
    "Smooth bar motion to reduce jitter in FFT amplitudes."),
  PV_SCHEMA_FIELD(
    bool, fft.cqt.enabled,
    "Enable Constant-Q Transform",
    "Use CQT for finer low-frequency resolution at higher CPU cost."),
  PV_SCHEMA_FIELD(
    bool, fft.sphere.enabled,
    "Enable Sphere Mode",
    "Render the spectrum using the 3D sphere visualization."),

  PV_SCHEMA_FIELD(
    bool, spectrogram.iterative_reassignment,
    "Enable Iterative Reassignment",
    "Sharpen broad peaks by iteratively moving energy toward dominant neighboring bins."),

  PV_SCHEMA_FIELD(
    bool, window.decorations,
    "Enable Window Decorations",
    "Show title bar and standard window borders from your desktop environment."),
  PV_SCHEMA_FIELD(
    bool, window.always_on_top,
    "Keep Window on Top",
    "Keep the window above other windows."),
  PV_SCHEMA_FIELD(
    bool, window.wayland,
    "Prefer Wayland",
    "Try to use Wayland instead of X11"),

  PV_SCHEMA_FIELD(
    bool, debug.log_fps,
    "Log FPS to Console",
    "Print frame-rate information to the console."),
  PV_SCHEMA_FIELD(
    bool, debug.show_bandpassed,
    "Show Pitch-Filtered Signal",
    "Display the band-pass filtered signal used by pitch detection."),

  PV_SCHEMA_FIELD(
    bool, phosphor.enabled,
    "Enable Phosphor Effect",
    "Enable or disable phosphor post-processing globally."),
  PV_SCHEMA_FIELD(
    bool, phosphor.beam.rainbow,
    "Enable Rainbow Beam",
    "Rotate beam hue based on drawing direction for a chroma trail effect.\n"
    "This can be GPU-intensive."),

  PV_SCHEMA_FIELD(
    bool, vu.momentum.enabled,
    "Enable Needle Momentum",
    "Use spring-damper physics for smoother analog-style needle motion."),
};

inline constexpr std::array intFields = {
  PV_SCHEMA_FIELD(
    int, oscilloscope.pitch.cycles,
    "Pitch Cycles",
    "Number of waveform cycles to display. Set to 0 for constant timeframe.",
    FieldUi<int>::slider(0, 16, 0, true)),
  PV_SCHEMA_FIELD(
    int, oscilloscope.lowpass.order,
    "Low-Pass Filter Order",
    "Filter order (steepness). Higher values are sharper but more CPU intensive.",
    FieldUi<int>::slider(1, 16, 0)),
  PV_SCHEMA_FIELD(
    int, oscilloscope.spline.segments,
    "Spline Segments",
    "Number of interpolated points between each pair of samples.\n"
    "Lower values are faster and more angular.\n"
    "Higher values are smoother but draw more geometry.",
    FieldUi<int>::slider(0, 32, 1, true)),

  PV_SCHEMA_FIELD(
    int, phosphor.screen.aa_size,
    "Anti-Aliasing size",
    "Size of the Anti-Aliasing pass during color-mapping.",
    FieldUi<int>::slider(2, 16, 0, false)),
  PV_SCHEMA_FIELD(
    int, phosphor.screen.decay.fast_frames,
    "Fast decay",
    "How many frames to take to get to threshold.",
    FieldUi<int>::slider(0, 100, 0, false)),
  PV_SCHEMA_FIELD(
    int, phosphor.screen.decay.slow_frames,
    "Slow decay",
    "How many frames to take to get to 0.",
    FieldUi<int>::slider(0, 500, 0, false)),

  PV_SCHEMA_FIELD(
    int, lissajous.spline.segments,
    "Spline Segments",
    "Number of interpolated points between each pair of samples.\n"
    "Lower values are faster and more angular.\n"
    "Higher values are smoother but draw more geometry.",
    FieldUi<int>::slider(0, 32, 1, true)),

  PV_SCHEMA_FIELD(
    int, fft.size,
    "FFT Size",
    "Analysis window size. Larger values improve frequency detail but increase latency.",
    FieldUi<int>::detentSlider(std::span<const int>(fftSizeDetents))),
  PV_SCHEMA_FIELD(
    int, fft.cqt.bins_per_octave,
    "CQT Bins per Octave",
    "Number of CQT bins per octave. Higher values improve note resolution but cost more CPU.",
    FieldUi<int>::slider(16, 128, 0)),

  PV_SCHEMA_FIELD(
    int, window.default_width,
    "Default Width (px)",
    "Initial window width in pixels.",
    FieldUi<int>::slider(100, 1920, 0)),
  PV_SCHEMA_FIELD(
    int, window.default_height,
    "Default Height (px)",
    "Initial window height in pixels.",
    FieldUi<int>::slider(100, 1920, 0)),
  PV_SCHEMA_FIELD(
    int, window.fps_limit,
    "Frame Rate Limit",
    "Target frame rate for rendering. Actual cadence may vary with audio backend buffer sizing.",
    FieldUi<int>::slider(1, 1000, 1)),
};

inline constexpr std::array floatFields = {
  PV_SCHEMA_FIELD(
    float, oscilloscope.beam_multiplier,
    "Beam Intensity",
    "Intensity multiplier applied to the oscilloscope beam.",
    FieldUi<float>::slider(0.f, 10.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.window,
    "Time Window (ms)",
    "Visible oscilloscope time span in milliseconds.",
    FieldUi<float>::slider(1.f, 500.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.edge_compression.range,
    "Edge Compression Amount",
    "Amount of edge compression to apply.",
    FieldUi<float>::slider(0.f, 1.f, 2)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.pitch.min_cycle_time,
    "Minimum Cycle Time (ms)",
    "Lower bound used when pitch-follow determines cycle length.",
    FieldUi<float>::slider(1.f, 100.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.lowpass.cutoff,
    "Low-Pass Cutoff (Hz)",
    "Cutoff frequency for the oscilloscope low-pass filter.",
    FieldUi<float>::slider(0.f, 4000.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.bandpass.bandwidth,
    "Band-Pass Bandwidth (Hz)",
    "Bandwidth of the pitch-detection band-pass filter.",
    FieldUi<float>::slider(0.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.bandpass.sidelobe,
    "Band-Pass Sidelobe (dB)",
    "Sidelobe attenuation for the pitch-detection band-pass filter.",
    FieldUi<float>::slider(0.f, 120.f, 1)),
  PV_SCHEMA_FIELD(
    float, oscilloscope.spline.tension,
    "Spline Tension",
    "Controls how strongly the curve bends between points.\n"
    "0 = straight segments (no smoothing), 1 = standard smooth Catmull-Rom.\n"
    "Higher values (>1) increase curvature and can cause overshoot around sharp peaks.",
    FieldUi<float>::slider(0.f, 2.f, 2)),

  PV_SCHEMA_FIELD(
    float, lissajous.beam_multiplier,
    "Beam Intensity",
    "Intensity multiplier applied to the Lissajous beam.",
    FieldUi<float>::slider(0.f, 10.f, 1)),
  PV_SCHEMA_FIELD(
    float, lissajous.spline.tension,
    "Spline Tension",
    "Controls how strongly the curve bends between points.\n"
    "0 = straight segments (no smoothing), 1 = standard smooth Catmull-Rom.\n"
    "Higher values (>1) increase curvature and can cause overshoot around sharp peaks.",
    FieldUi<float>::slider(0.f, 2.f, 2)),

  PV_SCHEMA_FIELD(
    float, fft.beam_multiplier,
    "Beam Intensity",
    "Intensity multiplier applied to FFT rendering.",
    FieldUi<float>::slider(0.f, 10.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.slope,
    "Slope Compensation (dB/oct)",
    "Display-only slope compensation per octave.",
    FieldUi<float>::slider(-12.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.limits.max_db,
    "Maximum Level (dB)",
    "Upper amplitude limit for FFT display.",
    FieldUi<float>::slider(-120.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.limits.max_freq,
    "Maximum Frequency (Hz)",
    "Upper frequency bound for FFT display.",
    FieldUi<float>::slider(10.f, 22000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.limits.min_db,
    "Minimum Level (dB)",
    "Lower amplitude limit for FFT display.",
    FieldUi<float>::slider(-120.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.limits.min_freq,
    "Minimum Frequency (Hz)",
    "Lower frequency bound for FFT display.",
    FieldUi<float>::slider(10.f, 22000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.smoothing.fall_speed,
    "Fall Speed",
    "How quickly FFT peaks fall when energy decreases.",
    FieldUi<float>::slider(10.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.smoothing.hover_fall_speed,
    "Hover Fall Speed",
    "How quickly FFT peaks fall when hovered.",
    FieldUi<float>::slider(10.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.smoothing.rise_speed,
    "Rise Speed",
    "How quickly FFT peaks rise when energy increases.",
    FieldUi<float>::slider(10.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.sphere.max_freq,
    "Sphere Max Frequency (Hz)",
    "Upper frequency bound for sphere mode.",
    FieldUi<float>::slider(10.f, 22000.f, 1)),
  PV_SCHEMA_FIELD(
    float, fft.sphere.base_radius,
    "Sphere Base Radius",
    "Base radius of the FFT sphere.",
    FieldUi<float>::slider(0.f, 1.f, 3)),

  PV_SCHEMA_FIELD(
    float, spectrogram.window,
    "History Length (s)",
    "Amount of time shown in the spectrogram history.",
    FieldUi<float>::slider(0.1f, 10.f, 1)),
  PV_SCHEMA_FIELD(
    float, spectrogram.slope,
    "Slope Compensation (dB/oct)",
    "Display-only slope compensation per octave.",
    FieldUi<float>::slider(-12.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, spectrogram.limits.min_freq,
    "Minimum Frequency (Hz)",
    "Lower frequency bound for spectrogram display.",
    FieldUi<float>::slider(10.f, 22000.f, 1)),
  PV_SCHEMA_FIELD(
    float, spectrogram.limits.max_freq,
    "Maximum Frequency (Hz)",
    "Upper frequency bound for spectrogram display.",
    FieldUi<float>::slider(10.f, 22000.f, 1)),
  PV_SCHEMA_FIELD(
    float, spectrogram.limits.max_db,
    "Maximum Level (dB)",
    "Upper amplitude limit for spectrogram coloring.",
    FieldUi<float>::slider(-120.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, spectrogram.limits.min_db,
    "Minimum Level (dB)",
    "Lower amplitude limit for spectrogram coloring.",
    FieldUi<float>::slider(-120.f, 12.f, 1)),

  PV_SCHEMA_FIELD(
    float, waveform.window,
    "History Length (s)",
    "Amount of time shown in waveform history.",
    FieldUi<float>::slider(0.1f, 10.f, 1)),
  PV_SCHEMA_FIELD(
    float, waveform.low_mid_split_hz,
    "Low/Mid Split (Hz)",
    "Frequency where low band transitions into mid band for waveform color mapping.",
    FieldUi<float>::slider(40.f, 2000.f, 1)),
  PV_SCHEMA_FIELD(
    float, waveform.mid_high_split_hz,
    "Mid/High Split (Hz)",
    "Frequency where mid band transitions into high band for waveform color mapping.",
    FieldUi<float>::slider(500.f, 12000.f, 1)),
  PV_SCHEMA_FIELD(
    float, waveform.slope,
    "Slope Compensation (dB/oct)",
    "Display-only tilt before waveform color analysis. Positive values emphasize highs.",
    FieldUi<float>::slider(-12.f, 12.f, 1)),

  PV_SCHEMA_FIELD(
    float, audio.silence_threshold,
    "Silence Threshold (dB)",
    "Signal level below which input is treated as silence.",
    FieldUi<float>::slider(-120.f, 0.f, 1)),
  PV_SCHEMA_FIELD(
    float, audio.sample_rate,
    "Sample Rate",
    "Input sample rate. For best results, match your actual audio capture/output rate.",
    FieldUi<float>::enumDrop(std::span<const Choice<float>>(audioSampleRateChoices))),
  PV_SCHEMA_FIELD(
    float, audio.gain_db,
    "Input Gain (dB)",
    "Pre-visualization gain adjustment. Increase for quiet sources, reduce to avoid clipping.",
    FieldUi<float>::slider(-60.f, 12.f, 1)),

  PV_SCHEMA_FIELD(
    float, phosphor.beam.energy,
    "Beam Energy",
    "Base beam energy driving phosphor brightness.",
    FieldUi<float>::slider(1.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, phosphor.blur.spread,
    "Blur Spread",
    "Radius/spread of the phosphor blur kernel.",
    FieldUi<float>::slider(1.f, 512.f, 1)),
  PV_SCHEMA_FIELD(
    float, phosphor.blur.near_intensity,
    "Near Blur Intensity",
    "Blur intensity for nearby pixels.",
    FieldUi<float>::slider(0.f, 1.f, 3)),
  PV_SCHEMA_FIELD(
    float, phosphor.blur.far_intensity,
    "Far Blur Intensity",
    "Blur intensity for distant pixels.",
    FieldUi<float>::slider(0.f, 1.f, 3)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.decay.threshold,
    "Decay Threshold",
    "When the rate of decay starts slowing down.",
    FieldUi<float>::slider(0.f, 4.f, 2)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.curvature,
    "Screen Curvature",
    "Curvature amount for CRT-style screen warping.",
    FieldUi<float>::slider(0.f, 0.5f, 3, true)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.gap,
    "Screen Gap",
    "Spacing factor for curved-screen edge geometry.",
    FieldUi<float>::slider(0.f, 1.f, 3)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.vignette,
    "Vignette Strength",
    "Darkening toward the screen edges.",
    FieldUi<float>::slider(0.f, 1.f, 3)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.chromatic_aberration,
    "Chromatic Aberration",
    "Color channel separation strength at high-contrast edges.",
    FieldUi<float>::slider(0.f, 1.f, 3)),
  PV_SCHEMA_FIELD(
    float, phosphor.screen.grain,
    "Film Grain",
    "Amount of procedural grain/noise over the phosphor image.",
    FieldUi<float>::slider(0.f, 1.f, 3, true)),
  PV_SCHEMA_FIELD(
    float, phosphor.reflections.strength,
    "Reflection Strength",
    "Intensity of simulated screen frame reflections.",
    FieldUi<float>::slider(0.f, 1.f, 3, true)),

  PV_SCHEMA_FIELD(
    float, vu.window,
    "Integration Window (ms)",
    "Time window used for VU level integration.",
    FieldUi<float>::slider(1.f, 500.f, 1)),
  PV_SCHEMA_FIELD(
    float, vu.calibration_db,
    "Calibration Offset (dB)",
    "Meter calibration offset. Positive values make the same signal read higher on the scale.",
    FieldUi<float>::slider(-12.f, 12.f, 1)),
  PV_SCHEMA_FIELD(
    float, vu.momentum.spring_constant,
    "Needle Spring Constant",
    "Restoring force of the virtual needle spring.",
    FieldUi<float>::slider(100.f, 1000.f, 1)),
  PV_SCHEMA_FIELD(
    float, vu.momentum.damping_ratio,
    "Needle Damping",
    "Damping applied to needle motion to control overshoot.",
    FieldUi<float>::slider(1.f, 100.f, 1)),
  PV_SCHEMA_FIELD(
    float, vu.needle_width,
    "Needle Width",
    "Visual thickness of the analog needle.",
    FieldUi<float>::slider(0.1f, 16.f, 1)),
};

inline constexpr std::array stringFields = {
  PV_SCHEMA_FIELD(
    std::string, oscilloscope.pitch.type,
    "Pitch Alignment Method",
    "Method used to align waveform phase for pitch-follow mode.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(oscPitchTypeChoices))),
  PV_SCHEMA_FIELD(
    std::string, oscilloscope.pitch.alignment,
    "Waveform Alignment",
    "Horizontal anchor position used when aligning cycles.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(oscAlignmentChoices))),

  PV_SCHEMA_FIELD(
    std::string, lissajous.mode,
    "Display Mode",
    "Select how the stereo phase trace is transformed.\n"
    "Normal: default XY trace.\n"
    "Rotate/Circle: geometric transforms.\n"
    "Pulsar/Black Hole: stylized variants with stronger center/edge emphasis.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(lissajousModeChoices))),

  PV_SCHEMA_FIELD(
    std::string, fft.key,
    "Note Naming",
    "Choose whether note labels prefer sharps or flats.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(fftKeyChoices))),
  PV_SCHEMA_FIELD(
    std::string, fft.mode,
    "Channel Mode",
    "Analyze channels as Mid/Side or Left/Right.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(fftStereoChoices))),
  PV_SCHEMA_FIELD(
    std::string, fft.frequency_scale,
    "Frequency Scale",
    "Choose logarithmic, mel or linear spacing on the frequency axis.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(frequencyScaleOptions))),

  PV_SCHEMA_FIELD(
    std::string, spectrogram.frequency_scale,
    "Frequency Scale",
    "Choose logarithmic, mel or linear spacing on the frequency axis.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(frequencyScaleOptions))),

  PV_SCHEMA_FIELD(
    std::string, waveform.mode,
    "Channel Mode",
    "Mono: draw Mid channel only.\n"
    "Stereo: use FFT channel mapping and split vertically.\n"
    "When fft.mode is midside, waveform uses Mid/Side.\n"
    "When fft.mode is leftright, waveform uses Left/Right.",
    FieldUi<std::string>::enumTick(std::span<const Choice<std::string_view>>(waveformModeChoices))),

  PV_SCHEMA_FIELD(
    std::string, audio.engine,
    "Audio Backend",
    "Select the capture backend.\n"
    "Auto is recommended unless you need a specific backend for compatibility.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(audioEngineChoices))),
  PV_SCHEMA_FIELD(
    std::string, audio.device,
    "Input Device",
    "Audio input device to capture from.",
    FieldUi<std::string>::enumTick(noStringChoices)),

  PV_SCHEMA_FIELD(
    std::string, window.theme,
    "Theme Preset",
    "Color/theme preset file used for rendering.",
    FieldUi<std::string>::enumTick(noStringChoices)),

  PV_SCHEMA_FIELD(
    std::string, lufs.mode,
    "Measurement Mode",
    "Momentary: 400 ms window.\n"
    "Short-term: 3 s window.\n"
    "Integrated: accumulated over session/runtime.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(lufsModeChoices))),
  PV_SCHEMA_FIELD(
    std::string, lufs.scale,
    "Meter Scale",
    "Display scaling mode for LUFS meter.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(linearScaleChoices))),
  PV_SCHEMA_FIELD(
    std::string, lufs.label,
    "Label Style",
    "On: full moving label beside bar.\n"
    "Off: hide label.\n"
    "Compact: compact label above bar.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(lufsLabelChoices))),

  PV_SCHEMA_FIELD(
    std::string, vu.style,
    "Meter Style",
    "Choose analog needle or digital bar style.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(vuStyleChoices))),
  PV_SCHEMA_FIELD(
    std::string, vu.scale,
    "Meter Scale",
    "Display scaling mode for VU meter.",
    FieldUi<std::string>::enumDrop(std::span<const Choice<std::string_view>>(linearScaleChoices))),

  PV_SCHEMA_FIELD(
    std::string, font,
    "Font File",
    "Path to the font file used for UI/readout text rendering."),
};

inline constexpr std::array rotationFields = {
  PV_SCHEMA_FIELD(
    Config::Rotation, oscilloscope.rotation,
    "Display Rotation",
    "Rotate oscilloscope rendering by 0/90/180/270 degrees.",
    FieldUi<Config::Rotation>::enumDrop(std::span<const Choice<Config::Rotation>>(rotationChoices))),
  PV_SCHEMA_FIELD(
    Config::Rotation, lissajous.rotation,
    "Display Rotation",
    "Rotate Lissajous rendering by 0/90/180/270 degrees.",
    FieldUi<Config::Rotation>::enumDrop(std::span<const Choice<Config::Rotation>>(rotationChoices))),
  PV_SCHEMA_FIELD(
    Config::Rotation, fft.rotation,
    "Display Rotation",
    "Rotate FFT rendering by 0/90/180/270 degrees.",
    FieldUi<Config::Rotation>::enumDrop(std::span<const Choice<Config::Rotation>>(rotationChoices))),
};
// clang-format on

/**
 * @brief Iterate all boolean schema fields.
 */
template <typename Fn> constexpr void forEachBoolField(Fn&& fn) {
  for (const auto& field : boolFields)
    fn(field);
}

/**
 * @brief Iterate all integer schema fields.
 */
template <typename Fn> constexpr void forEachIntField(Fn&& fn) {
  for (const auto& field : intFields)
    fn(field);
}

/**
 * @brief Iterate all float schema fields.
 */
template <typename Fn> constexpr void forEachFloatField(Fn&& fn) {
  for (const auto& field : floatFields)
    fn(field);
}

/**
 * @brief Iterate all string schema fields.
 */
template <typename Fn> constexpr void forEachStringField(Fn&& fn) {
  for (const auto& field : stringFields)
    fn(field);
}

/**
 * @brief Iterate all rotation schema fields.
 */
template <typename Fn> constexpr void forEachRotationField(Fn&& fn) {
  for (const auto& field : rotationFields)
    fn(field);
}

/**
 * @brief Iterate schema fields grouped by value type.
 */
template <typename FnBool, typename FnInt, typename FnFloat, typename FnString, typename FnRotation>
constexpr void forEachFieldByType(FnBool&& onBool, FnInt&& onInt, FnFloat&& onFloat, FnString&& onString,
                                  FnRotation&& onRotation) {
  auto&& boolFn = onBool;
  auto&& intFn = onInt;
  auto&& floatFn = onFloat;
  auto&& stringFn = onString;
  auto&& rotationFn = onRotation;

  forEachBoolField(boolFn);
  forEachIntField(intFn);
  forEachFloatField(floatFn);
  forEachStringField(stringFn);
  forEachRotationField(rotationFn);
}

/**
 * @brief Iterate all scalar fields regardless of type.
 */
template <typename Fn> constexpr void forEachScalarField(Fn&& fn) {
  auto&& callback = fn;
  forEachFieldByType([&](const auto& field) { callback(field); }, [&](const auto& field) { callback(field); },
                     [&](const auto& field) { callback(field); }, [&](const auto& field) { callback(field); },
                     [&](const auto& field) { callback(field); });
}

#undef PV_SCHEMA_FIELD

} // namespace ConfigSchema
