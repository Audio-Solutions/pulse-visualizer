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
/**
 * @brief Resolved plugin entry points for a dynamically loaded plugin.
 */
struct PluginInstance {
  void* handle;
  std::string key;
  std::string displayName;
  PvAPI api;
  pvPluginInitFn init;
  pvPluginStartFn start;
  pvPluginStopFn stop;
  pvPluginDrawFn draw;
  pvPluginHandleEventFn handleEvent;
  pvPluginConfigReloadFn onConfigReload;
};

/**
 * @brief API table exposed to plugins.
 */
extern PvAPI api;

/**
 * @brief Loaded plugin instances.
 */
extern std::deque<PluginInstance> plugins;

/**
 * @brief Load all plugins in the plugin folder.
 */
void loadAll();

/**
 * @brief Start all loaded plugins.
 */
void startAll();

/**
 * @brief Unload all loaded plugins.
 */
void unloadAll();

/**
 * @brief Draw all loaded plugins.
 */
void drawAll();

/**
 * @brief Pass an SDL event to all loaded plugins.
 * @param event The event to pass
 */
void handleEvent(SDL_Event& event);

/**
 * @brief Notify plugins that main config has been reloaded.
 */
void notifyConfigReload();
} // namespace Plugin