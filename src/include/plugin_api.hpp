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

#include <memory>
#include <optional>
#include <stdint.h>
#include <type_traits>

#define PLUGIN_API_VERSION 6

#ifdef _WIN32
#define PV_API extern "C" __declspec(dllexport)
#else
#define PV_API extern "C"
#endif

/**
 * @brief API function table exposed to external plugins.
 */
struct PvAPI {
  uint32_t apiVersion;

  /**
   * @brief Plugin identity key used for YAML namespace under plugins.<key>.
   */
  const char* pluginKey;

  /**
   * @brief Human-readable plugin name used in UI headers.
   */
  const char* pluginDisplayName;

  /**
   * @brief Opaque host-owned plugin context pointer.
   */
  void* pluginContext;

  /**
   * @brief Save configuration to file
   */
  void (*saveConfig)(void);

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
   * @brief Create a new SDL window.
   * @param group The group of the window
   * @param title The title of the window
   * @param width The width of the window
   * @param height The height of the window
   * @param flags The flags for the window (default: SDL_WINDOW_RESIZABLE)
   */
  void (*createWindow)(const std::string& group, const std::string& title, int width, int height, uint32_t flags);

  /**
   * @brief Destroy a window.
   * @param group The group of the window
   * @return true if the window was destroyed, false otherwise
   * @note if the window is the current window, the current window will be set to "main"
   */
  bool (*destroyWindow)(const std::string& group);

  /**
   * @brief Select a window for rendering.
   * @param group The group of the window
   * @return true if the window was selected, false otherwise
   */
  bool (*selectWindow)(const std::string& group);

  /**
   * @brief Get a window's dimensions.
   * @param group The group of the window
   * @return A pair of integers: width and height in pixels
   */
  std::optional<std::pair<int, int>> (*getWindowSize)(const std::string& group);

  /**
   * @brief Get a window's cursor position.
   * @param group The group of the window
   * @return A pair of integers: x and y in pixels
   */
  std::optional<std::pair<int, int>> (*getCursorPos)(const std::string& group);

  /**
   * @brief Set OpenGL viewport for rendering
   * @param bounds the bounds of the viewport
   */
  void (*setViewport)(WindowManager::Bounds bounds);

  /**
   * @brief Register a visualizer implementation.
   * @param visualizer Shared visualizer instance to add to registry
   * @return true on success, false on invalid input
   */
  bool (*registerVisualizer)(std::shared_ptr<WindowManager::VisualizerWindow> visualizer);

  /**
   * @brief Register a plugin configuration option.
   * @param pluginContext Opaque plugin context provided by the host
   * @param path Dot-separated option path under plugins.<key>
   * @param defaultValue Default value used when no override exists
   * @param descriptor Optional UI metadata and type-specific constraints
   * @return true on success, false on invalid input or duplicate registration
   */
  bool (*registerConfigOptionValue)(void* pluginContext, const char* path,
                                    const Config::PluginConfigValue* defaultValue,
                                    const Config::PluginConfigSpec* descriptor);

  /**
   * @brief Read a plugin configuration value.
   * @param pluginContext Opaque plugin context provided by the host
   * @param path Dot-separated option path under plugins.<key>
   * @param outValue Output pointer receiving the resolved value
   * @return true if value exists and type matches, false otherwise
   */
  bool (*getConfigOptionValue)(void* pluginContext, const char* path, Config::PluginConfigValue* outValue);

  /**
   * @brief Write a plugin configuration value.
   * @param pluginContext Opaque plugin context provided by the host
   * @param path Dot-separated option path under plugins.<key>
   * @param value New value to write
   * @return true on success, false on invalid path/type or constraint mismatch
   */
  bool (*setConfigOptionValue)(void* pluginContext, const char* path, const Config::PluginConfigValue* value);

  /**
   * @brief Pointer to live configuration state.
   */
  Config::Options* config;

  /**
   * @brief Pointer to live theme color state.
   */
  Theme::Colors* theme;

  /**
   * @brief Read-only pointer to main audio buffer.
   */
  const std::vector<float, AlignedAllocator<float, 32>>* bufferMid;

  /**
   * @brief Read-only pointer to side audio buffer.
   */
  const std::vector<float, AlignedAllocator<float, 32>>* bufferSide;

  /**
   * @brief Read-only pointer to raw main FFT buffer.
   */
  const std::vector<float>* fftMidRaw;

  /**
   * @brief Read-only pointer to smoothed main FFT buffer.
   */
  const std::vector<float>* fftMid;

  /**
   * @brief Read-only pointer to main FFT phase data.
   */
  const std::vector<float>* fftMidPhase;

  /**
   * @brief Read-only pointer to raw side FFT buffer.
   */
  const std::vector<float>* fftSideRaw;

  /**
   * @brief Read-only pointer to smoothed side FFT buffer.
   */
  const std::vector<float>* fftSide;

  /**
   * @brief Read-only pointer to side FFT phase data.
   */
  const std::vector<float>* fftSidePhase;

  /**
   * @brief Read-only pointer to current ring-buffer write position.
   */
  const size_t* writePos;

  /**
   * @brief Read-only pointer to SDL window state map.
   */
  const std::unordered_map<std::string, SDLWindow::State>* states;

  /**
   * @brief Read-only pointer to global debug flag.
   */
  const bool* debug;

  /**
   * @brief Type-safe convenience wrapper for registering a plugin config option.
   * @tparam T Option value type (`bool`, `int`, `float`, `std::string`)
   * @param path Dot-separated option path relative to this plugin namespace
   * @param defaultValue Default value used when no value exists in config
   * @param descriptor Optional metadata and detents/choices for UI behavior
   * @return true on success, false on invalid input or duplicate registration
   */
  template <typename T>
  bool registerConfigOption(std::string path, T defaultValue, const Config::PluginConfigSpec& descriptor = {}) const;

  /**
   * @brief Type-safe convenience wrapper for reading a plugin config option.
   * @tparam T Option value type (`bool`, `int`, `float`, `std::string`)
   * @param path Dot-separated option path relative to this plugin namespace
   * @return Resolved value on success, `std::nullopt` if missing or type mismatch
   */
  template <typename T> std::optional<T> getConfigOption(std::string path) const;

  /**
   * @brief Type-safe convenience wrapper for writing a plugin config option.
   * @tparam T Option value type (`bool`, `int`, `float`, `std::string`)
   * @param path Dot-separated option path relative to this plugin namespace
   * @param value New value to write
   * @return true on success, false on invalid path/type or constraint mismatch
   */
  template <typename T> bool setConfigOption(std::string path, T value) const;
};

/** @brief Plugin entry point type for initialization. */
using pvPluginInitFn = int (*)(const PvAPI* api);
/** @brief Plugin entry point type for startup. */
using pvPluginStartFn = void (*)(void);
/** @brief Plugin entry point type for shutdown. */
using pvPluginStopFn = void (*)(void);
/** @brief Plugin entry point type for rendering. */
using pvPluginDrawFn = void (*)(void);
/** @brief Plugin entry point type for SDL event handling. */
using pvPluginHandleEventFn = void (*)(SDL_Event& event);
/** @brief Plugin metadata provider entry point type. */
using pvPluginGetInfoFn = void (*)(const char** key, const char** displayName);
/** @brief Plugin config reload notification type. */
using pvPluginConfigReloadFn = void (*)(void);

/**
 * @brief Initialize plugin with host API.
 * @param api Host-provided API table
 * @return 0 on success, non-zero on failure
 */
PV_API int pvPluginInit(const PvAPI* api);

/**
 * @brief Start plugin runtime logic.
 */
PV_API void pvPluginStart();

/**
 * @brief Stop plugin runtime logic and release resources.
 */
PV_API void pvPluginStop();

/**
 * @brief Render plugin content for the current frame.
 */
PV_API void draw();

/**
 * @brief Handle an SDL input/window event.
 * @param event Event from the host event loop
 */
PV_API void handleEvent(SDL_Event& event);

/**
 * @brief Provide plugin key and display name metadata.
 * @param key Stable plugin key used in config YAML
 * @param displayName Human-readable plugin name for UI
 */
PV_API void pvPluginGetInfo(const char** key, const char** displayName);

/**
 * @brief Notify plugin that host config was reloaded.
 */
PV_API void pvPluginOnConfigReload();

template <typename T>
inline bool PvAPI::registerConfigOption(std::string path, T defaultValue,
                                        const Config::PluginConfigSpec& descriptor) const {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::string>);
  if (!registerConfigOptionValue)
    return false;

  return std::visit(
      [&](const Config::PluginConfigValue& value) {
        return registerConfigOptionValue(pluginContext, path.c_str(), &value, &descriptor);
      },
      std::variant<Config::PluginConfigValue> {Config::PluginConfigValue {std::move(defaultValue)}});
}

template <typename T> inline std::optional<T> PvAPI::getConfigOption(std::string path) const {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::string>);
  if (!getConfigOptionValue)
    return std::nullopt;

  Config::PluginConfigValue value;
  if (!getConfigOptionValue(pluginContext, path.c_str(), &value))
    return std::nullopt;

  if (const T* typed = std::get_if<T>(&value))
    return *typed;
  return std::nullopt;
}

template <typename T> inline bool PvAPI::setConfigOption(std::string path, T value) const {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> ||
                std::is_same_v<T, std::string>);
  if (!setConfigOptionValue)
    return false;

  return std::visit(
      [&](const Config::PluginConfigValue& resolvedValue) {
        return setConfigOptionValue(pluginContext, path.c_str(), &resolvedValue);
      },
      std::variant<Config::PluginConfigValue> {Config::PluginConfigValue {std::move(value)}});
}