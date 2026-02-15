/*
 * Pulse Audio Visualizer
 * Copyright (C) 2026 Beacroxx
 * Copyright (C) 2026 Contributors (see CONTRIBUTORS.md)
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

#include "include/update_window.hpp"

#include "include/graphics.hpp"
#include "include/sdl_window.hpp"

#include <regex>

#ifdef USE_UPDATER
#include <curl/curl.h>
#endif

namespace UpdaterWindow {
#ifdef USE_UPDATER
const int eventId = SDL_EVENT_USER + 1;
bool shown = false;
std::thread checkThread;

bool updateAvailable, updateShown;
std::string newVersion;

struct MemoryBlock {
  char* data;
  size_t size;
};

// Callback function for writing received data into our buffer
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  struct MemoryBlock* mem = (struct MemoryBlock*)userp;

  // Reallocate memory to fit new data
  char* ptr = (char*)realloc(mem->data, mem->size + total_size + 1);
  if (ptr == NULL) {
    LOG_ERROR("Error: Not enough memory to store data");
    return 0; // Returning 0 will signal libcurl to abort
  }

  mem->data = ptr;
  memcpy(&(mem->data[mem->size]), contents, total_size);
  mem->size += total_size;
  mem->data[mem->size] = '\0'; // Null-terminate

  return total_size;
}

void _check() {
  LOG_DEBUG("Checking for updates");

  CURL* curl = curl_easy_init();
  if (!curl) {
    LOG_ERROR("Failed to initialise curl request")
    return;
  }

  MemoryBlock chunk = {nullptr, 0};

  curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/Audio-Solutions/pulse-visualizer/releases/latest");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "pulse-visualizer/" VERSION_STRING);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    LOG_ERROR("curl_easy_perform() failed with: " << curl_easy_strerror(res));
    free(chunk.data);
    return;
  } else {
    // Successfully fetched JSON
    LOG_DEBUG("Received release json (" << chunk.size << " bytes)");
  }

  curl_easy_cleanup(curl);

  std::regex re(R"_rx("tag_name"\s*:\s*"v?(\d+(?:\.\d+)+)")_rx");
  std::smatch m;
  std::string json(chunk.data);

  updateShown = false;

  if (std::regex_search(json, m, re)) {
    std::string version = m[1];
    LOG_DEBUG("Latest version is: " << version << " while current version is: " << VERSION_STRING);

    newVersion = version;
    if (version != VERSION_STRING) {
      updateAvailable = true;
      LOG_DEBUG("Update available");
    }
  } else {
    updateAvailable = false;
    LOG_DEBUG("Regex failed");
  }

  SDL_Event event;
  SDL_zero(event);
  event.type = eventId;
  event.user.code = 1;
  if (!SDL_PushEvent(&event)) {
    LOG_ERROR("Failed to push SDL event");
  }

  free(chunk.data);
}

void check() {
  if (shown) {
    return;
  }

  checkThread = std::thread(_check);
}

bool mouseOverRect(float x, float y, float w, float h) {
  const float mx = SDLWindow::states["update"].mousePos.first;
  const float my = SDLWindow::states["update"].mousePos.second;

  if (mx > x && mx < x + w && my > y && my < y + h)
    return true;

  return false;
}

void hide() {
  shown = false;
  LOG_DEBUG("Destroying Update window");
  SDLWindow::destroyWindow("update");
}

void handleEvent(const SDL_Event& event) {
  if (!shown && event.type != eventId)
    return;

  if (SDLWindow::states.find("update") != SDLWindow::states.end() && !SDLWindow::states["update"].focused &&
      event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.type != eventId)
    return;

  switch (event.type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    hide();
    return;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      const auto textSize = Graphics::Font::getTextSize("OK", 20);
      const float x = 300 - 10 - (textSize.first + 20);
      const float y = 10;
      const float w = textSize.first + 20;
      const float h = textSize.second + 15;

      if (mouseOverRect(x, y, w, h)) {
        hide();
        return;
      }
    }
    break;

  case eventId:
    if (event.user.code == 1) {
      if (updateAvailable && !updateShown) {
        // create window
        LOG_DEBUG("Creating Update window");
        SDLWindow::createWindow("update", "Update available", 300, 100, 0);
        shown = true;
        updateShown = true;
      }
    }
    break;

  default:
    break;
  }
}

void draw() {
  if (!shown)
    return;

  SDLWindow::selectWindow("update");
  WindowManager::setViewport(0, 300, 100);

  Graphics::Font::drawText("An update is available!", 10, 100 - 30, 20, Theme::colors.text);

  std::stringstream ss;
  ss << VERSION_STRING << " -> " << newVersion;
  Graphics::Font::drawText(ss.str().c_str(), 10, 100 - 55, 16, Theme::colors.text);

  // render button
  {
    const auto textSize = Graphics::Font::getTextSize("OK", 20);
    const float x = 300 - 10 - (textSize.first + 20);
    const float y = 10;
    const float w = textSize.first + 20;
    const float h = textSize.second + 15;

    Graphics::drawFilledRect(x, y, w, h, mouseOverRect(x, y, w, h) ? Theme::colors.accent : Theme::colors.bgaccent);
    Graphics::Font::drawText("OK", 300 - 10 - (textSize.first + 10), 20, 20, Theme::colors.text);
  }
}

void cleanup() {
  if (checkThread.joinable()) {
    checkThread.join();
  }
}
#else
void check() {};
void handleEvent(SDL_Event*) {};
void draw() {};
void cleanup() {};
#endif
}; // namespace UpdaterWindow
