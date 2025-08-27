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

#include "include/config_window.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"

namespace ConfigWindow {

size_t sdlWindow = 0; // window index
bool shown = false;   // is the window currently visible?
int w = 500, h = 600; // current window width/height (also the default)

float offsetX = 0, offsetY = 0;                // current scroll offset
bool alt = false, ctrl = false, shift = false; // tracker for modifier keys

Page topPage;
std::map<PageType, Page> pages;
PageType currentPage = PageType::Audio;

// TODO: put font size in a constexpr

void init() {
  initTop();
  initPages();
}

void deinit() {
  for (std::pair<const std::string, Element>& kv : topPage.elements) {
    Element* e = &kv.second;
    if (e->cleanup)
      e->cleanup(e);
    if (e->data != nullptr)
      free(e->data);
  }
  topPage.elements.clear();

  for (std::pair<const PageType, Page>& kvp : pages) {
    for (std::pair<const std::string, Element>& kv : kvp.second.elements) {
      Element* e = &kv.second;
      if (e->cleanup)
        e->cleanup(e);
      if (e->data != nullptr)
        free(e->data);
    }
  }
  pages.clear();
}

inline void initTop() {
  // left chevron
  {
    Element leftChevron = {0};
    leftChevron.update = [](Element* self) {
      self->x = 0 + margin;
      self->y = h - margin - stdSize;
      self->w = self->h = stdSize;
    };

    leftChevron.render = [](Element* self) {
      float* bgcolor = Theme::colors.bgaccent;
      if (mouseOverRect(self->x, self->y, self->w, self->h))
        bgcolor = Theme::colors.accent;

      // Draw background
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, bgcolor);

      drawArrow(-1, self->x, self->y, self->w);
    };

    leftChevron.clicked = [](Element* self) {
      if (currentPage == PageType::Oscilloscope) {
        currentPage = PageType::Lowpass;
      }
      currentPage = static_cast<PageType>(static_cast<int>(currentPage) - 1);
    };

    topPage.elements.insert({"leftChevron", leftChevron});
  }

  // center label
  {
    Element centerLabel = {0};
    centerLabel.update = [](Element* self) {
      self->x = 0 + margin + stdSize + spacing;
      self->y = h - margin - stdSize;
      self->w = w - stdSize * 2 - margin * 2 - spacing * 2;
      self->h = stdSize;
    };

    centerLabel.render = [](Element* self) {
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, Theme::colors.bgaccent);

      const std::string str = pageToString(currentPage);
      std::pair<float, float> textSize = Graphics::Font::getTextSize(str.c_str(), fontSizeTop, sdlWindow);
      Graphics::Font::drawText(str.c_str(), self->x + self->w / 2 - textSize.first / 2,
                               (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeTop, Theme::colors.text,
                               sdlWindow);
    };

    topPage.elements.insert({"centerLabel", centerLabel});
  }

  // right chevron
  {
    Element rightChevron = {0};
    rightChevron.update = [](Element* self) {
      self->x = w - margin - stdSize;
      self->y = h - margin - stdSize;
      self->w = self->h = stdSize;
    };

    rightChevron.render = [](Element* self) {
      float* bgcolor = Theme::colors.bgaccent;
      if (mouseOverRect(self->x, self->y, self->w, self->h))
        bgcolor = Theme::colors.accent;

      // Draw background
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, bgcolor);

      drawArrow(1, self->x, self->y, self->w);
    };

    rightChevron.clicked = [](Element* self) {
      if (currentPage == PageType::Lowpass) {
        currentPage = PageType::Oscilloscope;
        return;
      }
      currentPage = static_cast<PageType>(static_cast<int>(currentPage) + 1);
    };

    topPage.elements.insert({"rightChevron", rightChevron});
  }

  // save button
  {
    Element saveButton = {0};
    saveButton.update = [](Element* self) {
      std::pair<float, float> textSize = Graphics::Font::getTextSize("Save", fontSizeTop, sdlWindow);
      self->w = textSize.first + (padding * 2);
      self->h = stdSize;
      self->x = w - self->w - margin;
      self->y = margin;
    };

    saveButton.render = [](Element* self) {
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                               self->hovered ? Theme::colors.accent : Theme::colors.bgaccent);

      std::pair<float, float> textSize = Graphics::Font::getTextSize("Save", fontSizeTop, sdlWindow);
      Graphics::Font::drawText("Save", self->x + padding, (int)(self->y + self->h / 2 - textSize.second / 2),
                               fontSizeTop, Theme::colors.text, sdlWindow);
    };

    saveButton.clicked = [](Element* self) { Config::save(); };

    topPage.elements.insert({"saveButton", saveButton});
  }
}

inline void initPages() {
  // TODO: refactor to use common cy variable in the Page struct

  const float cyInit = h - scrollingDisplayMargin;

  // oscilloscope
  {
    Page page;
    float cy = cyInit;

    // follow_pitch
    createCheckElement(page, cy, "follow_pitch", &Config::options.oscilloscope.follow_pitch, "Follow pitch",
                       "Stabilizes the oscilloscope to the pitch of the sound");

    // alignment
    {
      std::map<std::string, std::string> values = {
          {"left",   "Left"  },
          {"center", "Center"},
          {"right",  "Right" },
      };

      createEnumDropElement<std::string>(page, cy, "alignment", &Config::options.oscilloscope.alignment, values,
                                         "Alignment", "Alignment position");
    }

    // alignment_type
    {
      std::map<std::string, std::string> values = {
          {"peak",          "Peak"         },
          {"zero_crossing", "Zero crossing"},
      };

      createEnumDropElement<std::string>(page, cy, "alignment_type", &Config::options.oscilloscope.alignment_type,
                                         values, "Alignment type", "Alignment type");
    }

    // limit_cycles
    createCheckElement(page, cy, "limit_cycles", &Config::options.oscilloscope.limit_cycles, "Limit cycles",
                       "Only track n cycles of the waveform");

    // cycles
    createSliderElement<int>(page, cy, "cycles", &Config::options.oscilloscope.cycles, 1, 16, "Cycle count",
                             "Number of cycles to track when limit_cycles is true", 0);

    // min_cycle_time
    createSliderElement<float>(page, cy, "min_cycle_time", &Config::options.oscilloscope.min_cycle_time, 1.f, 100.f,
                               "Minimum cycle time (ms)", "Minimum time window to display in oscilloscope in ms");

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.oscilloscope.time_window, 1.f, 500.f,
                               "Time window (ms)", "Time window for oscilloscope in ms");

    // beam_multiplier
    createSliderElement<float>(page, cy, "beam_multiplier", &Config::options.oscilloscope.beam_multiplier, 0.f, 10.f,
                               "Beam multiplier", "Beam multiplier for phosphor effect");

    // enable_lowpass
    createCheckElement(page, cy, "enable_lowpass", &Config::options.oscilloscope.enable_lowpass, "Enable lowpass",
                       "Enable lowpass filter for oscilloscope");

    // rotation
    {
      std::map<Config::Rotation, std::string> values = {
          {Config::Rotation::ROTATION_0,   "0deg"  },
          {Config::Rotation::ROTATION_90,  "90deg" },
          {Config::Rotation::ROTATION_180, "180deg"},
          {Config::Rotation::ROTATION_270, "270deg"},
      };

      createEnumDropElement<Config::Rotation>(page, cy, "rotation", &Config::options.oscilloscope.rotation, values,
                                              "Display rotation", "Rotation of the oscilloscope display");
    }

    // flip_x
    createCheckElement(page, cy, "flip_x", &Config::options.oscilloscope.flip_x, "Flip X axis",
                       "Flip the oscilloscope display along the time axis");

    page.height = cyInit - cy;
    pages.insert({PageType::Oscilloscope, page});
  }

  // bandpass filter
  {
    Page page;
    float cy = cyInit;

    // bandwidth
    createSliderElement<float>(page, cy, "bandwidth", &Config::options.bandpass_filter.bandwidth, 0, 1000.f,
                               "Band width (Hz)", "Band width in Hz");

    page.height = cyInit - cy;
    pages.insert({PageType::BandpassFilter, page});
  }

  // lissajous
  {
    Page page;
    float cy = cyInit;

    // enable_splines
    createCheckElement(page, cy, "enable_splines", &Config::options.lissajous.enable_splines, "Enable splines",
                       "Enable Catmull-Rom spline interpolation for smoother curves");

    // bandwidth
    createSliderElement<float>(page, cy, "beam_multiplier", &Config::options.lissajous.beam_multiplier, 0.f, 10.f,
                               "Beam multiplier", "Beam multiplier for phosphor effect");

    // readback_multiplier
    createSliderElement<float>(page, cy, "readback_multiplier", &Config::options.lissajous.readback_multiplier, 1.f,
                               10.f, "Readback multiplier",
                               "Readback multiplier of the data\n"
                               "Defines how much of the previous data is redrawn\n"
                               "Higher value means more data is redrawn, which stabilizes the lissajous.\n"
                               "Caveat is a minor increase in CPU usage.");

    // mode
    {
      std::map<std::string, std::string> values = {
          {"rotate",     "Rotate"    },
          {"circle",     "Circle"    },
          {"pulsar",     "Pulsar"    },
          {"black_hole", "Black hole"},
          {"none",       "Default"   },
      };

      createEnumDropElement<std::string>(
          page, cy, "mode", &Config::options.lissajous.mode, values, "Mode",
          "rotate is a 45 degree rotation of the curve\n"
          "circle is the rotated curve stretched to a circle\n"
          "pulsar is all of the above, makes the outside be silent and the inside be loud which looks really cool\n"
          "black_hole is a rotated circle with a black hole - like effect\n"
          "none is the default mode");
    }

    // rotation
    {
      std::map<Config::Rotation, std::string> values = {
          {Config::Rotation::ROTATION_0,   "0deg"  },
          {Config::Rotation::ROTATION_90,  "90deg" },
          {Config::Rotation::ROTATION_180, "180deg"},
          {Config::Rotation::ROTATION_270, "270deg"},
      };

      createEnumDropElement<Config::Rotation>(page, cy, "rotation", &Config::options.lissajous.rotation, values,
                                              "Display rotation", "Rotation of the lissajous display");
    }

    page.height = cyInit - cy;
    pages.insert({PageType::Lissajous, page});
  }

  // fft
  {
    Page page;
    float cy = cyInit;

    // min_freq
    createSliderElement<float>(page, cy, "min_freq", &Config::options.fft.min_freq, 10.f, 22000.f,
                               "Minimum frequency (Hz)", "Minimum frequency to display in Hz");

    // max_freq
    createSliderElement<float>(page, cy, "max_freq", &Config::options.fft.max_freq, 10.f, 22000.f,
                               "Maximum frequency (Hz)", "Maximum frequency to display in Hz");

    // slope_correction_db
    createSliderElement<float>(page, cy, "slope_correction_db", &Config::options.fft.slope_correction_db, -12.f, 12.f,
                               "Slope correction (dB/oct)",
                               "Slope correction for frequency response (dB per octave, visual only)");

    // min_db
    createSliderElement<float>(page, cy, "min_db", &Config::options.fft.min_db, -120.f, 12.f, "Minimum level (dB)",
                               "dB level at the bottom of the display before slope correction");

    // max_db
    createSliderElement<float>(page, cy, "max_db", &Config::options.fft.max_db, -120.f, 12.f, "Maximum level (dB)",
                               "dB level at the top of the display before slope correction");

    // stereo_mode
    {
      std::map<std::string, std::string> values = {
          {"midside",   "Mid/Side"  },
          {"leftright", "Left/Right"},
      };

      createEnumDropElement<std::string>(page, cy, "stereo_mode", &Config::options.fft.stereo_mode, values,
                                         "Stereo mode", "Stereo mode: mid/side channels or left/right channels");
    }

    // note_key_mode
    {
      std::map<std::string, std::string> values = {
          {"sharp", "Sharp"},
          {"flat",  "Flat" },
      };

      createEnumDropElement<std::string>(page, cy, "note_key_mode", &Config::options.fft.note_key_mode, values,
                                         "Note key mode", "Note key mode: sharp or flat (affects frequency labels)");
    }

    // enable_cqt
    createCheckElement(page, cy, "enable_cqt", &Config::options.fft.enable_cqt, "Enable Constant-Q Transform",
                       "Enable Constant-Q Transform (better frequency resolution in the low end)");

    // cqt_bins_per_octave
    createSliderElement<int>(page, cy, "cqt_bins_per_octave", &Config::options.fft.cqt_bins_per_octave, 16, 128,
                             "CQT bins per octave",
                             "Number of frequency bins per octave for CQT (higher=better resolution)\n"
                             "Significant CPU usage increase with higher values",
                             0);

    // enable_smoothing
    createCheckElement(page, cy, "enable_smoothing", &Config::options.fft.enable_smoothing, "Enable smoothing",
                       "Enable velocity smoothing for FFT values");

    // rise_speed
    createSliderElement<float>(page, cy, "rise_speed", &Config::options.fft.rise_speed, 10.f, 1000.f, "Bar rise speed",
                               "Rise speed of FFT bars (higher=faster rise, more responsive)");

    // fall_speed
    createSliderElement<float>(page, cy, "fall_speed", &Config::options.fft.fall_speed, 10.f, 1000.f, "Bar fall speed",
                               "Fall speed of FFT bars (higher=faster fall, more responsive)");

    // hover_fall_speed
    createSliderElement<float>(page, cy, "hover_fall_speed", &Config::options.fft.hover_fall_speed, 10.f, 1000.f,
                               "Bar fall speed on hover",
                               "Fall speed when window is hovered over (higher=faster fall)");

    // size
    {
      std::vector<int> values = {128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
      createDetentSliderElement<int>(page, cy, "size", &Config::options.fft.size, values, "FFT Size",
                                     "FFT size (higher=better frequency resolution)", 0);
    }

    // beam_multiplier
    createSliderElement<float>(page, cy, "beam_multiplier", &Config::options.fft.beam_multiplier, 0.f, 10.f,
                               "Beam multiplier", "Beam multiplier for phosphor effect");

    // frequency_markers
    createCheckElement(page, cy, "frequency_markers", &Config::options.fft.frequency_markers,
                       "Enable frequency markers", "Enable frequency markers on the display");

    // rotation
    {
      std::map<Config::Rotation, std::string> values = {
          {Config::Rotation::ROTATION_0,   "0deg"  },
          {Config::Rotation::ROTATION_90,  "90deg" },
          {Config::Rotation::ROTATION_180, "180deg"},
          {Config::Rotation::ROTATION_270, "270deg"},
      };

      createEnumDropElement<Config::Rotation>(page, cy, "rotation", &Config::options.fft.rotation, values,
                                              "Display rotation", "Rotation of the FFT display");
    }

    // flip_x
    createCheckElement(page, cy, "flip_x", &Config::options.fft.flip_x, "Flip X axis",
                       "Flip the FFT display along the time axis");

    page.height = cyInit - cy;
    pages.insert({PageType::FFT, page});
  }

  // spectrogram
  {
    Page page;
    float cy = cyInit;

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.spectrogram.time_window, 0.1f, 10.f,
                               "Time window (seconds)", "Time window for spectrogram in seconds");

    // min_db
    createSliderElement<float>(page, cy, "min_db", &Config::options.spectrogram.min_db, -120.f, 12.f,
                               "Minimum level (dB)", "Minimum dB level for spectrogram display");

    // max_db
    createSliderElement<float>(page, cy, "max_db", &Config::options.spectrogram.max_db, -120.f, 12.f,
                               "Maximum level (dB)", "Maximum dB level for spectrogram display");

    // interpolation
    createCheckElement(page, cy, "interpolation", &Config::options.spectrogram.interpolation, "Enable interpolation",
                       "Enable interpolation for smoother spectrogram display");

    // frequency_scale
    {
      std::map<std::string, std::string> values = {
          {"log",   "Logarithmic"},
          {"liner", "Linear"     },
      };

      createEnumDropElement<std::string>(page, cy, "frequency_scale", &Config::options.spectrogram.frequency_scale,
                                         values, "Frequency scale", "Frequency scale: log or linear");
    }

    // min_freq
    createSliderElement<float>(page, cy, "min_freq", &Config::options.spectrogram.min_freq, 10.f, 22000.f,
                               "Minimum frequency (Hz)", "Minimum frequency to display in Hz");

    // max_freq
    createSliderElement<float>(page, cy, "max_freq", &Config::options.spectrogram.max_freq, 10.f, 22000.f,
                               "Maximum frequency (Hz)", "Maximum frequency to display in Hz");

    page.height = cyInit - cy;
    pages.insert({PageType::Spectrogram, page});
  }

  // audio
  {
    Page page;
    float cy = cyInit;

    // silence_threshold
    createSliderElement<float>(page, cy, "silence_threshold", &Config::options.audio.silence_threshold, -120.f, 0.f,
                               "Silence threshold",
                               "Threshold below which audio is considered silent (in dB)\n"
                               "Lower values = visualizer will draw even at lower volumes");

    // sample_rate
    {
      std::map<float, std::string> values = {
          {44100.f,  "44.1kHz" },
          {48000.f,  "48kHz"   },
          {88200.f,  "88.2kHz" },
          {96000.f,  "96kHz"   },
          {176400.f, "176.4kHz"},
          {192000.f, "192kHz"  },
          {352800.f, "352.8kHz"},
          {384000.f, "384kHz"  },
      };

      createEnumDropElement<float>(page, cy, "sample_rate", &Config::options.audio.sample_rate, values, "Sample rate",
                                   "Audio sample rate in Hz (must match your system's audio output)");
    }

    // gain_db
    createSliderElement<float>(page, cy, "gain_db", &Config::options.audio.gain_db, -60.f, 12.f, "Gain (dB)",
                               "Audio gain adjustment in dB (positive=louder, negative=quieter)\n"
                               "Adjust if audio is too quiet, like when Spotify or YouTube normalizes the volume ");

    // engine
    {
      std::map<std::string, std::string> values = {
          {"auto",       "Auto"      },
#if HAVE_PIPEWIRE
          {"pipewire",   "PipeWire"  },
#endif
#if HAVE_PULSEAUDIO
          {"pulseaudio", "PulseAudio"},
#endif
#if HAVE_WASAPI
          {"wasapi",     "WASAPI"    },
#endif
      };

      createEnumDropElement<std::string>(page, cy, "engine", &Config::options.audio.engine, values, "Audio engine",
                                         "Audio backend: pulseaudio, pipewire, wasapi, or auto (auto-detects best "
                                         "available, pipewire first, then pulseaudio, then wasapi)\n"
                                         "auto is recommended for most systems, but you can force a specific backend");
    }

    // TODO: audio device carousel

    page.height = cyInit - cy;
    pages.insert({PageType::Audio, page});
  }

  // visualizers
  {
    Page page;
    float cy = cyInit;

    createHeaderElement(page, cy, "tbd", "TBD");

    page.height = cyInit - cy;
    pages.insert({PageType::Visualizers, page});
  }

  // window
  {
    Page page;
    float cy = cyInit;

    // TODO: change limits for these or throw these two out entirely

    // default_width
    createSliderElement<int>(page, cy, "default_width", &Config::options.window.default_width, 100.f, 1920.f,
                             "Default width", "Default window width", 0);

    // default_height
    createSliderElement<int>(page, cy, "default_height", &Config::options.window.default_height, 100.f, 1920.f,
                             "Default height", "Default window height", 0);

    // theme
    {
      std::map<std::string, std::string> values = {};
      std::filesystem::directory_iterator iterator(expandUserPath("~/.config/pulse-visualizer/themes/"));
      for (auto file : iterator) {
        if (file.path().extension() == ".txt") {
          values.insert({file.path().filename().string(), file.path().filename().string()});
        }
      }

      createEnumTickElement<std::string>(page, cy, "theme", &Config::options.window.theme, values, "Theme",
                                         "Theme file");
    }

    // fps_limit
    createSliderElement<int>(
        page, cy, "fps_limit", &Config::options.window.fps_limit, 1, 1000, "FPS limit",
        "FPS limit for the main window\n"
        "This will also throttle the audio thread to save CPU\n"
        "Note: this does not define the exact FPS limit as pipewire/pulseaudio only accept base 2 read sizes.");

    // decorations
    createCheckElement(page, cy, "decorations", &Config::options.window.decorations, "Enable window decorations",
                       "Enable window decorations in your desktop environment");

    // always_on_top
    createCheckElement(page, cy, "always_on_top", &Config::options.window.always_on_top, "Always on top",
                       "Forces the window to always be on top");

    page.height = cyInit - cy;
    pages.insert({PageType::Window, page});
  }

  // debug
  {
    Page page;
    float cy = cyInit;

    // log_fps
    createCheckElement(page, cy, "log_fps", &Config::options.debug.log_fps, "Enable FPS logging",
                       "Enable FPS logging to console (true/false)\n"
                       "Useful for performance monitoring");

    // show_bandpassed
    createCheckElement(page, cy, "show_bandpassed", &Config::options.debug.show_bandpassed,
                       "Show bandpassed signal on oscilloscope", "Shows the filtered signal used for pitch detection");

    {
      Element element = {0};
      element.update = [](Element* self) {}; // uhh

      element.render = [](Element* self) {
        float cyOther = scrollingDisplayMargin;
        float fps = 1.f / WindowManager::dt;
        static float fpsLerp = 0.f;
        fpsLerp = std::lerp(fpsLerp, fps, 2.f * WindowManager::dt);

        // bottom-to-top

        // version text
        {
          Graphics::Font::drawText("Pulse " VERSION_STRING " commit " VERSION_COMMIT, 0, cyOther, fontSizeHeader,
                                   Theme::colors.text, sdlWindow);
          cyOther += fontSizeHeader;
        }

        // instantenous fps text
        {
          std::stringstream ss;
          ss << "FPS: " << std::fixed << std::setprecision(1) << fpsLerp;
          Graphics::Font::drawText(ss.str().c_str(), 0, cyOther, fontSizeHeader, Theme::colors.text, sdlWindow);
          cyOther += fontSizeHeader;
        }

        self->x = 0;
        self->y = scrollingDisplayMargin;
        self->w = 100;
        self->h = cyOther - scrollingDisplayMargin;
      };

      page.elements.insert({"debug", element});
    }

    page.height = cyInit - cy;
    pages.insert({PageType::Debug, page});
  }

  // phosphor
  {
    Page page;
    float cy = cyInit;

    // enabled
    createCheckElement(page, cy, "enabled", &Config::options.phosphor.enabled, "Enable",
                       "Enable or disable phosphor effects globally");

    // near_blur_intensity
    createSliderElement<float>(page, cy, "near_blur_intensity", &Config::options.phosphor.near_blur_intensity, 0.f, 1.f,
                               "Near blur intensity", "Intensity of blur for nearby pixels (higher=more blur)", 3);

    // far_blur_intensity
    createSliderElement<float>(page, cy, "far_blur_intensity", &Config::options.phosphor.far_blur_intensity, 0.f, 1.f,
                               "Far blur intensity", "Intensity of blur for distant pixels (higher=more blur)", 3);

    // beam_energy
    createSliderElement<float>(page, cy, "beam_energy", &Config::options.phosphor.beam_energy, 10.f, 1000.f,
                               "Beam energy", "Energy of the electron beam (affects brightness of phosphor effect)");

    // decay_slow
    createSliderElement<float>(page, cy, "decay_slow", &Config::options.phosphor.decay_slow, 1.f, 100.f,
                               "Slow decay rate",
                               "Slow decay rate of phosphor persistence (higher=longer persistence)");

    // decay_fast
    createSliderElement<float>(page, cy, "decay_fast", &Config::options.phosphor.decay_fast, 1.f, 100.f,
                               "Fast decay rate",
                               "Fast decay rate of phosphor persistence (higher=shorter persistence)");

    // line_blur_spread
    createSliderElement<float>(page, cy, "line_blur_spread", &Config::options.phosphor.line_blur_spread, 1.f, 100.f,
                               "Line blur spread",
                               "Spread of the blur effect (higher=more spread, more GPU intensive)");

    // line_width
    createSliderElement<float>(page, cy, "line_width", &Config::options.phosphor.line_width, 0.1f, 10.f, "Line width",
                               "Size of the electron beam (affects line thickness)", 2);

    // age_threshold
    createSliderElement<int>(page, cy, "age_threshold", &Config::options.phosphor.age_threshold, 1, 1000,
                             "Age threshold", "Age threshold for phosphor decay (higher=longer persistence)", 0);

    // range_factor
    createSliderElement<float>(page, cy, "range_factor", &Config::options.phosphor.range_factor, 0.f, 10.f,
                               "Range factor",
                               "Range factor for blur calculations (higher=more blur, more GPU intensive)", 2);

    // enable_grain
    createCheckElement(page, cy, "enable_grain", &Config::options.phosphor.enable_grain, "Enable grain",
                       "Enable phosphor grain effect (adds realistic CRT noise)");

    // grain_strength
    createSliderElement<float>(page, cy, "grain_strength", &Config::options.phosphor.grain_strength, 0.f, 1.f,
                               "Grain strength", "Grain strength (Spatial noise)", 3);

    // tension
    createSliderElement<float>(page, cy, "tension", &Config::options.phosphor.tension, 0.f, 1.f, "Tension",
                               "Tension of Catmull-Rom splines (affects curve smoothness)", 3);

    // enable_curved_screen
    createCheckElement(page, cy, "enable_curved_screen", &Config::options.phosphor.enable_curved_screen,
                       "Enable curved screen", "Enable curved phosphor screen effect (simulates curved CRT monitor)");

    // screen_curvature
    createSliderElement<float>(page, cy, "screen_curvature", &Config::options.phosphor.screen_curvature, 0.f, 1.f,
                               "Screen curvature", "Curvature intensity for curved screen effect", 3);

    // screen_gap
    createSliderElement<float>(page, cy, "screen_gap", &Config::options.phosphor.screen_gap, 0.f, 1.f, "Screen gap",
                               "Gap factor between screen edges and border (higher=less gap)", 3);

    // vignette_strength
    createSliderElement<float>(page, cy, "vignette_strength", &Config::options.phosphor.vignette_strength, 0.f, 1.f,
                               "Vignette strength", "Vignette strength", 3);

    // chromatic_aberration_strength
    createSliderElement<float>(page, cy, "chromatic_aberration_strength",
                               &Config::options.phosphor.chromatic_aberration_strength, 0.f, 1.f,
                               "Chromatic aberration strength", "Chromatic aberration strength", 3);

    // colorbeam
    createCheckElement(page, cy, "colorbeam", &Config::options.phosphor.colorbeam, "Enable colorbeam effect",
                       "Enable color beam effect\n"
                       "This will make the beam color rotate around the hue depending on the direction its going.\n"
                       "This is extremely GPU intensive, so it is disabled by default.");

    page.height = cyInit - cy;
    pages.insert({PageType::Phosphor, page});
  }

  // lufs
  {
    Page page;
    float cy = cyInit;

    // mode
    {
      std::map<std::string, std::string> values = {
          {"shortterm",  "Short-term"},
          {"momentary",  "Momentary" },
          {"integrated", "Integrated"},
      };

      createEnumDropElement<std::string>(page, cy, "mode", &Config::options.lufs.mode, values, "Mode",
                                         "momentary is over a 400ms window\n"
                                         "shortterm is over a 3s window\n"
                                         "integrated is over the entire recording");
    }

    // scale
    {
      std::map<std::string, std::string> values = {
          {"log",    "Logarithmic"},
          {"linear", "Linear"     },
      };

      createEnumDropElement<std::string>(page, cy, "scale", &Config::options.lufs.scale, values, "Scale", "Scale");
    }

    // label
    {
      std::map<std::string, std::string> values = {
          {"on",      "On"     },
          {"off",     "Off"    },
          {"compact", "Compact"},
      };

      createEnumDropElement<std::string>(page, cy, "label", &Config::options.lufs.label, values, "Label",
                                         "on: full label following the LUFS bar on the right\n"
                                         "off: no label\n"
                                         "compact: compact label in the top area");
    }

    page.height = cyInit - cy;
    pages.insert({PageType::LUFS, page});
  }

  // vu
  {
    Page page;
    float cy = cyInit;

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.vu.time_window, 1.f, 500.f, "Time window (ms)",
                               "Time window for VU meter in ms");

    // style
    {
      std::map<std::string, std::string> values = {
          {"analog",  "Analog" },
          {"digital", "Digital"},
      };

      createEnumDropElement<std::string>(page, cy, "style", &Config::options.vu.style, values, "Style",
                                         "Style: analog or digital");
    }

    // time_window
    createSliderElement<float>(page, cy, "calibration_db", &Config::options.vu.calibration_db, -12.f, 12.f,
                               "Calibration level (dB)",
                               "A calibration of 3dB means a 0dB pure sine wave is at 0dB in the meter");

    // scale
    {
      std::map<std::string, std::string> values = {
          {"log",    "Logarithmic"},
          {"linear", "Linear"     },
      };

      createEnumDropElement<std::string>(page, cy, "scale", &Config::options.vu.scale, values, "Scale", "Scale");
    }

    // enable_momentum
    createCheckElement(page, cy, "enable_momentum", &Config::options.vu.enable_momentum, "Enable momentum",
                       "Enable momentum for analog VU meter");

    // spring_constant
    createSliderElement<float>(page, cy, "spring_constant", &Config::options.vu.spring_constant, 100.f, 1000.f,
                               "Spring constant", "Spring constant of needle");

    // damping_ratio
    createSliderElement<float>(page, cy, "damping_ratio", &Config::options.vu.damping_ratio, 1.f, 100.f,
                               "Damping ratio", "Damping ratio of needle");

    // needle_width
    createSliderElement<float>(page, cy, "needle_width", &Config::options.vu.needle_width, 0.1f, 16.f, "Needle width",
                               "Needle width");

    page.height = cyInit - cy;
    pages.insert({PageType::VU, page});
  }

  // lowpass
  {
    Page page;
    float cy = cyInit;

    // cutoff
    createSliderElement<float>(page, cy, "cutoff", &Config::options.lowpass.cutoff, 10.f, 22000.f,
                               "Cutoff frequency (Hz)", "Cutoff frequency in Hz");

    // order
    createSliderElement<int>(page, cy, "order", &Config::options.lowpass.order, 1, 16, "Filter order",
                             "Order of the lowpass filter (higher = steeper filter, more CPU usage)", 0);

    page.height = cyInit - cy;
    pages.insert({PageType::Lowpass, page});
  }

  // TODO: put font somewhere
}

// TODO: move this into the Graphics::Font namespace
std::string wrapText(const std::string& text, float maxW, int fontSize) {
  std::istringstream lineStream(text);
  std::string line;
  std::string result;

  while (std::getline(lineStream, line)) {
    std::istringstream wordStream(line);
    std::string word;
    std::string currentLine;

    while (wordStream >> word) {
      std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
      auto size = Graphics::Font::getTextSize(testLine.c_str(), fontSize, sdlWindow);

      if (size.first <= maxW) {
        currentLine = testLine;
      } else {
        if (!currentLine.empty()) {
          result += currentLine + "\n";
        }
        currentLine = word;
      }
    }

    if (!currentLine.empty()) {
      result += currentLine + "\n";
    }
  }

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

void createLabelElement(Page& page, float& cy, const std::string key, const std::string label,
                        const std::string description) {
  Element labelElement = {0};
  labelElement.update = [cy](Element* self) {
    self->w = labelSize;
    self->h = stdSize;
    self->x = margin;
    self->y = cy - stdSize;
  };

  labelElement.render = [label, description](Element* self) {
    // TODO: implement cutoff for too long stuff i guess

    std::pair<float, float> textSize = Graphics::Font::getTextSize(label.c_str(), fontSizeLabel, sdlWindow);
    Graphics::Font::drawText(label.c_str(), self->x, (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeLabel,
                             Theme::colors.text, sdlWindow);

    if (self->hovered) {
      layer(2.f);
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float mouseY = SDLWindow::mousePos[sdlWindow].second;
      float maxW = w - mouseX - margin * 2 - padding * 2;

      std::string actualDescription = wrapText(description, maxW, 12);
      std::pair<float, float> textSize =
          Graphics::Font::getTextSize(actualDescription.c_str(), fontSizeTooltip, sdlWindow);
      Graphics::drawFilledRect(mouseX + margin - offsetX, mouseY - margin - textSize.second - padding * 2 - offsetY,
                               textSize.first + padding * 2, textSize.second + padding * 2, Theme::colors.accent);

      Graphics::Font::drawText(actualDescription.c_str(), mouseX + margin + padding - offsetX,
                               mouseY - margin - padding - fontSizeTooltip - offsetY, fontSizeTooltip,
                               Theme::colors.text, sdlWindow);
      layer();
    }
  };

  page.elements.insert({key + "#label", labelElement});
}

void createHeaderElement(Page& page, float& cy, const std::string key, const std::string header) {
  Element headerElement = {0};
  headerElement.update = [cy](Element* self) {
    self->w = w - margin * 2;
    self->h = stdSize;
    self->x = margin;
    self->y = cy - stdSize;
  };

  headerElement.render = [header](Element* self) {
    std::pair<float, float> textSize = Graphics::Font::getTextSize(header.c_str(), fontSizeHeader, sdlWindow);
    Graphics::Font::drawText(header.c_str(), self->x + self->w / 2 - textSize.first / 2,
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeHeader, Theme::colors.text,
                             sdlWindow);
  };

  page.elements.insert({key, headerElement});
  cy -= stdSize + margin;
}

void createCheckElement(Page& page, float& cy, const std::string key, bool* value, const std::string label,
                        const std::string description) {
  Element checkElement = {0};

  checkElement.update = [cy](Element* self) {
    self->w = stdSize;
    self->h = stdSize;
    self->x = w - margin - stdSize;
    self->y = cy - stdSize;
  };

  checkElement.render = [value, key](Element* self) {
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             self->hovered ? Theme::colors.accent : Theme::colors.bgaccent);

    if (*value) {
      Graphics::drawLine(self->x + padding, self->y + padding, self->x + self->w - padding, self->y + self->h - padding,
                         Theme::colors.text, 2);
      Graphics::drawLine(self->x + self->w - padding, self->y + padding, self->x + padding, self->y + self->h - padding,
                         Theme::colors.text, 2);
    }
  };

  checkElement.clicked = [value](Element* self) { *value = !(*value); };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#check", checkElement});
  cy -= stdSize + margin;
}

void toggle() {
  if (shown) {
    LOG_DEBUG("Destroying Config window");
    Graphics::Font::cleanup(sdlWindow);
    SDLWindow::destroyWindow(sdlWindow);
    deinit();
  } else {
    LOG_DEBUG("Creating Config window");
    sdlWindow = SDLWindow::createWindow("Configuration", w, h, 0);
    Graphics::Font::load(sdlWindow);
    init();
  }

  shown = !shown;
}

void handleEvent(const SDL_Event& event) {
  if (!shown)
    return;

  switch (event.type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    toggle();
    return;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (SDLWindow::focused[sdlWindow] && event.button.button == SDL_BUTTON_LEFT) {
      bool anyFocused = false;

      // loop over the top page, checking if any is focused (takes priority over non focused elements)
      for (std::pair<const std::string, Element>& kv : topPage.elements) {
        Element* e = &kv.second;
        if (e->focused) {
          e->hovered = mouseOverRect(e->x, e->y, e->w, e->h);
          if (e->hovered) {
            if (e->clicked != nullptr)
              e->clicked(e);

            e->click = true;

            LOG_DEBUG("Focused top page element clicked: " << kv.first);
          }

          anyFocused = true;
          break;
        }
      }

      // loop over the current page, checking if any is focused (takes priority over non focused elements)
      auto it = pages.find(currentPage);
      if (it != pages.end()) {
        for (std::pair<const std::string, Element>& kv : it->second.elements) {
          Element* e = &kv.second;
          if (e->focused) {
            e->hovered = mouseOverRectTranslated(e->x, e->y, e->w, e->h);
            if (e->hovered) {
              if (e->clicked != nullptr)
                e->clicked(e);

              e->click = true;

              LOG_DEBUG("Focused page element clicked: " << kv.first)
            }

            anyFocused = true;
            break;
          }
        }
      }

      // if any element is focused, exit early
      if (anyFocused)
        break;

      // loop over the top page
      for (std::pair<const std::string, Element>& kv : topPage.elements) {
        Element* e = &kv.second;
        e->hovered = mouseOverRect(e->x, e->y, e->w, e->h);
        if (e->hovered) {
          if (e->clicked != nullptr)
            e->clicked(e);

          e->click = true;

          LOG_DEBUG("Top page element clicked: " << kv.first)
        }
      }

      // loop over the current page
      if (it != pages.end()) {
        for (std::pair<const std::string, Element>& kv : it->second.elements) {
          Element* e = &kv.second;
          e->hovered = mouseOverRectTranslated(e->x, e->y, e->w, e->h);
          if (e->hovered) {
            if (e->clicked != nullptr)
              e->clicked(e);

            e->click = true;

            LOG_DEBUG("Page element clicked: " << kv.first)
          }
        }
      }
    }
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (SDLWindow::focused[sdlWindow] && event.button.button == SDL_BUTTON_LEFT) {
      // loop over the top page
      for (std::pair<const std::string, Element>& kv : topPage.elements) {
        Element* e = &kv.second;
        if (e->click) {
          if (e->unclicked != nullptr)
            e->unclicked(e);

          e->click = false;

          LOG_DEBUG("Top page element unclicked: " << kv.first)
        }
      }

      // loop over the current page
      auto it = pages.find(currentPage);
      if (it != pages.end()) {
        for (std::pair<const std::string, Element>& kv : it->second.elements) {
          Element* e = &kv.second;
          if (e->click) {
            if (e->unclicked != nullptr)
              e->unclicked(e);

            e->click = false;

            LOG_DEBUG("Page element unclicked: " << kv.first)
          }
        }
      }
    }
    break;

  case SDL_EVENT_MOUSE_WHEEL:
    if (SDLWindow::focused[sdlWindow]) {
      bool anyHovered = false;

      // loop over the top page
      for (std::pair<const std::string, Element>& kv : topPage.elements) {
        Element* e = &kv.second;
        e->hovered = mouseOverRect(e->x, e->y, e->w, e->h);
        if (e->hovered) {
          if (e->scrolled != nullptr) {
            e->scrolled(e, event.wheel.integer_y);
            anyHovered = true;
            break;
          }
        }
      }

      // loop over the current page
      auto it = pages.find(currentPage);
      if (it != pages.end()) {
        for (std::pair<const std::string, Element>& kv : it->second.elements) {
          Element* e = &kv.second;
          e->hovered = mouseOverRect(e->x + offsetX, e->y + offsetY, e->w, e->h);
          if (e->hovered) {
            if (e->scrolled != nullptr) {
              e->scrolled(e, event.wheel.integer_y);
              anyHovered = true;
              break;
            }
          }
        }
      }

      if (anyHovered)
        break;

      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        offsetY += event.wheel.integer_y * 20.f;
      } else {
        offsetY -= event.wheel.integer_y * 20.f;
      }

      if (offsetY < 0) {
        offsetY = 0;
      }

      // the other case is handled in the draw() function
    }
    break;

    // track modifier keys
  case SDL_EVENT_KEY_DOWN:
    if (event.key.key == SDLK_RALT || event.key.key == SDLK_LALT) {
      alt = true;
    }

    if (event.key.key == SDLK_RSHIFT || event.key.key == SDLK_LSHIFT) {
      shift = true;
    }

    if (event.key.key == SDLK_RCTRL || event.key.key == SDLK_LCTRL) {
      ctrl = true;
    }
    break;

    // track modifier keys
  case SDL_EVENT_KEY_UP:
    if (event.key.key == SDLK_RALT || event.key.key == SDLK_LALT) {
      alt = false;
    }

    if (event.key.key == SDLK_RSHIFT || event.key.key == SDLK_LSHIFT) {
      shift = false;
    }

    if (event.key.key == SDLK_RCTRL || event.key.key == SDLK_LCTRL) {
      ctrl = false;
    }
    break;

    // track window resize events so the layout doesn't break
  case SDL_EVENT_WINDOW_RESIZED:
    w = event.window.data1;
    h = event.window.data2;
    break;

  default:
    break;
  }
}

bool mouseOverRect(float x, float y, float w, float h) {
  const float mx = SDLWindow::mousePos[sdlWindow].first;
  const float my = SDLWindow::mousePos[sdlWindow].second;

  if (mx > x && mx < x + w && my > y && my < y + h)
    return true;

  return false;
}

bool mouseOverRectTranslated(float x, float y, float w, float h) {
  // add the scroll offset and call mouseOverRect
  x += offsetX;
  y += offsetY;

  const float my = SDLWindow::mousePos[sdlWindow].second;
  return my > scrollingDisplayMargin && my < ConfigWindow::h - scrollingDisplayMargin && mouseOverRect(x, y, w, h);
}

void drawArrow(int dir, float x, float y, const float buttonSize) {
  // Draw arrow
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_POLYGON_SMOOTH);

  glColor4fv(Theme::colors.text);
  glBegin(GL_TRIANGLES);
  const float v = static_cast<float>(std::abs(dir) == 2);
  const float h = 1.0f - v;
  const float alpha = 0.2f * static_cast<float>(dir);

  // Branchless coordinate calc because yes
  float x1 = x + buttonSize * (v * 0.3f + h * (0.5f - alpha));
  float y1 = y + buttonSize * (v * (0.5f - alpha / 2.0f) + h * 0.3f);
  float x2 = x + buttonSize * (v * 0.5f + h * (0.5f + alpha));
  float y2 = y + buttonSize * (v * (0.5f + alpha / 2.0f) + h * 0.5f);
  float x3 = x + buttonSize * (v * 0.7f + h * (0.5f - alpha));
  float y3 = y + buttonSize * (v * (0.5f - alpha / 2.0f) + h * 0.7f);
  glVertex2f(x1, y1);
  glVertex2f(x2, y2);
  glVertex2f(x3, y3);

  glEnd();
  glDisable(GL_POLYGON_SMOOTH);
  glDisable(GL_BLEND);
}

void configViewport(float width, float height, float offX = 0.f, float offY = 0.f) {
  // Set OpenGL viewport and projection matrix for rendering
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(offX, width + offX, -offY, height - offY, -10, 10);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void draw() {
  if (!shown)
    return;

  // make sure offset is correct
  auto it = pages.find(currentPage);
  {
    float currentHeight = 0;

    if (it != pages.end()) {
      currentHeight = it->second.height;
    }

    float viewportHeight = h - scrollingDisplayMargin * 2;
    float maxOffset = std::max(0.f, (currentHeight + 100) - viewportHeight);

    if (offsetY > maxOffset) {
      offsetY = maxOffset;
    }
  }

  // render
  SDLWindow::selectWindow(sdlWindow);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  // configure viewport for bottom layer
  configViewport(w, h, offsetX, offsetY);

  if (it != pages.end()) {
    for (std::pair<const std::string, Element>& kv : it->second.elements) {
      Element* e = &kv.second;
      e->update(e);
      e->hovered = mouseOverRectTranslated(e->x, e->y, e->w, e->h);
      e->render(e);
    }
  }

  // configure viewport for top layer
  configViewport(w, h);

  // make sure the current page doesnt draw below the elements
  Graphics::drawFilledRect(0, h - scrollingDisplayMargin, w, scrollingDisplayMargin, Theme::colors.background);
  Graphics::drawFilledRect(0, 0, w, scrollingDisplayMargin, Theme::colors.background);

  for (std::pair<const std::string, Element>& kv : topPage.elements) {
    Element* e = &kv.second;
    e->update(e);
    e->hovered = mouseOverRect(e->x, e->y, e->w, e->h);
    e->render(e);
  }

#if false
  {
    const float mx = SDLWindow::mousePos[sdlWindow].first;
    const float my = SDLWindow::mousePos[sdlWindow].second;
    const float size = 5;
    const float thickness = 1;

    Graphics::drawLine(mx - size, my - size, mx + size, my + size, Theme::colors.text, thickness);
    Graphics::drawLine(mx - size, my + size, mx + size, my - size, Theme::colors.text, thickness);
  }
#endif
}

void layer(float z) {
  // push the current matrix so we don't mess something up
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  // load identity and translate offset
  glLoadIdentity();
  glTranslatef(0.0f, 0.0f, z);
}

void layer() {
  // ensure we pop the right matrix :P
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

std::string pageToString(PageType page) {
  switch (page) {
  case PageType::Oscilloscope:
    return "Oscilloscope";
  case PageType::BandpassFilter:
    return "Bandpass Filter";
  case PageType::Lissajous:
    return "Lissajous";
  case PageType::FFT:
    return "FFT";
  case PageType::Spectrogram:
    return "Spectrogram";
  case PageType::Audio:
    return "Audio";
  case PageType::Visualizers:
    return "Visualizers";
  case PageType::Window:
    return "Window";
  case PageType::Debug:
    return "Debug";
  case PageType::Phosphor:
    return "Phosphor";
  case PageType::LUFS:
    return "LUFS";
  case PageType::VU:
    return "VU";
  case PageType::Lowpass:
    return "Lowpass";
  default:
    return "Unknown";
  }
}

}; // namespace ConfigWindow
