#pragma once
#include "common.hpp"

namespace WindowManager {
extern float dt;

constexpr int MIN_WIDTH = 80;

void setViewport(int x, int width, int height);

struct Splitter {
  SDL_Window* win = nullptr;
  int x, dx;
  bool draggable = true;
  bool dragging = false;
  bool hovering = false;

  void handleEvent(const SDL_Event& event);
  void draw();
};

struct VisualizerWindow {
  SDL_Window* win = nullptr;
  int x, width;
  float aspectRatio = 0;
  size_t pointCount;
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

  void transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type);
  void resizeTextures();
  void draw();
};

extern std::vector<VisualizerWindow> windows;
extern std::vector<Splitter> splitters;

void drawSplitters();
void renderAll();
void resizeTextures();
void resizeWindows();
int moveSplitter(int index, int targetX = -1);
void updateSplitters();
void reorder();

} // namespace WindowManager