#pragma once
#include "json.hpp"

#include <ctime>
#include <string>
#include <unordered_map>

class Config {
public:
  static void load(const std::string& filename = "~/.config/pulse-visualizer/config.json");
  static void reload();
  static int getInt(const std::string& key);
  static float getFloat(const std::string& key);
  static bool getBool(const std::string& key);
  static std::string getString(const std::string& key);
  static nlohmann::json get(const std::string& key);
  static bool reloadIfChanged();
  static size_t getVersion();

private:
  static nlohmann::json configData;
  static std::string configPath;
  static std::unordered_map<std::string, nlohmann::json> configCache;
  static void clearCache();
  static nlohmann::json& getJsonRef(const std::string& key);
  static time_t lastConfigMTime;
  static size_t configVersion;
};
