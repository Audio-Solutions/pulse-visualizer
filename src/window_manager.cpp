#include "window_manager.hpp"

#include "graphics.hpp"
#include "theme.hpp"

#include <GL/glew.h>
#include <iostream>
#include <mutex>

bool WindowManager::initialize(SDL_Window* sdlMainWindow, SDL_GLContext mainContext) {
  // Create main window wrapper
  auto mainWin = std::make_unique<VisualizerWindow>();
  mainWin->window = sdlMainWindow;
  mainWin->glContext = mainContext;
  mainWin->isPopout = false;
  mainWin->hasFocus = true;

  int w, h;
  SDL_GetWindowSize(sdlMainWindow, &w, &h);
  mainWin->width = w;
  mainWin->height = h;

  mainWindow = mainWin.get();
  windows.push_back(std::move(mainWin));

  return true;
}

void WindowManager::updateWindows(const std::vector<VisualizerBase*>& visualizers, const std::vector<bool>& popoutFlags,
                                  AudioData& audioData) {
  // Remove existing popout windows that are no longer needed
  windows.erase(std::remove_if(windows.begin() + 1, windows.end(), // Skip main window
                               [&](const std::unique_ptr<VisualizerWindow>& win) {
                                 if (!win->isPopout)
                                   return false;

                                 // Check if this visualizer should still be popped out
                                 for (size_t i = 0; i < visualizers.size() && i < popoutFlags.size(); ++i) {
                                   if (visualizers[i] == win->visualizer && popoutFlags[i]) {
                                     return false; // Keep this window
                                   }
                                 }
                                 return true; // Remove this window
                               }),
                windows.end());

  // Create new popout windows for visualizers that need them
  for (size_t i = 0; i < visualizers.size() && i < popoutFlags.size(); ++i) {
    if (!popoutFlags[i])
      continue;

    // Check if window already exists for this visualizer
    bool windowExists = false;
    for (const auto& win : windows) {
      if (win->visualizer == visualizers[i]) {
        windowExists = true;
        break;
      }
    }

    if (!windowExists) {
      // Calculate window dimensions based on visualizer needs
      int width = 800;  // Default width
      int height = 600; // Default height

      // Special handling for lissajous aspect ratio - ensure it's square
      if (auto* lissajous = dynamic_cast<LissajousVisualizer*>(visualizers[i])) {
        if (lissajous->shouldEnforceAspectRatio()) {
          // For lissajous, make it square (1:1 aspect ratio)
          width = height; // Use the height as width to make it square
        }
      }

      auto popoutWindow = createPopoutWindow(visualizers[i], width, height);
      if (popoutWindow) {
        windows.push_back(std::move(popoutWindow));
      }
    }
  }
}

void WindowManager::handleEvents(SDL_Event& event, AudioData& audioData) {
  switch (event.type) {
  case SDL_QUIT:
    running = false;
    break;

  case SDL_WINDOWEVENT: {
    VisualizerWindow* targetWindow = findWindowById(event.window.windowID);
    if (!targetWindow)
      break;

    switch (event.window.event) {
    case SDL_WINDOWEVENT_CLOSE:
      if (targetWindow == mainWindow) {
        running = false;
      } else if (targetWindow->isPopout) {
        // Notify that this popout window should be popped back into main window
        if (popoutClosedCallback && targetWindow->visualizer) {
          popoutClosedCallback(targetWindow->visualizer);
        }

        // Don't remove the window immediately - let the config update handle it
        // The window will be removed during the next updateWindows() call
      }
      break;

    case SDL_WINDOWEVENT_RESIZED:
      targetWindow->width = event.window.data1;
      targetWindow->height = event.window.data2;

      // Update audioData dimensions if this is the main window
      if (targetWindow == mainWindow) {
        audioData.windowWidth = targetWindow->width;
        audioData.windowHeight = targetWindow->height;
      }

      // For popout windows with fixed aspect ratio, adjust width to maintain ratio
      if (targetWindow->isPopout && targetWindow->visualizer) {
        if (auto* lissajous = dynamic_cast<LissajousVisualizer*>(targetWindow->visualizer)) {
          if (lissajous->shouldEnforceAspectRatio()) {
            int requiredWidth = lissajous->getRequiredWidth(targetWindow->height);
            if (requiredWidth > 0 && requiredWidth != targetWindow->width) {
              // Resize the window to maintain aspect ratio
              SDL_SetWindowSize(targetWindow->window, requiredWidth, targetWindow->height);
              targetWindow->width = requiredWidth;
            }
          }
        }
      }

      // Make context current and update viewport
      SDL_GL_MakeCurrent(targetWindow->window, targetWindow->glContext);
      glViewport(0, 0, targetWindow->width, targetWindow->height);

      // For popout windows, update visualizer dimensions immediately
      if (targetWindow->isPopout && targetWindow->visualizer) {
        targetWindow->visualizer->setPosition(0);
        targetWindow->visualizer->setWidth(targetWindow->width);
      }
      break;

    case SDL_WINDOWEVENT_FOCUS_GAINED:
      targetWindow->hasFocus = true;
      break;

    case SDL_WINDOWEVENT_FOCUS_LOST:
      targetWindow->hasFocus = false;
      break;
    }
    break;
  }

  case SDL_MOUSEMOTION: {
    VisualizerWindow* targetWindow = findWindowById(event.motion.windowID);
    if (!targetWindow || !targetWindow->hasFocus)
      break;

    // Handle FFT visualizer mouse tracking
    if (auto* fftVis = dynamic_cast<FFTVisualizer*>(targetWindow->visualizer)) {
      int mouseX = event.motion.x;
      int mouseY = event.motion.y;

      // Check if mouse is over the visualizer area
      bool isOver = (mouseX >= 0 && mouseX < targetWindow->width && mouseY >= 0 && mouseY < targetWindow->height);

      if (isOver) {
        float localX = static_cast<float>(mouseX);
        float localY = static_cast<float>(targetWindow->height - mouseY); // Flip Y coordinate
        fftVis->updateMousePosition(localX, localY);
        fftVis->setHovering(true);
      } else {
        fftVis->setHovering(false);
      }
    }
    break;
  }
  }
}

void WindowManager::drawAll(const AudioData& audioData) {
  // Draw popout windows only
  for (const auto& window : windows) {
    if (!window->visualizer || !window->isPopout)
      continue;

    {
      // Lock the shared audio buffer while we read from it to avoid data races
      std::lock_guard<std::mutex> lock(const_cast<AudioData&>(audioData).mutex);

      // Make this window's context current
      SDL_GL_MakeCurrent(window->window, window->glContext);

      // Set up viewport
      glViewport(0, 0, window->width, window->height);

      // Clear background
      const auto& background = Theme::ThemeManager::getBackground();
      glClearColor(background.r, background.g, background.b, background.a);
      glClear(GL_COLOR_BUFFER_BIT);

      // Set up graphics for this window
      Graphics::setupViewport(0, 0, window->width, window->height, window->height);

      // Update visualizer dimensions for popout windows
      window->visualizer->setPosition(0);
      window->visualizer->setWidth(window->width);

      // Temporarily modify audioData dimensions for this popout window
      AudioData& mutableAudioData = const_cast<AudioData&>(audioData);
      int originalWidth = mutableAudioData.windowWidth;
      int originalHeight = mutableAudioData.windowHeight;

      mutableAudioData.windowWidth = window->width;
      mutableAudioData.windowHeight = window->height;

      // Draw the visualizer with popout window dimensions
      window->visualizer->draw(audioData, 0);

      // Restore original dimensions
      mutableAudioData.windowWidth = originalWidth;
      mutableAudioData.windowHeight = originalHeight;
    }

    // Swap buffers after releasing the lock so other threads can proceed
    SDL_GL_SwapWindow(window->window);
  }
}

WindowManager::VisualizerWindow* WindowManager::getWindowForVisualizer(VisualizerBase* visualizer) {
  for (const auto& window : windows) {
    if (window->visualizer == visualizer) {
      return window.get();
    }
  }
  return nullptr;
}

std::unique_ptr<WindowManager::VisualizerWindow> WindowManager::createPopoutWindow(VisualizerBase* visualizer,
                                                                                   int width, int height) {
  std::string windowTitle = "Visualizer";

  // Set appropriate title based on visualizer type
  if (dynamic_cast<SpectrogramVisualizer*>(visualizer)) {
    windowTitle = "Spectrogram";
  } else if (dynamic_cast<LissajousVisualizer*>(visualizer)) {
    windowTitle = "Lissajous";
  } else if (dynamic_cast<OscilloscopeVisualizer*>(visualizer)) {
    windowTitle = "Oscilloscope";
  } else if (dynamic_cast<FFTVisualizer*>(visualizer)) {
    windowTitle = "FFT Analyzer";
  }

  SDL_Window* sdlWindow = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width,
                                           height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!sdlWindow) {
    std::cerr << "Failed to create popout window: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(sdlWindow);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context for popout window: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(sdlWindow);
    return nullptr;
  }

  auto window = std::make_unique<VisualizerWindow>();
  window->window = sdlWindow;
  window->glContext = glContext;
  window->visualizer = visualizer;
  window->isPopout = true;
  window->hasFocus = true;
  window->width = width;
  window->height = height;

  return window;
}

WindowManager::VisualizerWindow* WindowManager::findWindowById(Uint32 windowId) {
  for (const auto& window : windows) {
    if (SDL_GetWindowID(window->window) == windowId) {
      return window.get();
    }
  }
  return nullptr;
}