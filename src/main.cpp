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

std::mutex mainThread;
std::condition_variable mainCv;
std::atomic<bool> dataReady {false};

int main() {
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);

  SDLWindow::init();
  SDLWindow::clear();
  SDLWindow::display();

  Config::copyFiles();

  Config::load();
  Theme::load(Config::options.window.theme);
  Graphics::Font::load();
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();

  WindowManager::reorder();

  std::thread DSPThread(DSP::Threads::main);

  auto lastTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  while (1) {
    if (Config::reload()) {
      Theme::load(Config::options.window.theme);
      Graphics::Font::cleanup();
      Graphics::Font::load();
      WindowManager::reorder();
      AudioEngine::reconfigure();
      DSP::FFT::recreatePlans();
      DSP::ConstantQ::regenerate();
    }

    if (Theme::reload()) {
      Graphics::Font::cleanup();
      Graphics::Font::load();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      SDLWindow::handleEvent(event);
      for (auto& splitter : WindowManager::splitters)
        splitter.handleEvent(event);
    }
    if (!SDLWindow::running)
      break;
    glUseProgram(0);
    if (SDLWindow::width == 0 || SDLWindow::height == 0)
      continue;

    WindowManager::updateSplitters();
    WindowManager::resizeWindows();
    WindowManager::resizeTextures();

    std::unique_lock<std::mutex> lock(mainThread);
    mainCv.wait(lock, [] { return dataReady.load(); });
    dataReady = false;

    SDLWindow::clear();
    WindowManager::renderAll();
    WindowManager::drawSplitters();
    SDLWindow::display();

    auto now = std::chrono::steady_clock::now();
    WindowManager::dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

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

  DSPThread.join();

  AudioEngine::cleanup();
  DSP::FFT::cleanup();
  Graphics::Font::cleanup();
  SDLWindow::deinit();
  return 0;
}