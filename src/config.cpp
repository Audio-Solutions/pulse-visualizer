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
#include "include/sdl_window.hpp"
#include "include/visualizer_registry.hpp"

namespace Config {

bool broken = false;
bool visualizersBroken = false;

// Global configuration options
Options options;

// plugin option storage
std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher> pluginOptions;
std::mutex pluginOptionsMutex;

#ifdef __linux__
int inotifyFd = -1;
int inotifyWatch = -1;
#endif

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

/**
 * @brief Read a map of Node hierarchy trees from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed hierarchy tree map
 */
template <>
void get<std::unordered_map<std::string, WindowManager::Node>>(
    const YAML::Node& root, std::string_view path, std::unordered_map<std::string, WindowManager::Node>& out) {
  auto node = getNode(root, path);

  if (!node.has_value() || !node.value().IsMap()) {
    visualizersBroken = broken = true;
    return;
  }

  const YAML::Node& value = node.value();

  std::function<WindowManager::Node(const YAML::Node&, const std::string_view)> parseHierarchyTree;
  parseHierarchyTree = [&](const YAML::Node& n, const std::string_view grp) -> WindowManager::Node {
    if (!n || n.IsNull())
      return nullptr;

    if (n.IsScalar()) {
      try {
        // treat bare scalar as an id
        return std::make_shared<WindowManager::Variant>(VisualizerRegistry::find(n.as<std::string>()));
      } catch (...) {
        visualizersBroken = broken = true;
        return nullptr;
      }
    }

    if (n.IsMap()) {
      // Branch by explicit id
      if (n["id"] && n["id"].IsScalar()) {
        try {
          return std::make_shared<WindowManager::Variant>(VisualizerRegistry::find(n["id"].as<std::string>()));
        } catch (...) {
          visualizersBroken = broken = true;
          return nullptr;
        }
      }

      // Split node: require 'type' and 'children'
      if (!n["type"] || !n["children"] || !n["children"].IsSequence()) {
        visualizersBroken = broken = true;
        return nullptr;
      }

      WindowManager::Splitter s {};
      try {
        const std::string t = n["type"].as<std::string>();
        if (t == "vsplit")
          s.orientation = WindowManager::Orientation::Vertical;
        else if (t == "hsplit")
          s.orientation = WindowManager::Orientation::Horizontal;
        else {
          visualizersBroken = broken = true;
        }

        if (n["ratio"] && n["ratio"].IsScalar())
          s.ratio = n["ratio"].as<float>();
        else
          s.ratio = 0.5f;

        const YAML::Node children = n["children"];
        if (!children.IsSequence() || children.size() != 2) {
          visualizersBroken = broken = true;
          return nullptr;
        }

        // parse two children
        s.primary = parseHierarchyTree(children[0], grp);
        s.secondary = parseHierarchyTree(children[1], grp);
      } catch (...) {
        visualizersBroken = broken = true;
        return nullptr;
      }

      return std::make_shared<WindowManager::Variant>(std::move(s));
    }

    visualizersBroken = broken = true;
    return nullptr;
  };

  out.clear();
  for (const auto& item : value) {
    const std::string itemKey = item.first.as<std::string>();
    out[itemKey] = std::move(parseHierarchyTree(item.second, itemKey));
  }
}

/**
 * @brief Read a map of plugin configuration options from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed hierarchy tree map
 */
template <>
void get<std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher>>(
    const YAML::Node& root, std::string_view path,
    std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher>& out) {
  auto node = getNode(root, path);

  if (!node.has_value() || !node.value().IsMap()) {
    broken = true;
    return;
  }

  const YAML::Node& value = node.value();

  std::lock_guard<std::mutex> lock(pluginOptionsMutex);

  for (auto& [key, rec] : out) {
    std::string path = key.first + "." + key.second;

    switch (rec.spec.type) {
    case PluginConfigType::Bool: {
      bool tmp = std::get<bool>(rec.value);
      get<bool>(value, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::Int: {
      int tmp = std::get<int>(rec.value);
      get<int>(value, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::Float: {
      float tmp = std::get<float>(rec.value);
      get<float>(value, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::String: {
      std::string tmp = std::get<std::string>(rec.value);
      get<std::string>(value, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    }
  }
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

  PluginOptionKey key {pluginKey, keyPath};
  std::lock_guard<std::mutex> lock(pluginOptionsMutex);

  auto it = pluginOptions.find(key);
  if (it != pluginOptions.end() && !it->second.spec.label.empty()) {
    LOG_ERROR("Duplicate plugin config option registration for 'plugins." << pluginKey << "." << keyPath << "'");
    return false;
  }

  PluginOptionRecord& rec = pluginOptions[key];
  // merge descriptor
  rec.spec = descriptor ? *descriptor : PluginConfigSpec {};
  rec.spec.type = static_cast<PluginConfigType>(defaultValue->index());
  if (rec.spec.label.empty())
    rec.spec.label = keyPath;

  if (!plugin->displayName.empty())
    rec.displayName = plugin->displayName;
  else if (rec.displayName.empty())
    rec.displayName = pluginKey;

  if (it != pluginOptions.end()) {
    const auto& existing = it->second.value;
    if (existing.index() == static_cast<size_t>(rec.spec.type))
      rec.value = existing;
    else
      rec.value = *defaultValue;
  } else {
    rec.value = *defaultValue;
  }

  return true;
}

bool getPluginConfigOption(void* pluginContext, const char* path, Config::PluginConfigValue* outValue) {
  if (!pluginContext || !outValue)
    return false;
  const std::string pluginKey = static_cast<Plugin::PluginInstance*>(pluginContext)->key;
  PluginOptionKey key {pluginKey, std::string(path)};
  std::lock_guard<std::mutex> lock(pluginOptionsMutex);
  auto it = pluginOptions.find(key);
  if (it == pluginOptions.end())
    return false;

  *outValue = it->second.value;
  return true;
}

bool setPluginConfigOption(void* pluginContext, const char* path, const PluginConfigValue* value) {
  if (!pluginContext || !value)
    return false;
  const std::string pluginKey = static_cast<Plugin::PluginInstance*>(pluginContext)->key;
  PluginOptionKey key {pluginKey, std::string(path)};
  std::lock_guard<std::mutex> lock(pluginOptionsMutex);
  auto it = pluginOptions.find(key);
  if (it == pluginOptions.end())
    return false;
  if (static_cast<PluginConfigType>(value->index()) != it->second.spec.type)
    return false;

  it->second.value = *value;
  return true;
}

void rollBackup() {
  namespace fs = std::filesystem;
  static std::string basePath = expandUserPath("~/.config/pulse-visualizer/config.yml");
  fs::path configPath = fs::path(basePath);
  fs::path dirPath = configPath.parent_path() / "backups";

  fs::create_directory(dirPath);

  // Shift backups
  for (int i = 10; i >= 1; i--) {
    fs::path oldPath = dirPath / ("config.bak." + std::to_string(i));
    fs::path newPath = dirPath / ("config.bak." + std::to_string(i + 1));

    if (fs::exists(oldPath)) {
      if (i == 10)
        fs::remove(oldPath);
      else
        fs::rename(oldPath, newPath);
    }
  }

  // Copy current
  if (fs::exists(configPath)) {
    fs::path newPath = dirPath / "config.bak.1";
    fs::copy_file(configPath, newPath, fs::copy_options::overwrite_existing);
  }
}

void restoreBackup() {
  namespace fs = std::filesystem;
  static std::string basePath = expandUserPath("~/.config/pulse-visualizer/config.yml");
  fs::path configPath = fs::path(basePath);
  fs::path srcPath = configPath.parent_path() / "backups/config.bak.1";

  if (!fs::exists(srcPath))
    return;

  // Read backup to ram
  std::ifstream src(srcPath, std::ios::binary);
  if (!src)
    return;

  std::vector<char> buf((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
  src.close();

  // Backup current config
  rollBackup();

  // Write backup from ram
  std::ofstream dst(basePath, std::ios::binary);
  if (!dst)
    return;

  dst.write(buf.data(), buf.size());
  dst.close();
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
    return;
  }

  get<std::unordered_map<std::string, WindowManager::Node>>(configData, "visualizers", options.visualizers);

  get<std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher>>(configData, "plugins",
                                                                                      pluginOptions);

  ConfigSchema::forEachFieldByType(
      [&](const auto& field) { get<bool>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<int>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<float>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<std::string>(configData, field.path, field.get(options)); },
      [&](const auto& field) { get<Rotation>(configData, field.path, field.get(options)); });

  // If the config is broken, attempt to recover by merging defaults
  if (broken && !recovering) {
    LOG_ERROR("Config is broken, attempting to recover...");
    rollBackup();
    save();
    load(true);
  } else if (broken && recovering) {
    LOG_ERROR("Failed to recover config.");
    SDLWindow::running.store(false);
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
  // If the config was detected as broken, write a safe default layout
  // instead of persisting the invalid current layout.
  if (visualizersBroken) {
    YAML::Node defVis(YAML::NodeType::Map);
    YAML::Node mainNode(YAML::NodeType::Map);
    mainNode["type"] = "hsplit";
    mainNode["ratio"] = 0.5;
    YAML::Node children(YAML::NodeType::Sequence);
    YAML::Node c0(YAML::NodeType::Map);
    c0["id"] = "oscilloscope";
    children.push_back(c0);
    YAML::Node c1(YAML::NodeType::Map);
    c1["id"] = "spectrum_analyzer";
    children.push_back(c1);
    mainNode["children"] = children;
    defVis["main"] = mainNode;
    root["visualizers"] = defVis;
  } else {
    YAML::Node vis(YAML::NodeType::Map);
    std::function<YAML::Node(const WindowManager::Node&)> nodeToYaml;
    nodeToYaml = [&](const WindowManager::Node& node) -> YAML::Node {
      if (!node)
        return YAML::Node();
      // clang-format off
      return std::visit(Visitor {
        [&](std::shared_ptr<WindowManager::VisualizerWindow>& w) {
          YAML::Node m(YAML::NodeType::Map);
          m["id"] = w->id;
          return m;
        },
        [&](WindowManager::Splitter& s) {
          YAML::Node m(YAML::NodeType::Map);
          m["type"] = (s.orientation == WindowManager::Orientation::Vertical) ? "vsplit" : "hsplit";
          m["ratio"] = s.ratio;
          YAML::Node children(YAML::NodeType::Sequence);
          children.push_back(nodeToYaml(s.primary));
          children.push_back(nodeToYaml(s.secondary));
          m["children"] = children;
          return m;
        }
      }, *node);
      // clang-format on
    };

    for (const auto& [groupName, nodePtr] : options.visualizers) {
      vis[groupName] = nodeToYaml(nodePtr);
    }
    root["visualizers"] = vis;
  }

  // Persist plugin options from the consolidated map
  {
    YAML::Node pluginsNode(YAML::NodeType::Map);
    std::lock_guard<std::mutex> lock(pluginOptionsMutex);
    for (const auto& [key, rec] : pluginOptions) {
      YAML::Node pluginNode = pluginsNode[key.first];
      std::visit([&](const auto& v) { setNodeByPath(pluginNode, key.second, v); }, rec.value);
      pluginsNode[key.first] = pluginNode;
    }
    if (pluginsNode.IsDefined() && pluginsNode.size() > 0)
      root["plugins"] = pluginsNode;
  }

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