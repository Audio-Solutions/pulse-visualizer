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

#include "include/sdl_window.hpp"

#include "include/config.hpp"
#include "include/theme.hpp"

namespace SDLWindow {

// Global window state variables
std::vector<SDL_Window*> wins;
std::vector<SDL_GLContext> glContexts;
size_t currentWindow = 0;
bool focused = false;
std::atomic<bool> running {false};
int width, height;

// Mouse coordinates
int mouseX = 0, mouseY = 0;

void deinit() {
  for (size_t i = 0; i < wins.size(); i++) {
    SDL_DestroyWindow(wins[i]);
    SDL_GL_DestroyContext(glContexts[i]);
  }
  SDL_Quit();
}

void init() {
  // Prefer wayland natively
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");

  // Initialize SDL video subsystem
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
    exit(1);
  }

  // Configure OpenGL context attributes
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Disable compositor bypass to enable transparency in DE's like KDE
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

  // Create Base SDL window
  createWindow("Pulse", Config::options.window.default_width, Config::options.window.default_height);

  // Initialize GLEW for OpenGL extensions
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    throw std::runtime_error(std::string("WindowManager::init(): GLEW initialization failed") +
                             reinterpret_cast<const char*>(glewGetErrorString(err)));
  }

  // Configure OpenGL rendering state
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0f);

  // Initialise window size
  SDL_GetWindowSize(wins[currentWindow], &width, &height);

  running.store(true);
}

void handleEvent(SDL_Event& event) {
  // handle events for the base window only except for quit
  if (event.type == SDL_EVENT_QUIT) {
    running.store(false);
    return;
  }

  if (event.window.windowID == SDL_GetWindowID(wins[0])) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
      switch (event.key.key) {
      case SDLK_Q:
      case SDLK_ESCAPE:
        running.store(false);
        return;
      default:
        break;
      }
      break;

    case SDL_EVENT_MOUSE_MOTION:
      mouseX = event.motion.x;
      mouseY = height - event.motion.y;
      break;

    case SDL_EVENT_WINDOW_MOUSE_ENTER:
      focused = true;
      break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      focused = false;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      width = event.window.data1;
      height = event.window.data2;
      break;

    default:
      break;
    }
  }
}

void display() { SDL_GL_SwapWindow(wins[currentWindow]); }

void clear() {
  // Clear with current theme background color
  float* c = Theme::colors.background;
  glClearColor(c[0], c[1], c[2], c[3]);
  glClear(GL_COLOR_BUFFER_BIT);
}

size_t createWindow(const std::string& title, int width, int height, uint32_t flags) {
  // Create SDL window with OpenGL support
  SDL_Window* win = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL | flags);
  if (!win) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    return -1;
  }

  // Create OpenGL context
  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(win);
    return -1;
  }
  wins.push_back(win);
  glContexts.push_back(glContext);
  return wins.size() - 1;
}

bool destroyWindow(size_t index) {
  if (index >= wins.size())
    return false;
  SDL_DestroyWindow(wins[index]);
  SDL_GL_DestroyContext(glContexts[index]);
  wins.erase(wins.begin() + index);
  glContexts.erase(glContexts.begin() + index);
  if (currentWindow == index)
    currentWindow = 0;
  selectWindow(currentWindow);
  return true;
}

bool selectWindow(size_t index) {
  if (index >= wins.size())
    return false;
  currentWindow = index;
  SDL_GL_MakeCurrent(wins[index], glContexts[index]);
  return true;
}
} // namespace SDLWindow