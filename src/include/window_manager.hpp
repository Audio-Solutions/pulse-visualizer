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
 * @brief Window splitter for resizing visualizer windows
 */
struct Splitter {
  SDL_Window* win = nullptr;
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
  SDL_Window* win = nullptr;
  int x, width;
  float aspectRatio = 0;
  size_t pointCount;
  bool hovering = false;

  /**
   * @brief Phosphor effect rendering components
   */
  struct Phosphor {
    GLuint energyTexture = 0;
    GLuint ageTexture = 0;
    GLuint tempTexture = 0;
    GLuint tempTexture2 = 0;
    GLuint outputTexture = 0;
    GLuint frameBuffer = 0;
    GLuint vertexBuffer = 0;
    int textureWidth = 0;
    int textureHeight = 0;
  } phosphor;

  void (*render)() = nullptr;

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
};

// Global window management
extern std::vector<VisualizerWindow> windows;
extern std::vector<Splitter> splitters;

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
int moveSplitter(int index, int targetX = -1);

/**
 * @brief Update splitter states and positions
 */
void updateSplitters();

/**
 * @brief Reorder visualizer windows based on configuration
 */
void reorder();

} // namespace WindowManager