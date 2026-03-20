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
    logWarnAt(std::source_location::current(), "USERPROFILE environment variable not set, cannot setup user config");
#else
    logWarnAt(std::source_location::current(), "HOME environment variable not set, cannot setup user config");
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
        logDebug("Created user config file: {}/config.yml", userCfgDir);
      } catch (const std::exception& e) {
        logWarnAt(std::source_location::current(), "Failed to copy config template: {}", e.what());
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
      logDebug("Copied themes to: {}", userThemeDir);
    } catch (const std::exception& e) {
      logWarnAt(std::source_location::current(), "Failed to copy themes: {}", e.what());
    }
  }

  // Copy font file
  std::string fontSource = installDir + "/fonts/JetBrainsMonoNerdFont-Medium.ttf";
  std::string userFontFile = userFontDir + "/JetBrainsMonoNerdFont-Medium.ttf";
  if (std::filesystem::exists(fontSource)) {
    try {
      if (!std::filesystem::exists(userFontFile)) {
        std::filesystem::copy_file(fontSource, userFontFile);
        logDebug("Copied font to: {}", userFontFile);
      }
    } catch (const std::exception& e) {
      logWarnAt(std::source_location::current(), "Failed to copy font: {}", e.what());
    }
  }
}

YAML::Node getNode(const YAML::Node& root, std::string_view path) {
  const std::string pathStr(path);
  if (root[pathStr].IsDefined())
    return root[pathStr];

  const size_t dotIndex = path.find('.');
  if (dotIndex == std::string_view::npos) {
    throw makeErrorAt(std::source_location::current(), "Path '{}' not found", path);
  }

  const std::string section(path.substr(0, dotIndex));
  if (!root[section].IsDefined()) {
    throw makeErrorAt(std::source_location::current(), "Section '{}' missing from path '{}'", section, path);
  }

  const std::string_view subpath = path.substr(dotIndex + 1);
  return getNode(root[section], subpath);
}

/**
 * @brief Read a value from the configuration.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <typename T> void get(const YAML::Node& root, std::string_view path, T& out) {
  constexpr auto typeName = std::is_same_v<T, std::string> ? "string"
                            : std::is_same_v<T, int>       ? "int"
                            : std::is_same_v<T, float>     ? "float"
                            : std::is_same_v<T, Rotation>  ? "Rotation"
                                                           : "unknown";

  YAML::Node node;

  try {
    node = getNode(root, path);
    T value = node.as<T>();
    out = value;
    return;
  } catch (const typename YAML::TypedBadConversion<T>& e) {
    throw makeErrorAt(std::source_location::current(), "Type mismatch at config.yml:{} ({}): expected {}, got {}",
                      e.mark.line + 1, path, typeName, node.Tag());
  } catch (const YAML::InvalidNode& e) {
    throw makeErrorAt(std::source_location::current(), "Invalid node access at config.yml:{} ({})", e.mark.line + 1,
                      path);
  } catch (const YAML::Exception& e) {
    throw makeErrorAt(std::source_location::current(), "YAML error at config.yml:{} ({}): {}", path, e.mark.line + 1,
                      e.msg);
  }
}

/**
 * @brief Read a boolean from the configuration with permissive conversions.
 * @param root Root YAML node
 * @param path Dot-separated path to the desired value
 * @param out Output reference to receive the parsed value
 */
template <> void get<bool>(const YAML::Node& root, std::string_view path, bool& out) {
  YAML::Node node = getNode(root, path);

  if (node.IsScalar()) {
    std::string str = node.as<std::string>();
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);

    if (str == "true" || str == "yes" || str == "on" || str == "1") {
      out = true;
      return;
    }
    if (str == "false" || str == "no" || str == "off" || str == "0") {
      out = false;
      return;
    }
  }

  throw makeErrorAt(std::source_location::current(), "Converting {} to boolean failed", path);
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
  YAML::Node node = getNode(root, path);

  if (!node.IsMap())
    throw makeErrorAt(std::source_location::current(), "{} is not a map", path);

  std::function<WindowManager::Node(const YAML::Node&, const std::string_view, std::string)> parseHierarchyTree;
  parseHierarchyTree = [&](const YAML::Node& n, const std::string_view grp,
                           std::string subPath) -> WindowManager::Node {
    if (!n || n.IsNull())
      throw makeErrorAt(std::source_location::current(), "Node at {}.{} is null or undefined", path, subPath);

    if (n.IsScalar())
      return std::make_shared<WindowManager::Variant>(VisualizerRegistry::find(n.as<std::string>()));

    if (!n.IsMap())
      throw makeErrorAt(std::source_location::current(), "Expected map node at {}.{}", path, subPath);

    // Branch by explicit id
    if (n["id"] && n["id"].IsScalar())
      return std::make_shared<WindowManager::Variant>(VisualizerRegistry::find(n["id"].as<std::string>()));

    // Split node: require 'type' and 'children'
    if (!n["type"] || !n["children"] || !n["children"].IsSequence())
      throw makeErrorAt(std::source_location::current(), "missing 'type' and 'children' at {}.{}", path, subPath);

    WindowManager::Splitter s {};
    s.orientation = (n["type"].as<std::string>() == "hsplit") ? WindowManager::Orientation::Horizontal
                                                              : WindowManager::Orientation::Vertical;

    if (n["ratio"] && n["ratio"].IsScalar())
      s.ratio = n["ratio"].as<float>();
    else
      s.ratio = 0.5f;

    const YAML::Node children = n["children"];
    if (children.size() != 2) {
      throw makeErrorAt(std::source_location::current(), "'children' must contain exactly 2 nodes, got {} at {}.{}",
                        children.size(), path, subPath);
    }

    // parse two children
    s.primary = parseHierarchyTree(children[0], grp, subPath + ".children[0]");
    s.secondary = parseHierarchyTree(children[1], grp, subPath + ".children[1]");

    return std::make_shared<WindowManager::Variant>(std::move(s));
  };

  std::decay_t<decltype(out)> temp;
  for (const auto& item : node) {
    const std::string itemKey = item.first.as<std::string>();
    temp[itemKey] = std::move(parseHierarchyTree(item.second, itemKey, itemKey));
  }
  out = std::move(temp);
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

  if (!node.IsMap())
    throw makeErrorAt(std::source_location::current(), "{} is not a map", path);

  std::lock_guard<std::mutex> lock(pluginOptionsMutex);

  for (auto& [key, rec] : out) {
    std::string path = key.first + "." + key.second;

    switch (rec.spec.type) {
    case PluginConfigType::Bool: {
      bool tmp = std::get<bool>(rec.value);
      get<bool>(node, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::Int: {
      int tmp = std::get<int>(rec.value);
      get<int>(node, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::Float: {
      float tmp = std::get<float>(rec.value);
      get<float>(node, path, tmp);
      rec.value = PluginConfigValue(tmp);
      break;
    }
    case PluginConfigType::String: {
      std::string tmp = std::get<std::string>(rec.value);
      get<std::string>(node, path, tmp);
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
    logWarnAt(std::source_location::current(), "Duplicate plugin config option registration for 'plugins.{}.{}'",
              pluginKey, keyPath);
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

inline void tryRestore() {
  logWarnAt(std::source_location::current(), "Trying build working config");

  rollBackup();
  save();
  load(true);
}

void load(bool recovering) {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");
  YAML::Node configData;

#ifdef __linux__
  // Setup file watching for configuration changes
  if (inotifyFd == -1) {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyWatch != -1)
      inotify_rm_watch(inotifyFd, inotifyWatch);
    inotifyWatch = inotify_add_watch(inotifyFd, path.c_str(), IN_CLOSE_WRITE);
  }
#endif

  // Handle YAML errors
  try {
    configData = YAML::LoadFile(path);
  } catch (const YAML::BadFile&) {
    logWarnAt(std::source_location::current(), "Failed to load config file");
  } catch (const YAML::ParserException& e) {
    logWarnAt(std::source_location::current(), "Parser error when loading config: '{}' at {}:{}", e.msg,
              e.mark.line + 1, e.mark.column + 1);
    tryRestore();
    return;
  }

  try {
    get<std::unordered_map<std::string, WindowManager::Node>>(configData, "visualizers", options.visualizers);

    get<std::unordered_map<PluginOptionKey, PluginOptionRecord, PluginOptionKeyHasher>>(configData, "plugins",
                                                                                        pluginOptions);

    ConfigSchema::forEachFieldByType(
        [&](const auto& field) { get<bool>(configData, field.path, field.get(options)); },
        [&](const auto& field) { get<int>(configData, field.path, field.get(options)); },
        [&](const auto& field) { get<float>(configData, field.path, field.get(options)); },
        [&](const auto& field) { get<std::string>(configData, field.path, field.get(options)); },
        [&](const auto& field) { get<Rotation>(configData, field.path, field.get(options)); });
  } catch (const std::exception& e) {
    // if already recovering, rethrow
    if (recovering)
      throw e;

    logWarnAt(std::source_location::current(), "Config load failed: {}", e.what());
    tryRestore();
  }
}

void save() {
  static std::string path = expandUserPath("~/.config/pulse-visualizer/config.yml");

  YAML::Node root;

  ConfigSchema::forEachFieldByType(
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, field.getConst(options)); },
      [&](const auto& field) { setNodeByPath(root, field.path, static_cast<int>(field.getConst(options))); });

  // Visualizers
  try {
    YAML::Node vis(YAML::NodeType::Map);
    std::function<YAML::Node(const WindowManager::Node&)> nodeToYaml;
    nodeToYaml = [&](const WindowManager::Node& node) -> YAML::Node {
      if (!node)
        throw makeErrorAt(std::source_location::current(), "Invalid node");
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
  } catch (...) {
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
  if (!fout.is_open())
    throw makeErrorAt(std::source_location::current(), "Failed to open config file for writing");

  fout << out.c_str();

  if (fout.fail())
    throw makeErrorAt(std::source_location::current(), "Failed to write to config file");

  fout.close();
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
    logWarnAt(std::source_location::current(), "Could not stat config file.");
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