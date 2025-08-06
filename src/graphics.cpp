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

  float halfThickness = thickness * 0.5f;
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
  float halfThickness = thickness * 0.5f;

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
std::vector<FT_Face> faces;
std::vector<FT_Library> ftLibs;

// Glyph texture cache - per window
std::vector<std::unordered_map<std::pair<char, float>, GlyphTexture, PairHash>> glyphCaches;

void load(size_t sdlWindow) {
  // Ensure vectors are large enough
  if (sdlWindow >= faces.size()) {
    faces.resize(sdlWindow + 1, nullptr);
    ftLibs.resize(sdlWindow + 1, nullptr);
    glyphCaches.resize(sdlWindow + 1);
  }

  // Select the window for loading
  SDLWindow::selectWindow(sdlWindow);

  std::string path = expandUserPath(Config::options.font);
  struct stat buf;
  if (stat(path.c_str(), &buf) != 0)
    return;

  // Initialize FreeType library for this window
  if (!ftLibs[sdlWindow])
    if (FT_Init_FreeType(&ftLibs[sdlWindow]) != 0)
      return;

  // Load font face for this window
  if (FT_New_Face(ftLibs[sdlWindow], path.c_str(), 0, &faces[sdlWindow]))
    return;
}

void cleanup(size_t sdlWindow) {
  if (sdlWindow >= faces.size())
    return;

  // Select the window for cleanup
  SDLWindow::selectWindow(sdlWindow);

  // Cleanup glyph textures
  for (auto& [key, glyph] : glyphCaches[sdlWindow]) {
    if (glIsTexture(glyph.textureId)) {
      glDeleteTextures(1, &glyph.textureId);
    }
  }
  glyphCaches[sdlWindow].clear();

  // Cleanup FreeType resources for this window
  if (faces[sdlWindow]) {
    FT_Done_Face(faces[sdlWindow]);
    faces[sdlWindow] = nullptr;
  }
  if (ftLibs[sdlWindow]) {
    FT_Done_FreeType(ftLibs[sdlWindow]);
    ftLibs[sdlWindow] = nullptr;
  }
}

GlyphTexture& getGlyphTexture(char c, float size, size_t sdlWindow) {
  if (sdlWindow >= glyphCaches.size() || !faces[sdlWindow])
    return glyphCaches[0][std::make_pair(c, size)]; // Fallback to first window

  auto key = std::make_pair(c, size);
  auto it = glyphCaches[sdlWindow].find(key);
  if (it != glyphCaches[sdlWindow].end()) {
    return it->second;
  }

  // Set font size
  FT_Set_Pixel_Sizes(faces[sdlWindow], 0, size);

  if (FT_Load_Char(faces[sdlWindow], c, FT_LOAD_RENDER)) {
    static GlyphTexture empty = {0, 0, 0, 0, 0, 0};
    return empty;
  }

  FT_GlyphSlot g = faces[sdlWindow]->glyph;

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

  glyphCaches[sdlWindow][key] = glyph;
  return glyphCaches[sdlWindow][key];
}

void drawText(const char* text, const float& x, const float& y, const float& size, const float* color,
              size_t sdlWindow) {
  if (!text || !*text || sdlWindow >= faces.size() || !faces[sdlWindow])
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
      _y += size;
      continue;
    }

    // Get or create glyph texture
    GlyphTexture& glyph = getGlyphTexture(c, size, sdlWindow);
    if (glyph.textureId == 0)
      continue;

    glBindTexture(GL_TEXTURE_2D, glyph.textureId);

    // Calculate glyph position
    float x0 = _x + static_cast<float>(glyph.bearingX);
    float y0 = _y - static_cast<float>(glyph.height - glyph.bearingY);
    float w = static_cast<float>(glyph.width);
    float h = static_cast<float>(glyph.height);

    // Render glyph quad
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

    _x += static_cast<float>(glyph.advance);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

std::pair<float, float> getTextSize(const char* text, const float& size, size_t sdlWindow) {
  if (!text || !*text || sdlWindow >= faces.size() || !faces[sdlWindow])
    return {0.0f, 0.0f};

  float totalWidth = 0.0f;
  float maxHeight = 0.0f;
  float currentLineWidth = 0.0f;
  float lineHeight = size;

  for (const char* p = text; *p; p++) {
    const char& c = *p;
    if (c == '\n') {
      totalWidth = std::max(totalWidth, currentLineWidth);
      currentLineWidth = 0.0f;
      continue;
    }

    GlyphTexture& glyph = getGlyphTexture(c, size, sdlWindow);
    if (glyph.textureId == 0)
      continue;

    currentLineWidth += static_cast<float>(glyph.advance);
    maxHeight = std::max(maxHeight, static_cast<float>(glyph.height));
  }

  totalWidth = std::max(totalWidth, currentLineWidth);
  return {totalWidth, maxHeight};
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
  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  glBindBuffer(GL_ARRAY_BUFFER, window->phosphor.vertexBuffer);
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
std::vector<GLuint> shaders(4, 0);

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
  std::string inst = std::string(PULSE_DATA_DIR) + "/" + path;
  std::ifstream sFile(inst);
  if (sFile.is_open()) {
    std::string content;
    std::string line;
    while (std::getline(sFile, line))
      content += line + "\n";
    return content;
  }

  std::cerr << "Failed to open shader file '" << path << "'" << std::endl;
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

  std::cerr << "Shader compilation failed for '" << path << "': " << log.data() << std::endl;
  glDeleteShader(shader);
  return 0;
}

void ensureShaders() {
  if (std::all_of(shaders.begin(), shaders.end(), [](int x) { return x != 0; }))
    return;

  // Shader file paths
  static std::vector<std::string> shaderPaths = {"shaders/phosphor_compute.comp", "shaders/phosphor_decay.comp",
                                                 "shaders/phosphor_blur.comp", "shaders/phosphor_colormap.comp"};

  // Load and compile all shaders
  for (int i = 0; i < shaders.size(); i++) {
    if (shaders[i])
      continue;

    GLuint shader = load(shaderPaths[i].c_str(), GL_COMPUTE_SHADER);
    if (!shader)
      continue;

    // Create shader program
    shaders[i] = glCreateProgram();
    glAttachShader(shaders[i], shader);
    glLinkProgram(shaders[i]);

    GLint tmp;
    glGetProgramiv(shaders[i], GL_LINK_STATUS, &tmp);
    if (!tmp) {
      char log[512];
      glGetProgramInfoLog(shaders[i], 512, NULL, log);
      std::cerr << "Shader linking failed:" << log << std::endl;
      glDeleteProgram(shaders[i]);
      shaders[i] = 0;
    }

    glDeleteShader(shader);
  }
}

void dispatchCompute(const WindowManager::VisualizerWindow* win, const int& vertexCount, const GLuint& ageTex,
                     const GLuint& vertexBuffer, const GLuint& vertexColorBuffer, const GLuint& energyTexR,
                     const GLuint& energyTexG, const GLuint& energyTexB) {
  if (!shaders[0])
    return;

  glUseProgram(shaders[0]);

  glUniform1i(glGetUniformLocation(shaders[0], "colorbeam"), Config::options.phosphor.colorbeam);

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
  if (!shaders[1])
    return;

  glUseProgram(shaders[1]);

  float decaySlow = exp(-WindowManager::dt * Config::options.phosphor.decay_slow);
  float decayFast = exp(-WindowManager::dt * Config::options.phosphor.decay_fast);

  glUniform1f(glGetUniformLocation(shaders[1], "decaySlow"), decaySlow);
  glUniform1f(glGetUniformLocation(shaders[1], "decayFast"), decayFast);
  glUniform1ui(glGetUniformLocation(shaders[1], "ageThreshold"), Config::options.phosphor.age_threshold);
  glUniform1i(glGetUniformLocation(shaders[1], "colorbeam"), Config::options.phosphor.colorbeam);

  glBindImageTexture(0, energyTexR, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, energyTexG, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(2, energyTexB, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(3, ageTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchBlur(const WindowManager::VisualizerWindow* win, const int& dir, const int& kernel, const GLuint& inR,
                  const GLuint& inG, const GLuint& inB, const GLuint& outR, const GLuint& outG, const GLuint& outB) {
  if (!shaders[2])
    return;

  glUseProgram(shaders[2]);

  glUniform1f(glGetUniformLocation(shaders[2], "line_blur_spread"), Config::options.phosphor.line_blur_spread);
  glUniform1f(glGetUniformLocation(shaders[2], "line_width"), Config::options.phosphor.line_width);
  glUniform1f(glGetUniformLocation(shaders[2], "range_factor"), Config::options.phosphor.range_factor);
  glUniform1i(glGetUniformLocation(shaders[2], "blur_direction"), dir);
  glUniform1i(glGetUniformLocation(shaders[2], "kernel_type"), kernel);
  glUniform1f(glGetUniformLocation(shaders[2], "f_intensity"), Config::options.phosphor.near_blur_intensity);
  glUniform1f(glGetUniformLocation(shaders[2], "g_intensity"), Config::options.phosphor.far_blur_intensity);
  glUniform2f(glGetUniformLocation(shaders[2], "texSize"), static_cast<float>(win->width),
              static_cast<float>(SDLWindow::height));
  glUniform1i(glGetUniformLocation(shaders[2], "colorbeam"), Config::options.phosphor.colorbeam);

  glBindImageTexture(0, inR, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, inG, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(2, inB, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(3, outR, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(4, outG, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(5, outB, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
}

void dispatchColormap(const WindowManager::VisualizerWindow* win, const float* beamColor, const GLuint& inR,
                      const GLuint& inG, const GLuint& inB, const GLuint& out) {
  if (!shaders[3])
    return;

  glUseProgram(shaders[3]);

  glUniform3fv(glGetUniformLocation(shaders[3], "beamColor"), 1, beamColor);
  glUniform3fv(glGetUniformLocation(shaders[3], "blackColor"), 1, Theme::colors.background);
  glUniform1i(glGetUniformLocation(shaders[3], "enablePhosphorGrain"), Config::options.phosphor.enable_grain);
  glUniform1i(glGetUniformLocation(shaders[3], "enableCurvedScreen"), Config::options.phosphor.enable_curved_screen);
  glUniform1f(glGetUniformLocation(shaders[3], "screenCurvature"), Config::options.phosphor.screen_curvature);
  glUniform1f(glGetUniformLocation(shaders[3], "screenGapFactor"), Config::options.phosphor.screen_gap);
  glUniform1f(glGetUniformLocation(shaders[3], "grainStrength"), Config::options.phosphor.grain_strength);
  glUniform2i(glGetUniformLocation(shaders[3], "texSize"), win->width, SDLWindow::height);
  glUniform1f(glGetUniformLocation(shaders[3], "vignetteStrength"), Config::options.phosphor.vignette_strength);
  glUniform1f(glGetUniformLocation(shaders[3], "chromaticAberrationStrength"),
              Config::options.phosphor.chromatic_aberration_strength);
  glUniform1i(glGetUniformLocation(shaders[3], "colorbeam"), Config::options.phosphor.colorbeam);

  glBindImageTexture(0, inR, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(1, inG, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(2, inB, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(3, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  GLuint gX = (win->width + 7) / 8;
  GLuint gY = (SDLWindow::height + 7) / 8;
  glDispatchCompute(gX, gY, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  glUseProgram(0);
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
    Shader::dispatchCompute(win, static_cast<GLuint>(points.size()), win->phosphor.ageTexture,
                            win->phosphor.vertexBuffer, win->phosphor.vertexColorBuffer, win->phosphor.energyTextureR,
                            win->phosphor.energyTextureG, win->phosphor.energyTextureB);

  // Clear temp textures before blurring
  glBindFramebuffer(GL_FRAMEBUFFER, win->phosphor.frameBuffer);

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
  if (d > 1e-6f) {
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

  if (s <= 1e-6f) {
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