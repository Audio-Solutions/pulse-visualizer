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

// Consolidated plugin option storage
using PluginOptionKey = std::pair<std::string, std::string>;

struct PluginOptionRecord {
  Config::PluginConfigValue value;
  std::string displayName;
  PluginConfigSpec spec;
};

struct PluginOptionKeyHasher {
  std::size_t operator()(const PluginOptionKey& p) const {
    auto h1 = std::hash<std::string> {}(p.first);
    auto h2 = std::hash<std::string> {}(p.second);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
  }
};

extern std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher> pluginOptions;
extern std::mutex pluginOptionsMutex;

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
 * @return YAML node
 */
YAML::Node getNode(const YAML::Node& root, std::string_view path);

bool registerPluginConfigOption(void* pluginContext, const char* path, const Config::PluginConfigValue* defaultValue,
                                const Config::PluginConfigSpec* descriptor);
bool getPluginConfigOption(void* pluginContext, const char* path, Config::PluginConfigValue* outValue);
bool setPluginConfigOption(void* pluginContext, const char* path, const PluginConfigValue* value);

/**
 * @brief Read a value from the configuration into an output reference.
 * @tparam T Target value type
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, std::string_view path, T& out);

/**
 * @brief Load configuration from file
 */
void load();

/**
 * @brief Check if configuration file has been modified and reload if necessary
 * @return true if configuration was reloaded, false otherwise
 */
bool reload();

/**
 * @brief Save configuration to file
 */
void save();

/**
 * @brief Cleanup inotify resources
 */
void cleanup();

/**
 * @brief create rolling backup of current config
 */
void rollBackup();

/**
 * @brief restore latest backup
 */
void restoreBackup();

/**
 * @brief Get the installation directory
 * @return Installation directory
 */
std::string getInstallDir();
} // namespace Config