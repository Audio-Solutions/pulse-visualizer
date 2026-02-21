/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
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
#include "include/config_window.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/plugin.hpp"
#include "include/sdl_window.hpp"
#include "include/spline.hpp"
#include "include/theme.hpp"
#include "include/update_window.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

#include <SDL3/SDL_main.h>
#include <chrono>
#include <thread>

#if not(_WIN32)
#include <errno.h>
#include <sys/ptrace.h>
#endif

#ifdef USE_UPDATER
#include <curl/curl.h>
#endif

namespace CmdlineArgs {
bool debug = false;
bool help = false;
bool console = false;
} // namespace CmdlineArgs

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

// checks if a debugger is present
bool debuggerPresent() {
#if _WIN32
  return IsDebuggerPresent();
#else
  errno = 0;

  // try to trace, will fail if a debugger is present
  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
    return errno == EPERM;
  }

  // detach if we succeeded
  ptrace(PTRACE_DETACH, 0, nullptr, nullptr);
  return false;
#endif
}

void reconfigure() {
  Theme::load();

  // Cleanup fonts for all SDL windows
  LOG_DEBUG("Cleaning up fonts for all SDL windows");
  Graphics::Font::cleanup();

  LOG_DEBUG("Reordering windows");
  WindowManager::reorder();

  // Load fonts for each SDL window
  LOG_DEBUG("Loading fonts for each SDL window");
  Graphics::Font::load();

  // Set window decorations
  LOG_DEBUG("Setting window decorations");
  SDL_SetWindowBordered(SDLWindow::states["main"].win, Config::options.window.decorations);
  SDL_SetWindowAlwaysOnTop(SDLWindow::states["main"].win, Config::options.window.always_on_top);

  LOG_DEBUG("Reconfiguring...");
  AudioEngine::reconfigure();
  DSP::FFT::recreatePlans();
  DSP::ConstantQ::regenerate();
  DSP::Lowpass::reconfigure();
  DSP::LUFS::init();
}

// Thread synchronization variables for DSP data processing
std::mutex mainThread;
std::condition_variable mainCv;
std::atomic<bool> dataReady {false};

int main(int argc, char** argv) {
#ifndef _WIN32
  // Block Signals so they can be polled
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGWINCH);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGQUIT);
  sigprocmask(SIG_BLOCK, &sigset, nullptr);
#endif

  for (int i = 0; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case '-':
        if (std::string(argv[i]) == "--debug") {
          CmdlineArgs::debug = true;
        } else if (std::string(argv[i]) == "--help") {
          CmdlineArgs::help = true;
        } else if (std::string(argv[i]) == "--console") {
          CmdlineArgs::console = true;
        }
        break;
      case 'd':
        CmdlineArgs::debug = true;
        break;
      case 'h':
        CmdlineArgs::help = true;
        break;
      case 'c':
        CmdlineArgs::console = true;
        break;
      }
    }
  }

  std::cout << "pulse-visualizer v" << VERSION_STRING << " commit " << VERSION_COMMIT << std::endl;
  if (CmdlineArgs::help) {
    std::cout << "Usage: pulse-visualizer [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -d, --debug       Enable debug mode" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -c, --console     Open console window (Windows only)" << std::endl;
    return 0;
  }

  // force debug mode on if a debugger is present
  if (debuggerPresent()) {
    std::cout << "Debugger detected, activating debug mode" << std::endl;
    CmdlineArgs::debug = true;
  }

#ifdef _WIN32
  if (!CmdlineArgs::console && !CmdlineArgs::debug)
    FreeConsole();
#endif

  // Initialize DSP buffers
  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bandpassed.resize(DSP::bufferSize);
  DSP::lowpassed.resize(DSP::bufferSize);

  // Load plugins
  LOG_DEBUG("Loading Plugins");
  Plugin::loadAll();

  // Setup configuration
  LOG_DEBUG("Copying files");
  Config::copyFiles();
  Config::load();

  // Setup theme
  LOG_DEBUG("Loading theme");
  Theme::load();

  // Initialize SDL and OpenGL
  LOG_DEBUG("Initializing SDL and OpenGL");
  SDLWindow::init();

  // Check if initialization failed
  if (!SDLWindow::running.load()) {
    LOG_ERROR("SDL/OpenGL initialization failed, exiting");
    return 1;
  }

  // Clear and display the base window
  LOG_DEBUG("Clearing and displaying base window");
  SDLWindow::clear();
  SDLWindow::display();

  // Set window decorations
  LOG_DEBUG("Setting initial window decorations");
  SDL_SetWindowBordered(SDLWindow::states["main"].win, Config::options.window.decorations);
  SDL_SetWindowAlwaysOnTop(SDLWindow::states["main"].win, Config::options.window.always_on_top);

  // Initialize audio and DSP components
  LOG_DEBUG("Initializing audio and DSP components");
  AudioEngine::init();
  DSP::ConstantQ::init();
  DSP::ConstantQ::generate();
  DSP::FFT::init();
  DSP::Lowpass::init();
  DSP::LUFS::init();

  // Setup window management
  LOG_DEBUG("Setting up window management");
  WindowManager::reorder();

  // Start DSP processing thread
  LOG_DEBUG("Starting DSP processing thread");
  std::thread DSPThread(DSP::Threads::mainThread);

  // Initialise libcurl
#ifdef USE_UPDATER
  LOG_DEBUG("Initialising libcurl");
  if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
    LOG_DEBUG("Checking for updates");
    UpdaterWindow::check();
  }
#endif

  // Frame timing variables
  LOG_DEBUG("Setting up frame timing variables");
  int frameCount = 0;

  // Start plugins
  LOG_DEBUG("Starting plugins");
  Plugin::startAll();

  // Main application loop
  LOG_DEBUG("Starting main application loop");
  while (1) {
    // Handle configuration reloading
    if (Config::reload()) {
      LOG_DEBUG("Config reloaded");
      reconfigure();
    }

    // Handle theme reloading
    if (Theme::reload()) {
      // Cleanup fonts for all SDL windows
      LOG_DEBUG("Cleaning up fonts for all SDL windows");
      Graphics::Font::cleanup();

      // Load fonts for each SDL window
      LOG_DEBUG("Loading fonts for each SDL window");
      Graphics::Font::load();
    }

    // Process SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      SDLWindow::handleEvent(event);
      ConfigWindow::handleEvent(event);
      UpdaterWindow::handleEvent(event);
      WindowManager::handleEvent(event);
      for (auto& [key, vec] : WindowManager::splitters)
        for (auto& splitter : vec)
          splitter.handleEvent(event);
      for (auto& [key, vec] : WindowManager::windows)
        for (auto& window : vec)
          window.handleEvent(event);
      Plugin::handleEvent(event);
    }

#ifndef _WIN32
    // Poll Signals
    struct timespec ts = {0, 0};
    int sig = sigtimedwait(&sigset, nullptr, &ts);
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
      SDLWindow::running = false;
#endif

    if (!SDLWindow::running) {
      LOG_DEBUG("Quit signal received, exiting");
      break;
    }
    glUseProgram(0);
    if (std::any_of(SDLWindow::states.begin(), SDLWindow::states.end(),
                    [](auto& state) {
                      int width, height;
                      SDL_GetWindowSize(state.second.win, &width, &height);
                      return width == 0 || height == 0;
                    }) ||
        Config::broken)
      continue;

    // Update window management
    WindowManager::deleteMarkedWindows();
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
    ConfigWindow::draw();
    UpdaterWindow::draw();
    Plugin::drawAll();
    SDLWindow::display();

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
  LOG_DEBUG("Cleaning up...");
  SDLWindow::running.store(false);
  Plugin::unloadAll();
  Graphics::Font::cleanup();
  DSPThread.join();
  AudioEngine::cleanup();
  DSP::FFT::cleanup();
  Config::cleanup();
  Theme::cleanup();
  SDLWindow::deinit();

#ifdef USE_UPDATER
  UpdaterWindow::cleanup();
  curl_global_cleanup();
#endif

  return 0;
}