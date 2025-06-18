#include "theme.hpp"
#include <map>
#include <unordered_map>

namespace Theme {

// Define static members
ThemeDefinition ThemeManager::currentTheme = Themes::BREEZE_LIGHT;
std::unordered_map<std::string, ThemeDefinition> ThemeManager::availableThemes;

} 