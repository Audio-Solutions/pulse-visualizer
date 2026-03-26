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

namespace SDLWindow {

// Global buffer handles
extern GLuint vertexBuffer;
extern GLuint vertexColorBuffer;

// Window and OpenGL context handles
extern std::unordered_map<std::string, State> states;

extern std::stop_source stopSource;

/**
 * @brief Cleanup SDL window and OpenGL context
 */
void deinit();

/**
 * @brief Initialize SDL window and OpenGL context
 */
void init();

/**
 * @brief Handle SDL events (keyboard, window, quit)
 * @param event The SDL event to process
 */
void handleEvent(SDL_Event& event);

/**
 * @brief Swap the OpenGL buffers to display the rendered frame
 */
void display();

/**
 * @brief Clear the OpenGL color buffer with the current background color
 */
void clear();

/**
 * @brief Create a new window.
 * @param group The group of the window
 * @param title The title of the window
 * @param width The width of the window
 * @param height The height of the window
 * @param flags The flags for the window (default: SDL_WINDOW_RESIZABLE)
 */
void createWindow(const std::string& group, const std::string& title, int width, int height,
                  uint32_t flags = SDL_WINDOW_RESIZABLE);

/**
 * @brief Destroy a window.
 * @param group The group of the window
 * @return true if the window was destroyed, false otherwise
 * @note if the window is the current window, the current window will be set to "main"
 */
bool destroyWindow(const std::string& group);

/**
 * @brief Select a window for rendering.
 * @param group The group of the window
 * @return true if the window was selected, false otherwise
 */
bool selectWindow(const std::string& group);

/**
 * @brief Get a window's dimensions.
 * @param group The group of the window
 * @return A pair of integers: width and height in pixels
 */
std::optional<std::pair<int, int>> getWindowSize(const std::string& group);

/**
 * @brief Get a window's cursor position.
 * @param group The group of the window
 * @return A pair of integers: x and y in pixels
 */
std::optional<std::pair<int, int>> getCursorPos(const std::string& group);

/**
 * @brief Push a modelview layer and translate along Z.
 *
 * Pushes the current `GL_MODELVIEW` matrix and translates by `z`.
 * Call the no-argument `layer()` to pop and restore the previous state.
 * @param z Z offset for the new layer
 */
void layer(float z);

/**
 * @brief Pop the last modelview layer.
 *
 * Pops the matrix pushed by `layer(float)`. Must be called after
 * `layer(float)` to restore the previous modelview state.
 */
void layer();

/**
 * @brief initialize cursors.
 */
void initCursors();

/**
 * @brief free cursors.
 */
void freeCursors();

// Cursors
enum class CursorType { Default = 0, Horizontal, Vertical, Move, Count };

static std::array<SDL_Cursor*, static_cast<size_t>(CursorType::Count)> g_cursors {nullptr};

constexpr std::array<SDL_SystemCursor, static_cast<size_t>(CursorType::Count)> cursor_map = {
    SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_MOVE};

/**
 * @brief Set the cursor.
 * @param type The cursor type
 */
void setCursor(CursorType type);
} // namespace SDLWindow