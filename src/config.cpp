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

#include "include/config.hpp"

#include "include/config_schema.hpp"
#include "include/plugin.hpp"

namespace Config {

bool broken = false;

// Global configuration options
Options options;

PluginConfigSpecRegistry pluginConfigSpecs;
std::map<std::string, std::string> pluginDisplayNames;
std::unordered_set<std::string> registeredPluginPaths;
YAML::Node pluginValuesNode;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

std::string makePluginPathKey(std::string_view pluginKey, std::string_view path) {
  std::string key;
  key.reserve(pluginKey.size() + path.size() + 2);
  key.append(pluginKey);
  key.push_back('.');
  key.append(path);
  return key;
}

const PluginConfigSpec* findPluginSpec(std::string pluginKey, std::string path) {
  auto groupIt = pluginConfigSpecs.find(pluginKey);
  if (groupIt == pluginConfigSpecs.end())
    return nullptr;

  auto ruleIt = groupIt->second.find(path);
  if (ruleIt == groupIt->second.end())
    return nullptr;

  return &ruleIt->second;
}

std::string getInstallDir() {
#ifdef _WIN32
  char buffer[MAX_PATH];
  DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
  if (length == 0)
    return std::string();

  std::string path(buffer);
  size_t pos = path.find_last_of("\\/");
  if (pos != std::string::npos)
    return path.substr(0, pos);
  return std::string();
#else
  return std::string(PULSE_DATA_DIR);
#endif
}

void copyFiles() {
#ifdef _WIN32
  const char* home = getenv("USERPROFILE");
#else
  const char* home = getenv("HOME");
#endif
  if (!home) {
#ifdef _WIN32
    LOG_ERROR("Warning: USERPROFILE environment variable not set, cannot setup user config");
#else
    LOG_ERROR("Warning: HOME environment variable not set, cannot setup user config");
#endif
    return;
  }

  // Create user directories
  std::string userCfgDir = std::string(home) + "/.config/pulse-visualizer";
  std::string userThemeDir = userCfgDir + "/themes";
  std::string userFontDir = std::string(home) + "/.local/share/fonts/JetBrainsMono";

  std::filesystem::create_directories(userCfgDir);
  std::filesystem::create_directories(userThemeDir);
  std::filesystem::create_directories(userFontDir);

  std::string installDir = getInstallDir();

  // Copy configuration template if it doesn't exist
  if (!std::filesystem::exists(userCfgDir + "/config.yml")) {
    std::string cfgSource = installDir + "/config.yml.template";
    if (std::filesystem::exists(cfgSource)) {
      try {
        std::filesystem::copy_file(cfgSource, userCfgDir + "/config.yml");
        LOG_DEBUG("Created user config file: " << userCfgDir << "/config.yml");
      } catch (const std::exception& e) {
        LOG_ERROR("Warning: Failed to copy config template: " << e.what());
      }
    }
  }

  // Copy theme files
  std::string themeSource = installDir + "/themes";
  if (std::filesystem::exists(themeSource) && !std::filesystem::exists(userThemeDir + "/_TEMPLATE.txt")) {
    try {
      for (const auto& entry : std::filesystem::directory_iterator(themeSource)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
          std::string destFile = userThemeDir + "/" + entry.path().filename().string();
          if (!std::filesystem::exists(destFile)) {
            std::filesystem::copy_file(entry.path(), destFile);
          }
        }
      }
      LOG_DEBUG("Copied themes to: " << userThemeDir);
    } catch (const std::exception& e) {
      LOG_ERROR("Warning: Failed to copy themes: " << e.what());
    }
  }

  // Copy font file
  std::string fontSource = installDir + "/fonts/JetBrainsMonoNerdFont-Medium.ttf";
  std::string userFontFile = userFontDir + "/JetBrainsMonoNerdFont-Medium.ttf";
  if (std::filesystem::exists(fontSource)) {
    try {
      if (!std::filesystem::exists(userFontFile)) {
        std::filesystem::copy_file(fontSource, userFontFile);
        LOG_DEBUG("Copied font to: " << userFontFile);
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Warning: Failed to copy font: " << e.what());
    }
  }
}

std::optional<YAML::Node> getNode(const YAML::Node& root, std::string_view path) {
  const std::string pathStr(path);
  if (root[pathStr].IsDefined())
    return root[pathStr];

  const size_t dotIndex = path.find('.');
  if (dotIndex == std::string_view::npos) {
    broken = true;
    return std::nullopt;
  }

  const std::string section(path.substr(0, dotIndex));
  const std::string_view sub = path.substr(dotIndex + 1);

  if (!root[section].IsDefined()) {
    broken = true;
    return std::nullopt;
  }

  return getNode(root[section], sub);
}

/**
 * @brief Read a value from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, std::string_view path, T& out) {
  constexpr auto typeName = []() constexpr -> std::string_view {
    if constexpr (std::is_same_v<T, std::string>)
      return "string";
    else if constexpr (std::is_same_v<T, int>)
      return "int";
    else if constexpr (std::is_same_v<T, float>)
      return "float";
    else if constexpr (std::is_same_v<T, Rotation>)
      return "Rotation";
    else
      return "unknown";
  }();

  auto node = getNode(root, path);
  if (node.has_value()) {
    try {
      T value = node.value().as<T>();
      out = value;
      return;
    } catch (std::exception) {
      broken = true;
      LOG_ERROR("Converting " << path << " to " << typeName << " failed");
      return;
    }
  }

  broken = true;
  LOG_ERROR(path << " is missing.");
}

/**
 * @brief Read a boolean from the configuration with permissive conversions.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <> void get<bool>(const YAML::Node& root, std::string_view path, bool& out) {
  auto node = getNode(root, path);
  if (!node.has_value()) {
    broken = true;
    return;
  }

  const YAML::Node& value = node.value();

  try {
    out = value.as<bool>();
    return;
  } catch (...) {
    try {
      std::string str = value.as<std::string>();
      if (str == "true" || str == "yes" || str == "on") {
        out = true;
        return;
      }
      if (str == "false" || str == "no" || str == "off") {
        out = false;
        return;
      }
      LOG_ERROR("Converting " << path << " to bool failed");
      broken = true;
      return;
    } catch (...) {
      try {
        out = (value.as<int>() != 0);
        return;
      } catch (...) {
        LOG_ERROR("Converting " << path << " to bool failed");
        broken = true;
        return;
      }
    }
  }
}

/**
 * @brief Read a vector of strings from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed sequence
 */
template <>
void get<std::vector<std::string>>(const YAML::Node& root, std::string_view path, std::vector<std::string>& out) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsSequence()) {
    broken = true;
    return;
  }

  std::vector<std::string> result;
  for (const auto& item : node.value()) {
    result.push_back(item.as<std::string>());
  }
  out = std::move(result);
}

/**
 * @brief Read a map<string, vector<string>> from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed mapping
 */
template <>
void get<std::map<std::string, std::vector<std::string>>>(const YAML::Node& root, std::string_view path,
                                                          std::map<std::string, std::vector<std::string>>& out) {
  auto node = getNode(root, path);
  if (!node.has_value() || !node.value().IsMap()) {
    broken = true;
    return;
  }

  std::map<std::string, std::vector<std::string>> result;
  for (const auto& item : node.value()) {
    const std::string itemKey = item.first.as<std::string>();
    std::string nestedPath;
    nestedPath.reserve(path.size() + 1 + itemKey.size());
    nestedPath.append(path);
    nestedPath.push_back('.');
    nestedPath.append(itemKey);

    std::vector<std::string> arr;
    get<std::vector<std::string>>(root, nestedPath, arr);
    result[itemKey] = std::move(arr);
  }
  out = std::move(result);
}

/**
 * @brief Read a Rotation (backed by int) from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed enum value
 */
template <> void get<Rotation>(const YAML::Node& root, std::string_view path, Rotation& out) {
  int tmp = static_cast<int>(out);
  get<int>(root, path, tmp);
  out = static_cast<Rotation>(tmp);
}

template <typename T> void setNodeByPath(YAML::Node& node, std::string_view path, const T& value) {
  const size_t dot = path.find('.');
  if (dot == std::string_view::npos) {
    node[std::string(path)] = value;
    return;
  }

  const std::string section(path.substr(0, dot));
  YAML::Node child = node[section];
  setNodeByPath<T>(child, path.substr(dot + 1), value);
}

bool registerPluginConfigOption(void* pluginContext, const char* path, const Config::PluginConfigValue* defaultValue,
                                const Config::PluginConfigSpec* descriptor) {
  const auto plugin = static_cast<Plugin::PluginInstance*>(pluginContext);
  if (!plugin)
    return false;

  const std::string pluginKey = plugin->key;
  const std::string keyPath = std::string(path);

  if (pluginKey.empty() || keyPath.empty())
    return false;

  const std::string yamlPath = "plugins." + pluginKey + "." + keyPath;
  const std::string uniquePluginKey = makePluginPathKey(pluginKey, path);
  if (registeredPluginPaths.contains(uniquePluginKey)) {
    LOG_ERROR("Duplicate plugin config option registration for '" << yamlPath << "'");
    return false;
  }
  registeredPluginPaths.insert(uniquePluginKey);

  if (!plugin->displayName.empty())
    pluginDisplayNames[pluginKey] = plugin->displayName;
  else if (!pluginDisplayNames.contains(pluginKey))
    pluginDisplayNames[pluginKey] = pluginKey;

  PluginConfigSpec& rule = pluginConfigSpecs[pluginKey][std::string(path)];
  PluginConfigSpec empty;
  rule = descriptor ? *descriptor : empty;
  rule.type = static_cast<PluginConfigType>(defaultValue->index());
  if (rule.label.empty())
    rule.label = std::string(path);

  if (rule.type == PluginConfigType::Int) {
    rule.detentsFloat.clear();
    rule.choices.clear();
  } else if (rule.type == PluginConfigType::Float) {
    rule.detentsInt.clear();
    rule.choices.clear();
  } else if (rule.type == PluginConfigType::String) {
    rule.detentsInt.clear();
    rule.detentsFloat.clear();
  } else {
    rule.detentsInt.clear();
    rule.detentsFloat.clear();
    rule.choices.clear();
  }

  PluginConfigValue value = *defaultValue;
  const bool oldBroken = broken;
  auto node = getNode(pluginValuesNode, uniquePluginKey);
  broken = oldBroken;
  if (node.has_value()) {
    try {
      switch (rule.type) {
      case PluginConfigType::Bool:
        value = node.value().as<bool>();
        break;
      case PluginConfigType::Int:
        value = node.value().as<int>();
        break;
      case PluginConfigType::Float:
        value = node.value().as<float>();
        break;
      case PluginConfigType::String:
        value = node.value().as<std::string>();
        break;
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to parse plugin config value for '" << yamlPath << "': " << e.what());
    }
  } else {
    LOG_DEBUG("No plugin option found for '" << yamlPath << "', using default");
  }

  return setPluginConfigOption(pluginContext, path, &value);
}

bool getPluginConfigOption(void* pluginContext, const char* path, Config::PluginConfigValue* outValue) {
  const std::string pluginKey = static_cast<Plugin::PluginInstance*>(pluginContext)->key;
  const PluginConfigSpec* rule = findPluginSpec(pluginKey, path);
  if (!rule)
    return false;

  const std::string pluginPath = std::string(pluginKey) + "." + std::string(path);
  const bool oldBroken = broken;
  auto node = getNode(pluginValuesNode, pluginPath);
  broken = oldBroken;
  if (!node.has_value())
    return false;

  try {
    switch (rule->type) {
    case PluginConfigType::Bool:
      *outValue = std::move(node.value().as<bool>());
      return true;
    case PluginConfigType::Int:
      *outValue = std::move(node.value().as<int>());
      return true;
    case PluginConfigType::Float:
      *outValue = std::move(node.value().as<float>());
      return true;
    case PluginConfigType::String:
      *outValue = std::move(node.value().as<std::string>());
      return true;
    }
  } catch (const std::exception&) {
    return false;
  }

  return false;
}

bool setPluginConfigOption(void* pluginContext, const char* path, const PluginConfigValue* value) {
  const std::string pluginKey = static_cast<Plugin::PluginInstance*>(pluginContext)->key;
  const PluginConfigSpec* rule = findPluginSpec(pluginKey, path);
  if (!rule || rule->type != static_cast<PluginConfigType>(value->index()))
    return false;

  YAML::Node pluginNodeOut = pluginValuesNode[pluginKey];
  std::visit([&](const auto& v) { setNodeByPath(pluginNodeOut, path, v); }, *value);
  return true;
}

void load(bool recovering) {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData;

  broken = false;

  // Handle YAML errors
  try {
    configData = YAML::LoadFile(path);
  } catch (YAML::BadFile e) {
    LOG_ERROR("Failed to load config file");
  } catch (YAML::ParserException e) {
    LOG_ERROR("Parser error when loading the config file: \"" << e.msg << "\" at " << (e.mark.line + 1) << "("
                                                              << (e.mark.column + 1) << ")");
    broken = true;
  }

#ifdef __linux__
  // Setup file watching for configuration changes
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyWatch != -1)
      inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = inotify_add_watch(inotifyFd, path.c_str(), IN_CLOSE_WRITE);
  }
#endif

  // If the config fails to load we still want the watch to be created
  if (configData.IsNull()) {
    pluginValuesNode = YAML::Node(YAML::NodeType::Map);
    return;
  }

  pluginValuesNode = configData["plugins"];
  if (!pluginValuesNode.IsMap())
    pluginValuesNode = YAML::Node(YAML::NodeType::Map);

  // Load visualizer window layouts
  get<std::map<std::string, std::vector<std::string>>>(configData, "visualizers", options.visualizers);

  ConfigSchema::forEachFieldByType(
      [&](const auto& field) { get<bool>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<int>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<float>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<std::string>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<Rotation>(configData, field.path, field.get(options)); });

  // If the config is broken, attempt to recover by merging defaults
  if (broken && !recovering) {
    LOG_ERROR("Config is broken, attempting to recover...");
    save();
    load(true);
  }
}

bool save() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");

  YAML::Node root;

  ConfigSchema::forEachFieldByType(
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, static_cast<int>(field.getConst(options))); });

  // Visualizers
  {
    YAML::Node vis;
    for (const auto& [groupName, visualizerList] : options.visualizers) {
      YAML::Node arr(YAML::NodeType::Sequence);
      for (const auto& item : visualizerList)
        arr.push_back(item);
      vis[groupName] = arr;
    }
    root["visualizers"] = vis;
  }

  if (pluginValuesNode.IsDefined())
    root["plugins"] = pluginValuesNode;

  YAML::Emitter out;
  for (auto it = root.begin(); it != root.end(); ++it) {
    out << YAML::BeginMap;
    out << it->first << it->second;
    out << YAML::EndMap;
    out << YAML::Newline;
  }

  std::ofstream fout(path);
  if (!fout.is_open()) {
    LOG_ERROR("Failed to open config file for writing");
    return false;
  }

  fout << out.c_str();

  if (fout.fail()) {
    return false;
  }

  fout.close();

  return true;
}

bool reload() {
#ifdef __linux__
  // Check for file changes using inotify
  if (inotifyFd != -1 && inotifyWatch != -1) {
    char buf[sizeof(struct inotify_event) * 16];
    ssize_t len = read(inotifyFd, buf, sizeof(buf));
    if (len > 0) {
      for (int i = 0; i < len; i += sizeof(struct inotify_event)) {
        inotify_event* event = (inotify_event*)&buf[i];

        // check if the inotify got freed (file gets moved, deleted etc), kernel fires IN_IGNORED when that happens
        if ((event->mask & IN_IGNORED) == IN_IGNORED) {
          inotifyFd = -1;
          inotifyWatch = -1;
        }
      }

      load();
      return true;
    }
  }
#else
  // Check for file changes using stat (non-Linux systems)
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
#ifdef _WIN32
  struct _stat st;
  if (_stat(path.c_str(), &st) != 0) {
#else
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
#endif
    LOG_ERROR("Warning: could not stat config file.");
    return false;
  }
  static time_t lastConfigMTime = st.st_mtime;
  if (st.st_mtime != lastConfigMTime) {
    lastConfigMTime = st.st_mtime;
    load();
    return true;
  }
#endif
  return false;
}

void cleanup() {
#ifdef __linux__
  if (inotifyWatch != -1 && inotifyFd != -1) {
    inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = -1;
  }
  if (inotifyFd != -1) {
    close(inotifyFd);
    inotifyFd = -1;
  }
#endif
}

template void get<int>(const YAML::Node&, std::string_view, int&);
template void get<float>(const YAML::Node&, std::string_view, float&);
template void get<std::string>(const YAML::Node&, std::string_view, std::string&);

} // namespace Config