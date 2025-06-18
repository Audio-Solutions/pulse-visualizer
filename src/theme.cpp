#include "theme.hpp"
#include <map>
#include <unordered_map>

namespace Theme {

// Define static members
ThemeDefinition ThemeManager::currentTheme = Themes::LATTE;
std::unordered_map<std::string, ThemeDefinition> ThemeManager::availableThemes;

} 