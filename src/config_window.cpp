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

#include "include/config_window.hpp"

#include "include/audio_engine.hpp"
#include "include/config.hpp"
#include "include/config_schema.hpp"
#include "include/graphics.hpp"
#include "include/plugin.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizer_registry.hpp"
#include "include/window_manager.hpp"

namespace ConfigWindow {

bool shown = false; // is the window currently visible?

float offsetX = 0, offsetY = 0;                // current scroll offset
bool alt = false, ctrl = false, shift = false; // tracker for modifier keys

Page topPage;
std::map<PageType, Page> pages;
PageType currentPage = PageType::Audio;

std::vector<std::pair<float, std::string>> popupMessages;

// Drag state for Visualizers page
static struct DragState {
  bool active = false;
  std::string fromGroup;
  size_t index = 0;
  std::string viz;
} dragState;

// Friendly names for visualizers
static std::map<std::string, std::string> vizLabels;

static int newWindowCounter = 1;

inline constexpr std::array<PageType, 13> pageOrder = {
    PageType::Oscilloscope, PageType::Lissajous,   PageType::FFT,     PageType::Spectrogram, PageType::Waveform,
    PageType::Audio,        PageType::Visualizers, PageType::Plugins, PageType::Window,      PageType::Debug,
    PageType::Phosphor,     PageType::LUFS,        PageType::VU,
};

inline void ensurePageInitialized(PageType pageType);
inline void buildVisualizersPage();
inline void buildPluginsPage();

PageType pageAtOffset(PageType current, int offset) {
  auto it = std::find(pageOrder.begin(), pageOrder.end(), current);
  size_t index = (it == pageOrder.end()) ? 0 : static_cast<size_t>(std::distance(pageOrder.begin(), it));
  size_t size = pageOrder.size();
  size_t nextIndex =
      (index + size +
       static_cast<size_t>((offset % static_cast<int>(size) + static_cast<int>(size)) % static_cast<int>(size))) %
      size;
  return pageOrder[nextIndex];
}

constexpr ConfigSchema::SchemaPage schemaPageFromWindowPage(PageType pageType) {
  switch (pageType) {
  case PageType::Oscilloscope:
    return ConfigSchema::SchemaPage::Oscilloscope;
  case PageType::Lissajous:
    return ConfigSchema::SchemaPage::Lissajous;
  case PageType::FFT:
    return ConfigSchema::SchemaPage::FFT;
  case PageType::Spectrogram:
    return ConfigSchema::SchemaPage::Spectrogram;
  case PageType::Waveform:
    return ConfigSchema::SchemaPage::Waveform;
  case PageType::Audio:
    return ConfigSchema::SchemaPage::Audio;
  case PageType::Window:
    return ConfigSchema::SchemaPage::Window;
  case PageType::Debug:
    return ConfigSchema::SchemaPage::Debug;
  case PageType::Phosphor:
    return ConfigSchema::SchemaPage::Phosphor;
  case PageType::LUFS:
    return ConfigSchema::SchemaPage::LUFS;
  case PageType::VU:
    return ConfigSchema::SchemaPage::VU;
  case PageType::Visualizers:
  case PageType::Plugins:
    return ConfigSchema::SchemaPage::Unknown;
  }

  return ConfigSchema::SchemaPage::Unknown;
}

template <typename T> std::map<T, std::string> makeChoiceMap(std::span<const ConfigSchema::Choice<T>> choices) {
  std::map<T, std::string> values;
  for (const auto& choice : choices)
    values.emplace(choice.value, std::string(choice.label));
  return values;
}

/**
 * @brief flatten a Config::Node tree into an ordered vector of visualizer ids
 */
static void flattenConfigNode(const WindowManager::Node& node, std::vector<std::string>& out) {
  std::function<void(const WindowManager::Node&)> recurse = [&](const WindowManager::Node& n) {
    if (!n)
      return;

    if (auto wPtr = std::get_if<std::shared_ptr<WindowManager::VisualizerWindow>>(n.get())) {
      auto w = *wPtr;
      if (!w->id.empty())
        out.push_back(w->id);
      return;
    }
    auto& s = std::get<WindowManager::Splitter>(*n);
    recurse(s.primary);
    recurse(s.secondary);
  };
  recurse(node);
}

/**
 * @brief build display groups from map of Config::Node
 */
static std::vector<std::pair<std::string, std::vector<std::string>>>
buildDisplayGroups(const std::unordered_map<std::string, WindowManager::Node>* value) {
  using Group = std::pair<std::string, std::vector<std::string>>;
  std::vector<Group> groups;

  // Build groups from config
  for (const auto& kv : *value) {
    if (kv.first == "hidden")
      continue;
    std::vector<std::string> items;
    flattenConfigNode(kv.second, items);
    groups.push_back({kv.first, std::move(items)});
  }

  // Build hidden group
  std::unordered_set<std::string> loaded;
  for (const auto& g : groups) {
    for (const auto& id : g.second)
      loaded.insert(id);
  }
  std::vector<std::string> hidden;
  for (const auto& vptr : VisualizerRegistry::visualizers) {
    if (!vptr)
      continue;
    if (loaded.find(vptr->id) == loaded.end())
      hidden.push_back(vptr->id);
  }

  // Append hidden group
  groups.push_back({"hidden", std::move(hidden)});
  return groups;
}

std::map<std::string, std::string> makeChoiceMap(std::span<const ConfigSchema::Choice<std::string_view>> choices) {
  std::map<std::string, std::string> values;
  for (const auto& choice : choices)
    values.emplace(std::string(choice.value), std::string(choice.label));
  return values;
}

struct GeneratedField {
  std::string path;
  std::string groupKey;
  std::string groupLabel;
  std::function<void(Page&, float&)> render;
};

template <typename FieldType, typename RenderFn>
void tryPushField(PageType pageType, std::vector<GeneratedField>& out, const FieldType& field, RenderFn renderFn) {
  if (field.page != schemaPageFromWindowPage(pageType))
    return;

  std::string groupKey(field.groupKey);

  std::string groupLabel = groupKey;
  bool upperNext = true;
  for (char& ch : groupLabel) {
    if (ch == '_') {
      ch = ' ';
      upperNext = true;
      continue;
    }
    if (upperNext) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      upperNext = false;
    }
  }

  std::string elementKey(field.path);
  std::replace(elementKey.begin(), elementKey.end(), '.', '_');

  out.push_back({std::string(field.path), groupKey, groupLabel,
                 [field, renderFn, elementKey](Page& page, float& cy) { renderFn(page, cy, field, elementKey); }});
}

void buildSchemaPage(Page& page, float& cy, PageType pageType) {
  std::vector<GeneratedField> fields;
  fields.reserve(ConfigSchema::boolFields.size() + ConfigSchema::intFields.size() + ConfigSchema::floatFields.size() +
                 ConfigSchema::stringFields.size() + ConfigSchema::rotationFields.size());

  ConfigSchema::forEachFieldByType(
      [&](const auto& field) {
        tryPushField(pageType, fields, field, [](Page& p, float& localCy, const auto& f, const std::string& key) {
          bool& value = f.get(Config::options);
          createCheckElement(p, localCy, key, &value, std::string(f.label), std::string(f.description));
        });
      },
      [&](const auto& field) {
        tryPushField(pageType, fields, field, [](Page& p, float& localCy, const auto& f, const std::string& key) {
          int& value = f.get(Config::options);
          if (!f.ui.detents.empty()) {
            createDetentSliderElement<int>(p, localCy, key, &value, f.ui.detents, std::string(f.label),
                                           std::string(f.description), f.ui.precision);
            return;
          }

          if (f.ui.hasRange) {
            createSliderElement<int>(p, localCy, key, &value, f.ui.min, f.ui.max, std::string(f.label),
                                     std::string(f.description), f.ui.precision, f.ui.zeroOff);
          }
        });
      },
      [&](const auto& field) {
        tryPushField(pageType, fields, field, [](Page& p, float& localCy, const auto& f, const std::string& key) {
          float& value = f.get(Config::options);
          if (!f.ui.choices.empty()) {
            std::map<float, std::string> values = makeChoiceMap<float>(f.ui.choices);
            if (f.ui.useTick)
              createEnumTickElement<float>(p, localCy, key, &value, std::move(values), std::string(f.label),
                                           std::string(f.description));
            else
              createEnumDropElement<float>(p, localCy, key, &value, std::move(values), std::string(f.label),
                                           std::string(f.description));
            return;
          }

          if (f.ui.hasRange) {
            createSliderElement<float>(p, localCy, key, &value, f.ui.min, f.ui.max, std::string(f.label),
                                       std::string(f.description), f.ui.precision, f.ui.zeroOff);
          }
        });
      },
      [&](const auto& field) {
        if (field.path == "font")
          return;

        tryPushField(pageType, fields, field, [](Page& p, float& localCy, const auto& f, const std::string& key) {
          std::string& value = f.get(Config::options);
          std::map<std::string, std::string> values = makeChoiceMap(f.ui.choices);
          if (f.path == "audio.engine") {
#if !HAVE_PIPEWIRE
            values.erase("pipewire");
#endif
#if !HAVE_PULSEAUDIO
            values.erase("pulseaudio");
#endif
#if !HAVE_WASAPI
            values.erase("wasapi");
#endif
          }
          if (values.empty()) {
            if (f.path == "audio.device") {
              for (const std::string& device : AudioEngine::enumerate())
                values.emplace(device, device);
            } else if (f.path == "window.theme") {
              try {
                std::filesystem::directory_iterator iterator(expandUserPath("~/.config/pulse-visualizer/themes/"));
                for (const auto& file : iterator) {
                  if (file.path().extension() != ".txt")
                    continue;
                  const std::string filename = file.path().filename().string();
                  const std::string baseName = filename.substr(0, filename.size() - 4);
                  values.emplace(filename, baseName);
                }
              } catch (const std::exception&) {
              }
            }
          }
          if (values.empty()) {
            if (value.empty())
              return;
            values.emplace(value, value);
          }

          if (f.ui.useTick)
            createEnumTickElement<std::string>(p, localCy, key, &value, std::move(values), std::string(f.label),
                                               std::string(f.description));
          else
            createEnumDropElement<std::string>(p, localCy, key, &value, std::move(values), std::string(f.label),
                                               std::string(f.description));
        });
      },
      [&](const auto& field) {
        tryPushField(pageType, fields, field, [](Page& p, float& localCy, const auto& f, const std::string& key) {
          Config::Rotation& value = f.get(Config::options);
          std::map<Config::Rotation, std::string> values = makeChoiceMap<Config::Rotation>(f.ui.choices);
          createEnumDropElement<Config::Rotation>(p, localCy, key, &value, std::move(values), std::string(f.label),
                                                  std::string(f.description));
        });
      });

  std::sort(fields.begin(), fields.end(), [](const GeneratedField& a, const GeneratedField& b) {
    const int aMiscRank = a.groupKey == "misc" ? 1 : 0;
    const int bMiscRank = b.groupKey == "misc" ? 1 : 0;
    return std::tie(aMiscRank, a.groupKey, a.path) < std::tie(bMiscRank, b.groupKey, b.path);
  });

  std::string currentGroup;
  for (auto& field : fields) {
    if (field.groupKey != currentGroup) {
      createHeaderElement(page, cy, "group_" + field.groupKey, field.groupLabel);
      currentGroup = field.groupKey;
    }
    field.render(page, cy);
  }

  if (pageType == PageType::Debug) {
    Element element = {0};
    element.render = [](Element* self) {
      float cyOther = scrollingDisplayMargin;
      float fps = 1.f / WindowManager::dt;
      static float fpsLerp = 0.f;
      float diff = abs(fps - fpsLerp);
      float t = diff / (diff + Config::options.window.fps_limit * 2.0f);
      fpsLerp = std::lerp(fpsLerp, fps, t);

      Graphics::Font::drawText("Pulse " VERSION_STRING " commit " VERSION_COMMIT, 0, cyOther, fontSizeHeader,
                               Theme::colors.text);
      cyOther += fontSizeHeader;

      std::stringstream ss;
      ss << "FPS: " << std::fixed << std::setprecision(1) << fpsLerp;
      Graphics::Font::drawText(ss.str().c_str(), 0, cyOther, fontSizeHeader, Theme::colors.text);
      cyOther += fontSizeHeader;

      self->x = 0;
      self->y = scrollingDisplayMargin;
      self->w = 100;
      self->h = cyOther - scrollingDisplayMargin;
    };

    page.elements.insert({"debug", element});
  }
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
      float* bgcolor = Theme::colors.bgAccent;
      if (mouseOverRect(self->x, self->y, self->w, self->h))
        bgcolor = Theme::colors.accent;

      // Draw background
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, bgcolor);

      drawArrow(-1, self->x, self->y, self->w);
    };

    leftChevron.clicked = [](Element* self) {
      currentPage = pageAtOffset(currentPage, -1);
      ensurePageInitialized(currentPage);
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
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, Theme::colors.bgAccent);

      const std::string str = pageToString(currentPage);
      std::pair<float, float> textSize = Graphics::Font::getTextSize(str.c_str(), fontSizeTop);
      Graphics::Font::drawText(str.c_str(), self->x + self->w / 2 - textSize.first / 2,
                               (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeTop, Theme::colors.text);
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
      float* bgcolor = Theme::colors.bgAccent;
      if (mouseOverRect(self->x, self->y, self->w, self->h))
        bgcolor = Theme::colors.accent;

      // Draw background
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h, bgcolor);

      drawArrow(1, self->x, self->y, self->w);
    };

    rightChevron.clicked = [](Element* self) {
      currentPage = pageAtOffset(currentPage, 1);
      ensurePageInitialized(currentPage);
    };

    topPage.elements.insert({"rightChevron", rightChevron});
  }

  // load default button
  {
    Element defaultButton = {0};
    defaultButton.update = [](Element* self) {
      std::pair<float, float> textSize = Graphics::Font::getTextSize("Load default", fontSizeTop);
      self->w = textSize.first + (padding * 2);
      self->h = stdSize;
      self->x = margin;
      self->y = margin;
    };

    defaultButton.render = [](Element* self) {
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                               self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

      std::pair<float, float> textSize = Graphics::Font::getTextSize("Load default", fontSizeTop);
      Graphics::Font::drawText("Load default", self->x + padding, (int)(self->y + self->h / 2 - textSize.second / 2),
                               fontSizeTop, Theme::colors.text);
    };

    defaultButton.clicked = [](Element* self) {
      logDebug("Restoring config to default");
      Config::rollBackup();
      std::filesystem::remove(expandUserPath("~/.config/pulse-visualizer/config.yml"));
      Config::copyFiles();
      Config::load();

      SDLWindow::selectWindow("main");
      reconfigure();
      SDLWindow::selectWindow("menu");

      popupMessages.push_back({0.f, "Restored config to defaults"});
    };

    topPage.elements.insert({"defaultButton", defaultButton});
  }

  // Restore latest backup button
  {
    Element restoreButton = {0};
    restoreButton.update = [](Element* self) {
      std::pair<float, float> textSize = Graphics::Font::getTextSize("Restore latest", fontSizeTop);
      std::pair<float, float> textSizeDefaultBtn = Graphics::Font::getTextSize("Load default", fontSizeTop);
      self->w = textSize.first + (padding * 2);
      self->h = stdSize;
      self->x = margin * 2 + textSizeDefaultBtn.first + (padding * 2);
      self->y = margin;
    };

    restoreButton.render = [](Element* self) {
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                               self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

      std::pair<float, float> textSize = Graphics::Font::getTextSize("Restore latest", fontSizeTop);
      Graphics::Font::drawText("Restore latest", self->x + padding, (int)(self->y + self->h / 2 - textSize.second / 2),
                               fontSizeTop, Theme::colors.text);
    };

    restoreButton.clicked = [](Element* self) {
      logDebug("Restoring config to default");
      Config::restoreBackup();
      Config::load();

      SDLWindow::selectWindow("main");
      reconfigure();
      SDLWindow::selectWindow("menu");

      popupMessages.push_back({0.f, "Restored backup"});
    };

    topPage.elements.insert({"restoreButton", restoreButton});
  }

  // save button
  {
    Element saveButton = {0};
    saveButton.update = [](Element* self) {
      std::pair<float, float> textSize = Graphics::Font::getTextSize("Save", fontSizeTop);
      self->w = textSize.first + (padding * 2);
      self->h = stdSize;
      self->x = w - self->w - margin;
      self->y = margin;
    };

    saveButton.render = [](Element* self) {
      Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                               self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

      std::pair<float, float> textSize = Graphics::Font::getTextSize("Save", fontSizeTop);
      Graphics::Font::drawText("Save", self->x + padding, (int)(self->y + self->h / 2 - textSize.second / 2),
                               fontSizeTop, Theme::colors.text);
    };

    saveButton.clicked = [](Element* self) {
      const std::string cfgPath = expandUserPath("~/.config/pulse-visualizer/config.yml");
      try {
        std::filesystem::permissions(cfgPath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
        Config::save();
      } catch (std::exception& e) {
        logWarnAt(std::source_location::current(), "Failed to set permissions on config file: {}", e.what());
      }
      popupMessages.push_back({0.f, "Saved"});
    };

    topPage.elements.insert({"saveButton", saveButton});
  }

  // popup message
  {
    Element popupMessage = {0};
    popupMessage.update = [](Element* self) {
      for (size_t i = 0; i < popupMessages.size(); i++) {
        if (popupMessages[i].first > 3.f) {
          popupMessages.erase(popupMessages.begin() + i);
          i--;
        }
      }
    };

    popupMessage.render = [](Element* self) {
      int index = 0;
      const float fontSize = fontSizeTop;

      for (auto& pair : popupMessages) {
        float bgColor[4];
        std::copy(Theme::colors.bgAccent, Theme::colors.bgAccent + 4, bgColor);
        float fontColor[4];
        std::copy(Theme::colors.text, Theme::colors.text + 4, fontColor);
        std::pair<float, float> textSize = Graphics::Font::getTextSize(pair.second.c_str(), fontSize);

        pair.first += WindowManager::dt;

        if (pair.first < 3.f) {
          float x = w / 2 - padding - textSize.first / 2;
          float y = h / 2 - padding - textSize.second / 2 + (index * (fontSize + padding * 2 + spacing));
          float w = textSize.first + padding * 2;
          float h = textSize.second + padding * 2;

          if (mouseOverRect(x, y, w, h)) {
            pair.first = 0.f;
          }

          if (pair.first > 2.f) {
            float t = pair.first - 2.f;
            float alpha = std::lerp(1.f, 0.f, t);
            bgColor[3] = alpha;
            fontColor[3] = alpha;
          }

          layer(3.f);
          Graphics::drawFilledRect(x, y, w, h, bgColor);
          Graphics::Font::drawText(pair.second.c_str(), x + padding, y + padding, fontSize, fontColor);
          layer();
        }

        index++;
      }
    };

    topPage.elements.insert({"popupMessage", popupMessage});
  }
}

inline void initPages() {
  ensurePageInitialized(currentPage);
  ensurePageInitialized(PageType::Visualizers);
  ensurePageInitialized(PageType::Plugins);
}

inline void ensurePageInitialized(PageType pageType) {
  if (pages.find(pageType) != pages.end())
    return;

  if (pageType == PageType::Visualizers) {
    buildVisualizersPage();
    return;
  }

  if (pageType == PageType::Plugins) {
    buildPluginsPage();
    return;
  }

  const float cyInit = h - scrollingDisplayMargin;
  Page page;
  float cy = cyInit;
  buildSchemaPage(page, cy, pageType);
  page.height = cyInit - cy;
  pages.insert({pageType, std::move(page)});
}

inline void buildVisualizersPage() {
  if (pages.find(PageType::Visualizers) != pages.end())
    return;

  const float cyInit = h - scrollingDisplayMargin;
  Page page;
  float cy = cyInit;

  VisualizerRegistry::ensureBuiltinsRegistered();

  createVisualizerListElement(page, cy, "visualizers", &Config::options.visualizers, "Visualizers", "Visualizers");

  page.height = cyInit - cy;
  pages.insert({PageType::Visualizers, std::move(page)});
}

inline void buildPluginsPage() {
  if (pages.find(PageType::Plugins) != pages.end())
    return;

  const float cyInit = h - scrollingDisplayMargin;
  Page page;
  float cy = cyInit;

  // Build plugin groups snapshot
  std::map<std::string, std::vector<std::pair<std::string, Config::PluginOptionRecord>>> pluginGroups;
  {
    std::lock_guard<std::mutex> lock(Config::pluginOptionsMutex);
    for (auto& kv : Config::pluginOptions) {
      const auto& key = kv.first;
      pluginGroups[key.first].emplace_back(key.second, kv.second);
    }
  }

  if (pluginGroups.empty()) {
    createHeaderElement(page, cy, "plugins_none", "No Plugin Options Registered");
    page.height = cyInit - cy;
    pages.insert({PageType::Plugins, std::move(page)});
    return;
  }

  for (auto& [pluginKey, pluginRuleGroup] : pluginGroups) {

    // Prefer stored display name from the plugin option records
    std::string pluginHeader;
    if (!pluginRuleGroup.empty() && !pluginRuleGroup.front().second.displayName.empty())
      pluginHeader = pluginRuleGroup.front().second.displayName;
    else
      pluginHeader = pluginKey;

    createHeaderElement(page, cy, "plugin_header_" + pluginKey, pluginHeader);

    // Build a sorted list of copies of the plugin option records
    std::vector<std::pair<std::string, Config::PluginOptionRecord>> fields;
    fields.reserve(pluginRuleGroup.size());
    for (auto& item : pluginRuleGroup)
      fields.emplace_back(item.first, item.second);

    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    auto bindPersist = [&](const std::string& key, std::function<void()> persist) {
      static const std::array<const char*, 7> suffixes = {"#label",     "#check",    "#slider",   "#dropdown",
                                                          "#tickLabel", "#tickLeft", "#tickRight"};

      auto attachPersist = [&](Element& element) {
        auto oldClicked = element.clicked;
        element.clicked = [oldClicked, persist](Element* self) {
          if (oldClicked)
            oldClicked(self);
          persist();
        };

        auto oldUnclicked = element.unclicked;
        element.unclicked = [oldUnclicked, persist](Element* self) {
          if (oldUnclicked)
            oldUnclicked(self);
          persist();
        };

        auto oldScrolled = element.scrolled;
        element.scrolled = [oldScrolled, persist](Element* self, int amount) {
          if (oldScrolled)
            oldScrolled(self, amount);
          persist();
        };
      };

      bool attached = false;
      for (const char* suffix : suffixes) {
        const std::string elementId = key + suffix;
        auto it = page.elements.find(elementId);
        if (it == page.elements.end())
          continue;

        attachPersist(it->second);
        attached = true;
      }

      if (!attached)
        persist();
    };

    for (const auto& [path, rec] : fields) {
      const Config::PluginConfigSpec* rule = &rec.spec;
      std::string elementKey = "plugin_" + pluginKey + "_" + path;
      std::replace(elementKey.begin(), elementKey.end(), '.', '_');
      const std::string label = rule->label.empty() ? path : rule->label;

      switch (rule->type) {
      case Config::PluginConfigType::Bool: {
        // Allocate heap storage for the UI value so it outlives this function.
        bool* pvalue = (bool*)malloc(sizeof(bool));
        *pvalue = false;
        // Initialize from the copied plugin record value
        if (const bool* v = std::get_if<bool>(&rec.value))
          *pvalue = *v;

        createCheckElement(page, cy, elementKey, pvalue, label, rule->description);

        // Persist function that writes back via the public setter
        auto persist = [pvalue, pluginKey, path]() {
          Config::PluginConfigValue val(*pvalue);
          Plugin::PluginInstance tmp {};
          tmp.key = pluginKey;
          Config::setPluginConfigOption(&tmp, path.c_str(), &val);
        };

        auto it = page.elements.find(elementKey + "#check");
        if (it != page.elements.end()) {
          it->second.data = pvalue;
        }
        bindPersist(elementKey, persist);

        break;
      }
      case Config::PluginConfigType::Int: {
        int* pvalue = (int*)malloc(sizeof(int));
        *pvalue = 0;
        if (const int* v = std::get_if<int>(&rec.value))
          *pvalue = *v;

        if (!rule->detentsInt.empty()) {
          createDetentSliderElement<int>(page, cy, elementKey, pvalue, std::span<const int>(rule->detentsInt), label,
                                         rule->description, rule->precision);
        } else {
          const bool hasRange = std::abs(rule->max - rule->min) > FLT_EPSILON;
          int minValue = hasRange ? static_cast<int>(std::round(rule->min)) : (*pvalue - 100);
          int maxValue = hasRange ? static_cast<int>(std::round(rule->max)) : (*pvalue + 100);
          if (maxValue <= minValue)
            maxValue = minValue + 1;

          createSliderElement<int>(page, cy, elementKey, pvalue, minValue, maxValue, label, rule->description,
                                   rule->precision, rule->zeroOff);
        }

        // Persist wrapper
        auto persist = [pvalue, pluginKey, path]() {
          Config::PluginConfigValue val(*pvalue);
          Plugin::PluginInstance tmp {};
          tmp.key = pluginKey;
          Config::setPluginConfigOption(&tmp, path.c_str(), &val);
        };

        auto it = page.elements.find(elementKey + "#slider");
        if (it != page.elements.end()) {
          it->second.data = pvalue;
        }
        bindPersist(elementKey, persist);

        break;
      }
      case Config::PluginConfigType::Float: {
        float* pvalue = (float*)malloc(sizeof(float));
        *pvalue = 0.0f;
        if (const float* v = std::get_if<float>(&rec.value))
          *pvalue = *v;

        if (!rule->detentsFloat.empty()) {
          createDetentSliderElement<float>(page, cy, elementKey, pvalue, std::span<const float>(rule->detentsFloat),
                                           label, rule->description, rule->precision);
        } else {
          const bool hasRange = std::abs(rule->max - rule->min) > FLT_EPSILON;
          float minValue = hasRange ? rule->min : (*pvalue - 1.0f);
          float maxValue = hasRange ? rule->max : (*pvalue + 1.0f);
          if (maxValue <= minValue)
            maxValue = minValue + 1.0f;

          createSliderElement<float>(page, cy, elementKey, pvalue, minValue, maxValue, label, rule->description,
                                     rule->precision, rule->zeroOff);
        }

        auto persist = [pvalue, pluginKey, path]() {
          Config::PluginConfigValue val(*pvalue);
          Plugin::PluginInstance tmp {};
          tmp.key = pluginKey;
          Config::setPluginConfigOption(&tmp, path.c_str(), &val);
        };

        auto it = page.elements.find(elementKey + "#slider");
        if (it != page.elements.end()) {
          it->second.data = pvalue;
        }
        bindPersist(elementKey, persist);

        break;
      }
      case Config::PluginConfigType::String: {
        std::string* pvalue = new std::string();
        if (const std::string* v = std::get_if<std::string>(&rec.value))
          *pvalue = *v;

        std::map<std::string, std::string> values;
        for (const auto& choice : rule->choices)
          values.emplace(choice, choice);

        if (values.empty() && !pvalue->empty())
          values.emplace(*pvalue, *pvalue);

        if (rule->useTick)
          createEnumTickElement<std::string>(page, cy, elementKey, pvalue, std::move(values), label, rule->description);
        else
          createEnumDropElement<std::string>(page, cy, elementKey, pvalue, std::move(values), label, rule->description);

        // Persist wrapper
        auto persist = [pvalue, pluginKey, path]() {
          Config::PluginConfigValue val(*pvalue);
          Plugin::PluginInstance tmp {};
          tmp.key = pluginKey;
          Config::setPluginConfigOption(&tmp, path.c_str(), &val);
        };

        auto it = page.elements.find(elementKey + "#dropdown");
        if (it != page.elements.end()) {
          it->second.data = pvalue;
          it->second.cleanup = [](Element* self) {
            if (self->data) {
              delete static_cast<std::string*>(self->data);
              self->data = nullptr;
            }
          };
        }
        bindPersist(elementKey, persist);

        break;
      }
      }
    }
  }

  page.height = cyInit - cy;
  pages.insert({PageType::Plugins, std::move(page)});
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
    std::string actualLabel = ignoreTextOverflow ? label : Graphics::Font::truncateText(label, self->w, fontSizeLabel);
    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeLabel);
    Graphics::Font::drawText(actualLabel.c_str(), self->x, (int)(self->y + self->h / 2 - textSize.second / 2),
                             fontSizeLabel, Theme::colors.text);

    if (self->hovered) {
      layer(2.f);
      float mouseX = SDLWindow::states["menu"].mousePos.first;
      float mouseY = SDLWindow::states["menu"].mousePos.second;
      float maxW = w - mouseX - margin * 2 - padding * 2;

      std::string actualDescription = Graphics::Font::wrapText(description, maxW, fontSizeTooltip);
      std::pair<float, float> textSize = Graphics::Font::getTextSize(actualDescription.c_str(), fontSizeTooltip);
      Graphics::drawFilledRect(mouseX + margin - offsetX, mouseY - margin - textSize.second - padding * 2 - offsetY,
                               textSize.first + padding * 2, textSize.second + padding * 2, Theme::colors.accent);

      Graphics::Font::drawText(actualDescription.c_str(), mouseX + margin + padding - offsetX,
                               mouseY - margin - padding - fontSizeTooltip - offsetY, fontSizeTooltip,
                               Theme::colors.text);
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
    std::pair<float, float> textSize = Graphics::Font::getTextSize(header.c_str(), fontSizeHeader);
    Graphics::Font::drawText(header.c_str(), self->x + self->w / 2 - textSize.first / 2,
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeHeader, Theme::colors.text);
    Graphics::drawLine(self->x, self->y, self->x + self->w, self->y, Theme::colors.bgAccent, 2);
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
                             self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

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
    Config::save();
    logDebug("Destroying Config window");
    SDLWindow::destroyWindow("menu");
    deinit();
  } else {
    logDebug("Creating Config window");
    SDLWindow::createWindow("menu", "Configuration", w, h, 0);
    init();
  }

  shown = !shown;
}

void handleEvent(const SDL_Event& event) {
  if (!shown)
    return;

  if (SDLWindow::states.find("menu") != SDLWindow::states.end() && !SDLWindow::states["menu"].focused &&
      event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED)
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
      ensurePageInitialized(currentPage);
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

      ensurePageInitialized(currentPage);
      it = pages.find(currentPage);

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
  const float mx = SDLWindow::states["menu"].mousePos.first;
  const float my = SDLWindow::states["menu"].mousePos.second;

  if (mx > x && mx < x + w && my > y && my < y + h)
    return true;

  return false;
}

bool mouseOverRectTranslated(float x, float y, float w, float h) {
  // add the scroll offset and call mouseOverRect
  x += offsetX;
  y += offsetY;

  const float my = SDLWindow::states["menu"].mousePos.second;
  return my > scrollingDisplayMargin && my < ConfigWindow::h - scrollingDisplayMargin && mouseOverRect(x, y, w, h);
}

void drawArrow(int dir, float x, float y, const float buttonSize) {
  // Draw arrow
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_POLYGON_SMOOTH);

  glColor4fv(Theme::colors.text);
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
  float vertices[] = {x1, y1, x2, y2, x3, y3};
  glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

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

  ensurePageInitialized(currentPage);

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
  SDLWindow::selectWindow("menu");

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

  // make sure the current page doesn't draw below the elements
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
    return "Spectrum Analyzer";
  case PageType::Spectrogram:
    return "Spectrogram";
  case PageType::Waveform:
    return "Waveform";
  case PageType::Audio:
    return "Audio";
  case PageType::Visualizers:
    return "Visualizers";
  case PageType::Plugins:
    return "Plugins";
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
      float mouseX = SDLWindow::states["menu"].mousePos.first;
      float adjustedX = mouseX - (self->x + sliderPadding + sliderHandleWidth / 2);
      float mousePercent = adjustedX / slidableWidth;

      mousePercent = std::clamp(mousePercent, 0.f, 1.f);
      *value = min + mousePercent * (max - min);
    }

    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             hovered ? Theme::colors.accent : Theme::colors.bgAccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2, hovered ? Theme::colors.bgAccent : Theme::colors.accent);

    std::stringstream ss;
    if (zeroOff && *value <= FLT_EPSILON) {
      ss << "Off";
    } else {
      ss << std::fixed << std::setprecision(precision) << *value;
    }
    std::pair<float, float> textSize = Graphics::Font::getTextSize(ss.str().c_str(), fontSizeValue);
    Graphics::Font::drawText(ss.str().c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text);
  };

  sliderElement.clicked = [value, min, max](Element* self) {
    float percent = ((float)(*value) - min) / (max - min);
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;
    bool hovered = mouseOverRectTranslated(self->x, self->y, self->w, self->h);

    if (hovered) {
      self->focused = true;
      self->x = SDLWindow::states["menu"].mousePos.first - (sliderPadding + handleX + sliderHandleWidth / 2);
    }
  };

  sliderElement.unclicked = [](Element* self) { self->focused = false; };

  sliderElement.scrolled = [value, min, max, precision](Element* self, int amount) {
    float change = amount;
    float precisionAmount = std::powf(10, -precision);
    if (precision != 0) {
      if (shift) {
        change *= precisionAmount * 1000.f;
      } else if (ctrl) {
        change *= precisionAmount * 100.f;
      } else if (alt) {
        change *= precisionAmount;
      } else {
        change *= precisionAmount * 10.f;
      }
    } else {
      if (shift) {
        change *= precisionAmount * 100.f;
      } else if (ctrl) {
        change *= precisionAmount * 10.f;
      } else {
        change *= precisionAmount;
      }
    }

    *value = std::clamp(*value + (ValueType)change, min, max);
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#slider", sliderElement});
  cy -= stdSize + margin;
}

template <typename ValueType>
void createDetentSliderElement(Page& page, float& cy, const std::string key, ValueType* value,
                               const std::span<const ValueType> detents, const std::string label,
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
      float mouseX = SDLWindow::states["menu"].mousePos.first;
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
                             hovered ? Theme::colors.accent : Theme::colors.bgAccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2, hovered ? Theme::colors.bgAccent : Theme::colors.accent);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << *value;
    std::pair<float, float> textSize = Graphics::Font::getTextSize(ss.str().c_str(), fontSizeValue);
    Graphics::Font::drawText(ss.str().c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text);
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
      self->x = SDLWindow::states["menu"].mousePos.first - (sliderPadding + handleX + sliderHandleWidth / 2);
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
                             dropdownHovered ? Theme::colors.accent : Theme::colors.bgAccent);

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
          if (kvFirst == str) {
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
                                              : (current ? Theme::colors.text : Theme::colors.bgAccent));

        std::string actualLabel = Graphics::Font::truncateText(kv.second, self->w - padding * 2, fontSizeValue);

        std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue);
        Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                                 (int)(y + stdSize / 2 - textSize.second / 2), fontSizeValue,
                                 current && !thingHovered ? Theme::colors.background : Theme::colors.text);
      }
      i++;
    }

    layer();

    std::string actualLabel = Graphics::Font::truncateText(currentPair.second, self->w - padding * 2, fontSizeValue);

    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue);
    Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(originalY + stdSize / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text);
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
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h, Theme::colors.bgAccent);

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

    std::string actualLabel = Graphics::Font::truncateText(currentPair.second, self->w - padding * 2, fontSizeValue);

    std::pair<float, float> textSize = Graphics::Font::getTextSize(actualLabel.c_str(), fontSizeValue);
    Graphics::Font::drawText(actualLabel.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text);
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
                             self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

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
                             self->hovered ? Theme::colors.accent : Theme::colors.bgAccent);

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
                                 std::unordered_map<std::string, WindowManager::Node>* value, const std::string label,
                                 const std::string description) {

  Element visualizerListElement = {0};

  visualizerListElement.update = [cy, value](Element* self) {
    self->x = margin;
    self->w = w - margin * 2;

    float used = 0.f;
    const float groupHeaderH = stdSize;
    const float itemH = stdSize;
    const float groupBottomMargin = margin;
    auto groups = buildDisplayGroups(value);
    for (const auto& kv : groups) {
      const auto& items = kv.second;
      used += groupHeaderH;
      used += spacing;
      used += std::max((size_t)1, items.size()) * (itemH + spacing);
      used += groupBottomMargin;
    }
    const float createZoneH = stdSize * 1.5f;
    used += createZoneH + margin;

    self->h = used;
    self->y = cy - self->h;
  };

  visualizerListElement.render = [value](Element* self) {
    float yTop = self->y + self->h;

    const float mouseX = SDLWindow::states["menu"].mousePos.first;
    const float mouseY = SDLWindow::states["menu"].mousePos.second;

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

    auto groups = buildDisplayGroups(value);
    for (const auto& kv : groups) {
      const std::string& group = kv.first;
      const auto& items = kv.second;

      // Group header
      float headerY = yTop - groupHeaderH;
      Graphics::drawFilledRect(self->x, headerY, self->w, groupHeaderH, Theme::colors.bgAccent);
      std::pair<float, float> textSize = Graphics::Font::getTextSize(group.c_str(), fontSizeHeader);
      Graphics::Font::drawText(group.c_str(), (int)(self->x + padding),
                               (int)(headerY + groupHeaderH / 2 - textSize.second / 2), fontSizeHeader,
                               Theme::colors.text);

      // Items area
      float itemsTop = headerY - spacing;
      float y = itemsTop;
      float groupX = self->x;
      float groupW = self->w;

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
        float* bg = hovered ? Theme::colors.accent : Theme::colors.bgAccent;
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
        std::pair<float, float> t = Graphics::Font::getTextSize(label.c_str(), fontSizeValue);
        Graphics::Font::drawText(label.c_str(), (int)(itemX + padding), (int)(itemY + itemHh / 2 - t.second / 2),
                                 fontSizeValue, Theme::colors.text);

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
                             overCreate && dragState.active ? Theme::colors.accent : Theme::colors.bgAccent);
    const char* createLabel = "+ Create new window (drop here)";
    std::pair<float, float> t = Graphics::Font::getTextSize(createLabel, fontSizeHeader);
    Graphics::Font::drawText(createLabel, (int)(self->x + self->w / 2 - t.first / 2),
                             (int)(createY + createZoneH / 2 - t.second / 2), fontSizeHeader, Theme::colors.text);

    if (dragState.active) {
      for (const auto& target : targets) {
        if (mouseOverRectTranslated(target.x, target.y, target.w, target.h)) {
          Graphics::drawFilledRect(target.x, target.y, target.w, target.h, Theme::alpha(Theme::colors.accent, 0.2f));
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
      std::pair<float, float> ts = Graphics::Font::getTextSize(label.c_str(), fontSizeValue);
      float chipW = std::min(self->w - padding * 2, ts.first + padding * 2);

      float gx = mouseX - offsetX - chipW / 2.0f;
      float gy = mouseY - offsetY - chipH / 2.0f;

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      Graphics::drawFilledRect(gx, gy, chipW, chipH, Theme::alpha(Theme::colors.accent, 0.5f));

      std::pair<float, float> t2 = Graphics::Font::getTextSize(label.c_str(), fontSizeValue);
      Graphics::Font::drawText(label.c_str(), (int)(gx + chipW / 2 - t2.first / 2),
                               (int)(gy + chipH / 2 - t2.second / 2), fontSizeValue,
                               Theme::alpha(Theme::colors.text, 0.9f));

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

    auto groups = buildDisplayGroups(value);
    for (const auto& kv : groups) {
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

    std::string targetGroup;
    bool foundTarget = false;
    size_t dropIndex = 0;

    auto groups = buildDisplayGroups(value);
    for (const auto& kv : groups) {
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

    // cancel move if target is not main (determine source size from flattened tree)
    if (dragState.fromGroup == "main") {
      auto itSrc = value->find("main");
      size_t srcCount = 0;
      if (itSrc != value->end()) {
        std::vector<std::string> tmp;
        flattenConfigNode(itSrc->second, tmp);
        srcCount = tmp.size();
      }
      if (srcCount == 1) {
        if (!foundTarget || targetGroup != "main" || overCreate) {
          dragState.active = false;
          return;
        }
      }
    }

    if (foundTarget || overCreate) {
      // Add to destination, then remove from source.
      if (overCreate) {
        // Create new window
        std::string newKey;
        do {
          newKey = std::string("win_") + std::to_string(newWindowCounter++);
        } while (value->find(newKey) != value->end());
        WindowManager::addVisualizerToGroup(newKey, dragState.viz);
      } else {
        // Add to existing target group
        if (targetGroup != "hidden")
          WindowManager::addVisualizerToGroup(targetGroup, dragState.viz);
      }

      // Now remove from source group
      WindowManager::removeVisualizerFromGroup(dragState.fromGroup, dragState.viz);

      // Persist changes
      Config::save();
    }

    dragState.active = false;
  };

  createLabelElement(page, cy, key, label, description);
  page.elements.insert({key + "#visualizers", visualizerListElement});
  cy -= visualizerListElement.h + margin;
}
}; // namespace ConfigWindow
