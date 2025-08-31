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

#include "include/audio_engine.hpp"
#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/window_manager.hpp"

namespace ConfigWindow {

size_t sdlWindow = 0; // window index
bool shown = false;   // is the window currently visible?

float offsetX = 0, offsetY = 0;                // current scroll offset
bool alt = false, ctrl = false, shift = false; // tracker for modifier keys

Page topPage;
std::map<PageType, Page> pages;
PageType currentPage = PageType::Audio;

// Drag state for Visualizers page
static struct DragState {
  bool active = false;
  std::string fromGroup;
  size_t index = 0;
  std::string viz;
} dragState;

// Friendly names for visualizers
static std::map<std::string, std::string> vizLabels = {
    {"spectrum_analyzer", "Spectrum"    },
    {"lissajous",         "Lissajous"   },
    {"oscilloscope",      "Oscilloscope"},
    {"spectrogram",       "Spectrogram" },
    {"lufs",              "LUFS"        },
    {"vu",                "VU"          }
};

static int newWindowCounter = 1;
static std::string generateNewWindowKey() {
  // ensure uniqueness
  std::string key;
  do {
    key = std::string("win_") + std::to_string(newWindowCounter++);
  } while (Config::options.visualizers.find(key) != Config::options.visualizers.end());
  return key;
}

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
        currentPage = PageType::VU;
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
      if (currentPage == PageType::VU) {
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
    createCheckElement(page, cy, "follow_pitch", &Config::options.oscilloscope.pitch.follow, "Follow pitch",
                       "Stabilizes the oscilloscope to the pitch of the sound");

    // alignment
    {
      std::map<std::string, std::string> values = {
          {"left",   "Left"  },
          {"center", "Center"},
          {"right",  "Right" },
      };

      createEnumDropElement<std::string>(page, cy, "alignment", &Config::options.oscilloscope.pitch.alignment, values,
                                         "Alignment", "Alignment position");
    }

    // alignment_type
    {
      std::map<std::string, std::string> values = {
          {"peak",          "Peak"         },
          {"zero_crossing", "Zero crossing"},
      };

      createEnumDropElement<std::string>(page, cy, "alignment_type", &Config::options.oscilloscope.pitch.type, values,
                                         "Alignment type", "Alignment type");
    }

    // cycles (Implies limit_cycles == true if non-zero)
    createSliderElement<int>(page, cy, "cycles", &Config::options.oscilloscope.pitch.cycles, 0, 16, "Cycle count",
                             "Number of cycles to display", 0, true);

    // min_cycle_time
    createSliderElement<float>(page, cy, "min_cycle_time", &Config::options.oscilloscope.pitch.min_cycle_time, 1.f,
                               100.f, "Minimum time (ms)", "Minimum time window to display in oscilloscope in ms");

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.oscilloscope.window, 1.f, 500.f,
                               "Time window (ms)", "Time window for oscilloscope in ms");

    // beam_multiplier
    createSliderElement<float>(page, cy, "beam_multiplier", &Config::options.oscilloscope.beam_multiplier, 0.f, 10.f,
                               "Beam multiplier", "Beam intensity multiplier for phosphor effect");

    // enable_lowpass
    createCheckElement(page, cy, "enable_lowpass", &Config::options.oscilloscope.lowpass.enabled, "Enable lowpass",
                       "Enable lowpass filter for oscilloscope");

    // lowpass.cutoff
    createSliderElement<float>(page, cy, "cutoff", &Config::options.oscilloscope.lowpass.cutoff, 0, 4000.f,
                               "Lowpass cutoff (Hz)", "Cutoff frequency in Hz");

    // lowpass.order
    createSliderElement<int>(page, cy, "order", &Config::options.oscilloscope.lowpass.order, 1, 16, "Lowpass order",
                             "Cutoff frequency in Hz", 0);

    // bandwidth
    createSliderElement<float>(page, cy, "bandwidth", &Config::options.oscilloscope.bandpass.bandwidth, 0, 1000.f,
                               "Bandpass bandwidth (Hz)", "Bandwidth of the bandpass filter in Hz");

    // sidelobe
    createSliderElement<float>(page, cy, "sidelobe", &Config::options.oscilloscope.bandpass.sidelobe, 0, 120.f,
                               "Bandpass sidelobe (dB)", "Sidelobe attenuation of the bandpass filter in dB");

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

  // lissajous
  {
    Page page;
    float cy = cyInit;

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
          {"normal",     "Normal"    },
      };

      createEnumDropElement<std::string>(
          page, cy, "mode", &Config::options.lissajous.mode, values, "Mode",
          "rotate is a 45 degree rotation of the curve\n"
          "circle is the rotated curve stretched to a circle\n"
          "pulsar is all of the above, makes the outside be silent and the inside be loud which looks really cool\n"
          "black_hole is a rotated circle with a black hole-like effect\n"
          "normal is the default mode");
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
    createSliderElement<float>(page, cy, "min_freq", &Config::options.fft.limits.min_freq, 10.f, 22000.f,
                               "Minimum frequency (Hz)", "Minimum frequency to display in Hz");

    // max_freq
    createSliderElement<float>(page, cy, "max_freq", &Config::options.fft.limits.max_freq, 10.f, 22000.f,
                               "Maximum frequency (Hz)", "Maximum frequency to display in Hz");

    // slope_correction_db
    createSliderElement<float>(page, cy, "slope_correction_db", &Config::options.fft.slope, -12.f, 12.f,
                               "Slope correction (dB/oct)",
                               "Slope correction for frequency response (dB per octave, visual only)");

    // min_db
    createSliderElement<float>(page, cy, "min_db", &Config::options.fft.limits.min_db, -120.f, 12.f,
                               "Minimum level (dB)", "dB level at the bottom of the display before slope correction");

    // max_db
    createSliderElement<float>(page, cy, "max_db", &Config::options.fft.limits.max_db, -120.f, 12.f,
                               "Maximum level (dB)", "dB level at the top of the display before slope correction");

    // stereo_mode
    {
      std::map<std::string, std::string> values = {
          {"midside",   "Mid/Side"  },
          {"leftright", "Left/Right"},
      };

      createEnumDropElement<std::string>(page, cy, "stereo_mode", &Config::options.fft.mode, values, "Stereo mode",
                                         "Use mid/side channels or left/right channels");
    }

    // note_key_mode
    {
      std::map<std::string, std::string> values = {
          {"sharp", "Sharp"},
          {"flat",  "Flat" },
      };

      createEnumDropElement<std::string>(page, cy, "note_key_mode", &Config::options.fft.key, values, "Note key mode",
                                         "Use sharp or flat frequency label");
    }

    // enable_cqt
    createCheckElement(page, cy, "enable_cqt", &Config::options.fft.cqt.enabled, "Enable Constant-Q Transform",
                       "Enable Constant-Q Transform (better frequency resolution in the low end but more CPU usage)");

    // cqt_bins_per_octave
    createSliderElement<int>(page, cy, "cqt_bins_per_octave", &Config::options.fft.cqt.bins_per_octave, 16, 128,
                             "CQT bins per octave",
                             "Number of frequency bins per octave for CQT (higher=better resolution)\n"
                             "Significant CPU usage increase with higher values",
                             0);

    // enable_smoothing
    createCheckElement(page, cy, "enable_smoothing", &Config::options.fft.smoothing.enabled, "Enable smoothing",
                       "Enable velocity smoothing for FFT values");

    // rise_speed
    createSliderElement<float>(page, cy, "rise_speed", &Config::options.fft.smoothing.rise_speed, 10.f, 1000.f,
                               "Bar rise speed", "Rise speed of FFT bars");

    // fall_speed
    createSliderElement<float>(page, cy, "fall_speed", &Config::options.fft.smoothing.fall_speed, 10.f, 1000.f,
                               "Bar fall speed", "Fall speed of FFT bars");

    // hover_fall_speed
    createSliderElement<float>(page, cy, "hover_fall_speed", &Config::options.fft.smoothing.hover_fall_speed, 10.f,
                               1000.f, "Bar fall speed on hover", "Fall speed when window is hovered over");

    // size
    {
      std::vector<int> values = {128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
      createDetentSliderElement<int>(page, cy, "size", &Config::options.fft.size, values, "FFT Size", "FFT size", 0);
    }

    // beam_multiplier
    createSliderElement<float>(page, cy, "beam_multiplier", &Config::options.fft.beam_multiplier, 0.f, 10.f,
                               "Beam multiplier", "Beam multiplier for phosphor effect");

    // frequency_markers
    createCheckElement(page, cy, "frequency_markers", &Config::options.fft.markers, "Enable frequency markers",
                       "Enable frequency markers on the display");

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

    // Sphere enabled
    createCheckElement(page, cy, "sphere_enabled", &Config::options.fft.sphere.enabled, "Enable sphere",
                       "Enable sphere for FFT display");

    // Sphere max freq
    createSliderElement<float>(page, cy, "sphere_max_freq", &Config::options.fft.sphere.max_freq, 10.f, 22000.f,
                               "Sphere max frequency (Hz)", "Maximum frequency for sphere in Hz");

    // Sphere base radius
    createSliderElement<float>(page, cy, "sphere_base_radius", &Config::options.fft.sphere.base_radius, 0.f, 1.f,
                               "Sphere base radius", "Base radius for sphere", 3);

    page.height = cyInit - cy;
    pages.insert({PageType::FFT, page});
  }

  // spectrogram
  {
    Page page;
    float cy = cyInit;

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.spectrogram.window, 0.1f, 10.f,
                               "Time window (seconds)", "Time window for spectrogram in seconds");

    // min_db
    createSliderElement<float>(page, cy, "min_db", &Config::options.spectrogram.limits.min_db, -120.f, 12.f,
                               "Minimum level (dB)", "Minimum dB level for spectrogram display");

    // max_db
    createSliderElement<float>(page, cy, "max_db", &Config::options.spectrogram.limits.max_db, -120.f, 12.f,
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
                                         values, "Frequency scale", "Logarithmic or linear frequency scaling");
    }

    // min_freq
    createSliderElement<float>(page, cy, "min_freq", &Config::options.spectrogram.limits.min_freq, 10.f, 22000.f,
                               "Minimum frequency (Hz)", "Minimum frequency to display in Hz");

    // max_freq
    createSliderElement<float>(page, cy, "max_freq", &Config::options.spectrogram.limits.max_freq, 10.f, 22000.f,
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
                               "Silence threshold", "Threshold below which audio is considered silent (in dB)");

    // sample_rate
    // TODO: Read sample rate from audio engine
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
                                   "Audio sample rate in Hz (must match your system's audio output!)");
    }

    // gain_db
    createSliderElement<float>(page, cy, "gain_db", &Config::options.audio.gain_db, -60.f, 12.f, "Gain (dB)",
                               "Audio gain adjustment in dB\n"
                               "Adjust if audio is too quiet, like when Spotify or YouTube normalizes the volume.");

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
                                         "Select the audio engine to use\n"
                                         "auto is recommended for most systems, but you can force a specific backend");
    }

    {
      std::vector<std::string> result = AudioEngine::enumerate();
      std::map<std::string, std::string> values;

      for (std::string v : result) {
        values.insert({v, v});
      }

      createEnumTickElement<std::string>(page, cy, "device", &Config::options.audio.device, values, "Device",
                                         "Audio device name");
    }

    page.height = cyInit - cy;
    pages.insert({PageType::Audio, page});
  }

  // visualizers
  {
    Page page;
    float cy = cyInit;

    // Ensure a persistent 'hidden' window group exists and can be empty
    if (Config::options.visualizers.find("hidden") == Config::options.visualizers.end()) {
      Config::options.visualizers["hidden"] = {};
    }

    // Find all visualizers that don't exist in any group
    std::vector<std::string> missingVisualizers;
    for (auto& [vizKey, vizLabel] : vizLabels) {
      if (!std::any_of(Config::options.visualizers.begin(), Config::options.visualizers.end(), [&](const auto& group) {
            return std::find(group.second.begin(), group.second.end(), vizKey) != group.second.end();
          }))
        missingVisualizers.push_back(vizKey);
    }

    // If there are any missing visualizers, add them to hidden
    if (!missingVisualizers.empty()) {
      for (auto& visualizer : missingVisualizers) {
        Config::options.visualizers["hidden"].push_back(visualizer);
      }
      Config::save();
    }

    createVisualizerListElement(page, cy, "visualizers", &Config::options.visualizers, "Visualizers", "Visualizers");

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
          std::string filename = file.path().filename().string();
          std::string baseName = filename.substr(0, filename.size() - 4);
          values.insert({filename, baseName});
        }
      }

      createEnumTickElement<std::string>(page, cy, "theme", &Config::options.window.theme, values, "Theme", "Theme");
    }

    // fps_limit
    createSliderElement<int>(
        page, cy, "fps_limit", &Config::options.window.fps_limit, 1, 1000, "FPS limit",
        "FPS limit for the main window\n"
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
                       "Enable FPS logging to console");

    // show_bandpassed
    createCheckElement(page, cy, "show_bandpassed", &Config::options.debug.show_bandpassed,
                       "Show bandpassed signal on oscilloscope", "Shows the filtered signal used for pitch detection");

    {
      Element element = {0};

      element.render = [](Element* self) {
        float cyOther = scrollingDisplayMargin;
        float fps = 1.f / WindowManager::dt;
        static float fpsLerp = 0.f;
        float diff = abs(fps - fpsLerp);
        float t = diff / (diff + Config::options.window.fps_limit * 2.0f);
        fpsLerp = std::lerp(fpsLerp, fps, t);

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
    createSliderElement<float>(page, cy, "near_blur_intensity", &Config::options.phosphor.blur.near_intensity, 0.f, 1.f,
                               "Near blur intensity", "Intensity of blur for nearby pixels", 3);

    // far_blur_intensity
    createSliderElement<float>(page, cy, "far_blur_intensity", &Config::options.phosphor.blur.far_intensity, 0.f, 1.f,
                               "Far blur intensity", "Intensity of blur for distant pixels", 3);

    // beam_energy
    createSliderElement<float>(page, cy, "beam_energy", &Config::options.phosphor.beam.energy, 1.f, 1000.f,
                               "Beam energy", "Base energy of the electron beam");

    // decay_slow
    createSliderElement<float>(page, cy, "decay_slow", &Config::options.phosphor.decay.slow, 1.f, 100.f,
                               "Slow decay rate", "Slow decay rate of phosphor persistence");

    // decay_fast
    createSliderElement<float>(page, cy, "decay_fast", &Config::options.phosphor.decay.fast, 1.f, 100.f,
                               "Fast decay rate", "Fast decay rate of phosphor persistence");

    // line_blur_spread
    createSliderElement<float>(page, cy, "line_blur_spread", &Config::options.phosphor.blur.spread, 1.f, 512.f,
                               "Line blur spread", "Spread of the blur effect");

    // line_width
    createSliderElement<float>(page, cy, "line_width", &Config::options.phosphor.beam.width, 0.1f, 10.f, "Line width",
                               "Size of the electron beam", 2);

    // age_threshold
    createSliderElement<int>(page, cy, "age_threshold", &Config::options.phosphor.decay.threshold, 1, 1000,
                             "Age threshold", "Age threshold for phosphor decay", 0);

    // range_factor
    createSliderElement<float>(page, cy, "range_factor", &Config::options.phosphor.blur.range, 0.f, 10.f,
                               "Range factor", "Range factor for blur calculations", 2);

    // grain_strength (Implies enable_grain == true if non-zero)
    createSliderElement<float>(page, cy, "grain_strength", &Config::options.phosphor.screen.grain, 0.f, 1.f,
                               "Grain strength", "Grain strength", 3, true);

    // tension
    createSliderElement<float>(page, cy, "tension", &Config::options.phosphor.beam.tension, 0.f, 1.f, "Tension",
                               "Tension of Catmull-Rom splines", 3);

    // screen_curvature (Implies enable_curved_screen == true if non-zero)
    createSliderElement<float>(page, cy, "screen_curvature", &Config::options.phosphor.screen.curvature, 0.f, 1.f,
                               "Screen curvature", "Curvature intensity for curved screen effect", 3, true);

    // screen_gap
    createSliderElement<float>(page, cy, "screen_gap", &Config::options.phosphor.screen.gap, 0.f, 1.f, "Screen gap",
                               "Gap factor between screen edges and border", 3);

    // vignette_strength
    createSliderElement<float>(page, cy, "vignette_strength", &Config::options.phosphor.screen.vignette, 0.f, 1.f,
                               "Vignette strength", "Vignette strength", 3);

    // chromatic_aberration_strength
    createSliderElement<float>(page, cy, "chromatic_aberration_strength",
                               &Config::options.phosphor.screen.chromatic_aberration, 0.f, 1.f,
                               "Chromatic aberration strength", "Chromatic aberration strength", 3);

    // rainbow
    createCheckElement(page, cy, "rainbow", &Config::options.phosphor.beam.rainbow, "Enable rainbow beam effect",
                       "Enable rainbow beam effect\n"
                       "This will make the beam color rotate around the hue depending on the direction its going.\n"
                       "This is GPU intensive, so it is disabled by default.");

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
                                         "compact: compact label above the LUFS bar");
    }

    page.height = cyInit - cy;
    pages.insert({PageType::LUFS, page});
  }

  // vu
  {
    Page page;
    float cy = cyInit;

    // time_window
    createSliderElement<float>(page, cy, "time_window", &Config::options.vu.window, 1.f, 500.f, "Time window (ms)",
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
                               "A calibration of 3dB means a full scale pure sine wave is at 0dB in the meter");

    // scale
    {
      std::map<std::string, std::string> values = {
          {"log",    "Logarithmic"},
          {"linear", "Linear"     },
      };

      createEnumDropElement<std::string>(page, cy, "scale", &Config::options.vu.scale, values, "Scale", "Scale");
    }

    // enable_momentum
    createCheckElement(page, cy, "enable_momentum", &Config::options.vu.momentum.enabled, "Enable momentum",
                       "Enable physics simulation for the needle");

    // spring_constant
    createSliderElement<float>(page, cy, "spring_constant", &Config::options.vu.momentum.spring_constant, 100.f, 1000.f,
                               "Spring constant", "Spring constant of needle");

    // damping_ratio
    createSliderElement<float>(page, cy, "damping_ratio", &Config::options.vu.momentum.damping_ratio, 1.f, 100.f,
                               "Damping ratio", "Damping ratio of needle");

    // needle_width
    createSliderElement<float>(page, cy, "needle_width", &Config::options.vu.needle_width, 0.1f, 16.f, "Needle width",
                               "Needle width");

    page.height = cyInit - cy;
    pages.insert({PageType::VU, page});
  }

  // TODO: put font somewhere
  // TODO: text input
}

void createLabelElement(Page& page, float& cy, const std::string key, const std::string label,
                        const std::string description, const bool ignoreTextOverflow) {
  Element labelElement = {0};
  labelElement.update = [cy](Element* self) {
    self->w = labelSize;
    self->h = stdSize;
    self->x = margin;
    self->y = cy - stdSize;
  };

  labelElement.render = [label, description, ignoreTextOverflow](Element* self) {
    std::string actualLabel =
        ignoreTextOverflow ? label : Graphics::Font::truncateText(label, self->w, fontSizeLabel, sdlWindow);
    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeLabel, sdlWindow);
    Graphics::Font::drawText(actualLabel.c_str(), self->x, (int)(self->y + self->h / 2 - textSize.second / 2),
                             fontSizeLabel, Theme::colors.text, sdlWindow);

    if (self->hovered) {
      layer(2.f);
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float mouseY = SDLWindow::mousePos[sdlWindow].second;
      float maxW = w - mouseX - margin * 2 - padding * 2;

      std::string actualDescription = Graphics::Font::wrapText(description, maxW, fontSizeTooltip, sdlWindow);
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

  createLabelElement(page, cy, key, label, description, true);
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

  if (!SDLWindow::focused[sdlWindow] && event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    return;

  switch (event.type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    toggle();
    return;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
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
          }
        }
      }
    }
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      // loop over the top page
      for (std::pair<const std::string, Element>& kv : topPage.elements) {
        Element* e = &kv.second;
        if (e->click) {
          if (e->unclicked != nullptr)
            e->unclicked(e);

          e->click = false;
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
          }
        }
      }
    }
    break;

  case SDL_EVENT_MOUSE_WHEEL: {
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
  } break;

    // track modifier keys
  case SDL_EVENT_KEY_DOWN:
    switch (event.key.key) {
    case SDLK_RALT:
    case SDLK_LALT:
      alt = true;
      break;
    case SDLK_RSHIFT:
    case SDLK_LSHIFT:
      shift = true;
      break;
    case SDLK_RCTRL:
    case SDLK_LCTRL:
      ctrl = true;
      break;

    case SDLK_ESCAPE:
    case SDLK_Q:
      toggle();
      break;
    }
    break;

    // track modifier keys
  case SDL_EVENT_KEY_UP:
    switch (event.key.key) {
    case SDLK_RALT:
    case SDLK_LALT:
      alt = false;
      break;
    case SDLK_RSHIFT:
    case SDLK_LSHIFT:
      shift = false;
      break;
    case SDLK_RCTRL:
    case SDLK_LCTRL:
      ctrl = false;
      break;
    }
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
      if (e->update != nullptr)
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
    if (e->update != nullptr)
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
  default:
    return "Unknown";
  }
}

template <typename ValueType>
void createSliderElement(Page& page, float& cy, const std::string key, ValueType* value, ValueType min, ValueType max,
                         const std::string label, const std::string description, const int precision,
                         const bool zeroOff) {
  Element sliderElement = {0};

  sliderElement.update = [cy](Element* self) {
    self->w = w - (margin * 3) - labelSize;
    self->h = stdSize;
    self->x = margin * 2 + labelSize;
    self->y = cy - stdSize;
  };

  sliderElement.render = [value, min, max, precision, zeroOff](Element* self) {
    float percent = ((float)(*value) - min) / (max - min);
    // slidable region needs to be decreased by handle width so the handle can go to 0 and 100% safely
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;

    if (self->focused) {
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float adjustedX = mouseX - (self->x + sliderPadding + sliderHandleWidth / 2);
      float mousePercent = adjustedX / slidableWidth;

      mousePercent = std::clamp(mousePercent, 0.f, 1.f);
      *value = min + mousePercent * (max - min);
    }

    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             hovered ? Theme::colors.accent : Theme::colors.bgaccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2, hovered ? Theme::colors.bgaccent : Theme::colors.accent);

    std::stringstream ss;
    if (zeroOff && *value <= FLT_EPSILON) {
      ss << "Off";
    } else {
      ss << std::fixed << std::setprecision(precision) << *value;
    }
    std::pair<float, float> textSize = Graphics::Font::getTextSize(ss.str().c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(ss.str().c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text,
                             sdlWindow);
  };

  sliderElement.clicked = [value, min, max](Element* self) {
    float percent = ((float)(*value) - min) / (max - min);
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;
    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    if (hovered) {
      self->focused = true;
      self->x = SDLWindow::mousePos[sdlWindow].first - (sliderPadding + handleX + sliderHandleWidth / 2);
    }
  };

  sliderElement.unclicked = [](Element* self) { self->focused = false; };

  sliderElement.scrolled = [value, min, max, precision](Element* self, int amount) {
    float change = amount;
    float precisionAmount = std::powf(10, -precision);
    if (precision != 0) {
      if (shift)
        change *= precisionAmount * 1000.f;
      else if (ctrl)
        change *= precisionAmount * 100.f;
      else if (alt)
        change *= precisionAmount;
      else
        change *= precisionAmount * 10.f;
    } else {
      if (shift)
        change *= precisionAmount * 100.f;
      else if (ctrl)
        change *= precisionAmount * 10.f;
      else
        change *= precisionAmount;
    }

    *value = std::clamp(*value + (ValueType)change, min, max);
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#slider", sliderElement});
  cy -= stdSize + margin;
}

template <typename ValueType>
void createDetentSliderElement(Page& page, float& cy, const std::string key, ValueType* value,
                               const std::vector<ValueType>& detents, const std::string label,
                               const std::string description, const int precision) {
  Element sliderElement = {0};

  sliderElement.update = [cy](Element* self) {
    self->w = w - (margin * 3) - labelSize;
    self->h = stdSize;
    self->x = margin * 2 + labelSize;
    self->y = cy - stdSize;
  };

  sliderElement.render = [value, detents, precision](Element* self) {
    if (detents.empty())
      return;

    // Find index of current value
    auto it = std::find(detents.begin(), detents.end(), *value);
    int index = (it != detents.end()) ? std::distance(detents.begin(), it) : 0;

    float percent = static_cast<float>(index) / (detents.size() - 1);
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;

    if (self->focused) {
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float adjustedX = mouseX - (self->x + sliderPadding + sliderHandleWidth / 2);
      float mousePercent = adjustedX / slidableWidth;
      mousePercent = std::clamp(mousePercent, 0.f, 1.f);

      // Snap to nearest detent
      int nearestIndex = static_cast<int>(std::round(mousePercent * (detents.size() - 1)));
      nearestIndex = std::clamp(nearestIndex, 0, static_cast<int>(detents.size() - 1));
      *value = detents[nearestIndex];
    }

    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             hovered ? Theme::colors.accent : Theme::colors.bgaccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2, hovered ? Theme::colors.bgaccent : Theme::colors.accent);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << *value;
    std::pair<float, float> textSize = Graphics::Font::getTextSize(ss.str().c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(ss.str().c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text,
                             sdlWindow);
  };

  sliderElement.clicked = [value, detents](Element* self) {
    if (detents.empty())
      return;

    auto it = std::find(detents.begin(), detents.end(), *value);
    int index = (it != detents.end()) ? std::distance(detents.begin(), it) : 0;

    float percent = static_cast<float>(index) / (detents.size() - 1);
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;

    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    if (hovered) {
      self->focused = true;
      self->x = SDLWindow::mousePos[sdlWindow].first - (sliderPadding + handleX + sliderHandleWidth / 2);
    }
  };

  sliderElement.unclicked = [](Element* self) { self->focused = false; };

  sliderElement.scrolled = [value, detents](Element* self, int amount) {
    float change = amount;

    auto it = std::find(detents.begin(), detents.end(), *value);
    int index = (it != detents.end()) ? std::distance(detents.begin(), it) : 0;
    int newIndex = index + (int)change;

    newIndex = std::clamp(newIndex, 0, static_cast<int>(detents.size() - 1));
    *value = detents[newIndex];
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#slider", sliderElement});
  cy -= stdSize + margin;
}

template <typename ValueType>
void createEnumDropElement(Page& page, float& cy, const std::string key, ValueType* value,
                           std::map<ValueType, std::string> possibleValues, const std::string label,
                           const std::string description) {
  const float originalY = cy - stdSize;
  const float dropHeight = possibleValues.size() * stdSize;
  Element dropdownElement = {0};

  dropdownElement.update = [cy, dropHeight](Element* self) {
    self->w = w - (margin * 3) - labelSize;
    self->x = margin * 2 + labelSize;

    if (self->focused) {
      self->h = stdSize + dropHeight;
    } else {
      self->h = stdSize;
    }
    self->y = cy - self->h;
  };

  dropdownElement.render = [originalY, value, possibleValues](Element* self) {
    bool dropdownHovered = false;
    if (self->focused) {
      dropdownHovered = mouseOverRectTranslated(self->x, originalY, self->w, stdSize);
    } else {
      dropdownHovered = self->hovered;
    }

    // draw the dropdown button thing
    Graphics::drawFilledRect(self->x, originalY, self->w, stdSize,
                             dropdownHovered ? Theme::colors.accent : Theme::colors.bgaccent);

    drawArrow((self->focused ? 2 : -2), self->x + self->w - (stdSize * 0.75), originalY + (stdSize / 4), stdSize / 2);

    layer(1.f);

    int i = 0;
    std::pair<ValueType, std::string> currentPair;
    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      bool current = *value == kv.first;

      if constexpr (std::is_same_v<ValueType, std::string>) {
        if (!current) {
          std::string str = *value;
          std::string kvFirst = kv.first;

          std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
          std::transform(kvFirst.begin(), kvFirst.end(), kvFirst.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if (kvFirst.find(str) != std::string::npos) {
            *value = kv.first;

            currentPair = kv;
            current = true;
          }
        }
      }

      if (current) {
        currentPair = kv;
      }

      if (self->focused) {
        const float y = originalY - stdSize - (i * stdSize);
        bool thingHovered = mouseOverRectTranslated(self->x, y, self->w, stdSize);

        Graphics::drawFilledRect(self->x, y, self->w, stdSize,
                                 thingHovered ? Theme::colors.accent
                                              : (current ? Theme::colors.text : Theme::colors.bgaccent));

        std::string actualLabel =
            Graphics::Font::truncateText(kv.second, self->w - padding * 2, fontSizeValue, sdlWindow);

        std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue, sdlWindow);
        Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                                 (int)(y + stdSize / 2 - textSize.second / 2), fontSizeValue,
                                 current && !thingHovered ? Theme::colors.background : Theme::colors.text, sdlWindow);
      }
      i++;
    }

    layer();

    std::string actualLabel =
        Graphics::Font::truncateText(currentPair.second, self->w - padding * 2, fontSizeValue, sdlWindow);

    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(originalY + stdSize / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text,
                             sdlWindow);
  };

  dropdownElement.clicked = [value, possibleValues, originalY](Element* self) {
    bool dropdownHovered = false;
    if (self->focused) {
      dropdownHovered = mouseOverRectTranslated(self->x, originalY, self->w, stdSize);
    } else {
      dropdownHovered = self->hovered;
    }

    if (dropdownHovered) {
      self->focused = !self->focused;
      return;
    }

    if (!self->focused)
      return;

    int i = 0;
    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      const float y = originalY - stdSize - (i * stdSize);
      bool thingHovered = mouseOverRectTranslated(self->x, y, self->w, stdSize);

      if (thingHovered) {
        // clicked on value
        *value = kv.first;
        self->focused = false;
        return;
      }

      i++;
    }
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#dropdown", dropdownElement});
  cy -= stdSize + margin;
}

template <typename ValueType>
void createEnumTickElement(Page& page, float& cy, const std::string key, ValueType* value,
                           std::map<ValueType, std::string> possibleValues, const std::string label,
                           const std::string description) {
  Element tickLabelElement = {0};

  tickLabelElement.update = [cy](Element* self) {
    self->w = w - (margin * 3) - labelSize - stdSize * 2 - spacing * 2;
    self->h = stdSize;
    self->x = margin * 2 + labelSize + stdSize + spacing;
    self->y = cy - stdSize;
  };

  tickLabelElement.render = [value, possibleValues, key](Element* self) {
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h, Theme::colors.bgaccent);

    std::pair<ValueType, std::string> currentPair;
    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      if (*value == kv.first) {
        currentPair = kv;
        break;
      }

      if constexpr (std::is_same_v<ValueType, std::string>) {
        std::string str = *value;
        std::string kvFirst = kv.first;

        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(kvFirst.begin(), kvFirst.end(), kvFirst.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (kvFirst.find(str) != std::string::npos) {
          *value = kv.first;

          currentPair = kv;
          break;
        }
      }
    }

    std::string actualLabel =
        Graphics::Font::truncateText(currentPair.second, self->w - padding * 2, fontSizeValue, sdlWindow);

    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text,
                             sdlWindow);
  };

  Element tickLeftElement = {0};

  tickLeftElement.update = [cy](Element* self) {
    self->w = stdSize;
    self->h = stdSize;
    self->x = margin * 2 + labelSize;
    self->y = cy - stdSize;
  };

  tickLeftElement.render = [](Element* self) {
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             self->hovered ? Theme::colors.accent : Theme::colors.bgaccent);

    drawArrow(-1, self->x, self->y, stdSize);
  };

  tickLeftElement.clicked = [value, possibleValues](Element* self) {
    std::pair<ValueType, std::string> previous = *std::prev(possibleValues.end());
    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      if (*value == kv.first) {
        break;
      }

      previous = kv;
    }

    *value = previous.first;
  };

  Element tickRightElement = {0};

  tickRightElement.update = [cy](Element* self) {
    self->w = stdSize;
    self->h = stdSize;
    self->x = w - margin - stdSize;
    self->y = cy - stdSize;
  };

  tickRightElement.render = [](Element* self) {
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             self->hovered ? Theme::colors.accent : Theme::colors.bgaccent);

    drawArrow(1, self->x, self->y, stdSize);
  };

  tickRightElement.clicked = [value, possibleValues](Element* self) {
    std::pair<ValueType, std::string> next = *possibleValues.begin();
    bool captureNext = false;

    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      if (captureNext) {
        next = kv;
        break;
      }

      if (*value == kv.first) {
        captureNext = true;
      }
    }

    *value = next.first;
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#tickLabel", tickLabelElement});
  page.elements.insert({key + "#tickLeft", tickLeftElement});
  page.elements.insert({key + "#tickRight", tickRightElement});
  cy -= stdSize + margin;
}

void createVisualizerListElement(Page& page, float& cy, const std::string key,
                                 std::map<std::string, std::vector<std::string>>* value, const std::string label,
                                 const std::string description) {

  Element visualizerListElement = {0};

  visualizerListElement.update = [cy, value](Element* self) {
    self->x = margin;
    self->w = w - margin * 2;

    float used = 0.f;
    const float groupHeaderH = stdSize;
    const float itemH = stdSize;
    const float groupBottomMargin = margin;
    for (const auto& kv : *value) {
      const auto& items = kv.second;
      used += groupHeaderH;
      used += spacing;
#ifdef _WIN32
      used += std::max(1ull, items.size()) * (itemH + spacing);
#else
      used += std::max(1ul, items.size()) * (itemH + spacing);
#endif
      used += groupBottomMargin;
    }
    const float createZoneH = stdSize * 1.5f;
    used += createZoneH + margin;

    self->h = used;
    self->y = cy - self->h;
  };

  visualizerListElement.render = [value](Element* self) {
    float yTop = self->y + self->h;

    const float mouseX = SDLWindow::mousePos[sdlWindow].first;
    const float mouseY = SDLWindow::mousePos[sdlWindow].second;

    // Draw groups
    struct DropTarget {
      std::string group;
      float x, y, w, h;
    };
    std::vector<DropTarget> targets;

    const float groupHeaderH = stdSize;
    const float itemH = stdSize;
    const float groupBottomMargin = margin;
    const float itemPad = padding;

    for (const auto& kv : *value) {
      const std::string& group = kv.first;
      const auto& items = kv.second;

      // Group header
      float headerY = yTop - groupHeaderH;
      Graphics::drawFilledRect(self->x, headerY, self->w, groupHeaderH, Theme::colors.bgaccent);
      std::pair<float, float> textSize = Graphics::Font::getTextSize(group.c_str(), fontSizeHeader, sdlWindow);
      Graphics::Font::drawText(group.c_str(), (int)(self->x + padding),
                               (int)(headerY + groupHeaderH / 2 - textSize.second / 2), fontSizeHeader,
                               Theme::colors.text, sdlWindow);

      // Items area
      float itemsTop = headerY - spacing;
      float y = itemsTop;
      float groupX = self->x;
      float groupW = self->w;

      float groupAreaYBottomStart = y;

      // List items vertically
      for (size_t i = 0; i < std::max<size_t>(1, items.size()); ++i) {
        y -= itemH;
        float itemX = groupX + itemPad;
        float itemY = y;
        float itemW = groupW - itemPad * 2;
        float itemHh = itemH;

        bool isPlaceholder = items.empty();
        bool isDragged = dragState.active && dragState.fromGroup == group && dragState.index == i && !isPlaceholder;
        bool hovered = mouseOverRectTranslated(itemX, itemY, itemW, itemHh);
        float* bg = hovered ? Theme::colors.accent : Theme::colors.bgaccent;
        if (isDragged)
          bg = Theme::colors.accent;
        Graphics::drawFilledRect(itemX, itemY, itemW, itemHh, bg);

        std::string label;
        if (!isPlaceholder) {
          auto itLabel = vizLabels.find(items[i]);
          label = itLabel != vizLabels.end() ? itLabel->second : items[i];
        } else {
          label = "(empty)";
        }
        std::pair<float, float> t = Graphics::Font::getTextSize(label.c_str(), fontSizeValue, sdlWindow);
        Graphics::Font::drawText(label.c_str(), (int)(itemX + padding), (int)(itemY + itemHh / 2 - t.second / 2),
                                 fontSizeValue, Theme::colors.text, sdlWindow);

        y -= spacing;
      }

      float groupAreaBottom = y;
      float groupAreaHeight = (itemsTop - groupAreaBottom);
      targets.push_back({group, groupX, groupAreaBottom, groupW, groupAreaHeight});

      yTop = groupAreaBottom - groupBottomMargin;
    }

    const float createZoneH = stdSize * 1.5f;
    float createY = yTop - createZoneH;
    bool overCreate = mouseOverRectTranslated(self->x, createY, self->w, createZoneH);
    Graphics::drawFilledRect(self->x, createY, self->w, createZoneH,
                             overCreate && dragState.active ? Theme::colors.accent : Theme::colors.bgaccent);
    const char* createLabel = "+ Create new window (drop here)";
    std::pair<float, float> t = Graphics::Font::getTextSize(createLabel, fontSizeHeader, sdlWindow);
    Graphics::Font::drawText(createLabel, (int)(self->x + self->w / 2 - t.first / 2),
                             (int)(createY + createZoneH / 2 - t.second / 2), fontSizeHeader, Theme::colors.text,
                             sdlWindow);

    if (dragState.active) {
      for (const auto& target : targets) {
        if (mouseOverRectTranslated(target.x, target.y, target.w, target.h)) {
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glColor4fv(Theme::alpha(Theme::colors.accent, 0.2f));
          glBegin(GL_QUADS);
          glVertex2f(target.x, target.y);
          glVertex2f(target.x + target.w, target.y);
          glVertex2f(target.x + target.w, target.y + target.h);
          glVertex2f(target.x, target.y + target.h);
          glEnd();
          glDisable(GL_BLEND);
          break;
        }
      }
    }

    // Update dynamic page height for proper scrolling
    pages[PageType::Visualizers].height = self->h;

    // Draw ghost chip under cursor when dragging
    if (dragState.active && !dragState.viz.empty()) {
      layer(1.f);

      std::string label = vizLabels.count(dragState.viz) ? vizLabels[dragState.viz] : dragState.viz;
      const float chipH = stdSize;
      std::pair<float, float> ts = Graphics::Font::getTextSize(label.c_str(), fontSizeValue, sdlWindow);
      float chipW = std::min(self->w - padding * 2, ts.first + padding * 2);

      float gx = mouseX - offsetX - chipW / 2.0f;
      float gy = mouseY - offsetY - chipH / 2.0f;

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      Graphics::drawFilledRect(gx, gy, chipW, chipH, Theme::alpha(Theme::colors.accent, 0.5f));

      std::pair<float, float> t2 = Graphics::Font::getTextSize(label.c_str(), fontSizeValue, sdlWindow);
      Graphics::Font::drawText(label.c_str(), (int)(gx + chipW / 2 - t2.first / 2),
                               (int)(gy + chipH / 2 - t2.second / 2), fontSizeValue,
                               Theme::alpha(Theme::colors.text, 0.9f), sdlWindow);

      glDisable(GL_BLEND);
      layer();
    }
  };

  visualizerListElement.clicked = [value](Element* self) {
    const float groupHeaderH = stdSize;
    const float itemH = stdSize;
    const float groupBottomMargin = margin;
    const float itemPad = padding;

    float yTop = self->y + self->h;
    float mouseX = SDLWindow::mousePos[sdlWindow].first;
    float mouseY = SDLWindow::mousePos[sdlWindow].second;

    for (const auto& kv : *value) {
      const std::string& group = kv.first;
      const auto& items = kv.second;

      float headerY = yTop - groupHeaderH;
      float itemsTop = headerY - spacing;
      float y = itemsTop;

      // Walk the same rows as render, including a placeholder row when empty
      for (size_t i = 0; i < std::max<size_t>(1, items.size()); ++i) {
        y -= itemH;
        float itemX = self->x + itemPad;
        float itemY = y;
        float itemW = self->w - itemPad * 2;
        float itemHh = itemH;
        bool isPlaceholder = items.empty();
        if (!isPlaceholder && mouseOverRectTranslated(itemX, itemY, itemW, itemHh)) {
          dragState.active = true;
          dragState.fromGroup = group;
          dragState.index = i;
          dragState.viz = items[i];
          return;
        }
        y -= spacing;
      }

      float afterItemsBottom = y;
      yTop = afterItemsBottom - groupBottomMargin;
    }
  };

  visualizerListElement.unclicked = [value](Element* self) {
    if (!dragState.active)
      return;

    const float groupHeaderH = stdSize;
    const float itemH = stdSize;
    const float groupBottomMargin = margin;

    float yTop = self->y + self->h;
    float mouseX = SDLWindow::mousePos[sdlWindow].first;
    float mouseY = SDLWindow::mousePos[sdlWindow].second;

    std::string targetGroup;
    bool foundTarget = false;
    size_t dropIndex = 0;

    for (const auto& kv : *value) {
      const std::string& group = kv.first;
      const auto& items = kv.second;

      float headerY = yTop - groupHeaderH;
      float itemsTop = headerY - spacing;
      float y = itemsTop;
      // Walk the same rows as render, including a placeholder row when empty
      for (size_t i = 0; i < std::max<size_t>(1, items.size()); ++i) {
        y -= itemH;
        float itemX = self->x + padding;
        float itemY = y;
        float itemW = self->w - padding * 2;
        float itemHh = itemH;

        if (mouseOverRectTranslated(itemX, itemY, itemW, itemHh)) {
          targetGroup = group;
          foundTarget = true;
          if (items.empty()) {
            dropIndex = 0;
          } else {
            bool inBottomHalf = !mouseOverRectTranslated(itemX, itemY + itemHh / 2.0f, itemW, itemHh / 2.0f);
            dropIndex = i + static_cast<size_t>(inBottomHalf);
            if (dropIndex > items.size())
              dropIndex = items.size();
          }
          break;
        }

        if (i < std::max<size_t>(1, items.size()) - 1) {
          float gapY = y - spacing;
          float gapH = spacing;
          if (mouseOverRectTranslated(itemX, gapY, itemW, gapH)) {
            targetGroup = group;
            foundTarget = true;
            dropIndex = i + 1;
            break;
          }
        }

        y -= spacing;
      }

      float afterItemsBottom = y;
      yTop = afterItemsBottom - groupBottomMargin;
      if (foundTarget)
        break;
    }

    const float createZoneH = stdSize * 1.5f;
    float createY = yTop - createZoneH;
    bool overCreate = mouseOverRectTranslated(self->x, createY, self->w, createZoneH);

    // cancel move if target is not main
    if (dragState.fromGroup == "main" && Config::options.visualizers[dragState.fromGroup].size() == 1) {
      if (!foundTarget || targetGroup != "main" || overCreate) {
        dragState.active = false;
        return;
      }
    }

    if (foundTarget || overCreate) {
      // Remove from source by index
      auto& src = Config::options.visualizers[dragState.fromGroup];
      if (dragState.index < src.size())
        src.erase(src.begin() + dragState.index);

      // Target destination
      if (overCreate) {
        std::string newKey = generateNewWindowKey();
        Config::options.visualizers[newKey].push_back(dragState.viz);
      } else {
        auto& dst = Config::options.visualizers[targetGroup];
        // If moving within the same group and removing an earlier index, adjust dropIndex
        if (targetGroup == dragState.fromGroup && dropIndex > dragState.index)
          dropIndex--;
        if (dropIndex > dst.size())
          dropIndex = dst.size();
        dst.insert(dst.begin() + static_cast<long>(dropIndex), dragState.viz);
      }

      // Delete empty non-special windows (preserve 'main' and 'hidden')
      if (Config::options.visualizers[dragState.fromGroup].empty() && dragState.fromGroup != "main" &&
          dragState.fromGroup != "hidden") {
        Config::options.visualizers.erase(dragState.fromGroup);
      }

      WindowManager::reorder();
    }

    dragState.active = false;
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#visualizers", visualizerListElement});
  cy -= visualizerListElement.h + margin;
}

}; // namespace ConfigWindow
