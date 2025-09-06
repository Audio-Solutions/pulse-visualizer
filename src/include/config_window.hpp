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

namespace ConfigWindow {

// font sizes

constexpr float fontSizeTop = 16.f;
constexpr float fontSizeHeader = 20.f;
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

extern bool shown;
constexpr int w = 600, h = 800;

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
  Lissajous,
  FFT,
  Spectrogram,
  Audio,
  Visualizers,
  Window,
  Debug,
  Phosphor,
  LUFS,
  VU
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
 * @param ignoreTextOverflow Ignore the text overflow on the label (for checkbox)
 */
void createLabelElement(Page& page, float& cy, const std::string key, const std::string label,
                        const std::string description, const bool ignoreTextOverflow = false);

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
                         const std::string label, const std::string description, const int precision = 1,
                         const bool zeroOff = false);

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
                               const std::string description, const int precision = 1);

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
                           const std::string description);

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
                           const std::string description);

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

/**
 * @brief Creates a visualizer window list element
 * @param page Destination page
 * @param cy Current Y axis
 * @param key Element key
 * @param value Pointer to the value
 * @param label Label string
 * @param description Tooltip string
 */
void createVisualizerListElement(Page& page, float& cy, const std::string key,
                                 std::map<std::string, std::vector<std::string>>* value, const std::string label,
                                 const std::string description);

}; // namespace ConfigWindow
