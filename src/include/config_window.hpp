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
#include "graphics.hpp"
#include "sdl_window.hpp"
#include "theme.hpp"

namespace ConfigWindow {

// font sizes

constexpr float fontSizeTop = 16.f;
constexpr float fontSizeHeader = 14.f;
constexpr float fontSizeLabel = 12.f;
constexpr float fontSizeTooltip = 12.f;
constexpr float fontSizeValue = 14.f;

// layout config, should respond to these nicely

constexpr float padding = 10;
constexpr float margin = 10;
constexpr float spacing = 5;
constexpr float stdSize = padding * 2 + fontSizeTop; // + 16.f

constexpr float labelSize = padding * 2 + fontSizeTop * 10.f; // + 160.f
constexpr float sliderHandleWidth = 10.f;
constexpr float sliderPadding = 2.f;
constexpr float scrollingDisplayMargin = margin * 2 + stdSize;

// scroll multipliers for the sliders

constexpr float scrollAlt = 0.1f;
constexpr float scrollNormal = 1.f;
constexpr float scrollCtrl = 10.f;
constexpr float scrollShift = 100.f;

extern size_t sdlWindow;
extern bool shown;
extern int w, h;

extern float offsetX, offsetY;
extern bool alt, ctrl, shift;

/**
 * @brief create/destroy the window
 */
void toggle();

/**
 * @brief Handle window event
 * @param event Event to handle
 */
void handleEvent(const SDL_Event& event);

/**
 * @brief Draw the window
 */
void draw();

// "private" functions:

/**
 * @brief Creates the top page and the pages
 */
void init();
inline void initTop();
inline void initPages();

/**
 * @brief Check if the mouse is over a rectangle
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 * @return true if the mouse is over the specified rectangle
 */
bool mouseOverRect(float x, float y, float w, float h);

/**
 * @brief Check if the mouse is over a rectangle, with respect to the scroll offset
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 * @return true if the mouse is over the specified offset rectangle
 */
bool mouseOverRectTranslated(float x, float y, float w, float h);

/**
 * @brief Draw an arrow in the given @p x and @p y position
 * @param dir Direction of the arrow (-1 for left, 1 for right, 2 for up, -2 for down)
 * @param x X position
 * @param y Y position
 * @param buttonSize size of the arrow
 * @see WindowManager::VisualizerWindow::drawArrow
 */
void drawArrow(int dir, float x, float y, const float buttonSize);

/**
 * @brief Set Z axis offset to @p z
 * @param z Z axis value
 */
void layer(float z);

/**
 * @brief Pop the last matrix from the stack
 */
void layer();

enum class PageType {
  Oscilloscope,
  BandpassFilter,
  Lissajous,
  FFT,
  Spectrogram,
  Audio,
  Visualizers,
  Window,
  Debug,
  Phosphor,
  LUFS,
  VU,
  Lowpass
};
extern PageType currentPage;

/**
 * @brief Converts PageType into a string
 * @param page PageType to convert
 * @return String representation of @p page
 */
std::string pageToString(PageType page);

struct Element {
  // hitbox
  float x, y, w, h;

  bool hovered;
  bool click;   // for use by draw() function, don't write to this
  bool focused; // tracks whether or not the element is currently focused

  void* data = nullptr;

  std::function<void(Element*)> update;
  std::function<void(Element*)> render;
  std::function<void(Element*)> clicked;
  std::function<void(Element*)> unclicked;
  std::function<void(Element*, int)> scrolled;
  std::function<void(Element*)> cleanup;
};

struct SliderElementData {
  float mouseOffset; // tracks the mouse offset from the handle when it was clicked
};

struct Page {
  std::map<std::string, Element> elements;
  float height;
};

extern Page topPage;
extern std::map<PageType, Page> pages;

/**
 * @brief Creates a label element, for use by other create*Element functions
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param label Label string
 * @param description Tooltip string
 */
void createLabelElement(Page& page, float& cy, const std::string key, const std::string label,
                        const std::string description);

/**
 * @brief Creates a header element
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param label Header string
 */
void createHeaderElement(Page& page, float& cy, const std::string key, const std::string header);

/**
 * @brief Creates a slider element with the given @p ValueType
 * @tparam ValueType Type for @p value
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param min Minimum slider value
 * @param max Maximum slider value
 * @param label Label string
 * @param description Tooltip string
 * @param precision Precision to display (defaults to 1)
 */
template <typename ValueType>
void createSliderElement(Page& page, float& cy, const std::string key, ValueType* value, ValueType min, ValueType max,
                         const std::string label, const std::string description, const int precision = 1) {
  Element sliderElement = {0};
  sliderElement.data = calloc(1, sizeof(SliderElementData));

  sliderElement.update = [cy](Element* self) {
    self->w = w - (margin * 3) - labelSize;
    self->h = stdSize;
    self->x = margin * 2 + labelSize;
    self->y = cy - stdSize;
  };

  sliderElement.render = [value, min, max, precision](Element* self) {
    float percent = ((float)(*value) - min) / (max - min);
    // slidable region needs to be decreased by handle width so the handle can go to 0 and 100% safely
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;

    SliderElementData* data = (SliderElementData*)self->data;

    if (self->focused) {
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float adjustedX = mouseX - (self->x + sliderPadding + data->mouseOffset);
      float mousePercent = adjustedX / slidableWidth;

      mousePercent = std::clamp(mousePercent, 0.f, 1.f);
      *value = min + mousePercent * (max - min);
    }

    bool handleHovered = mouseOverRectTranslated(self->x + handleX + sliderPadding, self->y + sliderPadding,
                                                 sliderHandleWidth, self->h - sliderPadding * 2) ||
                         self->focused;

    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             handleHovered ? Theme::colors.accent : Theme::colors.bgaccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2,
                             handleHovered ? Theme::colors.bgaccent : Theme::colors.accent);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << *value;
    std::pair<float, float> textSize = Graphics::Font::getTextSize(ss.str().c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(ss.str().c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                             (int)(self->y + self->h / 2 - textSize.second / 2), fontSizeValue, Theme::colors.text,
                             sdlWindow);
  };

  sliderElement.clicked = [value, min, max](Element* self) {
    float percent = ((float)(*value) - min) / (max - min);
    float slidableWidth = self->w - sliderPadding * 2 - sliderHandleWidth;
    float handleX = slidableWidth * percent;
    bool handleHovered = mouseOverRectTranslated(self->x + handleX, self->y + sliderPadding, sliderHandleWidth,
                                                 self->h - sliderPadding * 2);

    SliderElementData* data = (SliderElementData*)self->data;

    if (handleHovered) {
      self->focused = true;
      data->mouseOffset = SDLWindow::mousePos[sdlWindow].first - (self->x + sliderPadding + handleX);
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

/**
 * @brief Creates a detented slider element with the given @p ValueType
 * @tparam ValueType Type for @p value
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param detents List of detents
 * @param label Label string
 * @param description Tooltip string
 * @param precision Precision to display (defaults to 1)
 */
template <typename ValueType>
void createDetentSliderElement(Page& page, float& cy, const std::string key, ValueType* value,
                               const std::vector<ValueType>& detents, const std::string label,
                               const std::string description, const int precision = 1) {
  Element sliderElement = {0};
  sliderElement.data = calloc(1, sizeof(SliderElementData));

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

    SliderElementData* data = (SliderElementData*)self->data;

    if (self->focused) {
      float mouseX = SDLWindow::mousePos[sdlWindow].first;
      float adjustedX = mouseX - (self->x + sliderPadding + data->mouseOffset);
      float mousePercent = adjustedX / slidableWidth;
      mousePercent = std::clamp(mousePercent, 0.f, 1.f);

      // Snap to nearest detent
      int nearestIndex = static_cast<int>(std::round(mousePercent * (detents.size() - 1)));
      nearestIndex = std::clamp(nearestIndex, 0, static_cast<int>(detents.size() - 1));
      *value = detents[nearestIndex];
    }

    bool handleHovered = mouseOverRectTranslated(self->x + handleX + sliderPadding, self->y + sliderPadding,
                                                 sliderHandleWidth, self->h - sliderPadding * 2) ||
                         self->focused;

    Graphics::drawFilledRect(self->x, self->y, self->w, self->h,
                             handleHovered ? Theme::colors.accent : Theme::colors.bgaccent);
    Graphics::drawFilledRect(self->x + handleX + sliderPadding, self->y + sliderPadding, sliderHandleWidth,
                             self->h - sliderPadding * 2,
                             handleHovered ? Theme::colors.bgaccent : Theme::colors.accent);

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

    bool handleHovered = mouseOverRectTranslated(self->x + handleX, self->y + sliderPadding, sliderHandleWidth,
                                                 self->h - sliderPadding * 2);

    SliderElementData* data = (SliderElementData*)self->data;

    if (handleHovered) {
      self->focused = true;
      data->mouseOffset = SDLWindow::mousePos[sdlWindow].first - (self->x + sliderPadding + handleX);
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

/**
 * @brief Creates a dropdown element with the given @p ValueType and @p possibleValues
 * @tparam ValueType Type for @p value
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param possibleValues Dictionary of possible values <Value, Display>
 * @param label Label string
 * @param description Tooltip string
 */
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
      const bool current = *value == kv.first;
      if (current) {
        currentPair = kv;
      }

      if (self->focused) {
        const float y = originalY - stdSize - (i * stdSize);
        bool thingHovered = mouseOverRectTranslated(self->x, y, self->w, stdSize);

        Graphics::drawFilledRect(self->x, y, self->w, stdSize,
                                 thingHovered ? Theme::colors.accent
                                              : (current ? Theme::colors.text : Theme::colors.bgaccent));

        std::pair<float, float> textSize = Graphics::Font::getTextSize(kv.second.c_str(), fontSizeValue, sdlWindow);
        Graphics::Font::drawText(kv.second.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
                                 (int)(y + stdSize / 2 - textSize.second / 2), fontSizeValue,
                                 current && !thingHovered ? Theme::colors.background : Theme::colors.text, sdlWindow);
      }
      i++;
    }

    layer();

    std::pair<float, float> textSize =
        Graphics::Font::getTextSize(currentPair.second.c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(currentPair.second.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
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

/**
 * @brief Creates a "carousel" element with the given @p ValueType and @p possibleValues
 * @tparam ValueType Type for @p value
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param possibleValues Dictionary of possible values <Value, Display>
 * @param label Label string
 * @param description Tooltip string
 */
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

  tickLabelElement.render = [value, possibleValues](Element* self) {
    Graphics::drawFilledRect(self->x, self->y, self->w, self->h, Theme::colors.bgaccent);

    std::pair<ValueType, std::string> currentPair;
    for (std::pair<const ValueType, std::string> kv : possibleValues) {
      if (*value == kv.first) {
        currentPair = kv;
        break;
      }
    }

    std::pair<float, float> textSize =
        Graphics::Font::getTextSize(currentPair.second.c_str(), fontSizeValue, sdlWindow);
    Graphics::Font::drawText(currentPair.second.c_str(), (int)(self->x + self->w / 2 - textSize.first / 2),
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

/**
 * @brief Creates a checkmark element
 * @tparam ValueType Type for @p value
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param label Label string
 * @param description Tooltip string
 */
void createCheckElement(Page& page, float& cy, const std::string key, bool* value, const std::string label,
                        const std::string description);

}; // namespace ConfigWindow
