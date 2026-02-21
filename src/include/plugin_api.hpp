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
#include "types.hpp"

#include <SDL3/SDL.h>
#include <optional>
#include <stdint.h>

#define PLUGIN_API_VERSION 1

#ifdef _WIN32
#define PV_API extern "C" __declspec(dllexport)
#else
#define PV_API extern "C"
#endif

struct PvAPI {
  uint32_t apiVersion;

  /**
   * @brief Save configuration to file
   * @return true if saved properly, false otherwise
   */
  bool (*saveConfig)(void);

  /**
   * @brief Expands a path starting with '~' to the user's home directory
   * @param path The path to expand
   * @return The expanded path with home directory substituted
   */
  std::string (*expandUserPath)(const std::string& path);

  /**
   * @brief Draw a line with specified color and thickness
   * @param x1 Start X coordinate
   * @param y1 Start Y coordinate
   * @param x2 End X coordinate
   * @param y2 End Y coordinate
   * @param color RGBA color array
   * @param thickness Line thickness
   */
  void (*drawLine)(const float& x1, const float& y1, const float& x2, const float& y2, const float* color,
                   const float& thickness);

  /**
   * @brief Draw a filled rectangle with specified color
   * @param x X coordinate
   * @param y Y coordinate
   * @param width Rectangle width
   * @param height Rectangle height
   * @param color RGBA color array
   */
  void (*drawFilledRect)(const float& x, const float& y, const float& width, const float& height, const float* color);

  /**
   * @brief Draw an arc with specified color and thickness
   * @param x X coordinate
   * @param y Y coordinate
   * @param radius Radius of the arc
   * @param startAngle Start angle in degrees
   * @param endAngle End angle in degrees
   * @param color RGBA color array
   * @param thickness Line thickness
   * @param segments Number of segments to use for the arc
   */
  void (*drawArc)(const float& x, const float& y, const float& radius, const float& startAngle, const float& endAngle,
                  const float* color, const float& thickness, const int& segments);

  /**
   * @brief Draw text at specified position
   * @param text Text to render
   * @param x X coordinate
   * @param y Y coordinate
   * @param size Font size
   * @param color RGBA color array
   */
  void (*drawText)(const char* text, const float& x, const float& y, const float& size, const float* color);

  /**
   * @brief Get text dimensions
   * @param text Text to measure
   * @param size Font size
   * @return Pair of (width, height) dimensions
   */
  std::pair<float, float> (*getTextSize)(const char* text, const float& size);

  /**
   * @brief Word wrap text into the given width (ignoring vertical size)
   * @param text Text to word wrap
   * @param maxW Maximum width
   * @param fontSize Font size
   * @return Word wrapped string
   */
  std::string (*wrapText)(const std::string& text, const float& maxW, const float& fontSize);

  /**
   * @brief Truncate text into the given width with an ellipsis (one line only)
   * @param text Text to truncate
   * @param maxW Maximum width
   * @param fontSize Font size
   * @return Truncated string
   */
  std::string (*truncateText)(const std::string& text, const float& maxW, const float& fontSize);

  /**
   * @brief Fit text into the given width and height using word wrap and then truncating
   * @param text Text to fit
   * @param maxW Maximum width
   * @param maxH Maximum height
   * @param fontSize Font size
   * @return Fitted string
   */
  std::string (*fitTextBox)(const std::string& text, const float& maxW, const float& maxH, const float& fontSize);

  /**
   * @brief Convert RGBA color to HSVA
   * @param rgba Input RGBA color array
   * @param hsva Output HSVA color array
   */
  void (*rgbaToHsva)(float* rgba, float* hsva);

  /**
   * @brief Convert HSVA color to RGBA
   * @param hsva Input HSVA color array
   * @param rgba Output RGBA color array
   */
  void (*hsvaToRgba)(float* hsva, float* rgba);

  /**
   * @brief Apply alpha transparency to a color
   * @param color Input color array (RGBA)
   * @param alpha Alpha value to apply
   * @return Pointer to temporary color with applied alpha
   */
  float* (*alpha)(float* color, float alpha);

  /**
   * @brief Linear interpolation between two colors
   * @param a First color array
   * @param b Second color array
   * @param out Output color array
   * @param t Interpolation factor (0.0 to 1.0)
   */
  void (*mix)(float* a, float* b, float* out, float t);

  /**
   * @brief create a new window
   * @param group The group of the window
   * @param title The title of the window
   * @param width The width of the window
   * @param height The height of the window
   * @param flags The flags for the window (default: SDL_WINDOW_RESIZABLE)
   */
  void (*createWindow)(const std::string& group, const std::string& title, int width, int height, uint32_t flags);

  /**
   * @brief destroy a window
   * @param group The group of the window
   * @return true if the window was destroyed, false otherwise
   * @note if the window is the current window, the current window will be set to "main"
   */
  bool (*destroyWindow)(const std::string& group);

  /**
   * @brief select a window for rendering
   * @param group The group of the window
   * @return true if the window was selected, false otherwise
   */
  bool (*selectWindow)(const std::string& group);

  /**
   * @brief get a window's dimensions
   * @param group the group of the window
   * @return a pair of integers: width and height in pixels.
   */
  std::optional<std::pair<int, int>> (*getWindowSize)(const std::string& group);

  /**
   * @brief get a window's cursor position
   * @param group the group of the window
   * @return a pair of integers: x and y in pixels
   */
  std::optional<std::pair<int, int>> (*getCursorPos)(const std::string& group);

  /**
   * @brief Set OpenGL viewport for rendering
   * @param x X position of viewport
   * @param width Width of viewport
   * @param height Height of viewport
   */
  void (*setViewport)(int x, int width, int height);

  // Pointer to config
  Config::Options* config;

  // Pointer to theme
  Theme::Colors* theme;

  // Pointer to debug flag
  bool* debug;
};

using pvPluginInitFn = int (*)(const PvAPI* api);
using pvPluginStartFn = void (*)(void);
using pvPluginStopFn = void (*)(void);
using pvPluginDrawFn = void (*)(void);
using pvPluginHandleEventFn = void (*)(SDL_Event& event);

PV_API int pvPluginInit(const PvAPI* api);
PV_API void pvPluginStart();
PV_API void pvPluginStop();
PV_API void draw();
PV_API void handleEvent(SDL_Event& event);