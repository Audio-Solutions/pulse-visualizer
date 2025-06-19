#include "config.hpp"

#include "json.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

std::string expandUserPath(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

nlohmann::json Config::configData;
std::string Config::configPath = "~/.config/pulse-visualizer/config.json";
std::unordered_map<std::string, nlohmann::json> Config::configCache;
time_t Config::lastConfigMTime = 0;
size_t Config::configVersion = 0;

void Config::clearCache() { configCache.clear(); }

void Config::load(const std::string& filename) {
  configPath = filename;
  std::string expanded = expandUserPath(filename);
  std::ifstream in(expanded);
  if (!in) {
    std::cerr << "Failed to open config file: " << expanded << std::endl;
    std::terminate();
  }
  in >> configData;
  clearCache();
  // Update last modification time
  struct stat st;
  if (stat(expanded.c_str(), &st) == 0) {
    lastConfigMTime = st.st_mtime;
  }
  configVersion++; // Increment version on every reload
}

void Config::reload() { load(configPath); }

bool Config::reloadIfChanged() {
  std::string expanded = expandUserPath(configPath);
  struct stat st;
  if (stat(expanded.c_str(), &st) != 0) {
    std::cerr << "Failed to stat config file: " << expanded << std::endl;
    std::terminate();
  }
  if (st.st_mtime != lastConfigMTime) {
    load(configPath);
    return true;
  }
  return false;
}

// Helper to traverse dot-separated keys
nlohmann::json& Config::getJsonRef(const std::string& key) {
  nlohmann::json* ref = &configData;
  std::stringstream ss(key);
  std::string segment;
  while (std::getline(ss, segment, '.')) {
    if (!ref->contains(segment) || (*ref)[segment].is_null()) {
      std::cerr << "Config error: key missing or null: " << key << std::endl;
      std::terminate();
    }
    ref = &(*ref)[segment];
  }
  return *ref;
}

int Config::getInt(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_number_integer()) {
      std::cerr << "Config error: key '" << key << "' is not an integer (cached)." << std::endl;
      std::terminate();
    }
    return it->second.get<int>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_number_integer()) {
    std::cerr << "Config error: key '" << key << "' is not an integer." << std::endl;
    std::terminate();
  }
  configCache[key] = val;
  return val.get<int>();
}

float Config::getFloat(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_number()) {
      std::cerr << "Config error: key '" << key << "' is not a number (cached)." << std::endl;
      std::terminate();
    }
    return it->second.get<float>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_number()) {
    std::cerr << "Config error: key '" << key << "' is not a number." << std::endl;
    std::terminate();
  }
  configCache[key] = val;
  return val.get<float>();
}

bool Config::getBool(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_boolean()) {
      std::cerr << "Config error: key '" << key << "' is not a boolean (cached)." << std::endl;
      std::terminate();
    }
    return it->second.get<bool>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_boolean()) {
    std::cerr << "Config error: key '" << key << "' is not a boolean." << std::endl;
    std::terminate();
  }
  configCache[key] = val;
  return val.get<bool>();
}

std::string Config::getString(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    if (!it->second.is_string()) {
      std::cerr << "Config error: key '" << key << "' is not a string (cached)." << std::endl;
      std::terminate();
    }
    return it->second.get<std::string>();
  }
  nlohmann::json& val = getJsonRef(key);
  if (!val.is_string()) {
    std::cerr << "Config error: key '" << key << "' is not a string." << std::endl;
    std::terminate();
  }
  configCache[key] = val;
  return val.get<std::string>();
}

nlohmann::json Config::get(const std::string& key) {
  auto it = configCache.find(key);
  if (it != configCache.end()) {
    return it->second;
  }
  nlohmann::json& val = getJsonRef(key);
  configCache[key] = val;
  return val;
}

size_t Config::getVersion() { return configVersion; }