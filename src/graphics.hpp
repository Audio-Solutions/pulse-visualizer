#include <cstddef>
#pragma once

#include <utility>
#include <vector>

namespace Graphics {
// OpenGL setup and viewport management
void initialize();
void setupViewport(int x, int y, int width, int height, int windowHeight);

// Drawing functions
void drawAntialiasedLine(float x1, float y1, float x2, float y2, const float color[4], float thickness = 2.0f);
void drawAntialiasedLines(const std::vector<std::pair<float, float>>& points, const float color[4],
                          float thickness = 2.0f);
void drawFilledRect(float x, float y, float width, float height, const float color[4]);

// Draw text using a TTF font (JetBrains Mono)
void drawText(const char* text, float x, float y, float size, const float color[4], const char* fontPath = nullptr);
} // namespace Graphics
