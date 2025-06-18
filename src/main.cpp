#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/gl.h>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <iostream>

#include "config.hpp"
#include "audio_data.hpp"
#include "graphics.hpp"
#include "audio_processing.hpp"
#include "visualizers.hpp"
#include "theme.hpp"

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return 1;
  }

  // Initialize the theme system
  Theme::ThemeManager::initialize();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window* win = SDL_CreateWindow("Audio Visualizer",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    Config::DEFAULT_WINDOW_WIDTH,
                                    Config::DEFAULT_WINDOW_HEIGHT,
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

  std::thread audioThreadHandle(AudioProcessing::audioThread, &audioData);

  bool running = true;
  bool draggingSplitter = false;
  int mouseX = 0;
  
  Uint32 frameCount = 0;
  Uint32 lastFpsUpdate = SDL_GetTicks();

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          running = false;
          break;
          
        case SDL_WINDOWEVENT:
          if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
            audioData.windowWidth = event.window.data1;
            audioData.windowHeight = event.window.data2;
            glViewport(0, 0, audioData.windowWidth, audioData.windowHeight);
          }
          break;
          
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.button == SDL_BUTTON_LEFT) {
            mouseX = event.button.x;
            int splitterX = static_cast<int>(audioData.windowWidth * audioData.splitterPos);
            if (abs(mouseX - splitterX) < 10) {
              draggingSplitter = true;
            }
          }
          break;
          
        case SDL_MOUSEBUTTONUP:
          if (event.button.button == SDL_BUTTON_LEFT) {
            draggingSplitter = false;
          }
          break;
          
        case SDL_MOUSEMOTION:
          if (draggingSplitter) {
            float newPos = static_cast<float>(event.motion.x) / audioData.windowWidth;
            audioData.splitterPos = std::max(0.2f, std::min(0.8f, newPos));
          }
          break;
      }
    }

    const auto& background = Theme::ThemeManager::getBackground();
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT);

    int lissajousSize = audioData.windowHeight;

    int splitterX = static_cast<int>(audioData.windowWidth * audioData.splitterPos);
    int scopeWidth = splitterX - lissajousSize;
    int fftWidth = audioData.windowWidth - splitterX;

    {
      std::lock_guard<std::mutex> lock(audioData.mutex);
      
      Visualizers::drawLissajous(audioData, lissajousSize);
      
      if (scopeWidth > 0) {
        Visualizers::drawOscilloscope(audioData, scopeWidth);
      }
      
      if (fftWidth > 0) {
        Visualizers::drawFFT(audioData, fftWidth);
      }
      
      Visualizers::drawSplitter(audioData, splitterX);
    }
    
    SDL_GL_SwapWindow(win);

    frameCount++;
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastFpsUpdate >= 1000) {
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