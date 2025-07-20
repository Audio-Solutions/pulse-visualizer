#include <cstddef>
#pragma once

#include <GL/glew.h>
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

void drawColoredLineSegments(const std::vector<float>& vertices, const std::vector<float>& colors,
                             float thickness = 2.0f);

// Spline utilities
std::vector<std::pair<float, float>> generateCatmullRomSpline(const std::vector<std::pair<float, float>>& points,
                                                              int segmentsPerSegment, float tension);
std::vector<float> calculateCumulativeDistances(const std::vector<std::pair<float, float>>& points);

// Shader utilities
GLuint loadShaderFromFile(const char* filepath, GLenum shaderType);
GLuint createShaderProgram(const char* vertexShaderPath, const char* fragmentShaderPath);

// Phosphor compute shader management
namespace Phosphor {
// Low-level compute shader functions (for advanced use)
void ensureComputeProgram();
void ensureDecayProgram();
void ensureBlurProgram();
void ensureCombineProgram();
void ensureColormapProgram();

void dispatchCompute(int vertexCount, int texWidth, int texHeight, float pixelWidth, GLuint splineVertexBuffer,
                     GLuint energyTex, GLuint ageTex, bool enableCurvedScreen, float screenCurvature,
                     float screenGapFactor);
void dispatchDecay(int texWidth, int texHeight, float deltaTime, float decaySlow, float decayFast,
                   uint32_t ageThreshold, GLuint inputTex, GLuint outputTex, GLuint ageTex);
void dispatchBlur(int texWidth, int texHeight, GLuint inputTex, GLuint outputTex, float lineBlurSpread, float lineWidth,
                  float rangeFactor, int blurDirection, int kernelType);
void dispatchCombine(int texWidth, int texHeight, GLuint kernelE, GLuint kernelF, GLuint kernelG, GLuint outputTex,
                     float nearBlurIntensity, float farBlurIntensity);
void dispatchColormap(int texWidth, int texHeight, const float* bgColor, const float* beamColor,
                      bool enablePhosphorGrain, bool enableCurvedScreen, float screenCurvature, float screenGapFactor,
                      GLuint energyTex, GLuint colorTex);

// High-level phosphor rendering functions
struct PhosphorContext;

// Create a phosphor rendering context for a visualizer
PhosphorContext* createPhosphorContext(const char* contextName);

// Render splines with phosphor effect and return final color texture
GLuint renderPhosphorSplines(PhosphorContext* context, const std::vector<std::pair<float, float>>& splinePoints,
                             const std::vector<float>& intensityLinear, const std::vector<float>& dwellTimes,
                             int renderWidth, int renderHeight, float deltaTime, float pixelWidth, const float* bgColor,
                             const float* lineColor);

// Draw current phosphor state without processing (just colormap existing energy)
GLuint drawCurrentPhosphorState(PhosphorContext* context, int renderWidth, int renderHeight, const float* bgColor,
                                const float* lineColor);

// Draw the phosphor result as a fullscreen textured quad
void drawPhosphorResult(GLuint colorTexture, int width, int height);

// Cleanup phosphor context
void destroyPhosphorContext(PhosphorContext* context);

} // namespace Phosphor
} // namespace Graphics
