#pragma once
#include "common.hpp"

namespace SDLWindow {

// Window and OpenGL context handles
extern std::vector<SDL_Window*> wins;
extern std::vector<SDL_GLContext> glContexts;
extern size_t currentWindow;
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

/**
 * @brief create a new window
 * @param title The title of the window
 * @param width The width of the window
 * @param height The height of the window
 * @param flags The flags for the window (default: SDL_WINDOW_RESIZABLE)
 * @return the index of the created window
 */
size_t createWindow(const std::string& title, int width, int height, uint32_t flags = SDL_WINDOW_RESIZABLE);

/**
 * @brief destroy a window
 * @param index The index of the window to destroy
 * @return true if the window was destroyed, false otherwise
 * @note if the window is the current window, the current window will be set to 0
 */
bool destroyWindow(size_t index);

/**
 * @brief select a window at an index for rendering
 * @param index The index of the window to select
 * @return true if the window was selected, false otherwise
 */
bool selectWindow(size_t index);
} // namespace SDLWindow