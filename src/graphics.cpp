/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include/graphics.hpp"

#include "include/config.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/window_manager.hpp"

namespace Graphics {

// pain and suffering for antialiased lines
void drawLine(const float& x1, const float& y1, const float& x2, const float& y2, const float* color,
              const float& thickness) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float dx = x2 - x1;
  float dy = y2 - y1;
  float length = sqrt(dx * dx + dy * dy);

  if (length < 0.001f)
    return;

  if (thickness <= 2.0f + FLT_EPSILON) {
    glColor4fv(color);
    glLineWidth(thickness);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    return;
  }

  float nx = -dy / length;
  float ny = dx / length;

  const int antialiasPasses = 5;
  const float antialiasWidth = 2.0f;

  for (int pass = 0; pass < antialiasPasses; ++pass) {
    float offset = (pass - antialiasPasses / 2.0f) * (antialiasWidth / antialiasPasses);
    float currentThickness = thickness + offset;
    float currentHalfThickness = currentThickness * 0.5f;

    float distance = abs(offset) / (antialiasWidth * 0.5f);
    float alpha = color[3] * (1.0f - distance * 0.8f);
    float passColor[4] = {color[0], color[1], color[2], alpha};
    glColor4fv(passColor);

    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(x1 + nx * currentHalfThickness, y1 + ny * currentHalfThickness);
    glVertex2f(x1 - nx * currentHalfThickness, y1 - ny * currentHalfThickness);
    glVertex2f(x2 + nx * currentHalfThickness, y2 + ny * currentHalfThickness);
    glVertex2f(x2 - nx * currentHalfThickness, y2 - ny * currentHalfThickness);
    glEnd();
  }

  glDisable(GL_BLEND);
}

void drawFilledRect(const float& x, const float& y, const float& width, const float& height, const float* color) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4fv(color);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);
  glEnd();
  glDisable(GL_BLEND);
}

// pain and suffering for antialiased arcs
void drawArc(const float& x, const float& y, const float& radius, const float& startAngle, const float& endAngle,
             const float* color, const float& thickness, const int& segments) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float startRad = (startAngle)*M_PI / 180.0f;
  float endRad = (endAngle)*M_PI / 180.0f;

  const int antialiasPasses = 5;
  const float antialiasWidth = 2.0f;

  for (int pass = 0; pass < antialiasPasses; ++pass) {
    float offset = (pass - antialiasPasses / 2.0f) * (antialiasWidth / antialiasPasses);
    float currentThickness = thickness + offset;
    float currentHalfThickness = currentThickness * 0.5f;

    float distance = abs(offset) / (antialiasWidth * 0.5f);
    float alpha = color[3] * (1.0f - distance * 0.8f);
    float passColor[4] = {color[0], color[1], color[2], alpha};
    glColor4fv(passColor);

    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segments; ++i) {
      float t = static_cast<float>(i) / segments;
      float angle = startRad + (endRad - startRad) * t;
      float dx = cos(angle);
      float dy = sin(angle);

      float px_outer = x + (radius + currentHalfThickness) * dx;
      float py_outer = y + (radius + currentHalfThickness) * dy;
      float px_inner = x + (radius - currentHalfThickness) * dx;
      float py_inner = y + (radius - currentHalfThickness) * dy;

      glVertex2f(px_outer, py_outer);
      glVertex2f(px_inner, py_inner);
    }
    glEnd();
  }

  glDisable(GL_BLEND);
}

namespace Font {

// FreeType handles - per window
FT_Face face = nullptr;
FT_Library ftLib = nullptr;

// Glyph texture cache - per window
std::unordered_map<std::pair<char, float>, GlyphTexture, PairHash> glyphCache;

std::string findFont(const std::string& path) {
  // Try expandUserPath(path) first
  std::string expanded = expandUserPath(path);
  struct stat buf;
  if (stat(expanded.c_str(), &buf) == 0)
    return expanded;

  // Try system path
  std::string inst = Config::getInstallDir() + "/" + path;
  if (stat(inst.c_str(), &buf) == 0)
    return inst;

  return "";
}

void load() {
  std::string path = findFont(Config::options.font);
  if (path.empty())
    return;

  // Initialize FreeType library for this window
  if (!ftLib) {
    if (FT_Init_FreeType(&ftLib) != 0)
      return;
  }

  // Load font face for this window
  if (FT_New_Face(ftLib, path.c_str(), 0, &face))
    return;

  glyphCache = {};
}

void cleanup() {
  if (face == nullptr)
    return;

  // Cleanup glyph textures
  for (auto& [key, glyph] : glyphCache) {
    if (glIsTexture(glyph.textureId)) {
      glDeleteTextures(1, &glyph.textureId);
    }
  }

  glyphCache.erase(glyphCache.begin(), glyphCache.end());

  // Cleanup FreeType resources for this window
  if (face) {
    FT_Done_Face(face);
    face = nullptr;
  }
  if (ftLib) {
    FT_Done_FreeType(ftLib);
    ftLib = nullptr;
  }
}

GlyphTexture& getGlyphTexture(char c, float size) {
  auto key = std::make_pair(c, size);
  if (glyphCache.find(key) != glyphCache.end()) {
    return glyphCache[key];
  }

  // Set font size
  FT_Set_Pixel_Sizes(face, 0, size);

  if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
    static GlyphTexture empty = {0, 0, 0, 0, 0, 0};
    return empty;
  }

  FT_GlyphSlot g = face->glyph;

  // Create OpenGL texture for glyph
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, g->bitmap.width, g->bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
               g->bitmap.buffer);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Create glyph texture information
  GlyphTexture glyph = {
      tex,           static_cast<int>(g->bitmap.width),  static_cast<int>(g->bitmap.rows), g->bitmap_left,
      g->bitmap_top, static_cast<int>(g->advance.x >> 6)};

  glyphCache[key] = glyph;
  return glyphCache[key];
}

void drawText(const char* text, const float& x, const float& y, const float& size, const float* color) {
  if (!text || !*text || face == nullptr)
    return;

  // Setup OpenGL state for text rendering
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glColor4fv(color);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  float _x = x, _y = y;
  for (const char* p = text; *p; p++) {
    const char& c = *p;
    if (c == '\n') {
      _x = x;
      _y -= size;
      continue;
    }

    // Get or create glyph texture
    GlyphTexture& glyph = getGlyphTexture(c, size);
    if (glyph.textureId == 0)
      continue;

    glBindTexture(GL_TEXTURE_2D, glyph.textureId);

    // Calculate glyph position
    float x0 = _x + static_cast<float>(glyph.bearingX);
    float y0 = _y - static_cast<float>(glyph.height - glyph.bearingY);
    float w = static_cast<float>(glyph.width);
    float h = static_cast<float>(glyph.height);

    float vertices[] = {
        x0,     y0 + h, 0.0f, 0.0f, // Top left
        x0,     y0,     0.0f, 1.0f, // Bottom left
        x0 + w, y0,     1.0f, 1.0f, // Bottom right
        x0 + w, y0 + h, 1.0f, 0.0f  // Top right
    };

    // Render glyph quad
    glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glDrawArrays(GL_QUADS, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _x += static_cast<float>(glyph.advance);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

std::pair<float, float> getTextSize(const char* text, const float& size) {
  if (!text || !*text || face == nullptr)
    return {0.0f, 0.0f};

  float totalWidth = 0.0f;
  float totalHeight = 0.0f;
  float currentLineWidth = 0.0f;

  for (const char* p = text; *p; p++) {
    const char& c = *p;
    if (c == '\n') {
      totalWidth = std::max(totalWidth, currentLineWidth);
      currentLineWidth = 0.0f;

      totalHeight += size;
      continue;
    }

    GlyphTexture& glyph = getGlyphTexture(c, size);
    if (glyph.textureId == 0)
      continue;

    currentLineWidth += static_cast<float>(glyph.advance);
  }

  totalWidth = std::max(totalWidth, currentLineWidth);
  totalHeight += size;
  return {totalWidth, totalHeight};
}

std::string wrapText(const std::string& text, const float& maxW, const float& fontSize) {
  std::istringstream lineStream(text);
  std::string line;
  std::string result;

  while (std::getline(lineStream, line)) {
    std::istringstream wordStream(line);
    std::string word;
    std::string currentLine;

    while (wordStream >> word) {
      std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
      auto size = getTextSize(testLine.c_str(), fontSize);

      if (size.first <= maxW) {
        currentLine = testLine;
      } else {
        if (!currentLine.empty()) {
          result += currentLine + "\n";
        }
        currentLine = word;
      }
    }

    if (!currentLine.empty()) {
      result += currentLine + "\n";
    }
  }

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

std::string truncateText(const std::string& text, const float& maxW, const float& fontSize) {
  const std::string ellipsis = "...";
  auto fullSize = getTextSize(text.c_str(), fontSize);

  // If the full text fits, return as-is
  if (fullSize.first <= maxW) {
    return text;
  }

  std::string truncated;
  for (size_t i = 0; i < text.size(); ++i) {
    std::string candidate = text.substr(0, i) + ellipsis;
    auto candidateSize = getTextSize(candidate.c_str(), fontSize);

    if (candidateSize.first > maxW) {
      break;
    }

    truncated = candidate;
  }

  return truncated;
}

std::string fitTextBox(const std::string& text, const float& maxW, const float& maxH, const float& fontSize) {
  const std::string ellipsis = "...";
  std::istringstream lineStream(text);
  std::string line;
  std::string result;

  const int lineHeight = fontSize;
  const int maxLines = static_cast<int>(maxH / lineHeight);
  int currentLineCount = 0;

  while (std::getline(lineStream, line) && currentLineCount < maxLines) {
    std::istringstream wordStream(line);
    std::string word;
    std::string currentLine;

    while (wordStream >> word) {
      std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
      auto size = getTextSize(testLine.c_str(), fontSize);

      if (size.first <= maxW) {
        currentLine = testLine;
      } else {
        if (!currentLine.empty()) {
          result += currentLine + "\n";
          currentLineCount++;
          if (currentLineCount >= maxLines)
            break;
        }
        currentLine = word;
      }
    }

    if (currentLineCount >= maxLines)
      break;

    if (!currentLine.empty()) {
      result += currentLine + "\n";
      currentLineCount++;
    }
  }

  // If we ran out of vertical space, truncate the last line
  if (currentLineCount >= maxLines) {
    size_t lastNewline = result.find_last_of('\n');
    std::string lastLine = (lastNewline != std::string::npos) ? result.substr(lastNewline + 1) : result;
    std::string truncatedLastLine;

    for (size_t i = 0; i < lastLine.size(); ++i) {
      std::string candidate = lastLine.substr(0, i) + ellipsis;
      auto candidateSize = getTextSize(candidate.c_str(), fontSize);
      if (candidateSize.first > maxW)
        break;
      truncatedLastLine = candidate;
    }

    result =
        (lastNewline != std::string::npos) ? result.substr(0, lastNewline + 1) + truncatedLastLine : truncatedLastLine;
  }

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

} // namespace Font

void drawLines(const WindowManager::VisualizerWindow* window, const std::vector<std::pair<float, float>> points,
               float* color) {
  std::vector<float> vertexData;
  vertexData.reserve(points.size() * 2);

  // Filter points to reduce vertex count
  float lastX = -10000.f, lastY = -10000.f;
  for (size_t i = 0; i < points.size(); i++) {
    float x = points[i].first;
    float y = points[i].second;
    if (i == 0 || std::abs(x - lastX) >= 1.0f || std::abs(y - lastY) >= 1.0f) {
      vertexData.push_back(x);
      vertexData.push_back(y);
      lastX = x;
      lastY = y;
    }
  }

  if (vertexData.size() < 4)
    return;

  // Setup viewport and rendering state
  WindowManager::setViewport(window->x, window->width, SDLWindow::states[window->group].windowSizes.second);

  glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STREAM_DRAW);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

  glColor4fv(color);
  glLineWidth(2.f);

  glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertexData.size() / 2));

  glDisable(GL_LINE_SMOOTH);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

namespace Shader {

// Shader program handles
std::unordered_map<std::string, GLuint> shaders;

std::string loadFile(const char* path) {
  // Try local path first
  std::string local = std::string("../") + path;
  std::ifstream lFile(local);
  if (lFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(lFile, line))
      content += line + "\n";
    return content;
  }

  // Try system path
  std::string inst = Config::getInstallDir() + "/" + path;
  std::ifstream sFile(inst);
  if (sFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(sFile, line))
      content += line + "\n";
    return content;
  }

  LOG_ERROR(std::string("Failed to open shader file '") + path + "'");
  return "";
}

GLuint load(const char* path, GLenum type) {
  std::string src = loadFile(path);
  if (src.empty())
    return 0;

  // Create and compile shader
  GLuint shader = glCreateShader(type);
  auto p = src.c_str();
  glShaderSource(shader, 1, &p, nullptr);
  glCompileShader(shader);

  GLint tmp = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &tmp);
  if (tmp != GL_FALSE)
    return shader;

  // Get compilation error
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &tmp);
  std::vector<char> log(tmp);
  glGetShaderInfoLog(shader, tmp, &tmp, log.data());

  LOG_ERROR(std::string("Shader compilation failed for '") + path + "': " + log.data());
  glDeleteShader(shader);
  return 0;
}

void ensureShaders() {
  if (std::all_of(shaders.begin(), shaders.end(), [](const auto& x) { return x.second != 0; }))
    return;

  // Shader file paths
  static std::unordered_map<std::string, std::pair<GLenum, std::string>> shaderPaths = {
      {"phosphor_compute",  {GL_COMPUTE_SHADER, "shaders/phosphor_compute.comp"} },
      {"phosphor_decay",    {GL_COMPUTE_SHADER, "shaders/phosphor_decay.comp"}   },
      {"phosphor_blur",     {GL_COMPUTE_SHADER, "shaders/phosphor_blur.comp"}    },
      {"phosphor_colormap", {GL_COMPUTE_SHADER, "shaders/phosphor_colormap.comp"}},
  };

  // Load and compile all shaders
  for (auto& [name, path] : shaderPaths) {
    auto& shader = shaders[name];
    if (shader != 0)
      continue;

    GLuint shaderProgram = load(path.second.c_str(), path.first);
    if (!shaderProgram)
      continue;

    LOG_DEBUG("Loading shader " << name);

    // Create shader program
    shader = glCreateProgram();
    glAttachShader(shader, shaderProgram);
    glLinkProgram(shader);

    GLint tmp;
    glGetProgramiv(shader, GL_LINK_STATUS, &tmp);
    if (!tmp) {
      char log[512];
      glGetProgramInfoLog(shader, 512, NULL, log);
      LOG_ERROR("Shader linking failed:" << log);
      glDeleteProgram(shader);
      shader = 0;
    }

    glDeleteShader(shaderProgram);
  }
}

void dispatchCompute(const WindowManager::VisualizerWindow* win, const int& vertexCount, const GLuint& ageTex,
                     const GLuint& vertexBuffer, const GLuint& vertexColorBuffer, const GLuint& energyTexR,
                     const GLuint& energyTexG, const GLuint& energyTexB) {

  auto& shader = shaders["phosphor_compute"];

  if (!shader)
    return;

  glUseProgram(shader);

  glUniform1i(glGetUniformLocation(shader, "colorbeam"), Config::options.phosphor.beam.rainbow);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vertexBuffer);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vertexColorBuffer);
  glBindImageTexture(0, energyTexR, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, energyTexG, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(2, energyTexB, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(3, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint g = (vertexCount + 63) / 64;
  glDispatchCompute(g, 1, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchDecay(const WindowManager::VisualizerWindow* win, const GLuint& ageTex, const GLuint& energyTexR,
                   const GLuint& energyTexG, const GLuint& energyTexB) {
  auto& shader = shaders["phosphor_decay"];
  if (!shader)
    return;

  glUseProgram(shader);

  float decaySlow = exp(-WindowManager::dt * Config::options.phosphor.decay.slow);
  float decayFast = exp(-WindowManager::dt * Config::options.phosphor.decay.fast);

  glUniform1f(glGetUniformLocation(shader, "decaySlow"), decaySlow);
  glUniform1f(glGetUniformLocation(shader, "decayFast"), decayFast);
  glUniform1ui(glGetUniformLocation(shader, "ageThreshold"), Config::options.phosphor.decay.threshold);
  glUniform1i(glGetUniformLocation(shader, "colorbeam"), Config::options.phosphor.beam.rainbow);

  glBindImageTexture(0, energyTexR, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, energyTexG, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(2, energyTexB, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(3, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::states[win->group].windowSizes.second + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& inR,
                  const GLuint& inG, const GLuint& inB, const GLuint& outR, const GLuint& outG, const GLuint& outB) {
  auto& shader = shaders["phosphor_blur"];
  if (!shader)
    return;

  glUseProgram(shader);

  glUniform1f(glGetUniformLocation(shader, "line_blur_spread"), Config::options.phosphor.blur.spread);
  glUniform1f(glGetUniformLocation(shader, "line_width"), Config::options.phosphor.beam.width);
  glUniform1f(glGetUniformLocation(shader, "range_factor"), Config::options.phosphor.blur.range);
  glUniform1i(glGetUniformLocation(shader, "blur_direction"), dir);
  glUniform1i(glGetUniformLocation(shader, "kernel_type"), kernel);
  glUniform1f(glGetUniformLocation(shader, "f_intensity"), Config::options.phosphor.blur.near_intensity);
  glUniform1f(glGetUniformLocation(shader, "g_intensity"), Config::options.phosphor.blur.far_intensity);
  glUniform2f(glGetUniformLocation(shader, "texSize"), static_cast<float>(win->width),
              static_cast<float>(SDLWindow::states[win->group].windowSizes.second));
  glUniform1i(glGetUniformLocation(shader, "colorbeam"), Config::options.phosphor.beam.rainbow);

  glBindImageTexture(0, inR, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, inG, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(2, inB, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(3, outR, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(4, outG, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(5, outB, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::states[win->group].windowSizes.second + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* beamColor, const GLuint& inR,
                      const GLuint& inG, const GLuint& inB, const GLuint& out) {
  auto& shader = shaders["phosphor_colormap"];
  if (!shader)
    return;

  glUseProgram(shader);

  glUniform3fv(glGetUniformLocation(shader, "beamColor"), 1, beamColor);
  glUniform3fv(glGetUniformLocation(shader, "blackColor"), 1, Theme::colors.background);
  glUniform1f(glGetUniformLocation(shader, "screenCurvature"), Config::options.phosphor.screen.curvature);
  glUniform1f(glGetUniformLocation(shader, "screenGapFactor"), Config::options.phosphor.screen.gap);
  glUniform1f(glGetUniformLocation(shader, "grainStrength"), Config::options.phosphor.screen.grain);
  glUniform2i(glGetUniformLocation(shader, "texSize"), win->width, SDLWindow::states[win->group].windowSizes.second);
  glUniform1f(glGetUniformLocation(shader, "vignetteStrength"), Config::options.phosphor.screen.vignette);
  glUniform1f(glGetUniformLocation(shader, "chromaticAberrationStrength"),
              Config::options.phosphor.screen.chromatic_aberration);
  glUniform1i(glGetUniformLocation(shader, "colorbeam"), Config::options.phosphor.beam.rainbow);

  glBindImageTexture(0, inR, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, inG, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(2, inB, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(3, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::states[win->group].windowSizes.second + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void cleanup() {
  for (auto& [_, shader] : shaders) {
    if (shader != 0)
      glDeleteProgram(shader);
  }

  shaders.erase(shaders.begin(), shaders.end());
}

} // namespace Shader

namespace Phosphor {
void render(const WindowManager::VisualizerWindow* win, const std::vector<std::pair<float, float>> points,
            bool renderPoints, const float* beamColor) {

  Graphics::Shader::ensureShaders();

  // Apply decay to phosphor effect
  Shader::dispatchDecay(win, win->phosphor.ageTexture, win->phosphor.energyTextureR, win->phosphor.energyTextureG,
                        win->phosphor.energyTextureB);

  // Add new points if rendering is enabled
  if (renderPoints)
    Shader::dispatchCompute(win, static_cast<GLuint>(points.size()), win->phosphor.ageTexture, SDLWindow::vertexBuffer,
                            SDLWindow::vertexColorBuffer, win->phosphor.energyTextureR, win->phosphor.energyTextureG,
                            win->phosphor.energyTextureB);

  // Clear temp textures before blurring
  glBindFramebuffer(GL_FRAMEBUFFER, SDLWindow::frameBuffer);

  // Clear first temp textures (R, G, B)
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTextureR, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTextureG, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTextureB, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  // Clear second temp textures (R, G, B)
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTexture2R, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTexture2G, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win->phosphor.tempTexture2B, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Apply multiple blur passes
  for (int k = 0; k < 3; k++) {
    Shader::dispatchBlur(win, 0, k, win->phosphor.energyTextureR, win->phosphor.energyTextureG,
                         win->phosphor.energyTextureB, win->phosphor.tempTextureR, win->phosphor.tempTextureG,
                         win->phosphor.tempTextureB);
    Shader::dispatchBlur(win, 1, k, win->phosphor.tempTextureR, win->phosphor.tempTextureG, win->phosphor.tempTextureB,
                         win->phosphor.tempTexture2R, win->phosphor.tempTexture2G, win->phosphor.tempTexture2B);
  }

  // Apply colormap to final result
  Shader::dispatchColormap(win, beamColor, win->phosphor.tempTexture2R, win->phosphor.tempTexture2G,
                           win->phosphor.tempTexture2B, win->phosphor.outputTexture);
}
} // namespace Phosphor

void rgbaToHsva(float* rgba, float* hsva) {
  float r = rgba[0], g = rgba[1], b = rgba[2], a = rgba[3];
  float maxc = std::max({r, g, b}), minc = std::min({r, g, b});
  float d = maxc - minc;
  float h = 0, s = (maxc == 0 ? 0 : d / maxc), v = maxc;
  if (d > FLT_EPSILON) {
    if (maxc == r)
      h = (g - b) / d + (g < b ? 6 : 0);
    else if (maxc == g)
      h = (b - r) / d + 2;
    else
      h = (r - g) / d + 4;
    h /= 6;
  }
  hsva[0] = h;
  hsva[1] = s;
  hsva[2] = v;
  hsva[3] = a;
}

void hsvaToRgba(float* hsva, float* rgba) {
  float h = hsva[0], s = hsva[1], v = hsva[2], a = hsva[3];
  float r, g, b;

  if (s <= FLT_EPSILON) {
    r = g = b = v;
  } else {
    h = std::fmod(h, 1.0f) * 6.0f;
    int i = static_cast<int>(std::floor(h));
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0:
      r = v;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = v;
      b = p;
      break;
    case 2:
      r = p;
      g = v;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = v;
      break;
    case 4:
      r = t;
      g = p;
      b = v;
      break;
    case 5:
    default:
      r = v;
      g = p;
      b = q;
      break;
    }
  }
  rgba[0] = r;
  rgba[1] = g;
  rgba[2] = b;
  rgba[3] = a;
}

} // namespace Graphics