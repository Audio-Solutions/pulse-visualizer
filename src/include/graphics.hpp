#pragma once
#include "common.hpp"
#include "window_manager.hpp"

namespace Graphics {

/**
 * @brief Draw a line with specified color and thickness
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color RGBA color array
 * @param thickness Line thickness
 */
void drawLine(const float& x1, const float& y1, const float& x2, const float& y2, const float* color,
              const float& thickness);

/**
 * @brief Draw a filled rectangle with specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color RGBA color array
 */
void drawFilledRect(const float& x, const float& y, const float& width, const float& height, const float* color);

/**
 * @brief Font rendering namespace
 */
namespace Font {
extern FT_Face face;
extern FT_Library ftLib;

/**
 * @brief Glyph texture information
 */
struct GlyphTexture {
  GLuint textureId;
  int width, height;
  int bearingX, bearingY;
  int advance;
};

extern std::unordered_map<char, GlyphTexture> glyphCache;

/**
 * @brief Load font from file
 */
void load();

/**
 * @brief Cleanup font resources
 */
void cleanup();

/**
 * @brief Get or create glyph texture for character
 * @param c Character to get texture for
 * @param size Font size
 * @return Glyph texture information
 */
GlyphTexture& getGlyphTexture(char c, float size);

/**
 * @brief Draw text at specified position
 * @param text Text to render
 * @param x X coordinate
 * @param y Y coordinate
 * @param size Font size
 * @param color RGBA color array
 */
void drawText(const char* text, const float& x, const float& y, const float& size, const float* color);

} // namespace Font

/**
 * @brief Draw connected lines
 * @param window Visualizer window
 * @param points Vector of (x, y) points
 * @param color RGBA color array
 */
void drawLines(const WindowManager::VisualizerWindow* window, const std::vector<std::pair<float, float>> points,
               float* color);

/**
 * @brief Shader management namespace
 */
namespace Shader {
extern std::vector<GLuint> shaders;

/**
 * @brief Load shader source from file
 * @param path File path
 * @return Shader source code
 */
std::string loadFile(const char* path);

/**
 * @brief Load and compile shader
 * @param path Shader file path
 * @param type Shader type (GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, etc.)
 * @return Compiled shader ID
 */
GLuint load(const char* path, GLenum type);

/**
 * @brief Ensure all shaders are loaded and compiled
 */
void ensureShaders();

/**
 * @brief Dispatch compute shader for phosphor effect
 * @param win Visualizer window
 * @param vertexCount Number of vertices
 * @param ageTex Age texture
 * @param vertexBuffer Vertex buffer
 * @param out Output texture
 */
void dispatchCompute(const WindowManager::VisualizerWindow* win, const int& vertexCount, const GLuint& ageTex,
                     const GLuint& vertexBuffer, const GLuint& out);

/**
 * @brief Dispatch decay shader for phosphor effect
 * @param win Visualizer window
 * @param ageTex Age texture
 * @param in Input texture
 * @param out Output texture
 */
void dispatchDecay(const WindowManager::VisualizerWindow* win, const GLuint& ageTex, const GLuint& in,
                   const GLuint& out);

/**
 * @brief Dispatch blur shader
 * @param win Visualizer window
 * @param dir Blur direction
 * @param kernel Blur kernel size
 * @param in Input texture
 * @param out Output texture
 */
void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& in,
                  const GLuint& out);

/**
 * @brief Dispatch colormap shader
 * @param win Visualizer window
 * @param lineColor Line color
 * @param in Input texture
 * @param out Output texture
 */
void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* lineColor, const GLuint& in,
                      const GLuint& out);

} // namespace Shader

/**
 * @brief Phosphor effect rendering namespace
 */
namespace Phosphor {

/**
 * @brief Render phosphor effect for visualizer
 * @param win Visualizer window
 * @param points Vector of (x, y) points
 * @param energies Energy values for each point
 * @param lineColor Line color
 * @param renderPoints Whether to render individual points
 */
void render(const WindowManager::VisualizerWindow* win, const std::vector<std::pair<float, float>> points,
            const std::vector<float>& energies, const float* lineColor, bool renderPoints = true);

} // namespace Phosphor

/**
 * @brief Convert RGBA color to HSVA
 * @param rgba Input RGBA color array
 * @param hsva Output HSVA color array
 */
void rgbaToHsva(float* rgba, float* hsva);

/**
 * @brief Convert HSVA color to RGBA
 * @param hsva Input HSVA color array
 * @param rgba Output RGBA color array
 */
void hsvaToRgba(float* hsva, float* rgba);

} // namespace Graphics