#include "graphics.hpp"
#include <GL/gl.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
#include <string>
#include <iostream>
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
    if (FT_Init_FreeType(&ftLib)) return false;
  }
  return true;
}

FontCache& getFontCache(const char* fontPath, int pixelSize) {
  std::string key = std::string(fontPath) + ":" + std::to_string(pixelSize);
  auto it = fontCaches.find(key);
  if (it != fontCaches.end()) return it->second;
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
  if (it != cache.glyphs.end()) return it->second;
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, g->bitmap.width, g->bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap.buffer);
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
}

void initialize() {
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
  if (points.size() < 2) return;
  
  glColor4fv(color);
  glLineWidth(thickness);
  glBegin(GL_LINE_STRIP);
  for (const auto& point : points) {
    glVertex2f(point.first, point.second);
  }
  glEnd();
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
  if (!text || !*text) return;
  
  int pixelSize = (int)size;
  FontCache& cache = getFontCache(fontPath, pixelSize);
  if (!cache.face) return;
  
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glColor4fv(color);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  
  float xpos = x, ypos = y;
  for (const char* p = text; *p; ++p) {
    unsigned long c = (unsigned char)*p;
    if (c == '\n') { xpos = x; ypos -= size; continue; }
    Glyph& glyph = loadGlyph(cache, c);
    if (!glyph.textureID) continue;
    
    float x0 = xpos + glyph.bearingX;
    float y0 = ypos - (glyph.height - glyph.bearingY);
    float w = glyph.width, h = glyph.height;
    glBindTexture(GL_TEXTURE_2D, glyph.textureID);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(x0,     y0);
    glTexCoord2f(1, 1); glVertex2f(x0 + w, y0);
    glTexCoord2f(1, 0); glVertex2f(x0 + w, y0 + h);
    glTexCoord2f(0, 0); glVertex2f(x0,     y0 + h);
    glEnd();
    xpos += glyph.advance;
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

} 