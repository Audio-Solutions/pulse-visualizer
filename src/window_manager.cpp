#include "include/window_manager.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"

/**
 * TODO: Implement separate window containers for multi-window layouts
 */
namespace WindowManager {

// Delta time for frame timing
float dt;

// Global window and splitter containers
std::vector<VisualizerWindow> windows;
std::vector<Splitter> splitters;

void setViewport(int x, int width, int height) {
  // Set OpenGL viewport and projection matrix for rendering
  glViewport(x, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, width, 0, height, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void Splitter::handleEvent(const SDL_Event& event) {
  if (!draggable)
    return;
  int mouseX;
  switch (event.type) {
  case SDL_MOUSEMOTION:
    mouseX = event.motion.x;
    // Check if mouse is hovering over splitter
    if (!dragging)
      hovering = abs(mouseX - x) < 5;
    // Handle dragging movement
    if (dragging) {
      dx = mouseX - x;
      x = mouseX;
    }
    break;

  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
      // Start dragging if mouse is over splitter
      if (abs(mouseX - x) < 5) {
        dragging = true;
      }
    }
    break;

  case SDL_MOUSEBUTTONUP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
      dragging = false;
      hovering = abs(mouseX - x) < 5;
    }
    break;

  default:
    break;
  }
}

void VisualizerWindow::handleEvent(const SDL_Event& event) {
  switch (event.type) {
  case SDL_MOUSEMOTION:
    // Check if mouse is hovering over this window
    hovering = (event.motion.x >= x && event.motion.x < x + width);
    break;

  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (event.button.x >= x && event.button.x < x + width) {
        size_t index = this - &windows[0];

        if (buttonPressed(-1, event.button.x, event.button.y))
          swapVisualizer<Direction::Left>(index);
        else if (buttonPressed(1, event.button.x, event.button.y))
          swapVisualizer<Direction::Right>(index);
      }
    }
    break;

  case SDL_WINDOWEVENT:
    if (event.window.event == SDL_WINDOWEVENT_LEAVE)
      hovering = false;
    break;
  }
}

void Splitter::draw() {
  if (SDLWindow::height == 0) [[unlikely]]
    return;

  // Select the window for rendering
  SDLWindow::selectWindow(sdlWindow);

  // Set viewport for splitter rendering
  setViewport(x - 5, 10, SDLWindow::height);

  // Draw splitter line
  float color[4] = {0.5, 0.5, 0.5, 1.0};
  Graphics::drawLine(5, 0, 5, SDLWindow::height, Theme::colors.accent, 2.0f);

  // Draw hover highlight if mouse is over splitter
  if (hovering) {
    glColor4fv(Theme::alpha(Theme::colors.accent, 0.3f));
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(10, 0);
    glVertex2f(10, SDLWindow::height);
    glVertex2f(0, SDLWindow::height);
    glEnd();
  }
}

void VisualizerWindow::transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type) {
  // Calculate minimum dimensions for texture transfer
  int minWidth = std::min(width, phosphor.textureWidth);
  int minHeight = std::min(SDLWindow::height, phosphor.textureHeight);
  std::vector<uint8_t> newData(width * SDLWindow::height * 4, 0);

  if (glIsTexture(oldTex)) [[likely]] {
    // Read pixel data from old texture
    std::vector<uint8_t> oldData(phosphor.textureWidth * phosphor.textureHeight * 4);
    glBindTexture(GL_TEXTURE_2D, oldTex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, oldData.data());

    // Calculate offset for centering texture
    int offsetX = std::max(0, (width - phosphor.textureWidth) / 2);
    int offsetY = std::max(0, (SDLWindow::height - phosphor.textureHeight) / 2);

#ifdef HAVE_AVX2
    // SIMD-optimized texture transfer
    int rowBytes = minWidth * 4;
    constexpr int SIMD_WIDTH = 32;

    for (int y = 0; y < minHeight; ++y) {
      int oldRow = y * phosphor.textureWidth * 4;
      int newRow = (y + offsetY) * width * 4 + offsetX * 4;

      int x = 0;
      for (; x + SIMD_WIDTH <= rowBytes; x += SIMD_WIDTH) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldData[oldRow + x]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&newData[newRow + x]), v);
      }
      for (; x < rowBytes; x += 4) {
        newData[newRow + x + 0] = oldData[oldRow + x + 0];
        newData[newRow + x + 1] = oldData[oldRow + x + 1];
        newData[newRow + x + 2] = oldData[oldRow + x + 2];
        newData[newRow + x + 3] = oldData[oldRow + x + 3];
      }
    }
#else
    // Standard texture transfer without SIMD
    for (int y = 0; y < minHeight; ++y) {
      for (int x = 0; x < minWidth; ++x) {
        int oldIdx = (y * phosphor.textureWidth + x) * 4;
        int newIdx = ((y + offsetY) * width + (x + offsetX)) * 4;
        if (newIdx + 3 < newData.size() && oldIdx + 3 < oldData.size()) {
          newData[newIdx] = oldData[oldIdx];
          newData[newIdx + 1] = oldData[oldIdx + 1];
          newData[newIdx + 2] = oldData[oldIdx + 2];
          newData[newIdx + 3] = oldData[oldIdx + 3];
        }
      }
    }
#endif
  }
  // Create new texture with transferred data
  glBindTexture(GL_TEXTURE_2D, newTex);
  glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED_INTEGER ? GL_R32UI : GL_RGBA8, width, SDLWindow::height, 0, format,
               type, newData.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void VisualizerWindow::resizeTextures() {
  // Check if texture resize is needed
  bool sizeChanged = (phosphor.textureWidth != width || phosphor.textureHeight != SDLWindow::height);

  // Array of phosphor effect texture handles
  GLuint* textures[] = {&phosphor.energyTexture, &phosphor.ageTexture, &phosphor.tempTexture, &phosphor.tempTexture2,
                        &phosphor.outputTexture};

  // Check if any textures are uninitialized
  bool textureUninitialized = [textures]() -> bool {
    for (auto texture : textures) {
      if (*texture == 0)
        return true;
    }
    return false;
  }();

  if (!sizeChanged && !textureUninitialized) [[likely]] {
    return;
  }

  // Synchronize OpenGL state before texture operations
  glFinish();
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Store old textures for data transfer
  std::vector<GLuint> oldTextures;
  oldTextures.reserve(5);

  // Recreate all phosphor textures
  for (int i = 0; i < 5; ++i) {
    oldTextures.push_back(*textures[i]);
    GLuint newTexture = 0;
    glGenTextures(1, &newTexture);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) [[unlikely]]
      throw std::runtime_error(
          "WindowManager::VisualizerWindow::resizeTextures(): OpenGL error during texture generation: " +
          std::to_string(err));
    // Transfer texture data with appropriate format
    if (i == 4)
      transferTexture(oldTextures[i], newTexture, GL_RGBA, GL_UNSIGNED_BYTE);
    else
      transferTexture(oldTextures[i], newTexture, GL_RED_INTEGER, GL_UNSIGNED_INT);
    if (oldTextures[i]) {
      glDeleteTextures(1, &oldTextures[i]);
    }
    *textures[i] = newTexture;
  }

  // Update texture dimensions
  phosphor.textureHeight = SDLWindow::height;
  phosphor.textureWidth = width;

  // Recreate framebuffer and vertex buffer
  glBindTexture(GL_TEXTURE_2D, 0);
  if (phosphor.frameBuffer)
    glDeleteFramebuffers(1, &phosphor.frameBuffer);
  glGenFramebuffers(1, &phosphor.frameBuffer);

  if (phosphor.vertexBuffer)
    glDeleteBuffers(1, &phosphor.vertexBuffer);
  glGenBuffers(1, &phosphor.vertexBuffer);
}

void VisualizerWindow::draw() {
  // Validate phosphor output texture
  if (!glIsTexture(phosphor.outputTexture)) [[unlikely]] {
    throw std::runtime_error("WindowManager::VisualizerWindow::draw(): outputTexture is not a Texture");
  }

  // Select the window for rendering
  SDLWindow::selectWindow(sdlWindow);

  // Set viewport for this window
  setViewport(x, width, SDLWindow::height);

  if (Config::options.phosphor.enabled) {
    // Render phosphor effect using output texture
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, phosphor.outputTexture);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    // Draw textured quad
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f);
    glVertex2f(0.f, 0.f);
    glTexCoord2f(1.f, 0.f);
    glVertex2f(static_cast<float>(width), 0.f);
    glTexCoord2f(1.f, 1.f);
    glVertex2f(static_cast<float>(width), static_cast<float>(SDLWindow::height));
    glTexCoord2f(0.f, 1.f);
    glVertex2f(0.f, static_cast<float>(SDLWindow::height));
    glEnd();

    glDisable(GL_TEXTURE_2D);
  }

  // Check for OpenGL errors
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    std::cout << "WindowManager::VisualizerWindow::draw(): OpenGL error during draw: " + std::to_string(err)
              << std::endl;
}

void VisualizerWindow::drawArrow(int dir) {
  if (SDLWindow::height == 0) [[unlikely]]
    return;

  SDLWindow::selectWindow(sdlWindow);
  setViewport(x, width, SDLWindow::height);

  int arrowX = (dir == -1) ? buttonPadding : width - buttonPadding - buttonSize;
  int arrowY = buttonPadding;

  // Draw background
  Graphics::drawFilledRect(arrowX, arrowY, buttonSize, buttonSize, Theme::colors.bgaccent);

  // Draw arrow
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_POLYGON_SMOOTH);

  glColor4fv(Theme::colors.text);
  glBegin(GL_TRIANGLES);
  float x1 = arrowX + buttonSize * (0.5f - 0.2f * dir);
  float y1 = arrowY + buttonSize * 0.3f;
  float x2 = arrowX + buttonSize * (0.5f + 0.2f * dir);
  float y2 = arrowY + buttonSize * 0.5f;
  float x3 = arrowX + buttonSize * (0.5f - 0.2f * dir);
  float y3 = arrowY + buttonSize * 0.7f;
  glVertex2f(x1, y1);
  glVertex2f(x2, y2);
  glVertex2f(x3, y3);

  glEnd();
  glDisable(GL_POLYGON_SMOOTH);
  glDisable(GL_BLEND);
}

bool VisualizerWindow::buttonPressed(int dir, int mouseX, int mouseY) {
  int arrowX = (dir == -1) ? buttonPadding : width - buttonPadding - buttonSize;
  int arrowY = SDLWindow::height - buttonPadding - buttonSize;
  return (mouseX >= x + arrowX && mouseX < x + arrowX + buttonSize && mouseY >= arrowY && mouseY < arrowY + buttonSize);
}

bool VisualizerWindow::buttonHovering(int dir, int mouseX, int mouseY) { return buttonPressed(dir, mouseX, mouseY); }

void drawSplitters() {
  // Draw all window splitters
  for (Splitter& splitter : splitters)
    splitter.draw();
}

void renderAll() {
  // Render all visualizer windows
  for (VisualizerWindow& window : windows)
    if (window.render)
      window.render();

  // Draw arrows for each window
  for (auto& window : windows) {
    if (window.hovering) {
      window.drawArrow(-1);
      window.drawArrow(1);
    }
  }
}

void resizeTextures() {
  // Resize phosphor textures for all windows
  for (VisualizerWindow& window : windows) {
    window.resizeTextures();
  }
}

void resizeWindows() {
  if (splitters.size() == 0) {
    // Single window case
    windows[0].x = 0;
    windows[0].width = SDLWindow::width;
  } else {
    // Multiple windows with splitters
    windows[0].x = 0;
    windows[0].width = splitters[0].x - 1;
    for (int i = 1; i < windows.size() - 1; i++) {
      windows[i].x = splitters[i - 1].x + 1;
      windows[i].width = splitters[i].x - splitters[i - 1].x - 2;
    }

    windows.back().x = splitters.back().x + 1;
    windows.back().width = SDLWindow::width - windows.back().x - 1;
  }
}

int moveSplitter(int index, int targetX) {
  // Get references to adjacent windows and splitters
  VisualizerWindow* left = &windows[index];
  VisualizerWindow* right = &windows[index + 1];
  Splitter* splitter = &splitters[index];
  Splitter* splitterLeft = (index != 0 ? &splitters.at(index - 1) : nullptr);
  Splitter* splitterRight = (index != splitters.size() - 1 ? &splitters.at(index + 1) : nullptr);

  static int iter = 0;

  // Set target position if provided
  if (targetX != -1)
    splitter->x = targetX;
  else
    iter = 0;

  // Prevent infinite loops
  if (++iter > 20) {
    std::cerr << "Unable to solve splitters, max iterations reached." << std::endl;
    return -1;
  }

  // Lambda function to handle splitter movement constraints
  auto move = [&](Splitter* neighborSplitter, VisualizerWindow* neighborWindow, int direction, int boundary) -> bool {
    bool solved = true;
    if (neighborWindow && neighborWindow->aspectRatio != 0.0f) {
      // Handle aspect ratio constraints
      int forceWidth = std::min(static_cast<int>(neighborWindow->aspectRatio * SDLWindow::height),
                                static_cast<int>(SDLWindow::width - (windows.size() - 1) * MIN_WIDTH));
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) != forceWidth) {
          moveSplitter(index + direction, splitter->x + direction * forceWidth);
          solved = false;
        }
      } else {
        if (direction * (boundary - splitter->x) != forceWidth) {
          splitter->x = boundary - direction * forceWidth;
          solved = false;
        }
      }
    } else {
      // Handle minimum width constraints
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) < MIN_WIDTH) {
          moveSplitter(index + direction, splitter->x + direction * MIN_WIDTH);
          solved = false;
        }
      } else {
        if (direction * (boundary - splitter->x) < MIN_WIDTH) {
          splitter->x = boundary - direction * MIN_WIDTH;
          solved = false;
        }
      }
    }
    return solved;
  };

  // Iteratively solve splitter constraints
  int i = 0;
  while (!(move(splitterRight, right, 1, SDLWindow::width) && move(splitterLeft, left, -1, 0)))
    i++;
  return i;
}

void updateSplitters() {
  // Find currently dragging splitter
  auto it =
      std::find_if(splitters.begin(), splitters.end(), [](const auto& splitter) { return splitter.dragging == true; });

  if (it == splitters.end()) [[likely]] {
    // No splitter is being dragged, update all splitters
    for (int i = 0; i < splitters.size(); i++) {
      moveSplitter(i);
    }
    return;
  }

  // Update only the dragging splitter
  int movingSplitterIndex = (it != splitters.end()) ? std::distance(splitters.begin(), it) : -1;

  moveSplitter(movingSplitterIndex);
}

void VisualizerWindow::cleanup() {
  // Cleanup phosphor effect textures
  if (phosphor.energyTexture) {
    glDeleteTextures(1, &phosphor.energyTexture);
    phosphor.energyTexture = 0;
  }
  if (phosphor.ageTexture) {
    glDeleteTextures(1, &phosphor.ageTexture);
    phosphor.ageTexture = 0;
  }
  if (phosphor.tempTexture) {
    glDeleteTextures(1, &phosphor.tempTexture);
    phosphor.tempTexture = 0;
  }
  if (phosphor.tempTexture2) {
    glDeleteTextures(1, &phosphor.tempTexture2);
    phosphor.tempTexture2 = 0;
  }
  if (phosphor.outputTexture) {
    glDeleteTextures(1, &phosphor.outputTexture);
    phosphor.outputTexture = 0;
  }
  if (phosphor.frameBuffer) {
    glDeleteFramebuffers(1, &phosphor.frameBuffer);
    phosphor.frameBuffer = 0;
  }
  if (phosphor.vertexBuffer) {
    glDeleteBuffers(1, &phosphor.vertexBuffer);
    phosphor.vertexBuffer = 0;
  }
}

void reorder() {
  // Define visualizer render functions and their window pointers
  std::vector<std::pair<std::function<void()>, WindowManager::VisualizerWindow*&>> visualizers = {
      {SpectrumAnalyzer::render, SpectrumAnalyzer::window},
      {Lissajous::render,        Lissajous::window       },
      {Oscilloscope::render,     Oscilloscope::window    },
      {Spectrogram::render,      Spectrogram::window     }
  };

  // Map visualizer names to their indices
  std::map<std::string, size_t> visualizerMap = {
      {"spectrum_analyzer", 0},
      {"lissajous",         1},
      {"oscilloscope",      2},
      {"spectrogram",       3}
  };

  if (Config::options.visualizers.empty()) {
    Config::options.visualizers = {"spectrum_analyzer", "oscilloscope", "spectrogram"};
  }

  // Cleanup existing windows before clearing
  for (auto& window : windows) {
    window.cleanup();
  }

  // Clear existing windows and splitters
  windows.clear();
  splitters.clear();
  windows.reserve(Config::options.visualizers.size());

  // Create windows in the order specified in configuration
  for (size_t i = 0; i < Config::options.visualizers.size(); ++i) {
    const std::string& visName = Config::options.visualizers[i];
    auto it = visualizerMap.find(visName);
    if (it == visualizerMap.end()) {
      std::cerr << "Warning: Unknown visualizer '" << visName << "' in configuration" << std::endl;
      continue;
    }

    size_t idx = it->second;
    VisualizerWindow vw {};
    // Set aspect ratio for Lissajous (square) visualizer
    vw.aspectRatio = (visName == "lissajous") ? 1.0f : 0.0f;
    vw.render = visualizers[idx].first;
    windows.push_back(vw);
    visualizers[idx].second = &windows.back();

    // Create splitter between windows
    if (i + 1 < Config::options.visualizers.size()) {
      Splitter s;
      // Disable dragging for first splitter if Lissajous is first
      if (i == 0 && vw.aspectRatio != 0.0f)
        s.draggable = false;
      splitters.push_back(s);
    }
  }

  // Disable dragging for last splitter if Lissajous is last
  if (!windows.empty() && !splitters.empty() && windows.back().aspectRatio != 0.0f)
    splitters.back().draggable = false;
}

template <Direction direction> void swapVisualizer(size_t index) {
  if (index < 0 || index >= windows.size() || index + direction < 0 || index + direction >= windows.size())
    return;

  std::swap(Config::options.visualizers[index], Config::options.visualizers[index + direction]);
  reorder();
}

} // namespace WindowManager
