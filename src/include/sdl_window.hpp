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

namespace SDLWindow {

struct State {
  SDL_Window* win = nullptr;
  SDL_WindowID winID = 0;
  SDL_GLContext glContext = nullptr;
  std::pair<int, int> windowSizes;
  std::pair<int, int> mousePos;
  bool focused;
};

// Global buffer handles
extern GLuint vertexBuffer;
extern GLuint vertexColorBuffer;
extern GLuint frameBuffer;

// Window and OpenGL context handles
extern std::unordered_map<std::string, State> states;

extern std::atomic<bool> running;

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
 * @brief create a new window
 * @param group The group of the window
 * @param title The title of the window
 * @param width The width of the window
 * @param height The height of the window
 * @param flags The flags for the window (default: SDL_WINDOW_RESIZABLE)
 */
void createWindow(const std::string& group, const std::string& title, int width, int height,
                  uint32_t flags = SDL_WINDOW_RESIZABLE);

/**
 * @brief destroy a window
 * @param group The group of the window
 * @return true if the window was destroyed, false otherwise
 * @note if the window is the current window, the current window will be set to "main"
 */
bool destroyWindow(const std::string& group);

/**
 * @brief select a window at an index for rendering
 * @param group The group of the window
 * @return true if the window was selected, false otherwise
 */
bool selectWindow(const std::string& group);
} // namespace SDLWindow