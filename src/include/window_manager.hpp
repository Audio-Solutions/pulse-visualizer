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

namespace WindowManager {

// Delta time for frame timing
extern float dt;

constexpr int MIN_WIDTH = 80;

/**
 * @brief Set OpenGL viewport for rendering
 * @param x X position of viewport
 * @param width Width of viewport
 * @param height Height of viewport
 */
void setViewport(int x, int width, int height);

/**
 * @brief Handle SDL events for the window manager
 * @param event SDL event to process
 */
void handleEvent(const SDL_Event& event);

/**
 * @brief Window splitter for resizing visualizer windows
 */
struct Splitter {
  std::string group;
  int x, dx;
  bool draggable = true;
  bool dragging = false;
  bool hovering = false;

  /**
   * @brief Handle SDL events for the splitter
   * @param event SDL event to process
   */
  void handleEvent(const SDL_Event& event);

  /**
   * @brief Draw the splitter visual element
   */
  void draw();
};

// Global window management
extern std::map<std::string, std::vector<std::shared_ptr<VisualizerWindow>>> windows;
extern std::map<std::string, std::vector<Splitter>> splitters;
extern std::vector<std::string> markedForDeletion;

/**
 * @brief Find an instantiated visualizer window by visualizer id.
 * @param id Visualizer id
 * @return Pointer to matching window instance, or nullptr if not found.
 */
VisualizerWindow* findWindowById(const std::string& id);

/**
 * @brief Find a window index by pointer within a window group.
 * @param group Window group key
 * @param window Target window pointer
 * @return Index if found, std::nullopt otherwise.
 */
std::optional<size_t> findWindowIndex(const std::string& group, const VisualizerWindow* window);

/**
 * @brief Delete windows marked for deletion
 */
void deleteMarkedWindows();

/**
 * @brief Draw all window splitters
 */
void drawSplitters();

/**
 * @brief Render all visualizer windows
 */
void renderAll();

/**
 * @brief Resize textures for all windows
 */
void resizeTextures();

/**
 * @brief Resize all visualizer windows
 */
void resizeWindows();

/**
 * @brief Move a splitter to a new position
 * @param key Window-group key for the splitter list
 * @param index Splitter index
 * @param targetX Target X position (-1 for current position)
 * @return New X position
 */
int moveSplitter(std::string key, int index, int targetX = -1);

/**
 * @brief Update splitter states and positions
 */
void updateSplitters();

/**
 * @brief Reorder visualizer windows based on configuration
 */
void reorder();

/**
 * @brief Direction for swapping visualizers
 */
enum Direction { Left = -1, Right = 1, Up = 2, Down = -2 };

/**
 * @brief Swap visualizer at index with its neighbor in the given direction
 * @param index Index of visualizer to swap
 * @param key Key of the window group
 */
template <Direction direction> void swapVisualizer(size_t index, std::string key);

/**
 * @brief Popout visualizer at index from its window group
 * @param index Index of visualizer to popout
 * @param key Key of the window group
 * @param popout Whether to pop out into a dedicated window group
 */
void popWindow(size_t index, std::string key, bool popout = false);

} // namespace WindowManager
