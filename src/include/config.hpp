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

namespace Config {
extern bool broken;

extern Options options;

#ifdef __linux__
extern int inotifyFd;
extern int inotifyWatch;
#endif

/**
 * @brief Copy default configuration files to user directory if they don't exist
 */
void copyFiles();

/**
 * @brief Get a nested node from YAML configuration
 * @param root Root YAML node
 * @param path Dot-separated path to the desired node
 * @return Optional YAML node if found
 */
std::optional<YAML::Node> getNode(const YAML::Node& root, const std::string& path);

/**
 * @brief Read a value from the configuration into an output reference.
 * @tparam T Target value type
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, const std::string& path, T& out);

/**
 * @brief Load configuration from file
 */
void load(bool recovering = false);

/**
 * @brief Check if configuration file has been modified and reload if necessary
 * @return true if configuration was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Save configuration to file
 * @return true if saved properly, false otherwise
 */
bool save();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

/**
 * @brief Get the installation directory
 * @return Installation directory
 */
std::string getInstallDir();
} // namespace Config