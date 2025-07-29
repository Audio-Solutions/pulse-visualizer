#include "include/audio_engine.hpp"
#include "include/common.hpp"
#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/spline.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

/**
 * TODO: Implement a configuration menu. sometime. idk when
 */

// Thread synchronization variables for DSP data processing
std::mutex mainThread;
std::condition_variable mainCv;
std::atomic<bool> dataReady {false};

int main() {
  // Set up signal handling for Ctrl+C
  signal(SIGINT, [](int) { SDLWindow::running = false; });

  // Initialize DSP buffers
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);

  // Initialize SDL and OpenGL
  SDLWindow::init();

  // Clear and display the base window
  SDLWindow::clear();
  SDLWindow::display();

  // Setup configuration and theme
  Config::copyFiles();
  Config::load();
  Theme::load(Config::options.window.theme);

  // Initialize audio and DSP components
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();

  // Setup window management
  WindowManager::reorder();

  // Load fonts for each visualizer window
  for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
    Graphics::Font::load(i);
  }

  // Start DSP processing thread
  std::thread DSPThread(DSP::Threads::main);

  // Frame timing variables
  auto lastTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  // Main application loop
  while (1) {
    // Handle configuration reloading
    if (Config::reload()) {
      std::cout << "Config reloaded" << std::endl;
      Theme::load(Config::options.window.theme);

      // Cleanup fonts for all windows
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::cleanup(i);
      }

      WindowManager::reorder();

      // Load fonts for each visualizer window
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::load(i);
      }

      AudioEngine::reconfigure();
      DSP::FFT::recreatePlans();
      DSP::ConstantQ::regenerate();
    }

    // Handle theme reloading
    if (Theme::reload()) {
      // Cleanup fonts for all windows
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::cleanup(i);
      }

      // Load fonts for each visualizer window
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::load(i);
      }
    }

    // Process SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      SDLWindow::handleEvent(event);
      for (auto& splitter : WindowManager::splitters)
        splitter.handleEvent(event);
      for (auto& window : WindowManager::windows)
        window.handleEvent(event);
    }
    if (!SDLWindow::running)
      break;
    glUseProgram(0);
    if (SDLWindow::width == 0 || SDLWindow::height == 0)
      continue;

    // Update window management
    WindowManager::updateSplitters();
    WindowManager::resizeWindows();
    WindowManager::resizeTextures();

    // Wait for DSP data to be ready
    std::unique_lock<std::mutex> lock(mainThread);
    mainCv.wait(lock, [] { return dataReady.load(); });
    dataReady = false;

    // Render frame
    SDLWindow::clear();
    WindowManager::renderAll();
    WindowManager::drawSplitters();
    SDLWindow::display();

    // Update timing
    auto now = std::chrono::steady_clock::now();
    WindowManager::dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    // FPS logging if enabled
    if (Config::options.debug.log_fps) {
      frameCount++;
      static float fpsTimeAccum = 0.0f;
      fpsTimeAccum += WindowManager::dt;
      if (fpsTimeAccum >= 1.0f) {
        std::cout << "FPS: " << static_cast<int>(frameCount / fpsTimeAccum) << std::endl;
        frameCount = 0;
        fpsTimeAccum = 0.0f;
      }
    }
  }

  // Cleanup
  DSPThread.join();
  AudioEngine::cleanup();
  DSP::FFT::cleanup();

  // Cleanup fonts for all windows
  for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
    Graphics::Font::cleanup(i);
  }

  Config::cleanup();
  Theme::cleanup();
  SDLWindow::deinit();
  return 0;
}