#pragma once
#include "common.hpp"

namespace SDLWindow {

// Window and OpenGL context handles
extern SDL_Window* win;
extern SDL_GLContext glContext;
extern bool focused;
extern bool running;
extern int width, height;

// Mouse coordinates
extern int mouseX, mouseY;

/**
 * @brief Cleanup SDL window and OpenGL context
 */
void deinit();

/**
 * @brief Initialize SDL window and OpenGL context
 */
void init();

/**
 * @brief Handle SDL events (keyboard, window, quit)
 * @param event The SDL event to process
 */
void handleEvent(SDL_Event& event);

/**
 * @brief Swap the OpenGL buffers to display the rendered frame
 */
void display();

/**
 * @brief Clear the OpenGL color buffer with the current background color
 */
void clear();

} // namespace SDLWindow