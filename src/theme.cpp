#include "theme.hpp"

#include <map>
#include <unordered_map>

namespace Theme {

// Define static members
ThemeDefinition ThemeManager::currentTheme = Themes::MOCHA;
std::unordered_map<std::string, ThemeDefinition> ThemeManager::availableThemes;

} // namespace Theme