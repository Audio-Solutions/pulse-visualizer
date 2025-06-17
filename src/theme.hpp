#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <memory>

namespace Theme {
    // Color structure to hold RGBA values
    struct Color {
        float r, g, b, a;
        
        // Default constructor
        constexpr Color() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
        
        constexpr Color(float r, float g, float b, float a = 1.0f)
            : r(r), g(g), b(b), a(a) {}
            
        // Convert to array for OpenGL functions
        constexpr std::array<float, 4> toArray() const {
            return {r, g, b, a};
        }
    };

    // Theme structure to hold all theme-related colors and properties
    struct ThemeDefinition {
        // Core colors
        Color background;
        Color grid;
        Color visualizer;
        Color splitter;
        Color text;
        
        // Semantic colors
        Color accent;
        Color warning;
        Color error;
        Color success;
        
        // UI element colors
        Color button;
        Color buttonHover;
        Color buttonActive;
        Color border;
        Color scrollbar;
        Color scrollbarHover;
        
        // Visualization specific colors
        Color waveform;
        Color spectrum;
        Color lissajous;
        Color oscilloscope;

        // Default constructor
        constexpr ThemeDefinition() = default;
    };

    // Predefined themes
    namespace Themes {
        // Catppuccin Mocha theme
        constexpr ThemeDefinition MOCHA = {
            // Core colors
            .background = Color(24.0f/255.0f, 24.0f/255.0f, 37.0f/255.0f),    // Mantle
            .grid = Color(49.0f/255.0f, 50.0f/255.0f, 68.0f/255.0f),         // Surface0
            .visualizer = Color(203.0f/255.0f, 166.0f/255.0f, 247.0f/255.0f), // Mauve
            .splitter = Color(49.0f/255.0f, 50.0f/255.0f, 68.0f/255.0f),     // Surface0
            .text = Color(205.0f/255.0f, 214.0f/255.0f, 244.0f/255.0f),      // Text
            
            // Semantic colors
            .accent = Color(137.0f/255.0f, 180.0f/255.0f, 250.0f/255.0f),    // Blue
            .warning = Color(250.0f/255.0f, 179.0f/255.0f, 135.0f/255.0f),   // Peach
            .error = Color(243.0f/255.0f, 139.0f/255.0f, 168.0f/255.0f),     // Red
            .success = Color(166.0f/255.0f, 227.0f/255.0f, 161.0f/255.0f),   // Green
            
            // UI element colors
            .button = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),       // Surface1
            .buttonHover = Color(88.0f/255.0f, 91.0f/255.0f, 112.0f/255.0f), // Surface2
            .buttonActive = Color(137.0f/255.0f, 180.0f/255.0f, 250.0f/255.0f), // Blue
            .border = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),       // Surface1
            .scrollbar = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),    // Surface1
            .scrollbarHover = Color(88.0f/255.0f, 91.0f/255.0f, 112.0f/255.0f), // Surface2
            
            // Visualization specific colors
            .waveform = Color(203.0f/255.0f, 166.0f/255.0f, 247.0f/255.0f),  // Mauve
            .spectrum = Color(137.0f/255.0f, 180.0f/255.0f, 250.0f/255.0f),  // Blue
            .lissajous = Color(166.0f/255.0f, 227.0f/255.0f, 161.0f/255.0f), // Green
            .oscilloscope = Color(250.0f/255.0f, 179.0f/255.0f, 135.0f/255.0f) // Peach
        };

        // Catppuccin Latte theme
        constexpr ThemeDefinition LATTE = {
            // Core colors
            .background = Color(239.0f/255.0f, 241.0f/255.0f, 245.0f/255.0f), // Base
            .grid = Color(220.0f/255.0f, 224.0f/255.0f, 232.0f/255.0f),      // Surface0
            .visualizer = Color(136.0f/255.0f, 57.0f/255.0f, 239.0f/255.0f), // Mauve
            .splitter = Color(220.0f/255.0f, 224.0f/255.0f, 232.0f/255.0f),  // Surface0
            .text = Color(76.0f/255.0f, 79.0f/255.0f, 105.0f/255.0f),        // Text
            
            // Semantic colors
            .accent = Color(30.0f/255.0f, 102.0f/255.0f, 245.0f/255.0f),     // Blue
            .warning = Color(254.0f/255.0f, 100.0f/255.0f, 11.0f/255.0f),    // Peach
            .error = Color(210.0f/255.0f, 15.0f/255.0f, 57.0f/255.0f),       // Red
            .success = Color(64.0f/255.0f, 160.0f/255.0f, 43.0f/255.0f),     // Green
            
            // UI element colors
            .button = Color(220.0f/255.0f, 224.0f/255.0f, 232.0f/255.0f),    // Surface0
            .buttonHover = Color(230.0f/255.0f, 233.0f/255.0f, 239.0f/255.0f), // Surface1
            .buttonActive = Color(30.0f/255.0f, 102.0f/255.0f, 245.0f/255.0f), // Blue
            .border = Color(220.0f/255.0f, 224.0f/255.0f, 232.0f/255.0f),    // Surface0
            .scrollbar = Color(220.0f/255.0f, 224.0f/255.0f, 232.0f/255.0f), // Surface0
            .scrollbarHover = Color(230.0f/255.0f, 233.0f/255.0f, 239.0f/255.0f), // Surface1
            
            // Visualization specific colors
            .waveform = Color(136.0f/255.0f, 57.0f/255.0f, 239.0f/255.0f),   // Mauve
            .spectrum = Color(30.0f/255.0f, 102.0f/255.0f, 245.0f/255.0f),   // Blue
            .lissajous = Color(64.0f/255.0f, 160.0f/255.0f, 43.0f/255.0f),   // Green
            .oscilloscope = Color(254.0f/255.0f, 100.0f/255.0f, 11.0f/255.0f) // Peach
        };
    }

    // Theme manager class to handle theme switching and access
    class ThemeManager {
    public:
        static void initialize() {
            if (availableThemes.empty()) {
                registerTheme("mocha", Themes::MOCHA);
                registerTheme("latte", Themes::LATTE);
            }
        }

        static void setTheme(const std::string& name) {
            auto it = availableThemes.find(name);
            if (it != availableThemes.end()) {
                currentTheme = it->second;
            }
        }

        static const ThemeDefinition& getCurrentTheme() {
            return currentTheme;
        }

        static void registerTheme(const std::string& name, const ThemeDefinition& theme) {
            availableThemes[name] = theme;
        }

        // Convenience methods to access current theme colors
        static const Color& getBackground() { return currentTheme.background; }
        static const Color& getGrid() { return currentTheme.grid; }
        static const Color& getVisualizer() { return currentTheme.visualizer; }
        static const Color& getSplitter() { return currentTheme.splitter; }
        static const Color& getText() { return currentTheme.text; }
        static const Color& getAccent() { return currentTheme.accent; }
        static const Color& getWarning() { return currentTheme.warning; }
        static const Color& getError() { return currentTheme.error; }
        static const Color& getSuccess() { return currentTheme.success; }

    private:
        static ThemeDefinition currentTheme;
        static std::unordered_map<std::string, ThemeDefinition> availableThemes;
    };
} 