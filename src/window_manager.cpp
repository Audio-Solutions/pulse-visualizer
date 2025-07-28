#include "include/window_manager.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"

namespace WindowManager {
float dt;

std::vector<VisualizerWindow> windows;
std::vector<Splitter> splitters;

void setViewport(int x, int width, int height) {
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
    if (!dragging)
      hovering = abs(mouseX - x) < 5;
    if (dragging) {
      dx = mouseX - x;
      x = mouseX;
    }
    break;

  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
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

void Splitter::draw() {
  if (SDLWindow::height == 0) [[unlikely]]
    return;

  setViewport(x - 5, 10, SDLWindow::height);

  float color[4] = {0.5, 0.5, 0.5, 1.0};
  Graphics::drawLine(5, 0, 5, SDLWindow::height, Theme::colors.accent, 2.0f);

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
  int minWidth = std::min(width, phosphor.textureWidth);
  int minHeight = std::min(SDLWindow::height, phosphor.textureHeight);
  std::vector<uint8_t> newData(width * SDLWindow::height * 4, 0);

  if (glIsTexture(oldTex)) [[likely]] {
    std::vector<uint8_t> oldData(phosphor.textureWidth * phosphor.textureHeight * 4);
    glBindTexture(GL_TEXTURE_2D, oldTex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, oldData.data());

    int offsetX = std::max(0, (width - phosphor.textureWidth) / 2);
    int offsetY = std::max(0, (SDLWindow::height - phosphor.textureHeight) / 2);

#ifdef HAVE_AVX2
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
  glBindTexture(GL_TEXTURE_2D, newTex);
  glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED_INTEGER ? GL_R32UI : GL_RGBA8, width, SDLWindow::height, 0, format,
               type, newData.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void VisualizerWindow::resizeTextures() {
  bool sizeChanged = (phosphor.textureWidth != width || phosphor.textureHeight != SDLWindow::height);

  GLuint* textures[] = {&phosphor.energyTexture, &phosphor.ageTexture,    &phosphor.tempTexture,
                        &phosphor.tempTexture2,  &phosphor.outputTexture, &phosphor.frameBuffer};

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

  glFinish();
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  std::vector<GLuint> oldTextures;
  oldTextures.reserve(5);

  for (int i = 0; i < 5; ++i) {
    oldTextures.push_back(*textures[i]);
    GLuint newTexture = 0;
    glGenTextures(1, &newTexture);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) [[unlikely]]
      throw std::runtime_error(
          "WindowManager::VisualizerWindow::resizeTextures(): OpenGL error during texture generation: " +
          std::to_string(err));
    if (i == 4)
      transferTexture(oldTextures[i], newTexture, GL_RGBA, GL_UNSIGNED_BYTE);
    else
      transferTexture(oldTextures[i], newTexture, GL_RED_INTEGER, GL_UNSIGNED_INT);
    if (oldTextures[i]) {
      glDeleteTextures(1, &oldTextures[i]);
    }
    *textures[i] = newTexture;
  }

  phosphor.textureHeight = SDLWindow::height;
  phosphor.textureWidth = width;

  glBindTexture(GL_TEXTURE_2D, 0);
  if (phosphor.frameBuffer)
    glDeleteFramebuffers(1, &phosphor.frameBuffer);
  glGenFramebuffers(1, &phosphor.frameBuffer);

  if (phosphor.vertexBuffer)
    glDeleteBuffers(1, &phosphor.vertexBuffer);
  glGenBuffers(1, &phosphor.vertexBuffer);
}

void VisualizerWindow::draw() {
  if (!glIsTexture(phosphor.outputTexture)) [[unlikely]] {
    throw std::runtime_error("WindowManager::VisualizerWindow::draw(): outputTexture is not a Texture");
  }

  setViewport(x, width, SDLWindow::height);

  if (Config::options.phosphor.enabled) {

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, phosphor.outputTexture);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

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

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    std::cout << "WindowManager::VisualizerWindow::draw(): OpenGL error during draw: " + std::to_string(err)
              << std::endl;
}

void drawSplitters() {
  for (Splitter& splitter : splitters)
    splitter.draw();
}

void renderAll() {
  for (VisualizerWindow& window : windows)
    if (window.render)
      window.render();
}

void resizeTextures() {
  for (VisualizerWindow& window : windows) {
    window.resizeTextures();
  }
}

void resizeWindows() {
  if (splitters.size() == 0) {
    windows[0].x = 0;
    windows[0].width = SDLWindow::width;
  } else {
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
  VisualizerWindow* left = &windows[index];
  VisualizerWindow* right = &windows[index + 1];
  Splitter* splitter = &splitters[index];
  Splitter* splitterLeft = (index != 0 ? &splitters.at(index - 1) : nullptr);
  Splitter* splitterRight = (index != splitters.size() - 1 ? &splitters.at(index + 1) : nullptr);

  static int iter = 0;

  if (targetX != -1)
    splitter->x = targetX;
  else
    iter = 0;

  if (++iter > 20) {
    std::cerr << "Unable to solve splitters, max iterations reached." << std::endl;
    return -1;
  }

  auto move = [&](Splitter* neighborSplitter, VisualizerWindow* neighborWindow, int direction, int boundary) -> bool {
    bool solved = true;
    if (neighborWindow && neighborWindow->aspectRatio != 0.0f) {
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

  int i = 0;
  while (!(move(splitterRight, right, 1, SDLWindow::width) && move(splitterLeft, left, -1, 0)))
    i++;
  return i;
}

void updateSplitters() {
  auto it =
      std::find_if(splitters.begin(), splitters.end(), [](const auto& splitter) { return splitter.dragging == true; });

  if (it == splitters.end()) [[likely]] {
    for (int i = 0; i < splitters.size(); i++) {
      moveSplitter(i);
    }
    return;
  }

  int movingSplitterIndex = (it != splitters.end()) ? std::distance(splitters.begin(), it) : -1;

  moveSplitter(movingSplitterIndex);
}

void reorder() {
  std::vector<std::pair<void (*)(), WindowManager::VisualizerWindow*&>> visualizers = {
      {SpectrumAnalyzer::render, SpectrumAnalyzer::window},
      {Lissajous::render,        Lissajous::window       },
      {Oscilloscope::render,     Oscilloscope::window    },
      {Spectrogram::render,      Spectrogram::window     }
  };

  std::vector<int*> orders = {&Config::options.visualizers.fft_order, &Config::options.visualizers.lissajous_order,
                              &Config::options.visualizers.oscilloscope_order,
                              &Config::options.visualizers.spectrogram_order};

  std::vector<std::pair<int, size_t>> orderIndexPairs;
  for (size_t i = 0; i < orders.size(); ++i)
    if (*orders[i] != -1)
      orderIndexPairs.emplace_back(*orders[i], i);

  std::sort(orderIndexPairs.begin(), orderIndexPairs.end());

  windows.clear();
  splitters.clear();
  windows.reserve(orders.size());

  for (size_t i = 0; i < orderIndexPairs.size(); ++i) {
    size_t idx = orderIndexPairs[i].second;
    VisualizerWindow vw {};
    vw.aspectRatio = (visualizers[idx].first == Lissajous::render) ? 1.0f : 0.0f;
    vw.render = visualizers[idx].first;
    windows.push_back(vw);
    visualizers[idx].second = &windows.back();

    if (i + 1 < orderIndexPairs.size()) {
      Splitter s;
      if (i == 0 && vw.aspectRatio != 0.0f)
        s.draggable = false;
      splitters.push_back(s);
    }
  }

  if (!windows.empty() && !splitters.empty() && windows.back().aspectRatio != 0.0f)
    splitters.back().draggable = false;
}

} // namespace WindowManager