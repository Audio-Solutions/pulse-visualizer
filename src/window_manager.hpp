#pragma once

#include "audio_data.hpp"
#include "visualizers.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <functional>
#include <memory>
#include <vector>

class WindowManager {
public:
  struct VisualizerWindow {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    VisualizerBase* visualizer = nullptr;
    bool isPopout = false;
    bool hasFocus = true;
    int width = 0;
    int height = 0;

    ~VisualizerWindow() {
      if (glContext) {
        SDL_GL_DeleteContext(glContext);
      }
      if (window) {
        SDL_DestroyWindow(window);
      }
    }
  };

  // Callback type for when a popout window is closed
  using PopoutClosedCallback = std::function<void(VisualizerBase*)>;

  WindowManager() = default;
  ~WindowManager() = default;

  // Initialize with main window
  bool initialize(SDL_Window* mainWindow, SDL_GLContext mainContext);

  // Create or destroy pop-out windows based on config
  void updateWindows(const std::vector<VisualizerBase*>& visualizers, const std::vector<bool>& popoutFlags,
                     AudioData& audioData);

  // Handle events for all windows
  void handleEvents(SDL_Event& event, AudioData& audioData);

  // Draw all visualizers in their respective windows
  void drawAll(const AudioData& audioData);

  // Get window for a specific visualizer
  VisualizerWindow* getWindowForVisualizer(VisualizerBase* visualizer);

  // Check if any window is running
  bool isRunning() const { return running; }

  // Set running state
  void setRunning(bool state) { running = state; }

  // Set callback for when popout windows are closed
  void setPopoutClosedCallback(PopoutClosedCallback callback) { popoutClosedCallback = callback; }

private:
  std::vector<std::unique_ptr<VisualizerWindow>> windows;
  VisualizerWindow* mainWindow = nullptr;
  bool running = true;
  PopoutClosedCallback popoutClosedCallback;

  // Create a new pop-out window
  std::unique_ptr<VisualizerWindow> createPopoutWindow(VisualizerBase* visualizer, int width, int height);

  // Find window by SDL window ID
  VisualizerWindow* findWindowById(Uint32 windowId);
};