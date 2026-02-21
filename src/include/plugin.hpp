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

#pragma once
#include "common.hpp"
#include "plugin_api.hpp"

namespace Plugin {
struct PluginInstance {
  void* handle;
  pvPluginInitFn init;
  pvPluginStartFn start;
  pvPluginStopFn stop;
  pvPluginDrawFn draw;
  pvPluginHandleEventFn handleEvent;
};

extern PvAPI api;
extern std::vector<PluginInstance> plugins;

/**
 * @brief loads all plugins in the plugin folder
 */
void loadAll();

/**
 * @brief starts all loaded plugins
 */
void startAll();

/**
 * @brief unloads all loaded plugins
 */
void unloadAll();

/**
 * @brief draws all plugins
 */
void drawAll();

/**
 * @brief passes sdl event to all plugins
 * @param event the event to pass
 */
void handleEvent(SDL_Event& event);
} // namespace Plugin