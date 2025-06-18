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

        // AMOLED theme (true black with vibrant accents)
        constexpr ThemeDefinition AMOLED = {
            // Core colors
            .background = Color(0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f),      // True black
            .grid = Color(30.0f/255.0f, 30.0f/255.0f, 30.0f/255.0f),         // Dark gray
            .visualizer = Color(255.0f/255.0f, 0.0f/255.0f, 255.0f/255.0f),  // Magenta
            .splitter = Color(30.0f/255.0f, 30.0f/255.0f, 30.0f/255.0f),     // Dark gray
            .text = Color(255.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f),      // White
            
            // Semantic colors
            .accent = Color(0.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f),      // Cyan
            .warning = Color(255.0f/255.0f, 165.0f/255.0f, 0.0f/255.0f),     // Orange
            .error = Color(255.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f),         // Red
            .success = Color(0.0f/255.0f, 255.0f/255.0f, 0.0f/255.0f),       // Lime
            
            // UI element colors
            .button = Color(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f),       // Darker gray
            .buttonHover = Color(60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f),  // Medium gray
            .buttonActive = Color(0.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f), // Cyan
            .border = Color(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f),       // Darker gray
            .scrollbar = Color(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f),    // Darker gray
            .scrollbarHover = Color(60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f), // Medium gray
            
            // Visualization specific colors
            .waveform = Color(255.0f/255.0f, 0.0f/255.0f, 255.0f/255.0f),    // Magenta
            .spectrum = Color(0.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f),    // Cyan
            .lissajous = Color(0.0f/255.0f, 255.0f/255.0f, 0.0f/255.0f),     // Lime
            .oscilloscope = Color(255.0f/255.0f, 165.0f/255.0f, 0.0f/255.0f) // Orange
        };

        // Catppuccin Frappe theme
        constexpr ThemeDefinition FRAPPE = {
            // Core colors
            .background = Color(48.0f/255.0f, 52.0f/255.0f, 70.0f/255.0f),   // Base
            .grid = Color(73.0f/255.0f, 77.0f/255.0f, 100.0f/255.0f),        // Surface0
            .visualizer = Color(202.0f/255.0f, 158.0f/255.0f, 230.0f/255.0f), // Mauve
            .splitter = Color(73.0f/255.0f, 77.0f/255.0f, 100.0f/255.0f),    // Surface0
            .text = Color(198.0f/255.0f, 208.0f/255.0f, 245.0f/255.0f),      // Text
            
            // Semantic colors
            .accent = Color(140.0f/255.0f, 170.0f/255.0f, 238.0f/255.0f),    // Blue
            .warning = Color(239.0f/255.0f, 159.0f/255.0f, 118.0f/255.0f),   // Peach
            .error = Color(231.0f/255.0f, 130.0f/255.0f, 132.0f/255.0f),     // Red
            .success = Color(166.0f/255.0f, 209.0f/255.0f, 137.0f/255.0f),   // Green
            
            // UI element colors
            .button = Color(81.0f/255.0f, 85.0f/255.0f, 109.0f/255.0f),      // Surface1
            .buttonHover = Color(92.0f/255.0f, 95.0f/255.0f, 119.0f/255.0f), // Surface2
            .buttonActive = Color(140.0f/255.0f, 170.0f/255.0f, 238.0f/255.0f), // Blue
            .border = Color(81.0f/255.0f, 85.0f/255.0f, 109.0f/255.0f),      // Surface1
            .scrollbar = Color(81.0f/255.0f, 85.0f/255.0f, 109.0f/255.0f),   // Surface1
            .scrollbarHover = Color(92.0f/255.0f, 95.0f/255.0f, 119.0f/255.0f), // Surface2
            
            // Visualization specific colors
            .waveform = Color(202.0f/255.0f, 158.0f/255.0f, 230.0f/255.0f),  // Mauve
            .spectrum = Color(140.0f/255.0f, 170.0f/255.0f, 238.0f/255.0f),  // Blue
            .lissajous = Color(166.0f/255.0f, 209.0f/255.0f, 137.0f/255.0f), // Green
            .oscilloscope = Color(239.0f/255.0f, 159.0f/255.0f, 118.0f/255.0f) // Peach
        };

        // Catppuccin Macchiato theme
        constexpr ThemeDefinition MACCHIATO = {
            // Core colors
            .background = Color(36.0f/255.0f, 39.0f/255.0f, 58.0f/255.0f),   // Base
            .grid = Color(54.0f/255.0f, 58.0f/255.0f, 79.0f/255.0f),         // Surface0
            .visualizer = Color(202.0f/255.0f, 158.0f/255.0f, 230.0f/255.0f), // Mauve
            .splitter = Color(54.0f/255.0f, 58.0f/255.0f, 79.0f/255.0f),     // Surface0
            .text = Color(202.0f/255.0f, 211.0f/255.0f, 245.0f/255.0f),      // Text
            
            // Semantic colors
            .accent = Color(138.0f/255.0f, 173.0f/255.0f, 244.0f/255.0f),    // Blue
            .warning = Color(250.0f/255.0f, 179.0f/255.0f, 135.0f/255.0f),   // Peach
            .error = Color(237.0f/255.0f, 135.0f/255.0f, 150.0f/255.0f),     // Red
            .success = Color(166.0f/255.0f, 218.0f/255.0f, 149.0f/255.0f),   // Green
            
            // UI element colors
            .button = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),       // Surface1
            .buttonHover = Color(88.0f/255.0f, 91.0f/255.0f, 112.0f/255.0f), // Surface2
            .buttonActive = Color(138.0f/255.0f, 173.0f/255.0f, 244.0f/255.0f), // Blue
            .border = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),       // Surface1
            .scrollbar = Color(69.0f/255.0f, 71.0f/255.0f, 90.0f/255.0f),    // Surface1
            .scrollbarHover = Color(88.0f/255.0f, 91.0f/255.0f, 112.0f/255.0f), // Surface2
            
            // Visualization specific colors
            .waveform = Color(202.0f/255.0f, 158.0f/255.0f, 230.0f/255.0f),  // Mauve
            .spectrum = Color(138.0f/255.0f, 173.0f/255.0f, 244.0f/255.0f),  // Blue
            .lissajous = Color(166.0f/255.0f, 218.0f/255.0f, 149.0f/255.0f), // Green
            .oscilloscope = Color(250.0f/255.0f, 179.0f/255.0f, 135.0f/255.0f) // Peach
        };

        // Tokyo Night theme
        constexpr ThemeDefinition TOKYO_NIGHT = {
            // Core colors
            .background = Color(26.0f/255.0f, 27.0f/255.0f, 38.0f/255.0f),   // Background
            .grid = Color(41.0f/255.0f, 42.0f/255.0f, 58.0f/255.0f),         // Darker
            .visualizer = Color(187.0f/255.0f, 154.0f/255.0f, 247.0f/255.0f), // Purple
            .splitter = Color(41.0f/255.0f, 42.0f/255.0f, 58.0f/255.0f),     // Darker
            .text = Color(169.0f/255.0f, 177.0f/255.0f, 214.0f/255.0f),      // Text
            
            // Semantic colors
            .accent = Color(122.0f/255.0f, 162.0f/255.0f, 247.0f/255.0f),    // Blue
            .warning = Color(255.0f/255.0f, 158.0f/255.0f, 100.0f/255.0f),   // Orange
            .error = Color(247.0f/255.0f, 118.0f/255.0f, 142.0f/255.0f),     // Red
            .success = Color(158.0f/255.0f, 206.0f/255.0f, 106.0f/255.0f),   // Green
            
            // UI element colors
            .button = Color(41.0f/255.0f, 42.0f/255.0f, 58.0f/255.0f),       // Darker
            .buttonHover = Color(65.0f/255.0f, 66.0f/255.0f, 82.0f/255.0f),  // Light
            .buttonActive = Color(122.0f/255.0f, 162.0f/255.0f, 247.0f/255.0f), // Blue
            .border = Color(41.0f/255.0f, 42.0f/255.0f, 58.0f/255.0f),       // Darker
            .scrollbar = Color(41.0f/255.0f, 42.0f/255.0f, 58.0f/255.0f),    // Darker
            .scrollbarHover = Color(65.0f/255.0f, 66.0f/255.0f, 82.0f/255.0f), // Light
            
            // Visualization specific colors
            .waveform = Color(187.0f/255.0f, 154.0f/255.0f, 247.0f/255.0f),  // Purple
            .spectrum = Color(122.0f/255.0f, 162.0f/255.0f, 247.0f/255.0f),  // Blue
            .lissajous = Color(158.0f/255.0f, 206.0f/255.0f, 106.0f/255.0f), // Green
            .oscilloscope = Color(255.0f/255.0f, 158.0f/255.0f, 100.0f/255.0f) // Orange
        };

        // Breeze Light theme (KDE Plasma)
        constexpr ThemeDefinition BREEZE_LIGHT = {
            // Core colors
            .background = Color(239.0f/255.0f, 240.0f/255.0f, 241.0f/255.0f), // Background
            .grid = Color(189.0f/255.0f, 191.0f/255.0f, 193.0f/255.0f),      // Grid
            .visualizer = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f), // Blue
            .splitter = Color(189.0f/255.0f, 191.0f/255.0f, 193.0f/255.0f),  // Grid
            .text = Color(35.0f/255.0f, 38.0f/255.0f, 41.0f/255.0f),         // Text
            
            // Semantic colors
            .accent = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),     // Blue
            .warning = Color(246.0f/255.0f, 116.0f/255.0f, 0.0f/255.0f),     // Orange
            .error = Color(237.0f/255.0f, 21.0f/255.0f, 21.0f/255.0f),       // Red
            .success = Color(39.0f/255.0f, 174.0f/255.0f, 96.0f/255.0f),     // Green
            
            // UI element colors
            .button = Color(252.0f/255.0f, 252.0f/255.0f, 252.0f/255.0f),    // Button
            .buttonHover = Color(239.0f/255.0f, 240.0f/255.0f, 241.0f/255.0f), // Button Hover
            .buttonActive = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f), // Button Active
            .border = Color(189.0f/255.0f, 191.0f/255.0f, 193.0f/255.0f),    // Border
            .scrollbar = Color(189.0f/255.0f, 191.0f/255.0f, 193.0f/255.0f), // Scrollbar
            .scrollbarHover = Color(149.0f/255.0f, 151.0f/255.0f, 153.0f/255.0f), // Scrollbar Hover
            
            // Visualization specific colors
            .waveform = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),   // Blue
            .spectrum = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),   // Blue
            .lissajous = Color(39.0f/255.0f, 174.0f/255.0f, 96.0f/255.0f),   // Green
            .oscilloscope = Color(246.0f/255.0f, 116.0f/255.0f, 0.0f/255.0f) // Orange
        };

        // Breeze Dark theme (KDE Plasma)
        constexpr ThemeDefinition BREEZE_DARK = {
            // Core colors
            .background = Color(35.0f/255.0f, 38.0f/255.0f, 41.0f/255.0f),   // Background
            .grid = Color(49.0f/255.0f, 54.0f/255.0f, 59.0f/255.0f),         // Grid
            .visualizer = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f), // Blue
            .splitter = Color(49.0f/255.0f, 54.0f/255.0f, 59.0f/255.0f),     // Grid
            .text = Color(239.0f/255.0f, 240.0f/255.0f, 241.0f/255.0f),      // Text
            
            // Semantic colors
            .accent = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),     // Blue
            .warning = Color(246.0f/255.0f, 116.0f/255.0f, 0.0f/255.0f),     // Orange
            .error = Color(237.0f/255.0f, 21.0f/255.0f, 21.0f/255.0f),       // Red
            .success = Color(39.0f/255.0f, 174.0f/255.0f, 96.0f/255.0f),     // Green
            
            // UI element colors
            .button = Color(49.0f/255.0f, 54.0f/255.0f, 59.0f/255.0f),       // Button
            .buttonHover = Color(55.0f/255.0f, 60.0f/255.0f, 65.0f/255.0f),  // Button Hover
            .buttonActive = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f), // Button Active
            .border = Color(49.0f/255.0f, 54.0f/255.0f, 59.0f/255.0f),       // Border
            .scrollbar = Color(49.0f/255.0f, 54.0f/255.0f, 59.0f/255.0f),    // Scrollbar
            .scrollbarHover = Color(55.0f/255.0f, 60.0f/255.0f, 65.0f/255.0f), // Scrollbar Hover
            
            // Visualization specific colors
            .waveform = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),   // Blue
            .spectrum = Color(61.0f/255.0f, 174.0f/255.0f, 233.0f/255.0f),   // Blue
            .lissajous = Color(39.0f/255.0f, 174.0f/255.0f, 96.0f/255.0f),   // Green
            .oscilloscope = Color(246.0f/255.0f, 116.0f/255.0f, 0.0f/255.0f) // Orange
        };

        // Windows Light theme
        constexpr ThemeDefinition WINDOWS_LIGHT = {
            // Core colors
            .background = Color(255.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f), // White
            .grid = Color(240.0f/255.0f, 240.0f/255.0f, 240.0f/255.0f),      // Light Gray
            .visualizer = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),  // Windows Blue
            .splitter = Color(240.0f/255.0f, 240.0f/255.0f, 240.0f/255.0f),  // Light Gray
            .text = Color(0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f),            // Black
            
            // Semantic colors
            .accent = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),      // Windows Blue
            .warning = Color(255.0f/255.0f, 140.0f/255.0f, 0.0f/255.0f),     // Orange
            .error = Color(232.0f/255.0f, 17.0f/255.0f, 35.0f/255.0f),       // Red
            .success = Color(0.0f/255.0f, 153.0f/255.0f, 68.0f/255.0f),      // Green
            
            // UI element colors
            .button = Color(240.0f/255.0f, 240.0f/255.0f, 240.0f/255.0f),    // Light Gray
            .buttonHover = Color(230.0f/255.0f, 230.0f/255.0f, 230.0f/255.0f), // Hover Gray
            .buttonActive = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f), // Windows Blue
            .border = Color(240.0f/255.0f, 240.0f/255.0f, 240.0f/255.0f),    // Light Gray
            .scrollbar = Color(240.0f/255.0f, 240.0f/255.0f, 240.0f/255.0f), // Light Gray
            .scrollbarHover = Color(230.0f/255.0f, 230.0f/255.0f, 230.0f/255.0f), // Hover Gray
            
            // Visualization specific colors
            .waveform = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),    // Windows Blue
            .spectrum = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),    // Windows Blue
            .lissajous = Color(0.0f/255.0f, 153.0f/255.0f, 68.0f/255.0f),    // Green
            .oscilloscope = Color(255.0f/255.0f, 140.0f/255.0f, 0.0f/255.0f) // Orange
        };

        // Windows Dark theme
        constexpr ThemeDefinition WINDOWS_DARK = {
            // Core colors
            .background = Color(32.0f/255.0f, 32.0f/255.0f, 32.0f/255.0f),   // Dark Gray
            .grid = Color(45.0f/255.0f, 45.0f/255.0f, 45.0f/255.0f),         // Slightly Lighter Gray
            .visualizer = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),  // Windows Blue
            .splitter = Color(45.0f/255.0f, 45.0f/255.0f, 45.0f/255.0f),     // Slightly Lighter Gray
            .text = Color(255.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f),      // White
            
            // Semantic colors
            .accent = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),      // Windows Blue
            .warning = Color(255.0f/255.0f, 140.0f/255.0f, 0.0f/255.0f),     // Orange
            .error = Color(232.0f/255.0f, 17.0f/255.0f, 35.0f/255.0f),       // Red
            .success = Color(0.0f/255.0f, 153.0f/255.0f, 68.0f/255.0f),      // Green
            
            // UI element colors
            .button = Color(45.0f/255.0f, 45.0f/255.0f, 45.0f/255.0f),       // Slightly Lighter Gray
            .buttonHover = Color(55.0f/255.0f, 55.0f/255.0f, 55.0f/255.0f),  // Hover Gray
            .buttonActive = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f), // Windows Blue
            .border = Color(45.0f/255.0f, 45.0f/255.0f, 45.0f/255.0f),       // Slightly Lighter Gray
            .scrollbar = Color(45.0f/255.0f, 45.0f/255.0f, 45.0f/255.0f),    // Slightly Lighter Gray
            .scrollbarHover = Color(55.0f/255.0f, 55.0f/255.0f, 55.0f/255.0f), // Hover Gray
            
            // Visualization specific colors
            .waveform = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),    // Windows Blue
            .spectrum = Color(0.0f/255.0f, 120.0f/255.0f, 215.0f/255.0f),    // Windows Blue
            .lissajous = Color(0.0f/255.0f, 153.0f/255.0f, 68.0f/255.0f),    // Green
            .oscilloscope = Color(255.0f/255.0f, 140.0f/255.0f, 0.0f/255.0f) // Orange
        };

        // Dayfox Theme (Light Theme)
        constexpr ThemeDefinition DAYFOX = {
            // Core colors
            .background = Color(242.0f/255.0f, 233.0f/255.0f, 225.0f/255.0f), // Primary UI Background (#F2E9E1)
            .grid = Color(246.0f/255.0f, 242.0f/255.0f, 238.0f/255.0f),      // General Background (#F6F2EE)
            .visualizer = Color(61.0f/255.0f, 43.0f/255.0f, 90.0f/255.0f),   // Accent (#3D2B5A)
            .splitter = Color(242.0f/255.0f, 233.0f/255.0f, 225.0f/255.0f),  // Divider (#F2E9E1)
            .text = Color(53.0f/255.0f, 44.0f/255.0f, 36.0f/255.0f),         // Secondary Text (#352C24)
            
            // Semantic colors
            .accent = Color(61.0f/255.0f, 43.0f/255.0f, 90.0f/255.0f),       // Accent (#3D2B5A)
            .warning = Color(165.0f/255.0f, 34.0f/255.0f, 47.0f/255.0f),     // Upvoted (#A5222F)
            .error = Color(72.0f/255.0f, 99.0f/255.0f, 182.0f/255.0f),       // Downvoted (#4863B6)
            .success = Color(87.0f/255.0f, 127.0f/255.0f, 99.0f/255.0f),     // Moderator Name (#577F63)
            
            // UI element colors
            .button = Color(61.0f/255.0f, 43.0f/255.0f, 90.0f/255.0f),       // Accent (#3D2B5A)
            .buttonHover = Color(72.0f/255.0f, 141.0f/255.0f, 147.0f/255.0f), // Current User Name (#488D93)
            .buttonActive = Color(72.0f/255.0f, 141.0f/255.0f, 147.0f/255.0f), // Current User Name (#488D93)
            .border = Color(242.0f/255.0f, 233.0f/255.0f, 225.0f/255.0f),    // Divider (#F2E9E1)
            .scrollbar = Color(72.0f/255.0f, 141.0f/255.0f, 147.0f/255.0f),  // Flair Background (#488D93)
            .scrollbarHover = Color(87.0f/255.0f, 127.0f/255.0f, 99.0f/255.0f), // Moderator Name (#577F63)
            
            // Visualization specific colors
            .waveform = Color(61.0f/255.0f, 43.0f/255.0f, 90.0f/255.0f),     // Accent (#3D2B5A)
            .spectrum = Color(72.0f/255.0f, 141.0f/255.0f, 147.0f/255.0f),   // Flair Background (#488D93)
            .lissajous = Color(87.0f/255.0f, 127.0f/255.0f, 99.0f/255.0f),   // Moderator Name (#577F63)
            .oscilloscope = Color(165.0f/255.0f, 34.0f/255.0f, 47.0f/255.0f) // Upvoted (#A5222F)
        };

        // Nightfox Theme (Dark Theme)
        constexpr ThemeDefinition NIGHTFOX = {
            // Core colors
            .background = Color(43.0f/255.0f, 59.0f/255.0f, 81.0f/255.0f),   // Primary UI Background (#2B3B51)
            .grid = Color(25.0f/255.0f, 35.0f/255.0f, 48.0f/255.0f),         // Darkest Background (#192330)
            .visualizer = Color(205.0f/255.0f, 206.0f/255.0f, 207.0f/255.0f), // Accent (#CDCECF)
            .splitter = Color(43.0f/255.0f, 59.0f/255.0f, 81.0f/255.0f),     // Divider (#2B3B51)
            .text = Color(223.0f/255.0f, 223.0f/255.0f, 224.0f/255.0f),      // Secondary Text (#DFDFE0)
            
            // Semantic colors
            .accent = Color(205.0f/255.0f, 206.0f/255.0f, 207.0f/255.0f),    // Accent (#CDCECF)
            .warning = Color(219.0f/255.0f, 192.0f/255.0f, 116.0f/255.0f),   // Upvoted (#DBC074)
            .error = Color(209.0f/255.0f, 105.0f/255.0f, 131.0f/255.0f),     // Downvoted (#D16983)
            .success = Color(157.0f/255.0f, 121.0f/255.0f, 214.0f/255.0f),   // Flair Background (#9D79D6)
            
            // UI element colors
            .button = Color(43.0f/255.0f, 59.0f/255.0f, 81.0f/255.0f),       // Primary UI Background (#2B3B51)
            .buttonHover = Color(57.0f/255.0f, 122.0f/255.0f, 214.0f/255.0f), // Current User Name (#7AD5D6)
            .buttonActive = Color(214.0f/255.0f, 122.0f/255.0f, 210.0f/255.0f), // Moderator Name (#D67AD2)
            .border = Color(43.0f/255.0f, 59.0f/255.0f, 81.0f/255.0f),       // Divider (#2B3B51)
            .scrollbar = Color(57.0f/255.0f, 59.0f/255.0f, 68.0f/255.0f),    // Unread Messages (#393B44)
            .scrollbarHover = Color(115.0f/255.0f, 128.0f/255.0f, 145.0f/255.0f), // Read Content (#738091)
            
            // Visualization specific colors
            .waveform = Color(205.0f/255.0f, 206.0f/255.0f, 207.0f/255.0f),  // Accent (#CDCECF)
            .spectrum = Color(57.0f/255.0f, 122.0f/255.0f, 214.0f/255.0f),   // Current User Name (#7AD5D6)
            .lissajous = Color(214.0f/255.0f, 122.0f/255.0f, 210.0f/255.0f), // Moderator Name (#D67AD2)
            .oscilloscope = Color(219.0f/255.0f, 192.0f/255.0f, 116.0f/255.0f) // Upvoted (#DBC074)
        };

        // Carbonfox Theme (AMOLED Dark Theme)
        constexpr ThemeDefinition CARBONFOX = {
            // Core colors
            .background = Color(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f),   // Primary UI Background (#282828)
            .grid = Color(22.0f/255.0f, 22.0f/255.0f, 22.0f/255.0f),         // Darkest Background (#161616)
            .visualizer = Color(242.0f/255.0f, 244.0f/255.0f, 248.0f/255.0f), // Accent (#F2F4F8)
            .splitter = Color(72.0f/255.0f, 72.0f/255.0f, 72.0f/255.0f),     // Divider (#484848)
            .text = Color(228.0f/255.0f, 228.0f/255.0f, 229.0f/255.0f),      // Secondary Text (#E4E4E5)
            
            // Semantic colors
            .accent = Color(242.0f/255.0f, 244.0f/255.0f, 248.0f/255.0f),    // Accent (#F2F4F8)
            .warning = Color(241.0f/255.0f, 109.0f/255.0f, 166.0f/255.0f),   // Upvoted (#F16DA6)
            .error = Color(82.0f/255.0f, 189.0f/255.0f, 255.0f/255.0f),      // Downvoted (#52BDFF)
            .success = Color(37.0f/255.0f, 190.0f/255.0f, 106.0f/255.0f),    // Moderator Name (#25BE6A)
            
            // UI element colors
            .button = Color(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f),       // Primary UI Background (#282828)
            .buttonHover = Color(45.0f/255.0f, 219.0f/255.0f, 217.0f/255.0f), // Current User Name (#2DC7C4)
            .buttonActive = Color(61.0f/255.0f, 219.0f/255.0f, 217.0f/255.0f), // Flair Background (#3DDBD9)
            .border = Color(72.0f/255.0f, 72.0f/255.0f, 72.0f/255.0f),       // Divider (#484848)
            .scrollbar = Color(72.0f/255.0f, 72.0f/255.0f, 72.0f/255.0f),    // Unread Messages (#484848)
            .scrollbarHover = Color(115.0f/255.0f, 128.0f/255.0f, 145.0f/255.0f), // Read Content (#738091)
            
            // Visualization specific colors
            .waveform = Color(0.0f, 0.8f, 1.0f),                            // Cyan (C)
            .spectrum = Color(0.0f, 0.8f, 1.0f),                            // Cyan (C)
            .lissajous = Color(1.0f, 0.0f, 0.0f),                           // Magenta (M)
            .oscilloscope = Color(1.0f, 0.8f, 0.0f)                         // Yellow (Y)
        };

        // CMYK Printer Theme
        constexpr ThemeDefinition CMYK = {
            // Core colors
            .background = Color(1.0f, 1.0f, 1.0f),                           // White paper
            .grid = Color(0.9f, 0.9f, 0.9f),                                 // Light gray grid
            .visualizer = Color(0.0f, 0.8f, 1.0f),                          // Cyan
            .splitter = Color(0.9f, 0.9f, 0.9f),                            // Light gray
            .text = Color(0.0f, 0.0f, 0.0f),                                // Black text
            
            // Semantic colors
            .accent = Color(1.0f, 0.0f, 0.0f),                              // Magenta
            .warning = Color(1.0f, 0.8f, 0.0f),                             // Yellow
            .error = Color(1.0f, 0.0f, 0.0f),                               // Magenta
            .success = Color(0.0f, 0.8f, 1.0f),                             // Cyan
            
            // UI element colors
            .button = Color(0.95f, 0.95f, 0.95f),                           // Light gray
            .buttonHover = Color(0.9f, 0.9f, 0.9f),                         // Medium gray
            .buttonActive = Color(1.0f, 0.0f, 0.0f),                        // Magenta
            .border = Color(0.8f, 0.8f, 0.8f),                              // Gray
            .scrollbar = Color(0.8f, 0.8f, 0.8f),                           // Gray
            .scrollbarHover = Color(0.7f, 0.7f, 0.7f),                      // Dark gray
            
            // Visualization specific colors
            .waveform = Color(0.0f, 1.0f, 1.0f),                            // Cyan (C)
            .spectrum = Color(0.0f, 1.0f, 1.0f),                            // Cyan (C)
            .lissajous = Color(1.0f, 0.0f, 1.0f),                           // Magenta (M)
            .oscilloscope = Color(1.0f, 1.0f, 0.0f)                         // Yellow (Y)
        };

        // Monochrome Printer Theme (K only)
        constexpr ThemeDefinition MONOCHROME = {
            // Core colors
            .background = Color(1.0f, 1.0f, 1.0f),                           // White paper
            .grid = Color(0.95f, 0.95f, 0.95f),                             // Very light gray
            .visualizer = Color(0.2f, 0.2f, 0.2f),                          // Dark gray
            .splitter = Color(0.9f, 0.9f, 0.9f),                            // Light gray
            .text = Color(0.0f, 0.0f, 0.0f),                                // Black text
            
            // Semantic colors
            .accent = Color(0.3f, 0.3f, 0.3f),                              // Medium gray
            .warning = Color(0.5f, 0.5f, 0.5f),                             // Gray
            .error = Color(0.7f, 0.7f, 0.7f),                               // Light gray
            .success = Color(0.1f, 0.1f, 0.1f),                             // Very dark gray
            
            // UI element colors
            .button = Color(0.95f, 0.95f, 0.95f),                           // Very light gray
            .buttonHover = Color(0.9f, 0.9f, 0.9f),                         // Light gray
            .buttonActive = Color(0.3f, 0.3f, 0.3f),                        // Medium gray
            .border = Color(0.8f, 0.8f, 0.8f),                              // Gray
            .scrollbar = Color(0.8f, 0.8f, 0.8f),                           // Gray
            .scrollbarHover = Color(0.7f, 0.7f, 0.7f),                      // Dark gray
            
            // Visualization specific colors
            .waveform = Color(0.0f, 0.0f, 0.0f),                            // Dark gray
            .spectrum = Color(0.0f, 0.0f, 0.0f),                            // Medium-dark gray
            .lissajous = Color(0.0f, 0.0f, 0.0f),                           // Medium gray
            .oscilloscope = Color(0.0f, 0.0f, 0.0f)                         // Light gray
        };
    }

    // Theme manager class to handle theme switching and access
    class ThemeManager {
    public:
        static void initialize() {
            if (availableThemes.empty()) {
                registerTheme("mocha", Themes::MOCHA);
                registerTheme("latte", Themes::LATTE);
                registerTheme("amoled", Themes::AMOLED);
                registerTheme("frappe", Themes::FRAPPE);
                registerTheme("macchiato", Themes::MACCHIATO);
                registerTheme("tokyo-night", Themes::TOKYO_NIGHT);
                registerTheme("breeze-light", Themes::BREEZE_LIGHT);
                registerTheme("breeze-dark", Themes::BREEZE_DARK);
                registerTheme("windows-light", Themes::WINDOWS_LIGHT);
                registerTheme("windows-dark", Themes::WINDOWS_DARK);
                registerTheme("dayfox", Themes::DAYFOX);
                registerTheme("nightfox", Themes::NIGHTFOX);
                registerTheme("carbonfox", Themes::CARBONFOX);
                registerTheme("cmyk", Themes::CMYK);
                registerTheme("monochrome", Themes::MONOCHROME);
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
        static const Color& getLissajous() { return currentTheme.lissajous; }
        static const Color& getOscilloscope() { return currentTheme.oscilloscope; }
        static const Color& getWaveform() { return currentTheme.waveform; }
        static const Color& getSpectrum() { return currentTheme.spectrum; }

    private:
        static ThemeDefinition currentTheme;
        static std::unordered_map<std::string, ThemeDefinition> availableThemes;
    };
} 