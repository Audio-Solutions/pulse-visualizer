#pragma once
#include "common.hpp"

namespace SDLWindow {
extern SDL_Window* win;
extern SDL_GLContext glContext;
extern bool focused;
extern bool running;
extern int width, height;

void deinit();
void init();
void handleEvent(SDL_Event& event);
void display();
void clear();

} // namespace SDLWindow