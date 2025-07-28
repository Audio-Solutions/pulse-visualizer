#pragma once
#include "common.hpp"
#include "window_manager.hpp"

namespace Graphics {
void drawLine(const float& x1, const float& y1, const float& x2, const float& y2, const float* color,
              const float& thickness);
void drawFilledRect(const float& x, const float& y, const float& width, const float& height, const float* color);

namespace Font {
extern FT_Face face;
extern FT_Library ftLib;

struct GlyphTexture {
  GLuint textureId;
  int width, height;
  int bearingX, bearingY;
  int advance;
};

extern std::unordered_map<char, GlyphTexture> glyphCache;

void load();
void cleanup();
GlyphTexture& getGlyphTexture(char c, float size);
void drawText(const char* text, const float& x, const float& y, const float& size, const float* color);
} // namespace Font

void drawLines(const WindowManager::VisualizerWindow* window, const std::vector<std::pair<float, float>> points,
               float* color);

namespace Shader {
extern std::vector<GLuint> shaders;

std::string loadFile(const char* path);
GLuint load(const char* path, GLenum type);
void ensureShaders();
void dispatchCompute(const WindowManager::VisualizerWindow* win, const int& vertexCount, const GLuint& ageTex,
                     const GLuint& vertexBuffer, const GLuint& out);
void dispatchDecay(const WindowManager::VisualizerWindow* win, const GLuint& ageTex, const GLuint& in,
                   const GLuint& out);
void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& in,
                  const GLuint& out);
void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* lineColor, const GLuint& in,
                      const GLuint& out);
} // namespace Shader

namespace Phosphor {
void render(const WindowManager::VisualizerWindow* win, const std::vector<std::pair<float, float>> points,
            const std::vector<float>& energies, const float* lineColor, bool renderPoints = true);
} // namespace Phosphor

void rgbaToHsva(float* rgba, float* hsva);
void hsvaToRgba(float* hsva, float* rgba);

} // namespace Graphics