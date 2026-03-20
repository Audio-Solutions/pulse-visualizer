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

#include "include/theme.hpp"

#include "include/config.hpp"

namespace Theme {

// Global theme colors
Colors colors;

#ifdef __linux__
int themeInotifyFd = -1;
int themeInotifyWatch = -1;
#endif

std::string currentThemePath;

float* alpha(float* color, float alpha) {
  static float tmp[4];
  memcpy(tmp, color, 3 * sizeof(float));
  tmp[3] = alpha;
  return tmp;
}

void mix(float* a, float* b, float* out, float t) {
  for (int i = 0; i < 4; ++i) {
    out[i] = a[i] * (1.0f - t) + b[i] * t;
  }
}

void load() {
  // Construct theme file path
  std::string path = "~/.config/pulse-visualizer/themes/" + Config::options.window.theme;
  if (Config::options.window.theme.find(".txt") == std::string::npos)
    path += ".txt";

  path = expandUserPath(path);
  std::ifstream file(path);
  if (!file.is_open()) {
    throw makeErrorAt(std::source_location::current(), "Failed to open theme file at {}", path);
  }

#ifdef __linux__
  // Setup file watching for theme changes
  if (themeInotifyFd == -1) {
    themeInotifyFd = inotify_init1(IN_NONBLOCK);
  }
  if (path != currentThemePath || themeInotifyWatch == -1) {
    if (themeInotifyWatch != -1) {
      inotify_rm_watch(themeInotifyFd, themeInotifyWatch);
    }
    themeInotifyWatch = inotify_add_watch(themeInotifyFd, path.c_str(), IN_CLOSE_WRITE);
  }
#endif

  currentThemePath = Config::options.window.theme;

  colors.spectrogram_gradient.clear();

  // Color mapping for array-based colors (RGBA)
  static std::unordered_map<std::string, float*> colorMapArrays = {
      {"color",                             colors.color                            },
      {"selection",                         colors.selection                        },
      {"text",                              colors.text                             },
      {"accent",                            colors.accent                           },
      {"bg",                                colors.background                       },
      {"bgaccent",                          colors.bgAccent                         },
      {"waveform",                          colors.waveform                         },
      {"history_low",                       colors.history_low                      },
      {"history_mid",                       colors.history_mid                      },
      {"history_high",                      colors.history_high                     },
      {"waveform_low",                      colors.waveform_low                     },
      {"waveform_mid",                      colors.waveform_mid                     },
      {"waveform_high",                     colors.waveform_high                    },
      {"oscilloscope_main",                 colors.oscilloscope_main                },
      {"oscilloscope_bg",                   colors.oscilloscope_bg                  },
      {"stereometer",                       colors.stereoMeter                      },
      {"stereometer_low",                   colors.stereoMeter_low                  },
      {"stereometer_mid",                   colors.stereoMeter_mid                  },
      {"stereometer_high",                  colors.stereoMeter_high                 },
      {"spectrum_analyzer_main",            colors.spectrum_analyzer_main           },
      {"spectrum_analyzer_secondary",       colors.spectrum_analyzer_secondary      },
      {"spectrum_analyzer_frequency_lines", colors.spectrum_analyzer_frequency_lines},
      {"spectrum_analyzer_reference_line",  colors.spectrum_analyzer_reference_line },
      {"spectrum_analyzer_threshold_line",  colors.spectrum_analyzer_threshold_line },
      {"spectrogram_main",                  colors.spectrogram_main                 },
      {"color_bars_main",                   colors.color_bars_main                  },
      {"loudness_main",                     colors.loudness_main                    },
      {"loudness_text",                     colors.loudness_text                    },
      {"vu_main",                           colors.vu_main                          },
      {"vu_caution",                        colors.vu_caution                       },
      {"vu_clip",                           colors.vu_clip                          },
      {"phosphor_border",                   colors.phosphor_border                  },
  };

  // Color mapping for single float values
  static std::unordered_map<std::string, float*> colorMapFloats = {
      {"rgb_waveform_opacity_with_history", &colors.rgb_waveform_opacity_with_history},
      {"spectrogram_low",                   &colors.spectrogram_low                  },
      {"spectrogram_high",                  &colors.spectrogram_high                 },
      {"color_bars_low",                    &colors.color_bars_low                   },
      {"color_bars_high",                   &colors.color_bars_high                  },
      {"color_bars_opacity",                &colors.color_bars_opacity               }
  };

  // Clear all colors
  for (auto& it : colorMapArrays) {
    memset(it.second, 0, 4 * sizeof(float));
  }
  for (auto& it : colorMapFloats) {
    *it.second = 0.0f;
  }

  // Parse theme file line by line
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t colonIndex = line.find(':');
    if (colonIndex == std::string::npos)
      continue;

    std::string key = line.substr(0, colonIndex);
    std::string value = line.substr(colonIndex + 1);

    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    // Parse and store color
    auto parseAndStoreColor = [&]() -> bool {
      auto itA = colorMapArrays.find(key);
      auto itB = colorMapFloats.find(key);
      if (itA == colorMapArrays.end() && itB == colorMapFloats.end())
        return true;

      std::istringstream iss(value);
      std::string token;
      std::vector<int> components;

      while (std::getline(iss, token, ','))
        try {
          components.push_back(std::stoi(token));
        } catch (...) {
          return false;
        }

      if (components.size() == 1) {
        *itB->second = components.at(0) / 255.f;
        return true;
      }

      if (components.size() < 3)
        return false;

      int alpha = components.size() >= 4 ? components.at(3) : 255;
      static float color[4];
      std::transform(components.begin(), components.begin() + 3, color, [](int v) { return v / 255.f; });
      color[3] = alpha / 255.f;

      memcpy(itA->second, color, 4 * sizeof(float));
      return true;
    };

    if (!parseAndStoreColor()) {
      logWarnAt(std::source_location::current(), "{} is invalid or has invalid data: {}", key, value);
    }

    std::string keyTrim = key;
    // trim whitespace both ends
    keyTrim.erase(0, keyTrim.find_first_not_of(" \t"));
    keyTrim.erase(keyTrim.find_last_not_of(" \t") + 1);
    if (keyTrim.size() > 2) {
      std::string suffix = keyTrim.substr(keyTrim.size() - 2);
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return std::tolower(c); });
      if (suffix == "db") {
        std::string dbStr = keyTrim.substr(0, keyTrim.size() - 2);
        // trim dbStr
        dbStr.erase(0, dbStr.find_first_not_of(" \t"));
        dbStr.erase(dbStr.find_last_not_of(" \t") + 1);
        try {
          float db = std::stof(dbStr);
          std::istringstream iss2(value);
          std::string token2;
          std::vector<int> comps;
          while (std::getline(iss2, token2, ',')) {
            try {
              // trim
              token2.erase(0, token2.find_first_not_of(" \t"));
              token2.erase(token2.find_last_not_of(" \t") + 1);
              comps.push_back(std::stoi(token2));
            } catch (...) {
            }
          }
          if (comps.size() >= 3) {
            std::array<float, 4> col = {comps[0] / 255.f, comps[1] / 255.f, comps[2] / 255.f,
                                        comps.size() >= 4 ? comps[3] / 255.f : 1.0f};
            colors.spectrogram_gradient.emplace_back(db, col);
          }
        } catch (...) {
        }
      }
    }
  }

  static std::vector<float*> mainColors = {colors.color,  colors.selection,  colors.text,
                                           colors.accent, colors.background, colors.bgAccent};

  static std::vector<std::string> colorNames = {"color", "selection", "text", "accent", "bg", "bgaccent"};

  for (size_t i = 0; i < mainColors.size(); ++i) {
    if (std::abs(mainColors[i][3]) < FLT_EPSILON) {
      logWarnAt(std::source_location::current(), "Color {} is missing", colorNames[i]);
    }
  }

  // Ensure gradient entries are ordered by dB ascending for easier lookup
  std::sort(colors.spectrogram_gradient.begin(), colors.spectrogram_gradient.end(),
            [](auto& a, auto& b) { return a.first < b.first; });
}

bool reload() {
#ifdef __linux__
  if (themeInotifyFd != -1 && themeInotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(themeInotifyFd, buf, sizeof(buf));
    if (len > 0) {

      for (int i = 0; i < len; i += sizeof(struct inotify_event)) {
        inotify_event* event = (inotify_event*)&buf[i];

        // check if the inotify got freed (file gets moved, deleted etc), kernel fires IN_IGNORED when that happens
        if ((event->mask & IN_IGNORED) == IN_IGNORED) {
          // if(event->wd == themeInotifyWatch) {
          themeInotifyWatch = -1;
          // }
        }
      }

      load();
      return true;
    }
  }
#else
  std::string path = "~/.config/pulse-visualizer/themes/" + Config::options.window.theme;
  if (Config::options.window.theme.find(".txt") == std::string::npos)
    path += ".txt";

  path = expandUserPath(path);

#ifdef _WIN32
  struct _stat st;
  if (_stat(path.c_str(), &st) != 0) {
#else
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
#endif
    logWarnAt(std::source_location::current(), "Could not stat theme file");
    return false;
  }
  static time_t lastThemeMTime = st.st_mtime;
  if (st.st_mtime != lastThemeMTime) {
    lastThemeMTime = st.st_mtime;
    load();
    return true;
  }
#endif
  return false;
}

void cleanup() {
#ifdef __linux__
  if (themeInotifyWatch != -1 && themeInotifyFd != -1) {
    inotify_rm_watch(themeInotifyFd, themeInotifyWatch);
    themeInotifyWatch = -1;
  }
  if (themeInotifyFd != -1) {
    close(themeInotifyFd);
    themeInotifyFd = -1;
  }
#endif
}

} // namespace Theme