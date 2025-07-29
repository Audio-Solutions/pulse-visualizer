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

// Thread synchronization variables for DSP data processing
std::mutex mainThread;
std::condition_variable mainCv;
std::atomic<bool> dataReady {false};

int main() {
  // Initialize DSP buffers
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);

  // Initialize SDL and OpenGL
  SDLWindow::init();
  SDLWindow::clear();
  SDLWindow::display();

  // Setup configuration and theme
  Config::copyFiles();
  Config::load();
  Theme::load(Config::options.window.theme);
  Graphics::Font::load();

  // Initialize audio and DSP components
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();

  // Setup window management
  WindowManager::reorder();

  // Start DSP processing thread
  std::thread DSPThread(DSP::Threads::main);

  // Frame timing variables
  auto lastTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  // Main application loop
  while (1) {
    // Handle configuration reloading
    if (Config::reload()) {
      Theme::load(Config::options.window.theme);
      Graphics::Font::cleanup();
      Graphics::Font::load();
      WindowManager::reorder();
      AudioEngine::reconfigure();
      DSP::FFT::recreatePlans();
      DSP::ConstantQ::regenerate();
    }

    // Handle theme reloading
    if (Theme::reload()) {
      Graphics::Font::cleanup();
      Graphics::Font::load();
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
  Graphics::Font::cleanup();
  SDLWindow::deinit();
  return 0;
}