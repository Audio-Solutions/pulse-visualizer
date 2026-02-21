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

namespace Theme {

extern Colors colors;

#ifdef __linux__
extern int themeInotifyFd;
extern int themeInotifyWatch;
extern std::string currentThemePath;
#endif

/**
 * @brief Apply alpha transparency to a color
 * @param color Input color array (RGBA)
 * @param alpha Alpha value to apply
 * @return Pointer to temporary color with applied alpha
 */
float* alpha(float* color, float alpha);

/**
 * @brief Linear interpolation between two colors
 * @param a First color array
 * @param b Second color array
 * @param out Output color array
 * @param t Interpolation factor (0.0 to 1.0)
 */
void mix(float* a, float* b, float* out, float t);

/**
 * @brief Load a theme from file
 */
void load();

/**
 * @brief Check if theme file has been modified and reload if necessary
 * @return true if theme was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

} // namespace Theme