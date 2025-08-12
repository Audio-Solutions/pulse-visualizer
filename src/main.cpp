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

#include <SDL3/SDL_main.h>

std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
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
std::atomic<bool> quitSignal {false};

int main(int argc, char** argv) {
  // Set up signal handling for Ctrl+C etc
  signal(SIGINT, [](int) { quitSignal.store(true); });
  signal(SIGTERM, [](int) { quitSignal.store(true); });
#ifndef _WIN32
  signal(SIGQUIT, [](int) { quitSignal.store(true); });
#endif

#ifdef _WIN32
  // Close console window if needed
  if (argc > 1 && std::string(argv[1]) == "--console") {
    FreeConsole();
  }
#endif

  // Initialize DSP buffers
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);
  DSP::lowpassed.resize(DSP::bufferSize);

  // Setup configuration
  Config::copyFiles();
  Config::load();

  // Setup theme
  Theme::load();

  // Initialize SDL and OpenGL
  SDLWindow::init();

  // Clear and display the base window
  SDLWindow::clear();
  SDLWindow::display();

  // Set window decorations
  SDL_SetWindowBordered(SDLWindow::wins[0], Config::options.window.decorations);
  SDL_SetWindowAlwaysOnTop(SDLWindow::wins[0], Config::options.window.always_on_top);

  // Initialize audio and DSP components
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();
  DSP::Lowpass::init();
  DSP::LUFS::init();

  // Setup window management
  WindowManager::reorder();

  // Load fonts for each visualizer window
  for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
    Graphics::Font::load(i);
  }

  // Start DSP processing thread
  std::thread DSPThread(DSP::Threads::mainThread);

  // Frame timing variables
  auto lastTime = std::chrono::steady_clock::now();
  int frameCount = 0;

  // Main application loop
  while (1) {
    // Handle configuration reloading
    if (Config::reload()) {
      std::cout << "Config reloaded" << std::endl;
      Theme::load();

      // Cleanup fonts for all windows
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::cleanup(i);
      }

      WindowManager::reorder();

      // Load fonts for each visualizer window
      for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
        Graphics::Font::load(i);
      }

      // Set window decorations
      SDL_SetWindowBordered(SDLWindow::wins[0], Config::options.window.decorations);
      SDL_SetWindowAlwaysOnTop(SDLWindow::wins[0], Config::options.window.always_on_top);

      AudioEngine::reconfigure();
      DSP::FFT::recreatePlans();
      DSP::ConstantQ::regenerate();
      DSP::Lowpass::reconfigure();
      DSP::LUFS::init();
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
    if (quitSignal.load() || !SDLWindow::running)
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
  SDLWindow::running.store(false);
  for (size_t i = 0; i < WindowManager::windows.size(); ++i) {
    Graphics::Font::cleanup(i);
  }
  DSPThread.join();
  AudioEngine::cleanup();
  DSP::FFT::cleanup();
  Config::cleanup();
  Theme::cleanup();
  SDLWindow::deinit();
  return 0;
}