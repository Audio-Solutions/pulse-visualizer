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

#pragma once
#include "common.hpp"

namespace UpdaterWindow {

/**
 * @brief Check for updates and show the window if necessary
 */
void check();

/**
 * @brief Draw the window
 */
void draw();

/**
 * @brief Handle window event
 * @param event Event to handle
 */
void handleEvent(const SDL_Event& event);

}; // namespace UpdaterWindow
