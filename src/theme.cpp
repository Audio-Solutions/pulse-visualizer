#include "include/theme.hpp"

#include "include/config.hpp"

namespace Theme {

// Global theme colors
Colors colors;

#ifdef __linux__
int themeInotifyFd = -1;
int themeInotifyWatch = -1;
std::string currentThemePath;
#endif

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

void load(const std::string& name) {
  // Construct theme file path
  std::string path = "~/.config/pulse-visualizer/themes/" + name;
  if (name.find(".txt") == std::string::npos)
    path += ".txt";

  path = expandUserPath(path);
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open theme file: " << path << std::endl;
    return;
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

  currentThemePath = path;

  // Color mapping for array-based colors (RGBA)
  static std::unordered_map<std::string, float*> colorMapArrays = {
      {"color",                             colors.color                            },
      {"selection",                         colors.selection                        },
      {"text",                              colors.text                             },
      {"accent",                            colors.accent                           },
      {"bg",                                colors.background                       },
      {"bgaccent",                          colors.bgaccent                         },
      {"waveform",                          colors.waveform                         },
      {"history_low",                       colors.history_low                      },
      {"history_mid",                       colors.history_mid                      },
      {"history_high",                      colors.history_high                     },
      {"waveform_low",                      colors.waveform_low                     },
      {"waveform_mid",                      colors.waveform_mid                     },
      {"waveform_high",                     colors.waveform_high                    },
      {"oscilloscope_main",                 colors.oscilloscope_main                },
      {"oscilloscope_bg",                   colors.oscilloscope_bg                  },
      {"stereometer",                       colors.stereometer                      },
      {"stereometer_low",                   colors.stereometer_low                  },
      {"stereometer_mid",                   colors.stereometer_mid                  },
      {"stereometer_high",                  colors.stereometer_high                 },
      {"spectrum_analyzer_main",            colors.spectrum_analyzer_main           },
      {"spectrum_analyzer_secondary",       colors.spectrum_analyzer_secondary      },
      {"spectrum_analyzer_frequency_lines", colors.spectrum_analyzer_frequency_lines},
      {"spectrum_analyzer_reference_line",  colors.spectrum_analyzer_reference_line },
      {"spectrum_analyzer_threshold_line",  colors.spectrum_analyzer_threshold_line },
      {"spectrogram_main",                  colors.spectrogram_main                 },
      {"color_bars_main",                   colors.color_bars_main                  },
      {"loudness_main",                     colors.loudness_main                    },
      {"loudness_text",                     colors.loudness_text                    },
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
      std::cerr << key << " is invalid or has invalid data: " << value << std::endl;
    }
  }

  static std::vector<float*> mainColors = {colors.color,  colors.selection,  colors.text,
                                           colors.accent, colors.background, colors.bgaccent};

  static std::vector<std::string> colorNames = {"color", "selection", "text", "accent", "bg", "bgaccent"};

  for (size_t i = 0; i < mainColors.size(); ++i) {
    if (std::abs(mainColors[i][3]) < 1e-6f) {
      std::cout << "Color " << colorNames[i] << " is missing!" << std::endl;
    }
  }
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

      load(Config::options.window.theme);
      return true;
    }
  }
#else
  struct stat st;
  if (stat(currentThemePath.c_str(), &st) != 0) {
    std::cerr << "Warning: could not stat theme file." << std::endl;
    return false;
  }
  static time_t lastThemeMTime = 0;
  if (st.st_mtime != lastThemeMTime) {
    lastThemeMTime = st.st_mtime;
    load(Config::options.window.theme);
    return true;
  }
#endif
  return false;
}

} // namespace Theme