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

#include "include/sdl_window.hpp"

#include "include/config.hpp"
#include "include/config_window.hpp"
#include "include/graphics.hpp"
#include "include/theme.hpp"
#include "include/window_manager.hpp"

#include <SDL3_image/SDL_image.h>

namespace SDLWindow {
// Map of window states by group
std::unordered_map<std::string, State> states;

// Global buffer handles
GLuint vertexBuffer = 0;
GLuint vertexColorBuffer = 0;

std::atomic<bool> running {false};

SDL_Surface* icon = nullptr;

void deinit() {
  for (auto& [group, state] : states) {
    SDL_DestroyWindow(state.win);
    SDL_GL_DestroyContext(state.glContext);
  }

  SDL_DestroySurface(icon);

  glDeleteBuffers(1, &vertexBuffer);
  glDeleteBuffers(1, &vertexColorBuffer);

  SDL_Quit();
}

void init() {
#ifdef __linux
  // Prefer wayland natively if enabled in the config
  if (Config::options.window.wayland)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
  else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif

  // Initialize SDL video subsystem
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    LOG_ERROR(std::string("SDL_Init failed: ") + SDL_GetError());
    exit(1);
  }

  // Configure OpenGL context attributes
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

  // Disable compositor bypass to enable transparency in DE's like KDE
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

  // Create Base SDL window
  createWindow("main", "Pulse " VERSION_FULL, Config::options.window.default_width,
               Config::options.window.default_height);

  // Check if window creation was successful
  if (states.find("main") == states.end()) {
    LOG_ERROR("Failed to create main window");
    running.store(false);
    return;
  }

  // Make the OpenGL context current before initializing GLEW
  if (!selectWindow("main")) {
    LOG_ERROR("Failed to select main window");
    running.store(false);
    return;
  }

  // Initialize GLEW
  GLenum err = glewInit();
  if (err != GLEW_OK) {
#ifdef __linux
    // If we're on Linux and GLEW failed, try falling back to X11
    if (Config::options.window.wayland && err == GLEW_ERROR_NO_GLX_DISPLAY) {
      LOG_DEBUG("GLEW failed under Wayland, attempting fallback to X11");

      // Clean up current state
      deinit();

      // Force X11 and retry
      SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");

      // Re-initialize SDL
      if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(std::string("SDL_Init failed on X11 fallback: ") + SDL_GetError());
        running.store(false);
        return;
      }

      // Re-configure OpenGL context attributes
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
      SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
      SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

      // Re-create window
      createWindow("main", "Pulse " VERSION_FULL, Config::options.window.default_width,
                   Config::options.window.default_height);

      if (states.find("main") == states.end()) {
        LOG_ERROR("Failed to create main window on X11 fallback");
        running.store(false);
        return;
      }

      if (!selectWindow("main")) {
        LOG_ERROR("Failed to select main window on X11 fallback");
        running.store(false);
        return;
      }

      // Retry GLEW initialization
      err = glewInit();
    }
#endif

    if (err != GLEW_OK) {
      std::string errorMsg = std::string("WindowManager::init(): GLEW initialization failed\n") +
                             "Error code: " + std::to_string(err) + "\n" +
                             "Error string: " + reinterpret_cast<const char*>(glewGetErrorString(err));
      LOG_ERROR(errorMsg);

      // Check if we have a valid OpenGL context
      const GLubyte* version = glGetString(GL_VERSION);
      if (version) {
        LOG_ERROR(std::string("OpenGL version: ") + reinterpret_cast<const char*>(version));
      } else {
        LOG_ERROR("No valid OpenGL context available");
      }

      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pulse Visualizer Error", errorMsg.c_str(), nullptr);
      running.store(false);
      return;
    }
  }

  Graphics::Font::load();

  glGenBuffers(1, &vertexBuffer);
  glGenBuffers(1, &vertexColorBuffer);

  // Configure OpenGL rendering state
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0f);

  // Initialise window size
  SDL_GetWindowSize(states["main"].win, &states["main"].windowSizes.first, &states["main"].windowSizes.second);

  running.store(true);
}

void handleEvent(SDL_Event& event) {
  std::string group = "";
  for (auto& [_group, state] : states) {
    if (event.window.windowID == state.winID) {
      group = _group;
      break;
    }
  }

  switch (event.type) {

  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    if (group != "main")
      return;
  case SDL_EVENT_QUIT:
    running.store(false);
    return;

  case SDL_EVENT_KEY_DOWN:
    switch (event.key.key) {
    case SDLK_Q:
    case SDLK_ESCAPE:
      if (group == "main")
        running.store(false);
      return;
    case SDLK_M:
      ConfigWindow::toggle();
      break;
    default:
      break;
    }
    break;

  case SDL_EVENT_MOUSE_MOTION:
    states[group].mousePos.first = event.motion.x;
    states[group].mousePos.second = states[group].windowSizes.second - event.motion.y;
    break;

  case SDL_EVENT_WINDOW_MOUSE_ENTER:
    states[group].focused = true;
    break;

  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    states[group].focused = false;
    break;

  case SDL_EVENT_WINDOW_RESIZED:
    states[group].windowSizes.first = event.window.data1;
    states[group].windowSizes.second = event.window.data2;
    break;

  default:
    break;
  }
}

void display() {
  for (auto& [_, state] : states) {
    SDL_GL_MakeCurrent(state.win, state.glContext);
    SDL_GL_SwapWindow(state.win);
  }
}

void clear() {
  // Clear with current theme background color
  float* c = Theme::colors.background;
  for (auto& [_, state] : states) {
    SDL_GL_MakeCurrent(state.win, state.glContext);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
}

void createWindow(const std::string& group, const std::string& title, int width, int height, uint32_t flags) {
  // Create SDL window with OpenGL support
  LOG_DEBUG(std::string("Creating window: ") + title);
  SDL_Window* win = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL | flags);
  if (!win) {
    LOG_ERROR(std::string("Failed to create window: ") + SDL_GetError());
    return;
  }

  // Create OpenGL context
  LOG_DEBUG(std::string("Creating OpenGL context"));
  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    LOG_ERROR(std::string("Failed to create OpenGL context: ") + SDL_GetError());
    SDL_DestroyWindow(win);
    return;
  }

  // Fix nvidia bug
  SDL_GL_SetSwapInterval(0);

  if (!icon)
    icon = IMG_Load((Config::getInstallDir() + "/icons/icon.ico").c_str());

  if (!icon)
    LOG_ERROR("Failed to load window icon (icons/icon.ico)");
  else
    SDL_SetWindowIcon(win, icon);

  states[group] = {win, SDL_GetWindowID(win), glContext, std::make_pair(width, height), std::make_pair(0, 0), false};
}

bool destroyWindow(const std::string& group) {
  if (states.find(group) == states.end())
    return false;

  LOG_DEBUG(std::string("Destroying window: ") + group);
  SDL_DestroyWindow(states[group].win);
  SDL_GL_DestroyContext(states[group].glContext);
  states.erase(group);
  selectWindow("main");
  return true;
}

bool selectWindow(const std::string& group) {
  if (states.find(group) == states.end())
    return false;
  SDL_GL_MakeCurrent(states[group].win, states[group].glContext);
  return true;
}
} // namespace SDLWindow