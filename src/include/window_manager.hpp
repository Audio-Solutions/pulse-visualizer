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
#include "types.hpp"

namespace WindowManager {

// Delta time for frame timing
extern float dt;

constexpr int MIN_SIDELENGTH = 80;
constexpr int MAX_SIDELENGTH = 8192;
constexpr int SPLITTER_WIDTH = 2;

/**
 * @brief Set OpenGL viewport for rendering
 * @param bounds The bounds of the viewport
 */
void setViewport(Bounds bounds);

/**
 * @brief update bounds for all branches and visualizers
 */
void updateBounds();

/**
 * @brief render all branches and visualizers
 */
void render();

/**
 * @brief pass SDL_Event to all branches and visualizers
 */
void handleEvent(const SDL_Event& event);

/**
 * @brief (Re-)Initialize all windows.
 */
void initialize();

/**
 * @brief Cleanup all visualizer windows.
 */
void cleanup();

/**
 * @brief Add a visualizer to a group.
 * @param group The group to add to
 * @param id The ID of the visualizer to add
 *
 * @returns `true` if successful, `false` otherwise.
 */
bool addVisualizerToGroup(const std::string& group, const std::string& id);

/**
 * @brief Remove a visualizer from a group.
 * @param group The group to remove from
 * @param id The ID of the visualizer to remove
 *
 * @returns `true` if successful, `false` otherwise.
 */
bool removeVisualizerFromGroup(const std::string& group, const std::string& id);

/**
 * @brief Hover region enum for sway-like rearrangement.
 */
enum class HoverRegion { None, Center, Top, Bottom, Left, Right };

} // namespace WindowManager
