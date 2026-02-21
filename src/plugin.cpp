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

#include "include/plugin.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/window_manager.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

namespace Plugin {
std::vector<PluginInstance> plugins;

PvAPI api {
    .apiVersion = PLUGIN_API_VERSION,
    .saveConfig = Config::save,
    .expandUserPath = expandUserPath,
    .drawLine = Graphics::drawLine,
    .drawFilledRect = Graphics::drawFilledRect,
    .drawArc = Graphics::drawArc,
    .drawText = Graphics::Font::drawText,
    .getTextSize = Graphics::Font::getTextSize,
    .wrapText = Graphics::Font::wrapText,
    .truncateText = Graphics::Font::truncateText,
    .fitTextBox = Graphics::Font::fitTextBox,
    .rgbaToHsva = Graphics::rgbaToHsva,
    .hsvaToRgba = Graphics::hsvaToRgba,
    .alpha = Theme::alpha,
    .mix = Theme::mix,
    .createWindow = SDLWindow::createWindow,
    .destroyWindow = SDLWindow::destroyWindow,
    .selectWindow = SDLWindow::selectWindow,
    .getWindowSize = SDLWindow::getWindowSize,
    .getCursorPos = SDLWindow::getCursorPos,
    .setViewport = WindowManager::setViewport,
    .config = &Config::options,
    .theme = &Theme::colors,
    .debug = &CmdlineArgs::debug,
};

void loadAll() {
  const std::string path = "~/.config/pulse-visualizer/plugins/";
  std::filesystem::path dir {expandUserPath(path)};

  if (!std::filesystem::exists(dir))
    std::filesystem::create_directories(dir);

  if (!std::filesystem::is_directory(dir))
    return;

  for (const auto& entry : std::filesystem::directory_iterator {dir}) {
#ifdef _WIN32
    if (!entry.is_regular_file() || entry.path().extension() != ".dll")
#else
    if (!entry.is_regular_file() || entry.path().extension() != ".so")
#endif
      continue;

    LOG_DEBUG("Loading " << entry.path().filename());

#ifdef _WIN32
    HMODULE handle = LoadLibrary(entry.path().string().c_str());
    if (!handle) {
      LOG_ERROR("LoadLibrary failed: " << GetLastError());
#else
    void* handle = dlopen(entry.path().string().c_str(), RTLD_NOW);
    if (!handle) {
      LOG_ERROR("dlopen failed: " << dlerror());
#endif
      continue;
    }

    auto loadSymbol = [&](auto& fn, const char* name) {
#ifdef _WIN32
      FARPROC sym = GetProcAddress(handle, name);
      if (!sym) {
        LOG_ERROR("GetProcAddress " << name << " failed: " << GetLastError());
        return false;
      }
#else
      void* sym = dlsym(handle, name);
      if (const char* e = dlerror()) {
        LOG_ERROR("dlsym " << name << " failed: " << e);
        return false;
      }
      if (!sym) {
        LOG_ERROR("dlsym " << name << " returned null");
        return false;
      }
#endif
      using Fn = std::remove_reference_t<decltype(fn)>;
      fn = reinterpret_cast<Fn>(sym);
      return true;
    };

    PluginInstance pl {};
    pl.handle = handle;

    if (!loadSymbol(pl.init, "pvPluginInit") || !loadSymbol(pl.start, "pvPluginStart") ||
        !loadSymbol(pl.stop, "pvPluginStop") || !loadSymbol(pl.draw, "draw") ||
        !loadSymbol(pl.handleEvent, "handleEvent")) {
#ifdef _WIN32
      FreeLibrary(handle);
#else
      dlclose(handle);
#endif
      continue;
    }

    if (pl.init(&api) != 0) {
      LOG_ERROR("init returned non-zero");
#ifdef _WIN32
      FreeLibrary(handle);
#else
      dlclose(handle);
#endif
      continue;
    }

    plugins.push_back(pl);
  }
}

void startAll() {
  for (auto& pl : plugins) {
    if (pl.start)
      pl.start();
  }
}

void unloadAll() {
  for (auto& pl : plugins) {
    if (pl.stop)
      pl.stop();

    if (pl.handle)
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(pl.handle));
#else
      dlclose(pl.handle);
#endif
  }

  plugins.clear();
}

void drawAll() {
  for (auto& pl : plugins) {
    if (pl.draw)
      pl.draw();
  }
}

void handleEvent(SDL_Event& event) {
  for (auto& pl : plugins) {
    if (pl.handleEvent)
      pl.handleEvent(event);
  }
}
} // namespace Plugin