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

/**
 * @brief Visualizer window with phosphor effect support
 */
struct VisualizerWindow {
  std::string group;
  int x, width;
  float aspectRatio = 0;
  size_t forceWidth = 0;
  size_t pointCount;
  bool hovering = false;
  constexpr static size_t buttonSize = 20;
  constexpr static size_t buttonPadding = 10;

  /**
   * @brief Phosphor effect rendering components
   */
  struct Phosphor {
    GLuint energyTextureR = 0;
    GLuint energyTextureG = 0;
    GLuint energyTextureB = 0;
    GLuint ageTexture = 0;
    GLuint tempTextureR = 0;
    GLuint tempTextureG = 0;
    GLuint tempTextureB = 0;
    GLuint tempTexture2R = 0;
    GLuint tempTexture2G = 0;
    GLuint tempTexture2B = 0;
    GLuint outputTexture = 0;
    GLuint frameBuffer = 0;
    GLuint vertexBuffer = 0;
    GLuint vertexColorBuffer = 0;
    int textureWidth = 0;
    int textureHeight = 0;
  } phosphor;

  void (*render)();

  /**
   * @brief Direction to arrowX and arrowY
   * @param dir Direction of the arrow
   * @returns pair of ints for arrowX and arrowY
   */
  std::pair<int, int> getArrowPos(int dir);

  /**
   * @brief Draw an arrow for the swap button
   * @param dir Direction of the arrow (-1 for left, 1 for right, 2 for up, -2 for down)
   */
  void drawArrow(int dir);

  /**
   * @brief Check if the swap button is pressed
   * @param dir Direction of the arrow (-1 for left, 1 for right, 2 for up, -2 for down)
   * @param mouseX X position of the mouse
   * @param mouseY Y position of the mouse
   */
  bool buttonPressed(int dir, int mouseX, int mouseY);

  /**
   * @brief Check if the swap button is hovering
   * @param dir Direction of the arrow (-1 for left, 1 for right, 2 for up, -2 for down)
   * @param mouseX X position of the mouse
   * @param mouseY Y position of the mouse
   */
  bool buttonHovering(int dir, int mouseX, int mouseY);

  /**
   * @brief Handle SDL events for the window
   * @param event SDL event to process
   */
  void handleEvent(const SDL_Event& event);

  /**
   * @brief Transfer texture data between OpenGL textures
   * @param oldTex Source texture
   * @param newTex Destination texture
   * @param format Texture format
   * @param type Texture data type
   */
  void transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type);

  /**
   * @brief Resize phosphor effect textures
   */
  void resizeTextures();

  /**
   * @brief Draw the visualizer window
   */
  void draw();

  /**
   * @brief Cleanup OpenGL resources
   */
  void cleanup();
};

// Global window management
extern std::map<std::string, std::vector<VisualizerWindow>> windows;
extern std::map<std::string, std::vector<Splitter>> splitters;
extern std::vector<std::string> markedForDeletion;

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
 * @param bool popout Whether to popout the window
 */
void popWindow(size_t index, std::string key, bool popout = false);

} // namespace WindowManager
