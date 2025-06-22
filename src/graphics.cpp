#include "graphics.hpp"

#include "config.hpp"

#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H
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

} // namespace Graphics
