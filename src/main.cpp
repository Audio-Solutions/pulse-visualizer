#include "audio_data.hpp"
#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "splitter.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <thread>

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return 1;
  }

  // Load config
  Config::load();

  bool drawFPS = Config::getBool("debug.log_fps");

  // Initialize the theme system
  Theme::ThemeManager::initialize();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window* win = SDL_CreateWindow("Audio Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     Config::getInt("window.default_width"), Config::getInt("window.default_height"),
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!win) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  Graphics::initialize();

  AudioData audioData;

  // Instantiate visualizer objects
  LissajousVisualizer lissajousVis;
  OscilloscopeVisualizer oscilloscopeVis;
  FFTVisualizer fftVis;

  // Create splitters
  Splitter fftScopeSplitter(&oscilloscopeVis, &fftVis, nullptr, true);
  Splitter lissajousScopeSplitter(&lissajousVis, &oscilloscopeVis, &fftScopeSplitter, false);

  // Set initial splitter positions
  int lissajousSplitterPos = audioData.windowHeight; // Lissajous is always square
  lissajousScopeSplitter.setPosition(lissajousSplitterPos);

  constexpr int MIN_SCOPE_WIDTH = 80;
  constexpr int MIN_FFT_WIDTH = 80;
  int lissajousWidth = audioData.windowHeight;
  int availableWidth = audioData.windowWidth - lissajousWidth;
  int minSplitterPos = lissajousWidth + MIN_SCOPE_WIDTH;
  int maxSplitterPos = audioData.windowWidth - MIN_FFT_WIDTH;
  int defaultOscilloscopeWidth =
      static_cast<int>(std::max(static_cast<float>(availableWidth) * 2 / 3, static_cast<float>(MIN_SCOPE_WIDTH)));
  int initialSplitterPos = lissajousWidth + defaultOscilloscopeWidth;
  if (initialSplitterPos < minSplitterPos)
    initialSplitterPos = minSplitterPos;
  if (initialSplitterPos > maxSplitterPos)
    initialSplitterPos = maxSplitterPos;
  fftScopeSplitter.setPosition(initialSplitterPos);

  // Initial layout
  lissajousScopeSplitter.update(audioData);

  std::thread audioThreadHandle(AudioProcessing::audioThread, &audioData);

  bool running = true;

  Uint32 frameCount = 0;
  Uint32 lastFpsUpdate = SDL_GetTicks();
  Uint32 lastThemeCheck = SDL_GetTicks();

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      fftScopeSplitter.handleEvent(event, audioData);
      lissajousScopeSplitter.handleEvent(event, audioData);
      switch (event.type) {
      case SDL_QUIT:
        running = false;
        break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          audioData.windowWidth = event.window.data1;
          audioData.windowHeight = event.window.data2;
          glViewport(0, 0, audioData.windowWidth, audioData.windowHeight);
          // Update splitter for Lissajous to keep it square
          lissajousScopeSplitter.setPosition(audioData.windowHeight);
          // Update layout on resize
          lissajousScopeSplitter.update(audioData);
        }
        break;
      }
    }

    // Check for theme changes every second
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastThemeCheck >= 1000) {
      Theme::ThemeManager::reloadIfChanged();
      lastThemeCheck = currentTime;
    }

    // Always update layout before drawing
    lissajousScopeSplitter.update(audioData);

    const auto& background = Theme::ThemeManager::getBackground();
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT);

    {
      std::lock_guard<std::mutex> lock(audioData.mutex);

      lissajousVis.draw(audioData, 0);
      if (oscilloscopeVis.getWidth() > 0) {
        oscilloscopeVis.draw(audioData, 0);
      }
      if (fftVis.getWidth() > 0) {
        fftVis.draw(audioData, 0);
      }
    }

    lissajousScopeSplitter.draw(audioData);
    fftScopeSplitter.draw(audioData);

    SDL_GL_SwapWindow(win);

    frameCount++;
    currentTime = SDL_GetTicks();
    if (currentTime - lastFpsUpdate >= 1000) {
      if (drawFPS) {
        std::cerr << "FPS: " << frameCount << std::endl;
      }
      frameCount = 0;
      lastFpsUpdate = currentTime;
    }
  }

  audioData.running = false;
  audioThreadHandle.join();

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();

  return 0;
}