#pragma once
#include "common.hpp"
#include "theme.hpp"
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
extern std::vector<FT_Face> faces;
extern std::vector<FT_Library> ftLibs;

/**
 * @brief Glyph texture information
 */
struct GlyphTexture {
  GLuint textureId;
  int width, height;
  int bearingX, bearingY;
  int advance;
};

// Hash function for std::pair<char, float>
struct PairHash {
  template <class T1, class T2> std::size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1> {}(p.first);
    auto h2 = std::hash<T2> {}(p.second);
    return h1 ^ (h2 << 1);
  }
};

extern std::vector<std::unordered_map<std::pair<char, float>, GlyphTexture, PairHash>> glyphCaches;

/**
 * @brief Load font from file for a specific window
 * @param sdlWindow Index of the window to load font for
 */
void load(size_t sdlWindow);

/**
 * @brief Cleanup font resources for a specific window
 * @param sdlWindow Index of the window to cleanup font for
 */
void cleanup(size_t sdlWindow);

/**
 * @brief Get or create glyph texture for character for a specific window
 * @param c Character to get texture for
 * @param size Font size
 * @param sdlWindow Index of the window to get glyph for
 * @return Glyph texture information
 */
GlyphTexture& getGlyphTexture(char c, float size, size_t sdlWindow);

/**
 * @brief Draw text at specified position for a specific window
 * @param text Text to render
 * @param x X coordinate
 * @param y Y coordinate
 * @param size Font size
 * @param color RGBA color array
 * @param sdlWindow Index of the window to draw text for
 */
void drawText(const char* text, const float& x, const float& y, const float& size, const float* color,
              size_t sdlWindow);

/**
 * @brief Get text dimensions for a specific window
 * @param text Text to measure
 * @param size Font size
 * @param sdlWindow Index of the window to measure text for
 * @return Pair of (width, height) dimensions
 */
std::pair<float, float> getTextSize(const char* text, const float& size, size_t sdlWindow);

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
                     const GLuint& vertexBuffer, const GLuint& vertexColorBuffer, const GLuint& energyTexR,
                     const GLuint& energyTexG, const GLuint& energyTexB);

/**
 * @brief Dispatch decay shader for phosphor effect
 * @param win Visualizer window
 * @param ageTex Age texture
 * @param in Input texture
 * @param out Output texture
 */
void dispatchDecay(const WindowManager::VisualizerWindow* win, const GLuint& ageTex, const GLuint& energyTexR,
                   const GLuint& energyTexG, const GLuint& energyTexB);

/**
 * @brief Dispatch blur shader
 * @param win Visualizer window
 * @param dir Blur direction
 * @param kernel Blur kernel size
 * @param in Input texture
 * @param out Output texture
 */
void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& inR,
                  const GLuint& inG, const GLuint& inB, const GLuint& outR, const GLuint& outG, const GLuint& outB);

/**
 * @brief Dispatch colormap shader
 * @param win Visualizer window
 * @param colorMap Color map texture
 * @param in Input texture
 * @param out Output texture
 */
void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* beamColor, const GLuint& inR,
                      const GLuint& inG, const GLuint& inB, const GLuint& out);

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
            bool renderPoints = true, const float* beamColor = Theme::colors.color);

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