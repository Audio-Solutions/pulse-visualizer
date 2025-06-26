#include "graphics.hpp"

#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <sys/stat.h>

namespace Graphics {

namespace {
struct Glyph {
  GLuint textureID;
  int width, height, bearingX, bearingY, advance;
};

struct FontCache {
  FT_Face face = nullptr;
  std::map<unsigned long, Glyph> glyphs;
  int pixelSize = 0;
};

FT_Library ftLib = nullptr;
std::map<std::string, FontCache> fontCaches;

bool ensureFTLib() {
  if (!ftLib) {
    if (FT_Init_FreeType(&ftLib))
      return false;
  }
  return true;
}

FontCache& getFontCache(const char* fontPath, int pixelSize) {
  std::string key = std::string(fontPath) + ":" + std::to_string(pixelSize);
  auto it = fontCaches.find(key);
  if (it != fontCaches.end())
    return it->second;
  FontCache cache;
  struct stat buffer;
  if (stat(fontPath, &buffer) != 0) {
    return fontCaches[key] = cache;
  }
  if (!ensureFTLib()) {
    return fontCaches[key] = cache;
  }
  if (FT_New_Face(ftLib, fontPath, 0, &cache.face)) {
    return fontCaches[key] = cache;
  }
  FT_Set_Pixel_Sizes(cache.face, 0, pixelSize);
  cache.pixelSize = pixelSize;
  return fontCaches[key] = cache;
}

Glyph& loadGlyph(FontCache& cache, unsigned long c) {
  auto it = cache.glyphs.find(c);
  if (it != cache.glyphs.end())
    return it->second;
  Glyph glyph = {};
  if (FT_Load_Char(cache.face, c, FT_LOAD_RENDER)) {
    return cache.glyphs[c] = glyph;
  }
  FT_GlyphSlot g = cache.face->glyph;
  glGenTextures(1, &glyph.textureID);
  if (!glyph.textureID) {
    return cache.glyphs[c] = glyph;
  }
  glBindTexture(GL_TEXTURE_2D, glyph.textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, g->bitmap.width, g->bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               g->bitmap.buffer);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glyph.width = g->bitmap.width;
  glyph.height = g->bitmap.rows;
  glyph.bearingX = g->bitmap_left;
  glyph.bearingY = g->bitmap_top;
  glyph.advance = g->advance.x >> 6;
  return cache.glyphs[c] = glyph;
}
} // namespace

void initialize() {
  static bool glewInitialized = false;
  if (!glewInitialized) {
    GLenum err = glewInit();
    if (err != GLEW_OK) {
      std::cerr << "GLEW initialization failed: " << glewGetErrorString(err) << std::endl;
    }
    glewInitialized = true;
  }
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0f);
}

void setupViewport(int x, int y, int width, int height, int windowHeight) {
  int glY = windowHeight - y - height;
  glViewport(x, glY, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, width, 0, height, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void drawAntialiasedLine(float x1, float y1, float x2, float y2, const float color[4], float thickness) {
  glColor4fv(color);
  glLineWidth(thickness);
  glBegin(GL_LINES);
  glVertex2f(x1, y1);
  glVertex2f(x2, y2);
  glEnd();
}

void drawAntialiasedLines(const std::vector<std::pair<float, float>>& points, const float color[4], float thickness) {
  if (points.size() < 2)
    return;

  // Build/expand a temporary vertex array (x,y pairs)
  static thread_local std::vector<float> vertexData;
  vertexData.clear();
  vertexData.reserve(points.size() * 2);
  for (const auto& p : points) {
    vertexData.push_back(p.first);
    vertexData.push_back(p.second);
  }

  // Create (or reuse) a VBO for the vertices
  static GLuint vertexBuffer = 0;
  if (vertexBuffer == 0) {
    glGenBuffers(1, &vertexBuffer);
  }

  // Upload vertex data to GPU
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);

  // Enable vertex array state and set pointer
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));

  // Set uniform color and line width
  glColor4fv(color);
  glLineWidth(thickness);

  // Draw the line strip
  glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(points.size()));

  // Disable client state and unbind VBO
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawFilledRect(float x, float y, float width, float height, const float color[4]) {
  glColor4fv(color);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);
  glEnd();
}

void drawText(const char* text, float x, float y, float size, const float color[4], const char* fontPath) {
  if (!text || !*text)
    return;

  int pixelSize = (int)size;
  FontCache& cache = getFontCache(fontPath, pixelSize);
  if (!cache.face)
    return;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glColor4fv(color);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  float xpos = x, ypos = y;
  for (const char* p = text; *p; ++p) {
    unsigned long c = (unsigned char)*p;
    if (c == '\n') {
      xpos = x;
      ypos -= size;
      continue;
    }
    Glyph& glyph = loadGlyph(cache, c);
    if (!glyph.textureID)
      continue;

    float x0 = xpos + glyph.bearingX;
    float y0 = ypos - (glyph.height - glyph.bearingY);
    float w = glyph.width, h = glyph.height;
    glBindTexture(GL_TEXTURE_2D, glyph.textureID);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(x0, y0);
    glTexCoord2f(1, 1);
    glVertex2f(x0 + w, y0);
    glTexCoord2f(1, 0);
    glVertex2f(x0 + w, y0 + h);
    glTexCoord2f(0, 0);
    glVertex2f(x0, y0 + h);
    glEnd();
    xpos += glyph.advance;
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

void drawColoredLineSegments(const std::vector<float>& vertices, const std::vector<float>& colors, float thickness) {
  if (vertices.size() < 4 || colors.size() < 8 || vertices.size() / 2 != colors.size() / 4) {
    return; // not enough data or mismatch
  }

  static GLuint vertexBuffer = 0;
  static GLuint colorBuffer = 0;

  if (vertexBuffer == 0) {
    glGenBuffers(1, &vertexBuffer);
  }
  if (colorBuffer == 0) {
    glGenBuffers(1, &colorBuffer);
  }

  // Upload vertex data
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));

  // Upload color data
  glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
  glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STREAM_DRAW);
  glEnableClientState(GL_COLOR_ARRAY);
  glColorPointer(4, GL_FLOAT, 0, reinterpret_cast<void*>(0));

  // Draw lines
  glLineWidth(thickness);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 2));

  // Disable states and unbind
  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

std::string loadTextFile(const char* filepath) {
  // Try local path (Development)
  std::string localPath = std::string("../") + filepath;
  std::ifstream localFile(localPath);
  if (localFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(localFile, line)) {
      content += line + "\n";
    }
    return content;
  }

  // Try system installation path
  std::string systemPath = std::string(PULSE_DATA_DIR) + "/" + filepath;
  std::ifstream systemFile(systemPath);
  if (systemFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(systemFile, line)) {
      content += line + "\n";
    }
    return content;
  }

  std::cerr << "Failed to open shader file: " << filepath << std::endl;
  return "";
}

GLuint loadShaderFromFile(const char* filepath, GLenum shaderType) {
  std::string source = loadTextFile(filepath);
  if (source.empty()) {
    return 0;
  }

  GLuint shader = glCreateShader(shaderType);
  const char* sourceCStr = source.c_str();
  glShaderSource(shader, 1, &sourceCStr, nullptr);
  glCompileShader(shader);

  // Check compilation status
  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_FALSE) {
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

    std::vector<char> errorLog(maxLength);
    glGetShaderInfoLog(shader, maxLength, &maxLength, errorLog.data());

    std::cerr << "Shader compilation failed (" << filepath << "): " << errorLog.data() << std::endl;

    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint createShaderProgram(const char* vertexShaderPath, const char* fragmentShaderPath) {
  GLuint vertexShader = loadShaderFromFile(vertexShaderPath, GL_VERTEX_SHADER);
  GLuint fragmentShader = loadShaderFromFile(fragmentShaderPath, GL_FRAGMENT_SHADER);

  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader != 0)
      glDeleteShader(vertexShader);
    if (fragmentShader != 0)
      glDeleteShader(fragmentShader);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  // Check link status
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    std::vector<char> errorLog(maxLength);
    glGetProgramInfoLog(program, maxLength, &maxLength, errorLog.data());

    std::cerr << "Shader program linking failed: " << errorLog.data() << std::endl;

    glDeleteProgram(program);
    program = 0;
  }

  // Clean up individual shaders (they're now part of the program)
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return program;
}

// Generate a Catmull-Rom spline from a set of control points
std::vector<std::pair<float, float>> generateCatmullRomSpline(const std::vector<std::pair<float, float>>& controlPoints,
                                                              int segmentsPerSegment, float tension) {
  // Not enough points for spline
  if (controlPoints.size() < 4) {
    return controlPoints;
  }

  std::vector<std::pair<float, float>> splinePoints;
  splinePoints.reserve((controlPoints.size() - 3) * (segmentsPerSegment + 1));

  // Pre-compute tension value and segment scale
  const float segmentScale = 1.0f / segmentsPerSegment;

  for (size_t i = 1; i < controlPoints.size() - 2; ++i) {
    const auto& p0 = controlPoints[i - 1];
    const auto& p1 = controlPoints[i];
    const auto& p2 = controlPoints[i + 1];
    const auto& p3 = controlPoints[i + 2];

    // Generate segments between p1 and p2
    for (int j = 0; j <= segmentsPerSegment; ++j) {
      float u = static_cast<float>(j) * segmentScale;
      float u2 = u * u;
      float u3 = u2 * u;

      // Catmull-Rom blending functions
      float b0 = -tension * u3 + 2.0f * tension * u2 - tension * u;
      float b1 = (2.0f - tension) * u3 + (tension - 3.0f) * u2 + 1.0f;
      float b2 = (tension - 2.0f) * u3 + (3.0f - 2.0f * tension) * u2 + tension * u;
      float b3 = tension * u3 - tension * u2;

      float x = b0 * p0.first + b1 * p1.first + b2 * p2.first + b3 * p3.first;
      float y = b0 * p0.second + b1 * p1.second + b2 * p2.second + b3 * p3.second;

      splinePoints.push_back({x, y});
    }
  }

  return splinePoints;
}

// Calculate cumulative distance along a path of points
std::vector<float> calculateCumulativeDistances(const std::vector<std::pair<float, float>>& points) {
  std::vector<float> distances;
  distances.reserve(points.size());

  float cumulativeDistance = 0.0f;
  distances.push_back(0.0f);

  for (size_t i = 1; i < points.size(); ++i) {
    const auto& p1 = points[i - 1];
    const auto& p2 = points[i];
    float dx = p2.first - p1.first;
    float dy = p2.second - p1.second;
    float segmentDistance = sqrtf(dx * dx + dy * dy);
    cumulativeDistance += segmentDistance;
    distances.push_back(cumulativeDistance);
  }

  return distances;
}

namespace Phosphor {
// Phosphor compute shader programs
static GLuint computeProgram = 0;
static GLuint decayProgram = 0;
static GLuint blurProgram = 0;
static GLuint colormapProgram = 0;

// Phosphor context structure for managing textures per visualizer
struct PhosphorContext {
  std::string name;
  GLuint energyTex[2] = {0, 0};
  GLuint ageTex = 0;
  GLuint colorTex = 0;
  GLuint outputFBO = 0;
  GLuint vertexBuffer = 0;
  int texWidth = 0;
  int texHeight = 0;

  PhosphorContext(const char* contextName) : name(contextName) {}

  ~PhosphorContext() {
    if (energyTex[0]) {
      glDeleteTextures(2, energyTex);
    }
    if (ageTex) {
      glDeleteTextures(1, &ageTex);
    }
    if (colorTex) {
      glDeleteTextures(1, &colorTex);
    }
    if (outputFBO) {
      glDeleteFramebuffers(1, &outputFBO);
    }
    if (vertexBuffer) {
      glDeleteBuffers(1, &vertexBuffer);
    }
  }

  void ensureTextures(int w, int h) {
    bool needsReinit =
        (texWidth != w || texHeight != h || !energyTex[0] || !energyTex[1] || !ageTex || !colorTex || !outputFBO);

    if (!needsReinit) {
      return;
    }

    // Force completion of any pending GPU operations before deleting textures
    glFinish();

    // Add memory barrier to ensure all compute shader writes are complete
    glMemoryBarrier(GL_ALL_BARRIER_BITS);

    // Unbind any currently bound textures/framebuffers to avoid issues
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

    texWidth = w;
    texHeight = h;

    // Clean up existing resources
    if (energyTex[0]) {
      glDeleteTextures(2, energyTex);
      energyTex[0] = energyTex[1] = 0;
    }
    if (ageTex) {
      glDeleteTextures(1, &ageTex);
      ageTex = 0;
    }
    if (colorTex) {
      glDeleteTextures(1, &colorTex);
      colorTex = 0;
    }
    if (outputFBO) {
      glDeleteFramebuffers(1, &outputFBO);
      outputFBO = 0;
    }

    // Create new resources
    glGenTextures(2, energyTex);
    glGenTextures(1, &colorTex);
    glGenTextures(1, &ageTex);
    glGenFramebuffers(1, &outputFBO);

    // Check for OpenGL errors after resource creation
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
      std::cerr << "OpenGL error during texture generation: " << error << std::endl;
      return;
    }

    // Create energy textures
    for (int i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, energyTex[i]);

      // Initialize texture with zero background level (linear energy)
      std::vector<float> initData(texWidth * texHeight, 0.0f);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, texWidth, texHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_INT,
                   initData.data());

      // Check for texture creation errors
      error = glGetError();
      if (error != GL_NO_ERROR) {
        std::cerr << "OpenGL error during energy texture " << i << " creation: " << error << std::endl;
        return;
      }

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // Create age texture (1D, 1 Channel uint32_t)
    glBindTexture(GL_TEXTURE_2D, ageTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, texWidth, texHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    // Create color texture
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Check for color texture creation errors
    error = glGetError();
    if (error != GL_NO_ERROR) {
      std::cerr << "OpenGL error during color texture creation: " << error << std::endl;
      return;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void ensureVertexBuffer() {
    if (!vertexBuffer) {
      glGenBuffers(1, &vertexBuffer);
    }
  }
};

void ensureComputeProgram() {
  if (computeProgram)
    return;

  // Load and compile compute shader
  GLuint computeShader = loadShaderFromFile("shaders/phosphor_compute.comp", GL_COMPUTE_SHADER);
  if (computeShader == 0) {
    std::cerr << "Failed to load compute shader" << std::endl;
    return;
  }

  // Create program and attach shader
  computeProgram = glCreateProgram();
  glAttachShader(computeProgram, computeShader);
  glLinkProgram(computeProgram);

  // Check linking status
  GLint success;
  glGetProgramiv(computeProgram, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(computeProgram, 512, NULL, infoLog);
    std::cerr << "Compute shader program linking failed: " << infoLog << std::endl;
    glDeleteProgram(computeProgram);
    computeProgram = 0;
  }

  glDeleteShader(computeShader);
}

void ensureDecayProgram() {
  if (decayProgram)
    return;

  // Load and compile decay compute shader
  GLuint decayShader = loadShaderFromFile("shaders/phosphor_decay.comp", GL_COMPUTE_SHADER);
  if (decayShader == 0) {
    std::cerr << "Failed to load decay compute shader" << std::endl;
    return;
  }

  // Create program and attach shader
  decayProgram = glCreateProgram();
  glAttachShader(decayProgram, decayShader);
  glLinkProgram(decayProgram);

  // Check linking status
  GLint success;
  glGetProgramiv(decayProgram, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(decayProgram, 512, NULL, infoLog);
    std::cerr << "Decay compute shader program linking failed: " << infoLog << std::endl;
    glDeleteProgram(decayProgram);
    decayProgram = 0;
  }

  glDeleteShader(decayShader);
}

void ensureBlurProgram() {
  if (blurProgram)
    return;

  // Load and compile blur compute shader
  GLuint blurShader = loadShaderFromFile("shaders/phosphor_blur.comp", GL_COMPUTE_SHADER);
  if (blurShader == 0) {
    std::cerr << "Failed to load blur compute shader" << std::endl;
    return;
  }

  // Create program and attach shader
  blurProgram = glCreateProgram();
  glAttachShader(blurProgram, blurShader);
  glLinkProgram(blurProgram);

  // Check linking status
  GLint success;
  glGetProgramiv(blurProgram, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(blurProgram, 512, NULL, infoLog);
    std::cerr << "Blur compute shader program linking failed: " << infoLog << std::endl;
    glDeleteProgram(blurProgram);
    blurProgram = 0;
  }

  glDeleteShader(blurShader);
}

void ensureColormapProgram() {
  if (colormapProgram)
    return;

  // Load and compile colormap compute shader
  GLuint colormapShader = loadShaderFromFile("shaders/phosphor_colormap.comp", GL_COMPUTE_SHADER);
  if (colormapShader == 0) {
    std::cerr << "Failed to load colormap compute shader" << std::endl;
    return;
  }

  // Create program and attach shader
  colormapProgram = glCreateProgram();
  glAttachShader(colormapProgram, colormapShader);
  glLinkProgram(colormapProgram);

  // Check linking status
  GLint success;
  glGetProgramiv(colormapProgram, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(colormapProgram, 512, NULL, infoLog);
    std::cerr << "Colormap compute shader program linking failed: " << infoLog << std::endl;
    glDeleteProgram(colormapProgram);
    colormapProgram = 0;
  }

  glDeleteShader(colormapShader);
}

// High-level interface implementation
PhosphorContext* createPhosphorContext(const char* contextName) { return new PhosphorContext(contextName); }

GLuint renderPhosphorSplines(PhosphorContext* context, const std::vector<std::pair<float, float>>& splinePoints,
                             const std::vector<float>& intensityLinear, const std::vector<float>& dwellTimes,
                             int renderWidth, int renderHeight, float deltaTime, float pixelWidth, const float* bgColor,
                             const float* lineColor, float beamSize, float lineBlurSpread, float lineWidth,
                             float decaySlow, float decayFast, uint32_t ageThreshold, float rangeFactor,
                             bool enablePhosphorGrain) {
  if (!context || splinePoints.empty()) {
    return 0;
  }

  // Ensure textures are created/resized for current dimensions
  context->ensureTextures(renderWidth, renderHeight);
  context->ensureVertexBuffer();

  // Upload vertex data to buffer (x, y, intensity, dwellTime format)
  std::vector<float> vertexData;
  vertexData.reserve(splinePoints.size() * 4);

  for (size_t i = 0; i < splinePoints.size(); ++i) {
    vertexData.push_back(splinePoints[i].first);                                  // x
    vertexData.push_back(splinePoints[i].second);                                 // y
    vertexData.push_back(i < intensityLinear.size() ? intensityLinear[i] : 0.1f); // intensity
    vertexData.push_back(i < dwellTimes.size() ? dwellTimes[i] : 1.0f);           // dwellTime
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, context->vertexBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // 0. Copy energyTex[0] to energyTex[1]
  glCopyImageSubData(context->energyTex[0], GL_TEXTURE_2D, 0, 0, 0, 0, context->energyTex[1], GL_TEXTURE_2D, 0, 0, 0, 0,
                     renderWidth, renderHeight, 1);

  // 1. Clear energyTex[0]
  glBindFramebuffer(GL_FRAMEBUFFER, context->outputFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, context->energyTex[0], 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // 2. Decay from energyTex[1] to energyTex[0]
  dispatchDecay(renderWidth, renderHeight, deltaTime, decaySlow, decayFast, ageThreshold, context->energyTex[1],
                context->energyTex[0], context->ageTex);

  // 3. Draw phosphor additively on tex0 (energyTex[0])
  dispatchCompute(static_cast<int>(splinePoints.size()), renderWidth, renderHeight, pixelWidth, context->vertexBuffer,
                  context->energyTex[0], context->ageTex);

  // 4. Clear tex1 (energyTex[1])
  glBindFramebuffer(GL_FRAMEBUFFER, context->outputFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, context->energyTex[1], 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // 5. Blur from tex0 (energyTex[0]) to tex1 (energyTex[1])
  dispatchBlur(renderWidth, renderHeight, context->energyTex[0], context->energyTex[1], lineBlurSpread, lineWidth,
               rangeFactor);

  // 6. Convert energy to color using compute shader
  dispatchColormap(renderWidth, renderHeight, bgColor, lineColor, enablePhosphorGrain, context->energyTex[1],
                   context->ageTex, context->colorTex);

  // Return the final color texture for drawing
  return context->colorTex;
}

GLuint drawCurrentPhosphorState(PhosphorContext* context, int renderWidth, int renderHeight, const float* bgColor,
                                const float* lineColor, bool enablePhosphorGrain) {
  if (!context) {
    return 0;
  }

  // Ensure textures exist for current dimensions
  context->ensureTextures(renderWidth, renderHeight);

  // Just convert current energy (energyTex[1]) to color using compute shader
  // This mimics the original fallback behavior - no decay, no blur, just colormap
  dispatchColormap(renderWidth, renderHeight, bgColor, lineColor, enablePhosphorGrain, context->energyTex[1],
                   context->ageTex, context->colorTex);

  // Return the color texture for drawing
  return context->colorTex;
}

void drawPhosphorResult(GLuint colorTexture, int width, int height) {
  if (!colorTexture)
    return;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, colorTexture);
  glColor4f(1.f, 1.f, 1.f, 1.f);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glBegin(GL_QUADS);
  glTexCoord2f(0.f, 0.f);
  glVertex2f(0.f, 0.f);
  glTexCoord2f(1.f, 0.f);
  glVertex2f(static_cast<float>(width), 0.f);
  glTexCoord2f(1.f, 1.f);
  glVertex2f(static_cast<float>(width), static_cast<float>(height));
  glTexCoord2f(0.f, 1.f);
  glVertex2f(0.f, static_cast<float>(height));
  glEnd();

  glDisable(GL_TEXTURE_2D);
}

void destroyPhosphorContext(PhosphorContext* context) { delete context; }

void dispatchCompute(int vertexCount, int texWidth, int texHeight, float pixelWidth, GLuint splineVertexBuffer,
                     GLuint energyTex, GLuint ageTex) {
  ensureComputeProgram();
  if (!computeProgram)
    return;

  // Use the compute shader
  glUseProgram(computeProgram);

  // Set uniforms
  glUniform1f(glGetUniformLocation(computeProgram, "pixelWidth"), pixelWidth);
  // glUniform1f(glGetUniformLocation(computeProgram, "beamSize"), beamSize);
  glUniform2i(glGetUniformLocation(computeProgram, "texSize"), texWidth, texHeight);

  // Bind vertex buffer
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, splineVertexBuffer);

  // Bind energy texture as image
  glBindImageTexture(0, energyTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Bind age texture as image
  glBindImageTexture(1, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Dispatch compute shader (one thread per vertex)
  GLuint numGroups = (vertexCount + 63) / 64; // Round up to multiple of local_size_x
  glDispatchCompute(numGroups, 1, 1);

  // Memory barrier to ensure writes are complete
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  glUseProgram(0);
}

void dispatchDecay(int texWidth, int texHeight, float deltaTime, float decaySlow, float decayFast,
                   uint32_t ageThreshold, GLuint inputTex, GLuint outputTex, GLuint ageTex) {
  ensureDecayProgram();
  if (!decayProgram)
    return;

  // Use the decay compute shader
  glUseProgram(decayProgram);

  float decaySlow_ = exp(-deltaTime * decaySlow);
  float decayFast_ = exp(-deltaTime * decayFast);

  // Set uniforms
  glUniform1f(glGetUniformLocation(decayProgram, "decaySlow"), decaySlow_);
  glUniform1f(glGetUniformLocation(decayProgram, "decayFast"), decayFast_);
  glUniform1ui(glGetUniformLocation(decayProgram, "ageThreshold"), ageThreshold);

  // Bind input and output textures
  glBindImageTexture(0, inputTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
  glBindImageTexture(2, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Dispatch compute shader (8x8 thread groups)
  GLuint groupsX = (texWidth + 7) / 8;  // Round up to multiple of 8
  GLuint groupsY = (texHeight + 7) / 8; // Round up to multiple of 8
  glDispatchCompute(groupsX, groupsY, 1);

  // Memory barrier to ensure writes are complete
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  glUseProgram(0);
}

void dispatchBlur(int texWidth, int texHeight, GLuint inputTex, GLuint outputTex, float lineBlurSpread, float lineWidth,
                  float rangeFactor) {
  ensureBlurProgram();
  if (!blurProgram)
    return;

  // Use the blur compute shader
  glUseProgram(blurProgram);

  // Set uniforms
  glUniform1f(glGetUniformLocation(blurProgram, "line_blur_spread"), lineBlurSpread);
  glUniform1f(glGetUniformLocation(blurProgram, "line_width"), lineWidth);
  glUniform1f(glGetUniformLocation(blurProgram, "range_factor"), rangeFactor);
  glUniform2f(glGetUniformLocation(blurProgram, "texSize"), static_cast<float>(texWidth),
              static_cast<float>(texHeight));

  // Bind input and output textures
  glBindImageTexture(0, inputTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);

  // Dispatch compute shader (8x8 thread groups)
  GLuint groupsX = (texWidth + 7) / 8;  // Round up to multiple of 8
  GLuint groupsY = (texHeight + 7) / 8; // Round up to multiple of 8
  glDispatchCompute(groupsX, groupsY, 1);

  // Memory barrier to ensure writes are complete
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  glUseProgram(0);
}

void dispatchColormap(int texWidth, int texHeight, const float* bgColor, const float* lissajousColor,
                      bool enablePhosphorGrain, GLuint energyTex, GLuint ageTex, GLuint colorTex) {
  ensureColormapProgram();
  if (!colormapProgram)
    return;

  // Use the colormap compute shader
  glUseProgram(colormapProgram);

  // Set uniforms
  glUniform3f(glGetUniformLocation(colormapProgram, "blackColor"), bgColor[0], bgColor[1], bgColor[2]);
  glUniform3f(glGetUniformLocation(colormapProgram, "beamColor"), lissajousColor[0], lissajousColor[1],
              lissajousColor[2]);
  glUniform1i(glGetUniformLocation(colormapProgram, "enablePhosphorGrain"), enablePhosphorGrain ? 1 : 0);

  // Bind input energy texture and output color texture
  glBindImageTexture(0, energyTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, colorTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
  glBindImageTexture(2, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Dispatch compute shader (8x8 thread groups)
  GLuint groupsX = (texWidth + 7) / 8;  // Round up to multiple of 8
  GLuint groupsY = (texHeight + 7) / 8; // Round up to multiple of 8
  glDispatchCompute(groupsX, groupsY, 1);

  // Memory barrier to ensure writes are complete
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  glUseProgram(0);
}

} // namespace Phosphor

} // namespace Graphics
