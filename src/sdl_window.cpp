#include "include/sdl_window.hpp"

#include "include/theme.hpp"

namespace SDLWindow {

// Global window state variables
SDL_Window* win = nullptr;
SDL_GLContext glContext = 0;
bool focused = false;
bool running = false;
int width, height;

// Mouse coordinates
int mouseX = 0, mouseY = 0;

void deinit() {
  if (glContext)
    SDL_GL_DeleteContext(glContext);
  if (win)
    SDL_DestroyWindow(win);
  SDL_Quit();
}

void init() {
  // Initialize SDL video subsystem
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
    exit(1);
  }

  // Configure OpenGL context attributes
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Create SDL window with OpenGL support
  win = SDL_CreateWindow("Pulse", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1080, 200,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    deinit();
    exit(1);
  }

  // Create OpenGL context
  glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
    deinit();
    exit(1);
  }

  // Initialize GLEW for OpenGL extensions
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    throw std::runtime_error(std::string("WindowManager::init(): GLEW initialization failed") +
                             reinterpret_cast<const char*>(glewGetErrorString(err)));
  }

  // Disable compositor bypass for better performance
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

  // Configure OpenGL rendering state
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0f);

  running = true;
}

void handleEvent(SDL_Event& event) {
  switch (event.type) {
  case SDL_QUIT:
    deinit();
    running = false;
    return;

  case SDL_KEYDOWN:
    switch (event.key.keysym.sym) {
    case SDLK_q:
    case SDLK_ESCAPE:
      deinit();
      running = false;
      return;

    default:
      break;
    }
    break;

  case SDL_MOUSEMOTION:
    mouseX = event.motion.x;

    // Invert Y coordinate because Window is technically upside down
    mouseY = height - event.motion.y;
    break;

  case SDL_WINDOWEVENT:
    switch (event.window.event) {
    case SDL_WINDOWEVENT_ENTER:
      focused = true;
      break;

    case SDL_WINDOWEVENT_LEAVE:
      focused = false;
      break;

    case SDL_WINDOWEVENT_RESIZED:
      width = event.window.data1;
      height = event.window.data2;

    default:
      break;
    }
    break;

  default:
    break;
  }
}

void display() { SDL_GL_SwapWindow(win); }

void clear() {
  // Clear with current theme background color
  float* c = Theme::colors.background;
  glClearColor(c[0], c[1], c[2], c[3]);
  glClear(GL_COLOR_BUFFER_BIT);
}

} // namespace SDLWindow